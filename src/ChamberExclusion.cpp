#include "PCH.h"
#include "ChamberExclusion.h"
#include <nlohmann/json.hpp>
#include <fstream>

namespace ChamberExclusion
{
	static constexpr const char* kModName   = "UneducatedReload.esm";
	static constexpr std::uint32_t kChamberExclusionLocalID = 0x800;
	static constexpr const char* kJSONFilename = "ChamberExclusion.json";

	// ----------------------------------------------------------------
	// Form lookup from a named plugin (mod) + local form ID
	// ----------------------------------------------------------------
	RE::TESForm* Manager::GetFormFromMod(const char* modName, std::uint32_t localFormID)
	{
		auto* handler = RE::TESDataHandler::GetSingleton();
		if (!handler) return nullptr;

		for (auto* file : handler->compiledFileCollection.files) {
			if (!file) continue;
			if (_stricmp(file->GetFilename().data(), modName) != 0) continue;

			std::uint32_t fullID = (static_cast<std::uint32_t>(file->GetCompileIndex()) << 24)
				| (localFormID & 0x00FFFFFF);
			return RE::TESForm::GetFormByID(fullID);
		}

		for (auto* file : handler->compiledFileCollection.smallFiles) {
			if (!file) continue;
			if (_stricmp(file->GetFilename().data(), modName) != 0) continue;

			std::uint32_t fullID = 0xFE000000
				| (static_cast<std::uint32_t>(file->GetSmallFileCompileIndex()) << 12)
				| (localFormID & 0x00000FFF);
			return RE::TESForm::GetFormByID(fullID);
		}

		return nullptr;
	}

	// ----------------------------------------------------------------
	// Initialization — resolve keyword, load JSON, apply to base forms
	// ----------------------------------------------------------------
	void Manager::Init()
	{
		auto* form = GetFormFromMod(kModName, kChamberExclusionLocalID);
		if (form) {
			chamberExclusionKW = static_cast<RE::BGSKeyword*>(form);
			installed = true;
			logger::info("[ChamberExclusion] UneducatedReload.esm found — ChamberExclusion keyword resolved (FormID 0x{:08X})",
				chamberExclusionKW->GetFormID());
		} else {
			installed = false;
			chamberExclusionKW = nullptr;
			logger::info("[ChamberExclusion] UneducatedReload.esm not loaded — feature disabled");
			return;
		}

		LoadJSON();

		for (auto& entry : excludedWeapons) {
			ApplyKeywordToBaseForm(entry.formID);
		}
		logger::info("[ChamberExclusion] Applied ChamberExclusion to {} weapon base forms", excludedWeapons.size());
	}

	// ----------------------------------------------------------------
	// Query
	// ----------------------------------------------------------------
	bool Manager::IsWeaponExcluded(std::uint32_t formID) const
	{
		std::lock_guard lock(mtx);
		for (auto& e : excludedWeapons) {
			if (e.formID == formID) return true;
		}
		return false;
	}

	// ----------------------------------------------------------------
	// Add / Remove
	// ----------------------------------------------------------------
	void Manager::AddWeapon(std::uint32_t formID, const std::string& editorID, const std::string& displayName)
	{
		{
			std::lock_guard lock(mtx);
			for (auto& e : excludedWeapons) {
				if (e.formID == formID) return;
			}
			excludedWeapons.push_back({ formID, editorID, displayName });
		}
		ApplyKeywordToBaseForm(formID);
		SaveJSON();
		logger::info("[ChamberExclusion] Added weapon '{}' (0x{:08X}) to exclusion list", editorID, formID);
	}

	void Manager::RemoveWeapon(std::uint32_t formID)
	{
		{
			std::lock_guard lock(mtx);
			std::erase_if(excludedWeapons, [formID](const ExcludedWeaponEntry& e) {
				return e.formID == formID;
			});
		}
		RemoveKeywordFromBaseForm(formID);
		SaveJSON();
		logger::info("[ChamberExclusion] Removed weapon (0x{:08X}) from exclusion list", formID);
	}

	// ----------------------------------------------------------------
	// Keyword manipulation on BGSKeywordForm
	// ----------------------------------------------------------------
	void Manager::AddKeywordToForm(RE::BGSKeywordForm* form, RE::BGSKeyword* kw)
	{
		if (!form || !kw) return;

		for (std::uint32_t i = 0; i < form->numKeywords; ++i) {
			if (form->keywords[i] == kw) return;
		}

		auto newCount = form->numKeywords + 1;
		auto** newArr = new RE::BGSKeyword*[newCount];
		for (std::uint32_t i = 0; i < form->numKeywords; ++i) {
			newArr[i] = form->keywords[i];
		}
		newArr[form->numKeywords] = kw;
		form->keywords = newArr;
		form->numKeywords = newCount;
	}

	void Manager::RemoveKeywordFromForm(RE::BGSKeywordForm* form, RE::BGSKeyword* kw)
	{
		if (!form || !kw) return;

		std::uint32_t idx = UINT32_MAX;
		for (std::uint32_t i = 0; i < form->numKeywords; ++i) {
			if (form->keywords[i] == kw) { idx = i; break; }
		}
		if (idx == UINT32_MAX) return;

		for (std::uint32_t i = idx; i < form->numKeywords - 1; ++i) {
			form->keywords[i] = form->keywords[i + 1];
		}
		form->numKeywords--;
	}

	// ----------------------------------------------------------------
	// Apply / remove keyword on base weapon form
	// ----------------------------------------------------------------
	void Manager::ApplyKeywordToBaseForm(std::uint32_t formID)
	{
		if (!chamberExclusionKW) return;

		auto* form = RE::TESForm::GetFormByID(formID);
		if (!form) return;

		auto* weap = form->As<RE::TESObjectWEAP>();
		if (!weap) return;

		auto* kwForm = static_cast<RE::BGSKeywordForm*>(weap);
		AddKeywordToForm(kwForm, chamberExclusionKW);
	}

	void Manager::RemoveKeywordFromBaseForm(std::uint32_t formID)
	{
		if (!chamberExclusionKW) return;

		auto* form = RE::TESForm::GetFormByID(formID);
		if (!form) return;

		auto* weap = form->As<RE::TESObjectWEAP>();
		if (!weap) return;

		auto* kwForm = static_cast<RE::BGSKeywordForm*>(weap);
		RemoveKeywordFromForm(kwForm, chamberExclusionKW);
	}

	// ----------------------------------------------------------------
	// Per-frame: ensure the equipped weapon instance also has the keyword
	// UneducatedReload checks instance->keywords, not the base form.
	// ----------------------------------------------------------------
	void Manager::ApplyKeywordToEquippedInstance(RE::PlayerCharacter* player)
	{
		if (!installed || !chamberExclusionKW || !player) return;
		if (!player->currentProcess || !player->currentProcess->middleHigh) return;

		auto* mh = player->currentProcess->middleHigh;
		RE::BSAutoLock lock(mh->equippedItemsLock);

		for (auto& equipped : mh->equippedItems) {
			auto* form = equipped.item.object;
			if (!form || form->GetFormType() != RE::ENUM_FORM_ID::kWEAP) continue;
			if (!equipped.item.instanceData) continue;

			auto formID = form->GetFormID();
			if (!IsWeaponExcluded(formID)) continue;

			auto* instance = static_cast<RE::TESObjectWEAP::InstanceData*>(
				equipped.item.instanceData.get());
			if (!instance) continue;

			if (instance->keywords) {
				AddKeywordToForm(instance->keywords, chamberExclusionKW);
			} else {
				// Instance has no keyword form — point it at the base weapon's
				// BGSKeywordForm component (which already has the keyword).
				auto* weap = form->As<RE::TESObjectWEAP>();
				if (weap) {
					instance->keywords = static_cast<RE::BGSKeywordForm*>(weap);
				}
			}
		}
	}

	// ----------------------------------------------------------------
	// Reapply all keywords (call on game load / weapon equip)
	// Game loads can reset base form keyword arrays; instance data is
	// always fresh after equip. This reapplies both layers.
	// ----------------------------------------------------------------
	void Manager::ReapplyAllKeywords(RE::PlayerCharacter* player)
	{
		if (!installed || !chamberExclusionKW) return;

		for (auto& entry : excludedWeapons) {
			ApplyKeywordToBaseForm(entry.formID);
		}

		if (player) {
			ApplyKeywordToEquippedInstance(player);
		}

		logger::info("[ChamberExclusion] Reapplied keywords to {} base forms + equipped instance",
			excludedWeapons.size());
	}

	// ----------------------------------------------------------------
	// JSON persistence
	// ----------------------------------------------------------------
	std::filesystem::path Manager::GetJSONPath() const
	{
		return std::filesystem::current_path() / "Data" / "F4SE" / "Plugins" / "FPGunplayOverhaul" / kJSONFilename;
	}

	void Manager::LoadJSON()
	{
		std::lock_guard lock(mtx);
		excludedWeapons.clear();

		auto path = GetJSONPath();
		if (!std::filesystem::exists(path)) return;

		try {
			std::ifstream ifs(path);
			auto j = nlohmann::json::parse(ifs);

			if (j.contains("excludedWeapons") && j["excludedWeapons"].is_array()) {
				for (auto& item : j["excludedWeapons"]) {
					ExcludedWeaponEntry entry;
					entry.formID      = item.value("formID", 0u);
					entry.editorID    = item.value("editorID", "");
					entry.displayName = item.value("displayName", "");
					if (entry.formID != 0) {
						excludedWeapons.push_back(std::move(entry));
					}
				}
			}
			logger::info("[ChamberExclusion] Loaded {} entries from {}", excludedWeapons.size(), path.string());
		} catch (const std::exception& ex) {
			logger::error("[ChamberExclusion] Failed to parse {}: {}", path.string(), ex.what());
		}
	}

	void Manager::SaveJSON()
	{
		std::lock_guard lock(mtx);

		auto path = GetJSONPath();
		std::error_code ec;
		std::filesystem::create_directories(path.parent_path(), ec);

		nlohmann::json j;
		auto& arr = j["excludedWeapons"];
		arr = nlohmann::json::array();
		for (auto& e : excludedWeapons) {
			arr.push_back({
				{ "formID", e.formID },
				{ "editorID", e.editorID },
				{ "displayName", e.displayName }
			});
		}

		try {
			std::ofstream ofs(path);
			ofs << j.dump(4);
			logger::info("[ChamberExclusion] Saved {} entries to {}", excludedWeapons.size(), path.string());
		} catch (const std::exception& ex) {
			logger::error("[ChamberExclusion] Failed to save {}: {}", path.string(), ex.what());
		}
	}
}
