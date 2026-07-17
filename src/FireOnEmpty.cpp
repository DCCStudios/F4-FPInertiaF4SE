#include "PCH.h"
#include "FireOnEmpty.h"

#include <algorithm>
#include <fstream>
#include <nlohmann/json.hpp>
#include <spdlog/spdlog.h>

namespace FireOnEmpty
{
	// ============================================================
	// Init / load
	// ============================================================
	void Manager::Init()
	{
		LoadAllEntries();

		std::size_t enabledCount = 0;
		{
			std::lock_guard lock(entriesMtx);
			for (auto& kv : entries) {
				if (kv.second.enabled) ++enabledCount;
			}
		}
		logger::info("[FireOnEmpty] Initialized. {} entries loaded ({} enabled).",
			entries.size(), enabledCount);
	}

	// ============================================================
	// Query
	// ============================================================
	bool Manager::HasEntry(const std::string& a_editorID) const
	{
		std::lock_guard lock(entriesMtx);
		return entries.find(a_editorID) != entries.end();
	}

	bool Manager::GetEntry(const std::string& a_editorID, FOEEntry& a_out) const
	{
		std::lock_guard lock(entriesMtx);
		auto it = entries.find(a_editorID);
		if (it == entries.end()) return false;
		a_out = it->second;
		return true;
	}

	// ============================================================
	// Mutation (menu-driven)
	// ============================================================
	void Manager::SetEntry(const std::string& a_editorID, const std::string& a_displayName,
		bool a_enabled, bool a_autoFire)
	{
		if (a_editorID.empty()) return;
		logger::info("[FireOnEmpty] SetEntry '{}' ('{}') enabled={} autoFire={}",
			a_editorID, a_displayName, a_enabled, a_autoFire);
		{
			std::lock_guard lock(entriesMtx);
			auto& e       = entries[a_editorID];
			e.editorID    = a_editorID;
			e.displayName = a_displayName;
			e.enabled     = a_enabled;
			e.autoFire    = a_autoFire;
		}
		SaveEntry(a_editorID);
	}

	void Manager::RemoveEntry(const std::string& a_editorID)
	{
		logger::info("[FireOnEmpty] RemoveEntry '{}'", a_editorID);
		{
			std::lock_guard lock(entriesMtx);
			entries.erase(a_editorID);
		}
		std::error_code ec;
		std::filesystem::remove(GetEntryPath(a_editorID), ec);
	}

	std::vector<FOEEntry> Manager::GetEntries() const
	{
		std::vector<FOEEntry> out;
		std::lock_guard lock(entriesMtx);
		out.reserve(entries.size());
		for (auto& kv : entries) out.push_back(kv.second);
		std::sort(out.begin(), out.end(), [](const FOEEntry& a, const FOEEntry& b) {
			return a.editorID < b.editorID;
		});
		return out;
	}

	// ============================================================
	// JSON persistence (one file per weapon)
	// ============================================================
	std::filesystem::path Manager::GetFolder() const
	{
		return std::filesystem::current_path() / "Data" / "F4SE" / "Plugins" / "FPGunplayOverhaul" / "FireOnEmpty";
	}

	std::filesystem::path Manager::GetEntryPath(const std::string& a_editorID) const
	{
		return GetFolder() / (a_editorID + ".json");
	}

	void Manager::SaveEntry(const std::string& a_editorID)
	{
		FOEEntry copy;
		{
			std::lock_guard lock(entriesMtx);
			auto it = entries.find(a_editorID);
			if (it == entries.end()) return;
			copy = it->second;
		}

		auto folder = GetFolder();
		std::error_code ec;
		std::filesystem::create_directories(folder, ec);

		nlohmann::json j;
		j["editorID"]    = copy.editorID;
		j["displayName"] = copy.displayName;
		j["enabled"]     = copy.enabled;
		j["autoFire"]    = copy.autoFire;

		try {
			std::ofstream ofs(GetEntryPath(a_editorID));
			ofs << j.dump(4);
			logger::info("[FireOnEmpty] Saved {} (enabled={}, autoFire={})",
				a_editorID, copy.enabled, copy.autoFire);
		} catch (const std::exception& ex) {
			logger::error("[FireOnEmpty] Failed to save '{}': {}", a_editorID, ex.what());
		}
	}

	void Manager::LoadAllEntries()
	{
		std::lock_guard lock(entriesMtx);
		entries.clear();

		auto folder = GetFolder();
		std::error_code ec;
		if (!std::filesystem::exists(folder, ec)) return;

		for (auto const& it : std::filesystem::directory_iterator(folder, ec)) {
			if (!it.is_regular_file()) continue;
			if (it.path().extension() != ".json") continue;

			try {
				std::ifstream ifs(it.path());
				auto j = nlohmann::json::parse(ifs);

				FOEEntry e;
				e.editorID    = j.value("editorID", it.path().stem().string());
				e.displayName = j.value("displayName", e.editorID);
				e.enabled     = j.value("enabled", false);
				e.autoFire    = j.value("autoFire", false);

				if (!e.editorID.empty()) {
					entries[e.editorID] = std::move(e);
				}
			} catch (const std::exception& ex) {
				logger::error("[FireOnEmpty] Failed to parse {}: {}", it.path().string(), ex.what());
			}
		}
	}
}
