#include "Menu.h"
#include "Inertia.h"
#include "ChamberExclusion.h"
#include "WeaponFOV.h"
#include <format>

// Pull ImGuiMCP types into scope so we don't need to prefix every ImVec4, ImGuiCol_*, etc.
using namespace ImGuiMCP;

// Helper to get equipped weapon base object in Fallout 4 (goes through middleHigh equippedItems)
static RE::TESBoundObject* GetEquippedWeaponBase(RE::PlayerCharacter* player)
{
	return Inertia::InertiaManager::GetEquippedWeaponBaseStatic(player);
}

static std::string GetEquippedWeaponEditorID(RE::PlayerCharacter* player)
{
	return Inertia::InertiaManager::GetEquippedWeaponEditorIDStatic(player);
}

namespace Menu
{
	// Static weapon types array
	static std::vector<WeaponTypeEntry> s_weaponTypes;
	static bool s_weaponTypesNeedRefresh = true;

	// Set to true while DrawWeaponInertiaEditor is executing; helpers use it to
	// accumulate a "any weapon setting changed" flag for IncrementSettingsVersion().
	static bool s_inWeaponEditor = false;
	static bool s_weaponEditorChanged = false;

	// ============================================================
	// Weapon type list
	// ============================================================
	void RefreshWeaponTypesList()
	{
		s_weaponTypes.clear();

		// Standard F4 weapon types (base + Power Armor variants)
		s_weaponTypes.emplace_back("Unarmed",   "Unarmed",   WeaponType::Unarmed);
		s_weaponTypes.emplace_back("Melee",     "Melee",     WeaponType::Melee);
		s_weaponTypes.emplace_back("Pistol",    "Pistol",    WeaponType::Pistol);
		s_weaponTypes.emplace_back("Rifle",     "Rifle",     WeaponType::Rifle);
		s_weaponTypes.emplace_back("Heavy",     "Heavy",     WeaponType::Heavy);
		s_weaponTypes.emplace_back("Energy",    "Energy",    WeaponType::Energy);
		s_weaponTypes.emplace_back("Throwable", "Throwable", WeaponType::Throwable);
		s_weaponTypes.emplace_back("PA Unarmed", "PA_Unarmed", WeaponType::PA_Unarmed);
		s_weaponTypes.emplace_back("PA Melee",   "PA_Melee",   WeaponType::PA_Melee);
		s_weaponTypes.emplace_back("PA Pistol",  "PA_Pistol",  WeaponType::PA_Pistol);
		s_weaponTypes.emplace_back("PA Rifle",   "PA_Rifle",   WeaponType::PA_Rifle);
		s_weaponTypes.emplace_back("PA Heavy",   "PA_Heavy",   WeaponType::PA_Heavy);
		s_weaponTypes.emplace_back("PA Energy",  "PA_Energy",  WeaponType::PA_Energy);

		// Custom keyword-based types
		auto* presets = InertiaPresets::GetSingleton();
		const auto& customTypes = presets->GetCustomWeaponTypes();
		for (const auto& typeName : customTypes) {
			s_weaponTypes.emplace_back(typeName);
		}

		s_weaponTypesNeedRefresh = false;
	}

	const std::vector<WeaponTypeEntry>& GetWeaponTypes()
	{
		if (s_weaponTypes.empty() || s_weaponTypesNeedRefresh) {
			RefreshWeaponTypesList();
		}
		return s_weaponTypes;
	}

	WeaponInertiaSettings& GetWeaponSettingsForEditingByEntry(const WeaponTypeEntry& entry)
	{
		auto* presets = InertiaPresets::GetSingleton();
		if (entry.isCustomType) {
			return presets->GetCustomWeaponTypeSettingsMutable(entry.internalName);
		} else {
			return presets->GetWeaponTypeSettingsMutable(entry.type);
		}
	}

	// ============================================================
	// Registration
	// ============================================================
	void Register()
	{
		if (!F4SEMenuFramework::IsInstalled()) {
			logger::warn("[FPInertia] F4SE Menu Framework is not installed - menu will not be available");
			return;
		}

		F4SEMenuFramework::SetSection("FP Inertia");
		F4SEMenuFramework::AddSectionItem("Inertia Configs", Render);
		F4SEMenuFramework::AddSectionItem("ADS Transitions", RenderADSTransitions);
		F4SEMenuFramework::AddSectionItem("Debug Info", RenderDebugInfo);
		F4SEMenuFramework::AddSectionItem("Extras", RenderExtras);
		State::debugPopoutWindow = F4SEMenuFramework::AddWindow(RenderDebugPopout, false);

		logger::info("[FPInertia] Menu registered with F4SE Menu Framework");
	}

	// ============================================================
	// Widget helpers
	// ============================================================
	bool SliderFloatWithTooltip(const char* label, float* value, float min, float max, const char* format, const char* tooltip)
	{
		bool changed = ImGuiMCP::SliderFloat(label, value, min, max, format);
		if (changed && s_inWeaponEditor) s_weaponEditorChanged = true;
		if (ImGuiMCP::IsItemHovered() && tooltip && tooltip[0]) {
			ImGuiMCP::SetTooltip("%s", tooltip);
		}
		return changed;
	}

	bool CheckboxWithTooltip(const char* label, bool* value, const char* tooltip)
	{
		bool changed = ImGuiMCP::Checkbox(label, value);
		if (changed && s_inWeaponEditor) s_weaponEditorChanged = true;
		if (ImGuiMCP::IsItemHovered() && tooltip && tooltip[0]) {
			ImGuiMCP::SetTooltip("%s", tooltip);
		}
		return changed;
	}

	bool SliderIntWithTooltip(const char* label, int* value, int min, int max, const char* format, const char* tooltip)
	{
		bool changed = ImGuiMCP::SliderInt(label, value, min, max, format);
		if (changed && s_inWeaponEditor) s_weaponEditorChanged = true;
		if (ImGuiMCP::IsItemHovered() && tooltip && tooltip[0]) {
			ImGuiMCP::SetTooltip("%s", tooltip);
		}
		return changed;
	}

	// ============================================================
	// Preset list helpers
	// ============================================================
	void RefreshPresetList()
	{
		auto* presets = InertiaPresets::GetSingleton();
		State::cachedPresetList = presets->GetAvailablePresets();

		const auto& activeName = presets->GetActivePresetName();
		State::selectedPresetIndex = 0;
		for (size_t i = 0; i < State::cachedPresetList.size(); ++i) {
			if (State::cachedPresetList[i] == activeName) {
				State::selectedPresetIndex = static_cast<int>(i);
				break;
			}
		}

		s_weaponTypesNeedRefresh = true;
	}

	// ============================================================
	// Main render callback
	// ============================================================
	void __stdcall Render()
	{
		if (!State::initialized) {
			State::initialized = true;
			State::hasUnsavedChanges = false;
			RefreshPresetList();
		}

		DrawHeader();
		DrawPresetSelector();
		ImGuiMCP::Separator();

		DrawGeneralSettings();
		DrawSettlingSettings();
		DrawMovementInertiaSettings();
		DrawActionBlendSettings();
		DrawPowerArmorSettings();
		DrawDebugSettings();

		ImGuiMCP::Separator();
		DrawWeaponTypeSettings();

		ImGuiMCP::Separator();
		DrawSpecificWeaponSettings();

		ImGuiMCP::Separator();
		DrawSaveLoadButtons();
	}

	// ============================================================
	// Debug content renderer (shared between tab and popout)
	// ============================================================
	static void DrawDebugContent(const Inertia::DebugSnapshot& snap)
	{
		// ---- Weapon Info ----
		ImGuiMCP::TextColored(ImVec4(0.9f, 0.8f, 0.3f, 1.0f), "[ Equipped Weapon ]");
		ImGuiMCP::Separator();

		if (snap.equippedEditorID.empty()) {
			ImGuiMCP::TextColored(ImVec4(0.6f, 0.6f, 0.6f, 1.0f), "  No weapon equipped");
		} else {
			ImGuiMCP::Text("  Name:      %s", snap.equippedDisplayName.empty() ? "(unknown)" : snap.equippedDisplayName.c_str());
			ImGuiMCP::Text("  EditorID:  %s", snap.equippedEditorID.c_str());
			ImGuiMCP::Text("  Base Type: %s", InertiaPresets::GetWeaponTypeDisplayName(snap.detectedWeaponType));
			ImGuiMCP::Text("  Base Wt:   %.2f", snap.baseWeight);

			if (snap.hasSpecificPreset)
				ImGuiMCP::TextColored(ImVec4(0.4f, 1.0f, 0.4f, 1.0f), "  [Has specific JSON preset]");
			else
				ImGuiMCP::TextColored(ImVec4(0.5f, 0.5f, 0.5f, 1.0f), "  [Using weapon-type defaults]");
		}

		ImGuiMCP::Spacing();

		// ---- Player State ----
		ImGuiMCP::TextColored(ImVec4(0.9f, 0.8f, 0.3f, 1.0f), "[ Player State ]");
		ImGuiMCP::Separator();

		auto StateRow = [](const char* label, bool active) {
			ImGuiMCP::Text("  %-18s", label);
			ImGuiMCP::SameLine();
			if (active)
				ImGuiMCP::TextColored(ImVec4(0.3f, 1.0f, 0.3f, 1.0f), "YES");
			else
				ImGuiMCP::TextColored(ImVec4(0.5f, 0.5f, 0.5f, 1.0f), "no");
		};

		StateRow("First Person:",  snap.inFirstPerson);
		StateRow("Weapon Drawn:",  snap.weaponDrawn);
		StateRow("ADS:",           snap.isADS);
		StateRow("Scoped:",        snap.isScoped);
		StateRow("Firing:",        snap.isFiring);
		StateRow("Reloading:",     snap.isReloading);
		StateRow("Melee Action:",  snap.isInMeleeAction);
		StateRow("Sprinting:",     snap.isSprinting);
		StateRow("Jumping:",       snap.isJumping);
		StateRow("In Air:",        snap.isInAir);
		StateRow("Falling:",       snap.isFalling);
		StateRow("Power Armor:",   snap.isInPowerArmor);

		// Havok character state
		const char* havokStateStr = "Unknown";
		switch (snap.havokCharState) {
		case 0: havokStateStr = "OnGround"; break;
		case 1: havokStateStr = "Jumping"; break;
		case 2: havokStateStr = "InAir"; break;
		case 3: havokStateStr = "Climbing"; break;
		case 4: havokStateStr = "Flying"; break;
		}
		ImGuiMCP::TextColored(ImVec4(0.6f, 0.6f, 0.6f, 1.0f),
			"  Havok State: %s (%d)", havokStateStr, snap.havokCharState);

		// Raw bitfield values
		ImGuiMCP::TextColored(ImVec4(0.6f, 0.6f, 0.6f, 1.0f),
			"  gunState=%u  recoil=%u  sustainedFire=%.2fs",
			snap.gunStateRaw, snap.recoilRaw, snap.sustainedFireTime);
		ImGuiMCP::TextColored(ImVec4(0.6f, 0.6f, 0.6f, 1.0f),
			"  moveMode=0x%X  flyState=%u  meleeAttack=%u  airTime=%.2fs",
			snap.moveModeRaw, snap.flyStateRaw, snap.meleeAttackStateRaw, snap.airTimeVal);
		ImGuiMCP::TextColored(ImVec4(0.6f, 0.6f, 0.6f, 1.0f),
			"  sgtm=%.4f", snap.globalTimeMult);

		ImGuiMCP::Spacing();

		// ---- Active Inertia State ----
		ImGuiMCP::TextColored(ImVec4(0.9f, 0.8f, 0.3f, 1.0f), "[ Active Inertia ]");
		ImGuiMCP::Separator();

		ImGuiMCP::Text("  Weight Mult:     %.4f", snap.cachedWeightMult);
		ImGuiMCP::Text("  Action Blend:    %.4f", snap.actionBlendFactor);
		ImGuiMCP::Text("  Settling:        %.4f", snap.settlingFactor);
		ImGuiMCP::Text("  Time Since Move: %.3fs", snap.timeSinceMovement);

		ImGuiMCP::Spacing();

		// ---- Spring Velocities ----
		ImGuiMCP::TextColored(ImVec4(0.9f, 0.8f, 0.3f, 1.0f), "[ Springs ]");
		ImGuiMCP::Separator();
		ImGuiMCP::Text("  CamPos:  %.3f %.3f %.3f", snap.camPosVel.x, snap.camPosVel.y, snap.camPosVel.z);
		ImGuiMCP::Text("  CamRot:  %.3f %.3f %.3f", snap.camRotVel.x, snap.camRotVel.y, snap.camRotVel.z);
		ImGuiMCP::Text("  MovePos: %.3f %.3f %.3f", snap.movePosVel.x, snap.movePosVel.y, snap.movePosVel.z);
		ImGuiMCP::Text("  MoveRot: %.3f %.3f %.3f", snap.moveRotVel.x, snap.moveRotVel.y, snap.moveRotVel.z);

		ImGuiMCP::Spacing();

		// ---- Applied Offset ----
		ImGuiMCP::TextColored(ImVec4(0.9f, 0.8f, 0.3f, 1.0f), "[ Applied Offset ]");
		ImGuiMCP::Separator();
		ImGuiMCP::Text("  Pos: %.4f %.4f %.4f", snap.appliedPosOffset.x, snap.appliedPosOffset.y, snap.appliedPosOffset.z);
		ImGuiMCP::Text("  Rot: %.4f %.4f %.4f", snap.appliedRotOffset.x, snap.appliedRotOffset.y, snap.appliedRotOffset.z);

		ImGuiMCP::Spacing();

		// ---- Event Log ----
		ImGuiMCP::TextColored(ImVec4(0.9f, 0.8f, 0.3f, 1.0f), "[ Recent Events (newest first) ]");
		ImGuiMCP::Separator();
		if (snap.eventCount == 0) {
			ImGuiMCP::TextColored(ImVec4(0.5f, 0.5f, 0.5f, 1.0f), "  (no events yet)");
		} else {
			for (int i = 0; i < snap.eventCount; ++i) {
				const auto& evt = snap.recentEvents[i];
				float age = snap.recentEvents[0].timestamp - evt.timestamp;
				ImVec4 col = (age < 1.0f)
					? ImVec4(0.3f, 1.0f, 0.3f, 1.0f)
					: ImVec4(0.5f, 0.5f, 0.5f, std::max(0.3f, 1.0f - age * 0.1f));
				ImGuiMCP::TextColored(col, "  [%.1fs ago] %s", age, evt.description);
			}
		}
	}

	// ============================================================
	// Debug Info tab — live-updating weapon & inertia state
	// ============================================================
	void __stdcall RenderDebugInfo()
	{
		using namespace Inertia;
		DebugSnapshot snap;
		InertiaManager::GetSingleton()->FillDebugSnapshot(snap);

		// Popout toggle — uses a framework-managed non-pausing window
		bool popoutOpen = State::debugPopoutWindow && State::debugPopoutWindow->IsOpen.load();
		if (ImGuiMCP::Checkbox("Popout Window", &popoutOpen)) {
			if (State::debugPopoutWindow)
				State::debugPopoutWindow->IsOpen.store(popoutOpen);
		}
		if (ImGuiMCP::IsItemHovered())
			ImGuiMCP::SetTooltip("Open a floating window that stays visible over the game\n(persists when the menu is closed)");
		ImGuiMCP::Spacing();

		DrawDebugContent(snap);
	}

	void __stdcall RenderDebugPopout()
	{
		auto viewport = ImGuiMCP::GetMainViewport();

		ImGuiMCP::ImVec2 windowSize = ImGuiMCP::ImVec2{
			viewport->Size.x * 0.3f, viewport->Size.y * 0.5f };
		ImGuiMCP::ImVec2 windowPos = ImGuiMCP::ImVec2{
			viewport->Pos.x + viewport->Size.x - windowSize.x - 20,
			viewport->Pos.y + 20 };

		ImGuiMCP::SetNextWindowPos(windowPos, ImGuiMCP::ImGuiCond_Appearing, { 0, 0 });
		ImGuiMCP::SetNextWindowSize(windowSize, ImGuiMCP::ImGuiCond_Appearing);

		ImGuiMCP::Begin("FP Inertia Debug##FPInertia", nullptr,
			ImGuiMCP::ImGuiWindowFlags_NoCollapse);

		using namespace Inertia;
		DebugSnapshot snap;
		InertiaManager::GetSingleton()->FillDebugSnapshot(snap);
		DrawDebugContent(snap);

		ImGuiMCP::End();
	}

	// ============================================================
	// Header
	// ============================================================
	void DrawHeader()
	{
		auto* settings = Settings::GetSingleton();

		ImGuiMCP::Text("FP Inertia v1.0.0");
		ImGuiMCP::SameLine();

		if (ImGuiMCP::Checkbox("Enabled", &settings->enabled)) {
			State::hasUnsavedChanges = true;
		}
		if (ImGuiMCP::IsItemHovered()) {
			ImGuiMCP::SetTooltip("Master switch — disables ALL features including extras (WBFOV, Chamber Exclusion).");
		}

		ImGuiMCP::SameLine();
		if (ImGuiMCP::Checkbox("Inertia Effects", &settings->inertiaEnabled)) {
			State::hasUnsavedChanges = true;
		}
		if (ImGuiMCP::IsItemHovered()) {
			ImGuiMCP::SetTooltip(
				"Disable inertia effects only.\n"
				"Other features (Weapon FOV, Chamber Exclusion, etc.) remain active.");
		}

		// Live camera state
		auto* camera = RE::PlayerCamera::GetSingleton();
		bool inFirstPerson = false;
		if (camera && camera->currentState) {
			auto sid = camera->currentState->id;
			inFirstPerson = (sid == RE::CameraStates::kFirstPerson || sid == RE::CameraStates::kIronSights);
		}

		ImGuiMCP::SameLine();
		if (inFirstPerson) {
			ImGuiMCP::TextColored(ImVec4(0.4f, 1.0f, 0.4f, 1.0f), "[1st Person]");
		} else {
			ImGuiMCP::TextColored(ImVec4(0.6f, 0.6f, 0.6f, 1.0f), "[3rd Person]");
		}

		// Live weapon drawn state (using the same currentProcess check as Inertia.cpp)
		auto* player = RE::PlayerCharacter::GetSingleton();
		if (player) {
			bool weaponDrawn = player->currentProcess ? player->GetWeaponMagicDrawn() : false;
			ImGuiMCP::SameLine();
			if (weaponDrawn) {
				ImGuiMCP::TextColored(ImVec4(1.0f, 0.8f, 0.3f, 1.0f), "[Weapon Drawn]");
			} else {
				ImGuiMCP::TextColored(ImVec4(0.6f, 0.6f, 0.6f, 1.0f), "[Sheathed]");
			}
		}

		if (State::hasUnsavedChanges) {
			ImGuiMCP::TextColored(ImVec4(1.0f, 0.8f, 0.2f, 1.0f), "(Unsaved changes - session only)");
		}
	}

	// ============================================================
	// Preset Selector
	// ============================================================
	void DrawPresetSelector()
	{
		auto* presets = InertiaPresets::GetSingleton();

		ImGuiMCP::Spacing();

		ImGuiMCP::Text("Active Preset:");
		ImGuiMCP::SameLine();

		if (State::cachedPresetList.empty() && !State::presetListAttemptedRefresh) {
			State::presetListAttemptedRefresh = true;
			RefreshPresetList();
		}

		if (!State::cachedPresetList.empty()) {
			const char* previewValue = (State::selectedPresetIndex >= 0 &&
				State::selectedPresetIndex < static_cast<int>(State::cachedPresetList.size()))
				? State::cachedPresetList[State::selectedPresetIndex].c_str()
				: "None";

			ImGuiMCP::SetNextItemWidth(200.0f);
			if (ImGuiMCP::BeginCombo("##PresetCombo", previewValue)) {
				for (int i = 0; i < static_cast<int>(State::cachedPresetList.size()); ++i) {
					bool isSelected = (State::selectedPresetIndex == i);
					if (ImGuiMCP::Selectable(State::cachedPresetList[i].c_str(), isSelected)) {
						if (State::selectedPresetIndex != i) {
							presets->SetActivePreset(State::cachedPresetList[i]);
							State::selectedPresetIndex = i;
							State::hasUnsavedChanges = false;
						}
					}
					if (ImGuiMCP::IsItemHovered()) {
						auto path = presets->GetPresetPath(State::cachedPresetList[i]);
						ImGuiMCP::SetTooltip("%s", path.string().c_str());
					}
					if (isSelected) ImGuiMCP::SetItemDefaultFocus();
				}
				ImGuiMCP::EndCombo();
			}
			if (ImGuiMCP::IsItemHovered()) {
				std::string tip = "Select a weapon type preset profile.\nEach preset contains settings for all weapon types.\nChanges are auto-saved when switching presets.";
				if (State::selectedPresetIndex >= 0 && State::selectedPresetIndex < static_cast<int>(State::cachedPresetList.size())) {
					auto path = presets->GetPresetPath(State::cachedPresetList[State::selectedPresetIndex]);
					tip += "\n\nFile: " + path.string();
				}
				ImGuiMCP::SetTooltip("%s", tip.c_str());
			}
		} else {
			ImGuiMCP::TextColored(ImVec4(0.8f, 0.4f, 0.4f, 1.0f), "No presets found");
		}

		ImGuiMCP::SameLine();
		if (ImGuiMCP::Button("New")) {
			State::showCreatePresetPopup = true;
			memset(State::newPresetName, 0, sizeof(State::newPresetName));
		}
		if (ImGuiMCP::IsItemHovered()) ImGuiMCP::SetTooltip("Create a new preset from current settings");

		ImGuiMCP::SameLine();
		if (ImGuiMCP::Button("Refresh")) {
			State::presetListAttemptedRefresh = true;
			RefreshPresetList();
		}
		if (ImGuiMCP::IsItemHovered()) ImGuiMCP::SetTooltip("Refresh the list of available presets from disk");

		if (State::cachedPresetList.size() > 1) {
			ImGuiMCP::SameLine();
			ImGuiMCP::PushStyleColor(ImGuiCol_Button, ImVec4(0.6f, 0.2f, 0.2f, 1.0f));
			ImGuiMCP::PushStyleColor(ImGuiCol_ButtonHovered, ImVec4(0.8f, 0.3f, 0.3f, 1.0f));
			if (ImGuiMCP::Button("Delete")) {
				State::showDeletePresetConfirm = true;
			}
			ImGuiMCP::PopStyleColor(2);
			if (ImGuiMCP::IsItemHovered()) ImGuiMCP::SetTooltip("Delete a preset file (cannot delete the active preset)");
		}

		ImGuiMCP::TextColored(ImVec4(0.5f, 0.5f, 0.5f, 1.0f),
			"Editing: Data\\F4SE\\Plugins\\FPInertia\\%s.json", presets->GetActivePresetName().c_str());

		// Create preset popup
		if (State::showCreatePresetPopup) {
			ImGuiMCP::OpenPopup("Create New Preset");
		}

		if (ImGuiMCP::BeginPopupModal("Create New Preset", &State::showCreatePresetPopup, ImGuiWindowFlags_AlwaysAutoResize)) {
			ImGuiMCP::Text("Enter a name for the new preset:");
			ImGuiMCP::SetNextItemWidth(250.0f);
			ImGuiMCP::InputText("##NewPresetName", State::newPresetName, sizeof(State::newPresetName));
			ImGuiMCP::Spacing();

			bool validName = strlen(State::newPresetName) > 0;
			bool nameExists = false;
			for (const auto& p : State::cachedPresetList) {
				if (p == State::newPresetName) { nameExists = true; break; }
			}
			if (nameExists) {
				ImGuiMCP::TextColored(ImVec4(1.0f, 0.4f, 0.4f, 1.0f), "A preset with this name already exists!");
				validName = false;
			}

			if (!validName) ImGuiMCP::BeginDisabled();
			if (ImGuiMCP::Button("Create", ImVec2(120, 0))) {
				presets->CreateNewPreset(State::newPresetName);
				RefreshPresetList();
				State::showCreatePresetPopup = false;
				State::hasUnsavedChanges = false;
			}
			if (!validName) ImGuiMCP::EndDisabled();

			ImGuiMCP::SameLine();
			if (ImGuiMCP::Button("Cancel", ImVec2(120, 0))) {
				State::showCreatePresetPopup = false;
			}
			ImGuiMCP::EndPopup();
		}

		// Delete preset popup
		if (State::showDeletePresetConfirm) {
			ImGuiMCP::OpenPopup("Delete Preset");
		}

		if (ImGuiMCP::BeginPopupModal("Delete Preset", &State::showDeletePresetConfirm, ImGuiWindowFlags_AlwaysAutoResize)) {
			ImGuiMCP::Text("Select a preset to delete:");
			ImGuiMCP::Spacing();

			static int deleteIndex = -1;
			ImGuiMCP::SetNextItemWidth(250.0f);
			const char* deletePreview = (deleteIndex >= 0 && deleteIndex < static_cast<int>(State::cachedPresetList.size()))
				? State::cachedPresetList[deleteIndex].c_str() : "Select...";

			if (ImGuiMCP::BeginCombo("##DeletePresetCombo", deletePreview)) {
				for (int i = 0; i < static_cast<int>(State::cachedPresetList.size()); ++i) {
					if (State::cachedPresetList[i] == presets->GetActivePresetName()) continue;
					bool isSelected = (deleteIndex == i);
					if (ImGuiMCP::Selectable(State::cachedPresetList[i].c_str(), isSelected)) {
						deleteIndex = i;
					}
					if (ImGuiMCP::IsItemHovered()) {
						auto path = presets->GetPresetPath(State::cachedPresetList[i]);
						ImGuiMCP::SetTooltip("%s", path.string().c_str());
					}
				}
				ImGuiMCP::EndCombo();
			}

			ImGuiMCP::Spacing();
			bool canDelete = deleteIndex >= 0 && deleteIndex < static_cast<int>(State::cachedPresetList.size())
				&& State::cachedPresetList[deleteIndex] != presets->GetActivePresetName();

			if (!canDelete) ImGuiMCP::BeginDisabled();
			ImGuiMCP::PushStyleColor(ImGuiCol_Button, ImVec4(0.6f, 0.2f, 0.2f, 1.0f));
			ImGuiMCP::PushStyleColor(ImGuiCol_ButtonHovered, ImVec4(0.8f, 0.3f, 0.3f, 1.0f));
			if (ImGuiMCP::Button("Delete", ImVec2(120, 0))) {
				std::string toDelete = State::cachedPresetList[deleteIndex];
				presets->DeletePreset(toDelete);
				RefreshPresetList();
				deleteIndex = -1;
				State::showDeletePresetConfirm = false;
			}
			ImGuiMCP::PopStyleColor(2);
			if (!canDelete) ImGuiMCP::EndDisabled();

			ImGuiMCP::SameLine();
			if (ImGuiMCP::Button("Cancel", ImVec2(120, 0))) {
				deleteIndex = -1;
				State::showDeletePresetConfirm = false;
			}
			ImGuiMCP::TextColored(ImVec4(0.6f, 0.6f, 0.6f, 1.0f), "(Cannot delete the currently active preset)");
			ImGuiMCP::EndPopup();
		}

		ImGuiMCP::Spacing();
	}

	// ============================================================
	// General Settings
	// ============================================================
	void DrawGeneralSettings()
	{
		auto* settings = Settings::GetSingleton();
		ImGuiTreeNodeFlags flags = State::generalExpanded ? ImGuiTreeNodeFlags_DefaultOpen : 0;

		if (ImGuiMCP::CollapsingHeader("General Settings", flags)) {
			State::generalExpanded = true;

			if (CheckboxWithTooltip("Enable Position Offset", &settings->enablePosition,
				"Enable positional movement (weapon moves based on camera/movement)")) {
				State::hasUnsavedChanges = true;
			}
			if (CheckboxWithTooltip("Enable Rotation Offset", &settings->enableRotation,
				"Enable rotational movement (weapon tilts based on camera/movement)")) {
				State::hasUnsavedChanges = true;
			}
			if (CheckboxWithTooltip("Require Weapon Drawn", &settings->requireWeaponDrawn,
				"Only apply inertia effects when weapon is drawn")) {
				State::hasUnsavedChanges = true;
			}

			ImGuiMCP::Spacing();

			if (SliderFloatWithTooltip("Global Intensity", &settings->globalIntensity, 0.0f, 5.0f, "%.2f",
				"Master multiplier for all inertia effects\n1.0 = normal, 0.5 = half, 2.0 = double")) {
				State::hasUnsavedChanges = true;
			}
			if (SliderFloatWithTooltip("Smoothing Factor", &settings->smoothingFactor, 0.0f, 1.0f, "%.2f",
				"Camera velocity smoothing (0 = no smoothing, 1 = maximum)\nHigher values reduce jitter but add latency")) {
				State::hasUnsavedChanges = true;
			}
		} else {
			State::generalExpanded = false;
		}
	}

	// ============================================================
	// Settling
	// ============================================================
	void DrawSettlingSettings()
	{
		auto* settings = Settings::GetSingleton();
		ImGuiTreeNodeFlags flags = State::settlingExpanded ? ImGuiTreeNodeFlags_DefaultOpen : 0;

		if (ImGuiMCP::CollapsingHeader("Settling Behavior", flags)) {
			State::settlingExpanded = true;

			ImGuiMCP::TextWrapped("When the camera stops moving, the spring gradually dampens to reduce wobbling.");
			ImGuiMCP::Spacing();

			if (SliderFloatWithTooltip("Settle Delay", &settings->settleDelay, 0.0f, 2.0f, "%.2f sec",
				"Delay before settling starts after camera stops moving")) {
				State::hasUnsavedChanges = true;
			}
			if (SliderFloatWithTooltip("Settle Speed", &settings->settleSpeed, 0.5f, 10.0f, "%.1f",
				"How fast the damping increases once settling begins")) {
				State::hasUnsavedChanges = true;
			}
			if (SliderFloatWithTooltip("Settle Damping Mult", &settings->settleDampingMult, 1.0f, 10.0f, "%.1fx",
				"Maximum damping multiplier when fully settled")) {
				State::hasUnsavedChanges = true;
			}
		} else {
			State::settlingExpanded = false;
		}
	}

	// ============================================================
	// Movement Inertia
	// ============================================================
	void DrawMovementInertiaSettings()
	{
		auto* settings = Settings::GetSingleton();
		ImGuiTreeNodeFlags flags = State::movementExpanded ? ImGuiTreeNodeFlags_DefaultOpen : 0;

		if (ImGuiMCP::CollapsingHeader("Movement Inertia", flags)) {
			State::movementExpanded = true;

			ImGuiMCP::TextWrapped("Arm sway based on player movement. Per-weapon settings are in the weapon section.");
			ImGuiMCP::Spacing();

			if (CheckboxWithTooltip("Enable Movement Inertia (Global)", &settings->movementInertiaEnabled,
				"Master toggle for movement-based arm sway")) {
				State::hasUnsavedChanges = true;
			}

			if (settings->movementInertiaEnabled) {
				if (SliderFloatWithTooltip("Strength", &settings->movementInertiaStrength, 0.0f, 20.0f, "%.1f",
					"Global strength multiplier for movement inertia")) {
					State::hasUnsavedChanges = true;
				}
				if (SliderFloatWithTooltip("Threshold", &settings->movementInertiaThreshold, 0.0f, 200.0f, "%.0f units/sec",
					"Minimum movement speed to trigger effect")) {
					State::hasUnsavedChanges = true;
				}

				ImGuiMCP::Spacing();
				ImGuiMCP::Text("Forward/Backward Movement:");
				if (CheckboxWithTooltip("Enable Forward/Back Inertia", &settings->forwardBackInertia,
					"Enable arm sway for forward/backward movement\nDefault: OFF (only strafing causes sway)")) {
					State::hasUnsavedChanges = true;
				}

				ImGuiMCP::Spacing();
				if (CheckboxWithTooltip("Disable Vanilla Sway", &settings->disableVanillaSway,
					"Disable the game's built-in walk/run arm sway\nUseful if this mod's movement inertia feels doubled")) {
					State::hasUnsavedChanges = true;
				}
			}
		} else {
			State::movementExpanded = false;
		}
	}

	// ============================================================
	// Action Blending
	// ============================================================
	void DrawActionBlendSettings()
	{
		auto* settings = Settings::GetSingleton();
		ImGuiTreeNodeFlags flags = State::actionBlendExpanded ? ImGuiTreeNodeFlags_DefaultOpen : 0;

		if (ImGuiMCP::CollapsingHeader("Action Blending", flags)) {
			State::actionBlendExpanded = true;

			ImGuiMCP::TextWrapped("Smoothly reduce inertia during combat actions for better animation blending.");
			ImGuiMCP::Spacing();

			if (CheckboxWithTooltip("Blend During Firing", &settings->blendDuringFiring,
				"Reduce inertia while actively firing")) {
				State::hasUnsavedChanges = true;
			}
			if (settings->blendDuringFiring) {
				if (SliderFloatWithTooltip("  Firing Min Intensity", &settings->blendFiringMinIntensity, 0.0f, 1.0f, "%.2f",
					"Minimum inertia intensity while firing\n0 = fully suppressed, 1 = no reduction")) {
					State::hasUnsavedChanges = true;
				}
			}

			if (CheckboxWithTooltip("Blend During Reload", &settings->blendDuringReload,
				"Reduce inertia while reloading")) {
				State::hasUnsavedChanges = true;
			}
			if (settings->blendDuringReload) {
				if (SliderFloatWithTooltip("  Reload Min Intensity", &settings->blendReloadMinIntensity, 0.0f, 1.0f, "%.2f",
					"Minimum inertia intensity while reloading\n0 = fully suppressed, 1 = no reduction")) {
					State::hasUnsavedChanges = true;
				}
			}

			if (CheckboxWithTooltip("Blend During Melee", &settings->blendDuringMelee,
				"Reduce inertia while performing melee attacks")) {
				State::hasUnsavedChanges = true;
			}
			if (settings->blendDuringMelee) {
				if (SliderFloatWithTooltip("  Melee Min Intensity", &settings->blendMeleeMinIntensity, 0.0f, 1.0f, "%.2f",
					"Minimum inertia intensity during melee attacks\n0 = fully suppressed, 1 = no reduction")) {
					State::hasUnsavedChanges = true;
				}
			}

			ImGuiMCP::Spacing();

			if (SliderFloatWithTooltip("Blend Speed", &settings->actionBlendSpeed, 1.0f, 20.0f, "%.1f",
				"How fast to blend inertia in/out during actions\nHigher = snappier transitions")) {
				State::hasUnsavedChanges = true;
			}

			if (SliderFloatWithTooltip("Blend-Back Lead Time", &settings->actionBlendBackLeadTime, 0.0f, 2.0f, "%.2f s",
				"Start blending inertia back this many seconds before the action ends\n"
				"Uses the measured duration of the previous action of each type\n"
				"0 = only blend back after the action ends")) {
				State::hasUnsavedChanges = true;
			}
		} else {
			State::actionBlendExpanded = false;
		}
	}

	// ============================================================
	// Power Armor
	// ============================================================
	void DrawPowerArmorSettings()
	{
		auto* settings = Settings::GetSingleton();
		ImGuiTreeNodeFlags flags = State::powerArmorExpanded ? ImGuiTreeNodeFlags_DefaultOpen : 0;

		if (ImGuiMCP::CollapsingHeader("Power Armor", flags)) {
			State::powerArmorExpanded = true;

			ImGuiMCP::TextWrapped("Settings for inertia when wearing Power Armor.");
			ImGuiMCP::Spacing();

			if (CheckboxWithTooltip("Enable Inertia in Power Armor", &settings->enableInPowerArmor,
				"Apply inertia effects when wearing Power Armor")) {
				State::hasUnsavedChanges = true;
			}

			if (settings->enableInPowerArmor) {
				if (CheckboxWithTooltip("Use Separate PA Profiles", &settings->usePASeparateProfiles,
					"Use dedicated PA_* weapon type profiles when in Power Armor\n"
					"(PA Rifle, PA Pistol, etc.) — allows heavier, more sluggish feel\n"
					"Disable to use base profiles with the multiplier below")) {
					State::hasUnsavedChanges = true;
				}

				if (!settings->usePASeparateProfiles) {
					if (SliderFloatWithTooltip("PA Multiplier", &settings->powerArmorMult, 0.0f, 3.0f, "%.2f",
						"Global intensity multiplier applied when in Power Armor\n"
						"Used when separate profiles are disabled")) {
						State::hasUnsavedChanges = true;
					}
				}

				ImGuiMCP::TextColored(ImVec4(0.6f, 0.6f, 0.6f, 1.0f),
					"PA weapon type profiles (PA Rifle, PA Pistol, etc.) are\nedited in the Per-Weapon Type Settings section below.");
			}
		} else {
			State::powerArmorExpanded = false;
		}
	}

	// ============================================================
	// Debug
	// ============================================================
	void DrawDebugSettings()
	{
		auto* settings = Settings::GetSingleton();
		ImGuiTreeNodeFlags flags = State::debugExpanded ? ImGuiTreeNodeFlags_DefaultOpen : 0;

		if (ImGuiMCP::CollapsingHeader("Debug", flags)) {
			State::debugExpanded = true;

			if (CheckboxWithTooltip("Debug Logging", &settings->debugLogging,
				"Enable detailed debug messages in the log file")) {
				State::hasUnsavedChanges = true;
			}
			if (CheckboxWithTooltip("Debug On Screen", &settings->debugOnScreen,
				"Show debug information on screen")) {
				State::hasUnsavedChanges = true;
			}

			ImGuiMCP::Spacing();
			ImGuiMCP::Separator();
			ImGuiMCP::Text("Quick Actions:");

			if (ImGuiMCP::Button("Reset Springs")) {
				Inertia::InertiaManager::GetSingleton()->Reset();
			}
			if (ImGuiMCP::IsItemHovered()) {
				ImGuiMCP::SetTooltip("Reset all spring states to zero (stops any current motion)");
			}
		} else {
			State::debugExpanded = false;
		}
	}

	// ============================================================
	// Per-Weapon Type Settings
	// ============================================================
	void DrawWeaponTypeSettings()
	{
		auto* presets = InertiaPresets::GetSingleton();
		ImGuiTreeNodeFlags flags = State::weaponSettingsExpanded ? ImGuiTreeNodeFlags_DefaultOpen : 0;

		if (ImGuiMCP::CollapsingHeader("Per-Weapon Type Settings", flags)) {
			State::weaponSettingsExpanded = true;

			ImGuiMCP::TextWrapped("Each weapon type has its own spring parameters. Power Armor variants are listed separately.");
			ImGuiMCP::Spacing();

			const auto& types = GetWeaponTypes();

			// Clamp index if types list changed
			if (State::selectedWeaponTypeIndex >= static_cast<int>(types.size())) {
				State::selectedWeaponTypeIndex = 0;
			}

			std::string comboLabel = types[State::selectedWeaponTypeIndex].displayName;
			if (types[State::selectedWeaponTypeIndex].isCustomType) comboLabel += " *";

			ImGuiMCP::SetNextItemWidth(220.0f);
			if (ImGuiMCP::BeginCombo("Weapon Type", comboLabel.c_str())) {
				bool shownCustomSep = false;
				bool shownPASep = false;

				for (int i = 0; i < static_cast<int>(types.size()); ++i) {
					// Separator before PA types
					if (!shownPASep && !types[i].isCustomType &&
						(types[i].type == WeaponType::PA_Unarmed)) {
						ImGuiMCP::Separator();
						ImGuiMCP::TextDisabled("-- Power Armor Variants --");
						shownPASep = true;
					}
					// Separator before custom types
					if (types[i].isCustomType && !shownCustomSep) {
						ImGuiMCP::Separator();
						ImGuiMCP::TextDisabled("-- Custom Types --");
						shownCustomSep = true;
					}

					bool isSelected = (State::selectedWeaponTypeIndex == i);
					std::string label = types[i].displayName;
					if (types[i].isCustomType) label += " *";

					if (ImGuiMCP::Selectable(label.c_str(), isSelected)) {
						State::selectedWeaponTypeIndex = i;
					}
					if (isSelected) ImGuiMCP::SetItemDefaultFocus();
				}
				ImGuiMCP::EndCombo();
			}
			if (ImGuiMCP::IsItemHovered()) {
				ImGuiMCP::SetTooltip("Select weapon type to edit.\n* = Custom keyword-based type from mapping files");
			}

			// Show equipped weapon info
			auto* player = RE::PlayerCharacter::GetSingleton();
			std::string equippedWeaponID;
			bool hasSpecificPreset = false;
			std::string equippedWeaponName;

			if (player) {
				auto* base = GetEquippedWeaponBase(player);
				if (base) {
					equippedWeaponID = GetEquippedWeaponEditorID(player);
					auto* fn_obj = base->As<RE::TESFullName>();
					if (fn_obj) equippedWeaponName = fn_obj->GetFullName();
					if (equippedWeaponName.empty()) equippedWeaponName = equippedWeaponID;
					hasSpecificPreset = !equippedWeaponID.empty() && presets->HasSpecificWeaponSettings(equippedWeaponID);

					ImGuiMCP::SameLine();
					if (hasSpecificPreset) {
						ImGuiMCP::TextColored(ImVec4(0.5f, 1.0f, 0.5f, 1.0f),
							"(Equipped: %s - SPECIFIC PRESET ACTIVE)", equippedWeaponName.empty() ? equippedWeaponID.c_str() : equippedWeaponName.c_str());
					} else {
						ImGuiMCP::TextColored(ImVec4(0.4f, 0.8f, 1.0f, 1.0f),
							"(Equipped: %s)", equippedWeaponName.empty() ? equippedWeaponID.c_str() : equippedWeaponName.c_str());
					}
				}
			}

			// Specific preset buttons for equipped weapon
			if (!equippedWeaponID.empty()) {
				if (hasSpecificPreset) {
					if (ImGuiMCP::Button("Update Specific Preset")) {
						const auto& entry = types[State::selectedWeaponTypeIndex];
						const auto& currentSettings = GetWeaponSettingsForEditingByEntry(entry);
						auto& specificSettings = presets->GetOrCreateSpecificWeaponSettings(equippedWeaponID, entry.type);
						specificSettings = currentSettings;
						presets->SetWeaponTypeOverride(equippedWeaponID, entry.type);
						presets->SaveSpecificWeaponPreset(equippedWeaponID);
						State::saveStatusMsg = std::format("Updated preset for {}", equippedWeaponID);
						State::saveStatusTimer = 4.0f;
					}
					if (ImGuiMCP::IsItemHovered()) {
						ImGuiMCP::SetTooltip("Update the specific preset for '%s' with current type settings", equippedWeaponID.c_str());
					}
					ImGuiMCP::SameLine();
					if (ImGuiMCP::Button("Delete Specific Preset")) {
						presets->RemoveSpecificWeaponSettings(equippedWeaponID);
						State::saveStatusMsg = std::format("Deleted preset for {}", equippedWeaponID);
						State::saveStatusTimer = 4.0f;
					}
					if (ImGuiMCP::IsItemHovered()) {
						ImGuiMCP::SetTooltip("Delete the specific preset - weapon will use type settings instead");
					}
				} else {
					if (ImGuiMCP::Button("Save as Specific Weapon Preset")) {
						const auto& entry = types[State::selectedWeaponTypeIndex];
						const auto& currentSettings = GetWeaponSettingsForEditingByEntry(entry);
						auto& specificSettings = presets->GetOrCreateSpecificWeaponSettings(equippedWeaponID, entry.type);
						specificSettings = currentSettings;
						presets->SetWeaponTypeOverride(equippedWeaponID, entry.type);
						presets->SaveSpecificWeaponPreset(equippedWeaponID);
						State::saveStatusMsg = std::format("Created preset for {} ({})", equippedWeaponID, entry.displayName);
						State::saveStatusTimer = 4.0f;
					}
					if (ImGuiMCP::IsItemHovered()) {
						ImGuiMCP::SetTooltip("Save current type settings as a specific preset for '%s'", equippedWeaponID.c_str());
					}
				}
			}

			// Select equipped type button
			if (ImGuiMCP::Button("Select Equipped Type")) {
				if (player) {
					auto* base = GetEquippedWeaponBase(player);
					if (base) {
						// Ask the InertiaManager to detect the type (reuses its full detection logic)
						auto* mgr = Inertia::InertiaManager::GetSingleton();
						(void)mgr;  // Detection uses internal state; best we can do here is keyword lookup

						// Basic keyword/type detection for menu display
						auto* weap = base->As<RE::TESObjectWEAP>();
						WeaponType detected = WeaponType::Rifle;
						if (weap) {
							// Check custom keyword types first
							std::string customType = presets->GetBestKeywordMatch(weap);
							if (!customType.empty()) {
								for (int i = 0; i < static_cast<int>(types.size()); ++i) {
									if (types[i].isCustomType && types[i].internalName == customType) {
										State::selectedWeaponTypeIndex = i;
										detected = WeaponType::Unarmed; // sentinel to skip standard search
										break;
									}
								}
							} else {
								// EditorID override?
								if (!equippedWeaponID.empty() && presets->HasWeaponTypeOverride(equippedWeaponID)) {
									detected = presets->GetWeaponTypeOverride(equippedWeaponID);
								} else {
									// F4: weaponData.type int8_t: 0=HandToHand, 1-6=Melee, 14=Thrown
									auto t = static_cast<std::int8_t>(weap->weaponData.type);
									if (t == 0)                      detected = WeaponType::Unarmed;
									else if (t >= 1 && t <= 6)       detected = WeaponType::Melee;
									else if (t == 14)                detected = WeaponType::Throwable;
									else                             detected = WeaponType::Rifle;
								}

								for (int i = 0; i < static_cast<int>(types.size()); ++i) {
									if (!types[i].isCustomType && types[i].type == detected) {
										State::selectedWeaponTypeIndex = i;
										break;
									}
								}
							}
						}
					}
				}
			}
			if (ImGuiMCP::IsItemHovered()) {
				ImGuiMCP::SetTooltip("Switch to editing the currently equipped weapon's type");
			}

			// Copy to Preset button
			ImGuiMCP::SameLine();
			if (ImGuiMCP::Button("Copy to Preset...")) {
				if (State::selectedWeaponTypeIndex >= 0 && State::selectedWeaponTypeIndex < static_cast<int>(types.size())) {
					State::copySourceWeaponType = types[State::selectedWeaponTypeIndex].displayName;
					State::copySourcePreset = presets->GetActivePresetName();
					State::copyTargetPresetIndex = 0;
					State::showCopyToPresetPopup = true;
					RefreshPresetList();
				}
			}
			if (ImGuiMCP::IsItemHovered()) {
				ImGuiMCP::SetTooltip("Copy settings for this weapon type to another preset");
			}

			// Copy to Preset popup
			if (State::showCopyToPresetPopup) {
				ImGuiMCP::OpenPopup("Copy to Preset");
			}

			if (ImGuiMCP::BeginPopupModal("Copy to Preset", &State::showCopyToPresetPopup, ImGuiWindowFlags_AlwaysAutoResize)) {
				ImGuiMCP::Text("Copy Weapon Type Settings");
				ImGuiMCP::Separator();

				ImGuiMCP::TextColored(ImVec4(0.4f, 0.8f, 1.0f, 1.0f), "Source:");
				ImGuiMCP::BulletText("Preset: %s", State::copySourcePreset.c_str());
				ImGuiMCP::BulletText("Weapon Type: %s", State::copySourceWeaponType.c_str());

				ImGuiMCP::Spacing();
				ImGuiMCP::Separator();
				ImGuiMCP::Spacing();

				ImGuiMCP::TextColored(ImVec4(1.0f, 0.8f, 0.4f, 1.0f), "Target Preset:");

				const char* targetPreview = (State::copyTargetPresetIndex >= 0 &&
					State::copyTargetPresetIndex < static_cast<int>(State::cachedPresetList.size()))
					? State::cachedPresetList[State::copyTargetPresetIndex].c_str() : "";

				if (ImGuiMCP::BeginCombo("##TargetPreset", targetPreview)) {
					for (int i = 0; i < static_cast<int>(State::cachedPresetList.size()); ++i) {
						bool isSameAsSource = (State::cachedPresetList[i] == State::copySourcePreset);
						if (isSameAsSource) ImGuiMCP::PushStyleColor(ImGuiCol_Text, ImVec4(0.5f, 0.5f, 0.5f, 1.0f));

						std::string lbl = State::cachedPresetList[i];
						if (isSameAsSource) lbl += " (current)";
						bool sel = (State::copyTargetPresetIndex == i);
						if (ImGuiMCP::Selectable(lbl.c_str(), sel)) State::copyTargetPresetIndex = i;
						if (ImGuiMCP::IsItemHovered()) {
							auto path = presets->GetPresetPath(State::cachedPresetList[i]);
							ImGuiMCP::SetTooltip("%s", path.string().c_str());
						}
						if (isSameAsSource) ImGuiMCP::PopStyleColor();
						if (sel) ImGuiMCP::SetItemDefaultFocus();
					}
					ImGuiMCP::EndCombo();
				}

				ImGuiMCP::Spacing();

				std::string targetPreset = (State::copyTargetPresetIndex >= 0 &&
					State::copyTargetPresetIndex < static_cast<int>(State::cachedPresetList.size()))
					? State::cachedPresetList[State::copyTargetPresetIndex] : "";

				bool canConfirm = !targetPreset.empty() && targetPreset != State::copySourcePreset;

				if (!targetPreset.empty() && targetPreset != State::copySourcePreset) {
					ImGuiMCP::TextColored(ImVec4(1.0f, 1.0f, 0.4f, 1.0f), "Warning:");
					ImGuiMCP::TextWrapped("This will OVERWRITE the '%s' settings in preset '%s'.",
						State::copySourceWeaponType.c_str(), targetPreset.c_str());
				} else if (targetPreset == State::copySourcePreset) {
					ImGuiMCP::TextColored(ImVec4(1.0f, 0.4f, 0.4f, 1.0f), "Cannot copy to the same preset!");
				}

				ImGuiMCP::Spacing();
				ImGuiMCP::Separator();
				ImGuiMCP::Spacing();

				if (!canConfirm) ImGuiMCP::PushStyleVar(ImGuiStyleVar_Alpha, 0.5f);
				if (ImGuiMCP::Button("Confirm Copy", ImVec2(120, 0)) && canConfirm) {
					const auto& sourceEntry = types[State::selectedWeaponTypeIndex];
					const auto& sourceSettings = GetWeaponSettingsForEditingByEntry(sourceEntry);
					std::string currentPreset = presets->GetActivePresetName();
					presets->SetActivePreset(targetPreset);
					auto& targetSettings = GetWeaponSettingsForEditingByEntry(sourceEntry);
					targetSettings = sourceSettings;
					presets->SaveWeaponTypePresets();
					presets->SetActivePreset(currentPreset);
					logger::info("[FPInertia] Copied {} settings from '{}' to '{}'",
						State::copySourceWeaponType, currentPreset, targetPreset);
					State::showCopyToPresetPopup = false;
					ImGuiMCP::CloseCurrentPopup();
				}
				if (!canConfirm) ImGuiMCP::PopStyleVar();

				ImGuiMCP::SameLine();
				if (ImGuiMCP::Button("Cancel", ImVec2(120, 0))) {
					State::showCopyToPresetPopup = false;
					ImGuiMCP::CloseCurrentPopup();
				}
				ImGuiMCP::EndPopup();
			}

			ImGuiMCP::Separator();

			// The actual weapon type editor
			if (State::selectedWeaponTypeIndex >= 0 && State::selectedWeaponTypeIndex < static_cast<int>(types.size())) {
				const auto& entry = types[State::selectedWeaponTypeIndex];
				auto& weaponSettings = GetWeaponSettingsForEditingByEntry(entry);
				if (entry.isCustomType) {
					ImGuiMCP::TextColored(ImVec4(0.4f, 0.8f, 1.0f, 1.0f), "(Custom Keyword-Based Type)");
				}
				DrawWeaponInertiaEditor(weaponSettings, entry.displayName.c_str());
			}
		} else {
			State::weaponSettingsExpanded = false;
		}
	}

	// ============================================================
	// Per-weapon inertia editor (full parameter set)
	// ============================================================
	void DrawWeaponInertiaEditor(WeaponInertiaSettings& settings, const char* label)
	{
		ImGuiMCP::PushID(label);
		s_inWeaponEditor = true;
		s_weaponEditorChanged = false;

		// Master toggle
		ImGuiMCP::PushStyleColor(ImGuiCol_Text,
			settings.enabled ? ImVec4(0.4f, 1.0f, 0.4f, 1.0f) : ImVec4(1.0f, 0.4f, 0.4f, 1.0f));
		if (ImGuiMCP::Checkbox("Enable Inertia for This Weapon Type", &settings.enabled)) {
			State::hasUnsavedChanges = true;
			s_weaponEditorChanged = true;
		}
		ImGuiMCP::PopStyleColor();
		if (ImGuiMCP::IsItemHovered()) {
			ImGuiMCP::SetTooltip("Master toggle - when disabled, no inertia is applied for this weapon type.");
		}

		if (!settings.enabled) {
			ImGuiMCP::TextColored(ImVec4(1.0f, 0.6f, 0.2f, 1.0f), "Inertia disabled for this weapon type");
			ImGuiMCP::Spacing();
		}

		if (!settings.enabled) ImGuiMCP::PushStyleVar(ImGuiStyleVar_Alpha, 0.5f);

		// ---- Camera Inertia ----
		if (ImGuiMCP::TreeNodeEx("Camera Inertia", ImGuiTreeNodeFlags_DefaultOpen)) {
			ImGuiMCP::TextColored(ImVec4(0.7f, 0.7f, 0.7f, 1.0f), "Responds to looking around (camera rotation)");

			if (SliderFloatWithTooltip("Stiffness##cam", &settings.stiffness, 10.0f, 1000.0f, "%.0f",
				"Spring stiffness - higher = faster return to center")) { State::hasUnsavedChanges = true; }
			if (SliderFloatWithTooltip("Damping##cam", &settings.damping, 1.0f, 100.0f, "%.1f",
				"Damping - reduces oscillation/wobble")) { State::hasUnsavedChanges = true; }
			if (SliderFloatWithTooltip("Max Offset##cam", &settings.maxOffset, 0.0f, 50.0f, "%.1f",
				"Maximum position offset in units")) { State::hasUnsavedChanges = true; }
			if (SliderFloatWithTooltip("Max Rotation##cam", &settings.maxRotation, 0.0f, 90.0f, "%.1f deg",
				"Maximum rotation offset in degrees")) { State::hasUnsavedChanges = true; }
			if (SliderFloatWithTooltip("Mass##cam", &settings.mass, 0.1f, 10.0f, "%.2f",
				"Virtual mass - heavier = more inertia/lag")) { State::hasUnsavedChanges = true; }

			ImGuiMCP::Spacing();
			ImGuiMCP::Text("Multipliers:");
			if (SliderFloatWithTooltip("Pitch Mult##cam", &settings.pitchMultiplier, 0.0f, 5.0f, "%.2f",
				"Multiplier for pitch rotation (looking up/down tilt)")) { State::hasUnsavedChanges = true; }
			if (SliderFloatWithTooltip("Roll Mult##cam", &settings.rollMultiplier, 0.0f, 5.0f, "%.2f",
				"Multiplier for roll effect (wavy side-to-side motion)")) { State::hasUnsavedChanges = true; }
			if (SliderFloatWithTooltip("Global Pitch Mult##cam", &settings.cameraPitchMult, 0.0f, 5.0f, "%.2f",
				"Global multiplier for ALL pitch effects")) { State::hasUnsavedChanges = true; }
			if (SliderFloatWithTooltip("Momentum Decay##cam", &settings.momentumDecay, 0.5f, 50.0f, "%.1f",
				"How quickly the weapon catches up to camera movement.\n"
				"Lower = heavier feel, more visible lag/inertia during fast gameplay.\n"
				"Higher = snappier, weapon follows camera more closely.\n"
				"This controls the exponential lag rate for pitch and yaw tracking.")) { State::hasUnsavedChanges = true; }

			if (CheckboxWithTooltip("Invert Pitch (Up/Down)##cam", &settings.invertCameraPitch,
				"Invert the pitch (up/down look) camera inertia")) { State::hasUnsavedChanges = true; }
			ImGuiMCP::SameLine();
			if (CheckboxWithTooltip("Invert Yaw (Left/Right)##cam", &settings.invertCameraYaw,
				"Invert the yaw (left/right look) camera inertia")) { State::hasUnsavedChanges = true; }

			ImGuiMCP::TreePop();
		}

		// ---- ADS Inertia Multipliers ----
		if (ImGuiMCP::TreeNodeEx("ADS Inertia Multipliers", ImGuiTreeNodeFlags_DefaultOpen)) {
			ImGuiMCP::TextColored(ImVec4(0.7f, 0.7f, 0.7f, 1.0f), "Scale inertia when aiming down sights");

			if (CheckboxWithTooltip("Enable ADS Inertia##ads", &settings.adsInertiaEnabled,
				"Apply inertia when aiming down sights (iron sights)")) { State::hasUnsavedChanges = true; }
			if (settings.adsInertiaEnabled) {
				if (SliderFloatWithTooltip("ADS Inertia Mult##ads", &settings.adsInertiaMult, 0.0f, 2.0f, "%.2f",
					"Inertia multiplier when ADS (iron sights)\n0 = no inertia when aiming, 1 = full")) { State::hasUnsavedChanges = true; }
			}
			if (CheckboxWithTooltip("Enable Scoped Inertia##scope", &settings.adsScopeInertiaEnabled,
				"Apply inertia when looking through a scope")) { State::hasUnsavedChanges = true; }
			if (settings.adsScopeInertiaEnabled) {
				if (SliderFloatWithTooltip("Scope Inertia Mult##scope", &settings.adsScopeInertiaMult, 0.0f, 2.0f, "%.2f",
					"Inertia multiplier when looking through a scope\nUsually lower than iron sights")) { State::hasUnsavedChanges = true; }
			}

			ImGuiMCP::TreePop();
		}

		// ---- Movement Inertia ----
		if (ImGuiMCP::TreeNodeEx("Movement Inertia", ImGuiTreeNodeFlags_DefaultOpen)) {
			ImGuiMCP::TextColored(ImVec4(0.7f, 0.7f, 0.7f, 1.0f), "Responds to player movement (strafing, forward/back)");

			if (CheckboxWithTooltip("Enable##mov", &settings.movementInertiaEnabled,
				"Enable movement inertia for this weapon type")) { State::hasUnsavedChanges = true; }

			if (settings.movementInertiaEnabled) {
				if (SliderFloatWithTooltip("Stiffness##mov", &settings.movementStiffness, 10.0f, 500.0f, "%.0f",
					"Spring stiffness for movement response")) { State::hasUnsavedChanges = true; }
				if (SliderFloatWithTooltip("Damping##mov", &settings.movementDamping, 1.0f, 50.0f, "%.1f",
					"Damping for movement spring")) { State::hasUnsavedChanges = true; }
				if (SliderFloatWithTooltip("Max Offset##mov", &settings.movementMaxOffset, 0.0f, 50.0f, "%.1f",
					"Maximum position offset from strafing")) { State::hasUnsavedChanges = true; }
				if (SliderFloatWithTooltip("Max Rotation##mov", &settings.movementMaxRotation, 0.0f, 90.0f, "%.1f deg",
					"Maximum rotation from strafing")) { State::hasUnsavedChanges = true; }

				ImGuiMCP::Spacing();
				ImGuiMCP::Text("Lateral (Left/Right) Multipliers:");
				if (SliderFloatWithTooltip("Left Mult##mov", &settings.movementLeftMult, 0.0f, 5.0f, "%.2f",
					"Multiplier when strafing LEFT")) { State::hasUnsavedChanges = true; }
				if (SliderFloatWithTooltip("Right Mult##mov", &settings.movementRightMult, 0.0f, 5.0f, "%.2f",
					"Multiplier when strafing RIGHT")) { State::hasUnsavedChanges = true; }

				auto* gs = Settings::GetSingleton();
				if (gs->forwardBackInertia) {
					ImGuiMCP::Spacing();
					ImGuiMCP::Text("Forward/Back Multipliers:");
					if (SliderFloatWithTooltip("Forward Mult##mov", &settings.movementForwardMult, 0.0f, 5.0f, "%.2f",
						"Multiplier when moving FORWARD")) { State::hasUnsavedChanges = true; }
					if (SliderFloatWithTooltip("Backward Mult##mov", &settings.movementBackwardMult, 0.0f, 5.0f, "%.2f",
						"Multiplier when moving BACKWARD")) { State::hasUnsavedChanges = true; }
				}

				ImGuiMCP::Spacing();
				if (CheckboxWithTooltip("Invert Lateral (L/R)##mov", &settings.invertMovementLateral,
					"Invert the left/right strafe movement inertia")) { State::hasUnsavedChanges = true; }
				ImGuiMCP::SameLine();
				if (CheckboxWithTooltip("Invert Forward/Back##mov", &settings.invertMovementForwardBack,
					"Invert the forward/backward movement inertia")) { State::hasUnsavedChanges = true; }
			}
			ImGuiMCP::TreePop();
		}

		// ---- Walk Direction Offsets ----
		if (ImGuiMCP::TreeNodeEx("Walk Direction Offsets", ImGuiTreeNodeFlags_None)) {
			ImGuiMCP::TextColored(ImVec4(0.7f, 0.7f, 0.7f, 1.0f),
				"Additive pose offsets per walk direction (blends out in ADS)");
			ImGuiMCP::Spacing();

			if (CheckboxWithTooltip("Enable Walk Offsets##walkdir", &settings.walkOffsetsEnabled,
				"Master toggle for walk direction offsets")) { State::hasUnsavedChanges = true; }

			if (settings.walkOffsetsEnabled) {
				if (SliderFloatWithTooltip("Blend In Speed##walkdir", &settings.walkOffsetBlendInSpeed, 0.5f, 30.0f, "%.1f",
					"How fast offsets blend in when starting to move")) { State::hasUnsavedChanges = true; }
				if (SliderFloatWithTooltip("Blend Out Speed##walkdir", &settings.walkOffsetBlendOutSpeed, 0.5f, 30.0f, "%.1f",
					"How fast offsets blend out when stopping")) { State::hasUnsavedChanges = true; }
				if (SliderFloatWithTooltip("ADS Blend Out Speed##walkdir", &settings.walkOffsetAdsBlendOutSpeed, 1.0f, 50.0f, "%.1f",
					"Rapid blend-out speed when entering ADS")) { State::hasUnsavedChanges = true; }

				auto drawDirBlock = [&](const char* label, WalkDirectionOffset& d, const char* id) {
					std::string header = std::string(label) + "##" + id;
					if (ImGuiMCP::TreeNodeEx(header.c_str(), ImGuiTreeNodeFlags_None)) {
						std::string posxId = std::string("Pos X (vert)##") + id;
						std::string posyId = std::string("Pos Y (fwd)##") + id;
						std::string poszId = std::string("Pos Z (lat)##") + id;
						std::string rpId   = std::string("Rot Pitch##") + id;
						std::string ryId   = std::string("Rot Yaw##") + id;
						std::string rrId   = std::string("Rot Roll##") + id;
						if (SliderFloatWithTooltip(posxId.c_str(), &d.posX, -5.0f, 5.0f, "%.3f",
							"Vertical position offset")) { State::hasUnsavedChanges = true; }
						if (SliderFloatWithTooltip(posyId.c_str(), &d.posY, -5.0f, 5.0f, "%.3f",
							"Forward/back position offset")) { State::hasUnsavedChanges = true; }
						if (SliderFloatWithTooltip(poszId.c_str(), &d.posZ, -5.0f, 5.0f, "%.3f",
							"Lateral position offset")) { State::hasUnsavedChanges = true; }
						if (SliderFloatWithTooltip(rpId.c_str(), &d.rotPitch, -15.0f, 15.0f, "%.2f",
							"Pitch rotation offset (degrees)")) { State::hasUnsavedChanges = true; }
						if (SliderFloatWithTooltip(ryId.c_str(), &d.rotYaw, -15.0f, 15.0f, "%.2f",
							"Yaw rotation offset (degrees)")) { State::hasUnsavedChanges = true; }
						if (SliderFloatWithTooltip(rrId.c_str(), &d.rotRoll, -15.0f, 15.0f, "%.2f",
							"Roll rotation offset (degrees)")) { State::hasUnsavedChanges = true; }
						ImGuiMCP::TreePop();
					}
				};
				drawDirBlock("Forward",  settings.walkForward,  "wdFwd");
				drawDirBlock("Backward", settings.walkBackward, "wdBack");
				drawDirBlock("Left",     settings.walkLeft,     "wdLeft");
				drawDirBlock("Right",    settings.walkRight,    "wdRight");
			}
			ImGuiMCP::TreePop();
		}

		// ---- Simultaneous Blend Scaling ----
		if (ImGuiMCP::TreeNodeEx("Simultaneous Blend Scaling", ImGuiTreeNodeFlags_None)) {
			ImGuiMCP::TextColored(ImVec4(0.7f, 0.7f, 0.7f, 1.0f), "Scale effects when both looking AND moving at once");
			ImGuiMCP::Spacing();

			if (SliderFloatWithTooltip("Activation Threshold##simul", &settings.simultaneousThreshold, 0.0f, 5.0f, "%.2f",
				"How much of both camera AND movement must be active before scaling applies")) { State::hasUnsavedChanges = true; }
			if (SliderFloatWithTooltip("Camera Scale##simul", &settings.simultaneousCameraMult, 0.0f, 2.0f, "%.2fx",
				"Scale camera inertia when also moving")) { State::hasUnsavedChanges = true; }
			if (SliderFloatWithTooltip("Movement Scale##simul", &settings.simultaneousMovementMult, 0.0f, 2.0f, "%.2fx",
				"Scale movement inertia when also looking around")) { State::hasUnsavedChanges = true; }

			ImGuiMCP::TreePop();
		}

		// ---- Sprint Inertia ----
		if (ImGuiMCP::TreeNodeEx("Sprint Transition Inertia", ImGuiTreeNodeFlags_DefaultOpen)) {
			ImGuiMCP::TextColored(ImVec4(0.7f, 0.7f, 0.7f, 1.0f), "Momentum on sprint start/stop");

			if (CheckboxWithTooltip("Enable##sprint", &settings.sprintInertiaEnabled,
				"Master toggle for sprint impulses")) { State::hasUnsavedChanges = true; }

			if (settings.sprintInertiaEnabled) {
				ImGuiMCP::Spacing();
				if (CheckboxWithTooltip("Enable Sprint Start Impulse##sprintStart", &settings.sprintStartEnabled,
					"Fire impulse when starting to sprint")) { State::hasUnsavedChanges = true; }

				if (settings.sprintStartEnabled) {
					ImGuiMCP::Text("Sprint Start Impulse:");
					if (SliderFloatWithTooltip("X Impulse (Lateral)##sprintStart", &settings.sprintImpulseX, -30.0f, 30.0f, "%.1f",
						"Left/right impulse on sprint start")) { State::hasUnsavedChanges = true; }
					if (SliderFloatWithTooltip("Y Impulse (Depth)##sprintStart", &settings.sprintImpulseY, -50.0f, 50.0f, "%.1f",
						"Forward/back impulse on sprint start")) { State::hasUnsavedChanges = true; }
					if (SliderFloatWithTooltip("Z Impulse (Height)##sprintStart", &settings.sprintImpulseZ, -30.0f, 30.0f, "%.1f",
						"Vertical impulse on sprint start")) { State::hasUnsavedChanges = true; }
					if (SliderFloatWithTooltip("Rotation Impulse##sprintStart", &settings.sprintRotImpulse, -45.0f, 45.0f, "%.1f deg",
						"Rotation impulse on sprint start")) { State::hasUnsavedChanges = true; }
				}

				ImGuiMCP::Spacing();
				if (CheckboxWithTooltip("Enable Sprint Stop Impulse##sprintStop", &settings.sprintStopEnabled,
					"Fire impulse when stopping sprint")) { State::hasUnsavedChanges = true; }

				if (settings.sprintStopEnabled) {
					ImGuiMCP::Text("Sprint Stop Impulse:");
					if (SliderFloatWithTooltip("X Impulse (Lateral)##sprintStop", &settings.sprintStopImpulseX, -30.0f, 30.0f, "%.1f",
						"Left/right impulse on sprint stop")) { State::hasUnsavedChanges = true; }
					if (SliderFloatWithTooltip("Y Impulse (Depth)##sprintStop", &settings.sprintStopImpulseY, -50.0f, 50.0f, "%.1f",
						"Forward/back impulse on sprint stop")) { State::hasUnsavedChanges = true; }
					if (SliderFloatWithTooltip("Z Impulse (Height)##sprintStop", &settings.sprintStopImpulseZ, -30.0f, 30.0f, "%.1f",
						"Vertical impulse on sprint stop")) { State::hasUnsavedChanges = true; }
					if (SliderFloatWithTooltip("Rotation Impulse##sprintStop", &settings.sprintStopRotImpulse, -45.0f, 45.0f, "%.1f deg",
						"Rotation impulse on sprint stop")) { State::hasUnsavedChanges = true; }
				}

				ImGuiMCP::Spacing();
				ImGuiMCP::Text("Spring Parameters:");
				if (SliderFloatWithTooltip("Impulse Blend Time##sprint", &settings.sprintImpulseBlendTime, 0.0f, 1.0f, "%.2f sec",
					"Time to blend into impulse (0 = instant)")) { State::hasUnsavedChanges = true; }
				if (SliderFloatWithTooltip("Stiffness##sprint", &settings.sprintStiffness, 10.0f, 500.0f, "%.0f",
					"Spring stiffness for settling")) { State::hasUnsavedChanges = true; }
				if (SliderFloatWithTooltip("Damping##sprint", &settings.sprintDamping, 1.0f, 50.0f, "%.1f",
					"Damping for sprint spring")) { State::hasUnsavedChanges = true; }
			}
			ImGuiMCP::TreePop();
		}

		// ---- Jump/Land Inertia ----
		if (ImGuiMCP::TreeNodeEx("Jump/Landing Inertia", ImGuiTreeNodeFlags_DefaultOpen)) {
			ImGuiMCP::TextColored(ImVec4(0.7f, 0.7f, 0.7f, 1.0f), "Momentum on jump and landing, scaled by air time");

			if (CheckboxWithTooltip("Enable##jump", &settings.jumpInertiaEnabled,
				"Enable jump/landing momentum")) { State::hasUnsavedChanges = true; }
			if (SliderFloatWithTooltip("Camera Inertia Air Mult##air", &settings.cameraInertiaAirMult, 0.0f, 1.0f, "%.2f",
				"Camera inertia multiplier while airborne\n0 = no camera inertia in air")) { State::hasUnsavedChanges = true; }

			if (settings.jumpInertiaEnabled) {
				ImGuiMCP::Spacing();
				ImGuiMCP::Text("Jump (Takeoff) Settings:");
				if (SliderFloatWithTooltip("Jump X Impulse (Lateral)##jump", &settings.jumpImpulseX, -30.0f, 30.0f, "%.1f",
					"Left/right impulse when jumping")) { State::hasUnsavedChanges = true; }
				if (SliderFloatWithTooltip("Jump Y Impulse (Depth)##jump", &settings.jumpImpulseY, -30.0f, 30.0f, "%.1f",
					"Forward/back impulse when jumping")) { State::hasUnsavedChanges = true; }
				if (SliderFloatWithTooltip("Jump Z Impulse (Height)##jump", &settings.jumpImpulseZ, -30.0f, 30.0f, "%.1f",
					"Vertical impulse when jumping")) { State::hasUnsavedChanges = true; }
				if (SliderFloatWithTooltip("Jump Rot Pitch##jump", &settings.jumpRotPitch, -30.0f, 30.0f, "%.1f deg",
					"Pitch (forward tilt) rotation impulse when jumping")) { State::hasUnsavedChanges = true; }
				if (SliderFloatWithTooltip("Jump Rot Yaw##jump", &settings.jumpRotYaw, -30.0f, 30.0f, "%.1f deg",
					"Yaw (horizontal turn) rotation impulse when jumping")) { State::hasUnsavedChanges = true; }
				if (SliderFloatWithTooltip("Jump Rot Roll##jump", &settings.jumpRotRoll, -30.0f, 30.0f, "%.1f deg",
					"Roll (tilt sideways) rotation impulse when jumping")) { State::hasUnsavedChanges = true; }

				ImGuiMCP::Spacing();
				ImGuiMCP::Text("Fall (Ledge) Settings:");
				if (SliderFloatWithTooltip("Fall X Impulse (Lateral)##fall", &settings.fallImpulseX, -30.0f, 30.0f, "%.1f",
					"Left/right impulse when falling off a ledge")) { State::hasUnsavedChanges = true; }
				if (SliderFloatWithTooltip("Fall Y Impulse (Depth)##fall", &settings.fallImpulseY, -30.0f, 30.0f, "%.1f",
					"Forward/back impulse when falling off a ledge")) { State::hasUnsavedChanges = true; }
				if (SliderFloatWithTooltip("Fall Z Impulse (Height)##fall", &settings.fallImpulseZ, -30.0f, 30.0f, "%.1f",
					"Vertical impulse when falling off a ledge")) { State::hasUnsavedChanges = true; }
				if (SliderFloatWithTooltip("Fall Rot Pitch##fall", &settings.fallRotPitch, -30.0f, 30.0f, "%.1f deg",
					"Pitch rotation impulse when falling from a ledge")) { State::hasUnsavedChanges = true; }
				if (SliderFloatWithTooltip("Fall Rot Yaw##fall", &settings.fallRotYaw, -30.0f, 30.0f, "%.1f deg",
					"Yaw rotation impulse when falling from a ledge")) { State::hasUnsavedChanges = true; }
				if (SliderFloatWithTooltip("Fall Rot Roll##fall", &settings.fallRotRoll, -30.0f, 30.0f, "%.1f deg",
					"Roll rotation impulse when falling from a ledge")) { State::hasUnsavedChanges = true; }

			ImGuiMCP::Spacing();
			ImGuiMCP::Text("Jump Spring:");
			if (SliderFloatWithTooltip("Jump Stiffness##jump", &settings.jumpStiffness, 10.0f, 200.0f, "%.0f",
				"Spring stiffness after an active jump")) { State::hasUnsavedChanges = true; }
			if (SliderFloatWithTooltip("Jump Damping##jump", &settings.jumpDamping, 1.0f, 20.0f, "%.1f",
				"Damping after an active jump")) { State::hasUnsavedChanges = true; }

			ImGuiMCP::Spacing();
			ImGuiMCP::Text("Fall Spring:");
			if (SliderFloatWithTooltip("Fall Stiffness##fall", &settings.fallStiffness, 10.0f, 200.0f, "%.0f",
				"Spring stiffness after falling from a ledge")) { State::hasUnsavedChanges = true; }
			if (SliderFloatWithTooltip("Fall Damping##fall", &settings.fallDamping, 1.0f, 20.0f, "%.1f",
				"Damping after falling from a ledge")) { State::hasUnsavedChanges = true; }

				ImGuiMCP::Spacing();
				ImGuiMCP::Text("Landing Settings:");
				if (SliderFloatWithTooltip("Land X Impulse (Lateral)##land", &settings.landImpulseX, -30.0f, 30.0f, "%.1f",
					"Left/right impulse on landing")) { State::hasUnsavedChanges = true; }
				if (SliderFloatWithTooltip("Land Y Impulse (Depth)##land", &settings.landImpulseY, -30.0f, 30.0f, "%.1f",
					"Forward/back impulse on landing")) { State::hasUnsavedChanges = true; }
				if (SliderFloatWithTooltip("Land Z Impulse (Height)##land", &settings.landImpulseZ, -50.0f, 50.0f, "%.1f",
					"Vertical impulse on landing")) { State::hasUnsavedChanges = true; }
				if (SliderFloatWithTooltip("Land Rot Pitch##land", &settings.landRotPitch, -30.0f, 30.0f, "%.1f deg",
					"Pitch rotation impulse on landing")) { State::hasUnsavedChanges = true; }
				if (SliderFloatWithTooltip("Land Rot Yaw##land", &settings.landRotYaw, -30.0f, 30.0f, "%.1f deg",
					"Yaw rotation impulse on landing")) { State::hasUnsavedChanges = true; }
				if (SliderFloatWithTooltip("Land Rot Roll##land", &settings.landRotRoll, -30.0f, 30.0f, "%.1f deg",
					"Roll rotation impulse on landing")) { State::hasUnsavedChanges = true; }
				if (SliderFloatWithTooltip("Land Stiffness##land", &settings.landStiffness, 20.0f, 500.0f, "%.0f",
					"Spring stiffness on landing")) { State::hasUnsavedChanges = true; }
				if (SliderFloatWithTooltip("Land Damping##land", &settings.landDamping, 2.0f, 50.0f, "%.1f",
					"Damping on landing")) { State::hasUnsavedChanges = true; }
				if (SliderFloatWithTooltip("Air Time Scale##air", &settings.airTimeImpulseScale, 0.0f, 5.0f, "%.2f",
					"How much air time amplifies landing impulse")) { State::hasUnsavedChanges = true; }
			}
			ImGuiMCP::TreePop();
		}

		// ---- Equip Impulse ----
		if (ImGuiMCP::TreeNodeEx("Equip Impulse (On Draw)", ImGuiTreeNodeFlags_None)) {
			ImGuiMCP::TextColored(ImVec4(0.7f, 0.7f, 0.7f, 1.0f), "Impulse when drawing/equipping weapon");

			if (CheckboxWithTooltip("Enable##equip", &settings.equipImpulseEnabled,
				"Enable impulse when drawing weapon")) { State::hasUnsavedChanges = true; }

			if (settings.equipImpulseEnabled) {
				if (SliderFloatWithTooltip("X Impulse (Lateral)##equip", &settings.equipImpulseX, -20.0f, 20.0f, "%.1f",
					"Left/right impulse on weapon draw")) { State::hasUnsavedChanges = true; }
				if (SliderFloatWithTooltip("Y Impulse (Depth)##equip", &settings.equipImpulseY, -30.0f, 30.0f, "%.1f",
					"Forward/back impulse on weapon draw")) { State::hasUnsavedChanges = true; }
				if (SliderFloatWithTooltip("Z Impulse (Height)##equip", &settings.equipImpulseZ, -20.0f, 20.0f, "%.1f",
					"Vertical impulse on weapon draw")) { State::hasUnsavedChanges = true; }
				if (SliderFloatWithTooltip("Rotation Impulse##equip", &settings.equipRotImpulse, 0.0f, 30.0f, "%.1f deg",
					"Rotation impulse on weapon draw")) { State::hasUnsavedChanges = true; }
				if (SliderFloatWithTooltip("Blend Time##equip", &settings.equipBlendTime, 0.0f, 1.0f, "%.2f sec",
					"Time to blend into equip impulse")) { State::hasUnsavedChanges = true; }
				if (SliderFloatWithTooltip("Stiffness##equip", &settings.equipStiffness, 10.0f, 500.0f, "%.0f",
					"Spring stiffness for equip settle")) { State::hasUnsavedChanges = true; }
				if (SliderFloatWithTooltip("Damping##equip", &settings.equipDamping, 1.0f, 50.0f, "%.1f",
					"Damping for equip spring")) { State::hasUnsavedChanges = true; }
			}
			ImGuiMCP::TreePop();
		}

		// ---- Lean Inertia (UneducatedShooter) ----
		if (ImGuiMCP::TreeNodeEx("Lean Inertia (UneducatedShooter)", ImGuiTreeNodeFlags_None)) {
			ImGuiMCP::TextColored(ImVec4(0.7f, 0.7f, 0.7f, 1.0f), "Additive offset and impulse when the player leans");
			ImGuiMCP::TextWrapped(
				"Requires UneducatedShooter to be installed. Lean direction is detected by reading the "
				"rotation of the CameraInserted1st bone. If UneducatedShooter is not loaded the effect "
				"is silently skipped. Axis convention: X=vertical, Y=forward, Z=lateral.");
			ImGuiMCP::Spacing();

			if (CheckboxWithTooltip("Enable Lean Offset##lean", &settings.leanOffsetEnabled,
				"Smoothly shift weapon position in the lean direction.\n"
				"Positive Z = right when leaning right, left when leaning left.")) { State::hasUnsavedChanges = true; }

			if (settings.leanOffsetEnabled) {
				if (CheckboxWithTooltip("Disable Offset in ADS##leanOff", &settings.leanOffsetDisableInADS,
					"When checked, the lean offset smoothly blends out while aiming down sights.")) { State::hasUnsavedChanges = true; }
				if (SliderFloatWithTooltip("Lean Offset X (Vertical)##lean", &settings.leanOffsetX, -5.0f, 5.0f, "%.2f",
					"Vertical additive offset at full lean")) { State::hasUnsavedChanges = true; }
				if (SliderFloatWithTooltip("Lean Offset Y (Forward)##lean", &settings.leanOffsetY, -5.0f, 5.0f, "%.2f",
					"Forward additive offset at full lean")) { State::hasUnsavedChanges = true; }
				if (SliderFloatWithTooltip("Lean Offset Z (Lateral)##lean", &settings.leanOffsetZ, -5.0f, 5.0f, "%.2f",
					"Lateral additive offset at full lean (signed with lean direction)")) { State::hasUnsavedChanges = true; }
				if (SliderFloatWithTooltip("Lean Offset Blend Speed##lean", &settings.leanOffsetBlendSpeed, 0.5f, 30.0f, "%.1f",
					"How fast the additive offset tracks the lean weight (units/sec)")) { State::hasUnsavedChanges = true; }
			}

			ImGuiMCP::Spacing();

			if (CheckboxWithTooltip("Enable Lean Impulse##lean", &settings.leanImpulseEnabled,
				"Fire a spring impulse each time lean direction changes (start/stop lean).")) { State::hasUnsavedChanges = true; }

			if (settings.leanImpulseEnabled) {
				if (CheckboxWithTooltip("Disable Impulse in ADS##leanImp", &settings.leanImpulseDisableInADS,
					"When checked, lean direction changes while aiming down sights will not fire impulses.")) { State::hasUnsavedChanges = true; }
				if (SliderFloatWithTooltip("Lean Impulse X (Vertical)##lean", &settings.leanImpulseX, -20.0f, 20.0f, "%.1f",
					"Vertical impulse on lean transition")) { State::hasUnsavedChanges = true; }
				if (SliderFloatWithTooltip("Lean Impulse Y (Forward)##lean", &settings.leanImpulseY, -20.0f, 20.0f, "%.1f",
					"Forward impulse on lean transition")) { State::hasUnsavedChanges = true; }
				if (SliderFloatWithTooltip("Lean Impulse Z (Lateral)##lean", &settings.leanImpulseZ, -20.0f, 20.0f, "%.1f",
					"Lateral impulse on lean transition (signed with lean direction)")) { State::hasUnsavedChanges = true; }
				if (SliderFloatWithTooltip("Lean Rot Pitch##lean", &settings.leanRotImpulsePitch, -20.0f, 20.0f, "%.1f deg",
					"Pitch rotation impulse on lean transition")) { State::hasUnsavedChanges = true; }
				if (SliderFloatWithTooltip("Lean Rot Yaw##lean", &settings.leanRotImpulseYaw, -20.0f, 20.0f, "%.1f deg",
					"Yaw rotation impulse on lean transition")) { State::hasUnsavedChanges = true; }
				if (SliderFloatWithTooltip("Lean Rot Roll##lean", &settings.leanRotImpulseRoll, -20.0f, 20.0f, "%.1f deg",
					"Roll rotation on lean transition (default: small tilt toward lean side)")) { State::hasUnsavedChanges = true; }
				if (SliderFloatWithTooltip("Lean Stiffness##lean", &settings.leanStiffness, 10.0f, 300.0f, "%.0f",
					"Spring stiffness for lean impulse")) { State::hasUnsavedChanges = true; }
				if (SliderFloatWithTooltip("Lean Damping##lean", &settings.leanDamping, 1.0f, 30.0f, "%.1f",
					"Damping for lean impulse spring")) { State::hasUnsavedChanges = true; }
			}
			ImGuiMCP::TreePop();
		}

		// ---- Sneak Impulse ----
		if (ImGuiMCP::TreeNodeEx("Sneak Impulse (Enter/Exit)", ImGuiTreeNodeFlags_None)) {
			ImGuiMCP::TextColored(ImVec4(0.7f, 0.7f, 0.7f, 1.0f), "Optional spring impulse when crouching or standing up");
			ImGuiMCP::TextWrapped(
				"Detected via the moveMode bitfield (same method as sprint detection). "
				"Disabled by default — enable to add a subtle dip when crouching.");
			ImGuiMCP::Spacing();

			if (CheckboxWithTooltip("Enable Sneak Impulse##sneak", &settings.sneakImpulseEnabled,
				"Fire impulse springs when entering or exiting sneak mode.")) { State::hasUnsavedChanges = true; }

			if (settings.sneakImpulseEnabled) {
				ImGuiMCP::Spacing();
				ImGuiMCP::Text("Sneak Enter (Crouch Down):");
				if (SliderFloatWithTooltip("Enter X (Vertical)##sneakEnter", &settings.sneakEnterImpulseX, -20.0f, 20.0f, "%.1f",
					"Vertical impulse when entering sneak")) { State::hasUnsavedChanges = true; }
				if (SliderFloatWithTooltip("Enter Y (Forward)##sneakEnter", &settings.sneakEnterImpulseY, -20.0f, 20.0f, "%.1f",
					"Forward impulse when entering sneak")) { State::hasUnsavedChanges = true; }
				if (SliderFloatWithTooltip("Enter Z (Lateral)##sneakEnter", &settings.sneakEnterImpulseZ, -20.0f, 20.0f, "%.1f",
					"Lateral impulse when entering sneak")) { State::hasUnsavedChanges = true; }
				if (SliderFloatWithTooltip("Enter Rotation##sneakEnter", &settings.sneakEnterRotImpulse, -20.0f, 20.0f, "%.1f deg",
					"Pitch rotation impulse when crouching down\nPositive = weapon tilts forward")) { State::hasUnsavedChanges = true; }

				ImGuiMCP::Spacing();
				ImGuiMCP::Text("Sneak Exit (Stand Up):");
				if (SliderFloatWithTooltip("Exit X (Vertical)##sneakExit", &settings.sneakExitImpulseX, -20.0f, 20.0f, "%.1f",
					"Vertical impulse when exiting sneak")) { State::hasUnsavedChanges = true; }
				if (SliderFloatWithTooltip("Exit Y (Forward)##sneakExit", &settings.sneakExitImpulseY, -20.0f, 20.0f, "%.1f",
					"Forward impulse when exiting sneak")) { State::hasUnsavedChanges = true; }
				if (SliderFloatWithTooltip("Exit Z (Lateral)##sneakExit", &settings.sneakExitImpulseZ, -20.0f, 20.0f, "%.1f",
					"Lateral impulse when exiting sneak")) { State::hasUnsavedChanges = true; }
				if (SliderFloatWithTooltip("Exit Rotation##sneakExit", &settings.sneakExitRotImpulse, -20.0f, 20.0f, "%.1f deg",
					"Pitch rotation impulse when standing up\nNegative = weapon tilts back up")) { State::hasUnsavedChanges = true; }

				ImGuiMCP::Spacing();
				if (SliderFloatWithTooltip("Sneak Stiffness##sneak", &settings.sneakStiffness, 10.0f, 300.0f, "%.0f",
					"Spring stiffness for sneak impulse")) { State::hasUnsavedChanges = true; }
				if (SliderFloatWithTooltip("Sneak Damping##sneak", &settings.sneakDamping, 1.0f, 30.0f, "%.1f",
					"Damping for sneak impulse spring")) { State::hasUnsavedChanges = true; }
			}
			ImGuiMCP::TreePop();
		}

		// ---- ADS Enter Impulse ----
		if (ImGuiMCP::TreeNodeEx("ADS Enter Impulse", ImGuiTreeNodeFlags_None)) {
			ImGuiMCP::TextColored(ImVec4(0.7f, 0.7f, 0.7f, 1.0f), "Impulse when entering ADS (raising sights)");

			if (CheckboxWithTooltip("Enable##adsEnter", &settings.adsEnterImpulseEnabled,
				"Enable impulse when starting to aim")) { State::hasUnsavedChanges = true; }

			if (settings.adsEnterImpulseEnabled) {
				if (SliderFloatWithTooltip("X Impulse (Lateral)##adsEnter", &settings.adsEnterImpulseX, -20.0f, 20.0f, "%.1f",
					"Left/right impulse when raising sights")) { State::hasUnsavedChanges = true; }
				if (SliderFloatWithTooltip("Y Impulse (Depth)##adsEnter", &settings.adsEnterImpulseY, -20.0f, 20.0f, "%.1f",
					"Forward/back impulse when raising sights")) { State::hasUnsavedChanges = true; }
				if (SliderFloatWithTooltip("Z Impulse (Height)##adsEnter", &settings.adsEnterImpulseZ, -20.0f, 20.0f, "%.1f",
					"Vertical impulse when raising sights")) { State::hasUnsavedChanges = true; }
				if (SliderFloatWithTooltip("Rotation Impulse##adsEnter", &settings.adsEnterRotImpulse, 0.0f, 20.0f, "%.1f deg",
					"Rotation impulse when raising sights")) { State::hasUnsavedChanges = true; }
				if (SliderFloatWithTooltip("Stiffness##adsEnter", &settings.adsEnterStiffness, 10.0f, 500.0f, "%.0f",
					"Spring stiffness for ADS enter settle")) { State::hasUnsavedChanges = true; }
				if (SliderFloatWithTooltip("Damping##adsEnter", &settings.adsEnterDamping, 1.0f, 50.0f, "%.1f",
					"Damping for ADS enter spring")) { State::hasUnsavedChanges = true; }
			}
			ImGuiMCP::TreePop();
		}

		// ---- ADS Exit Impulse ----
		if (ImGuiMCP::TreeNodeEx("ADS Exit Impulse", ImGuiTreeNodeFlags_None)) {
			ImGuiMCP::TextColored(ImVec4(0.7f, 0.7f, 0.7f, 1.0f), "Impulse when exiting ADS (lowering sights)");

			if (CheckboxWithTooltip("Enable##adsExit", &settings.adsExitImpulseEnabled,
				"Enable impulse when stopping to aim")) { State::hasUnsavedChanges = true; }

			if (settings.adsExitImpulseEnabled) {
				if (SliderFloatWithTooltip("X Impulse (Lateral)##adsExit", &settings.adsExitImpulseX, -20.0f, 20.0f, "%.1f",
					"Left/right impulse when lowering sights")) { State::hasUnsavedChanges = true; }
				if (SliderFloatWithTooltip("Y Impulse (Depth)##adsExit", &settings.adsExitImpulseY, -20.0f, 20.0f, "%.1f",
					"Forward/back impulse when lowering sights")) { State::hasUnsavedChanges = true; }
				if (SliderFloatWithTooltip("Z Impulse (Height)##adsExit", &settings.adsExitImpulseZ, -20.0f, 20.0f, "%.1f",
					"Vertical impulse when lowering sights")) { State::hasUnsavedChanges = true; }
				if (SliderFloatWithTooltip("Rotation Impulse##adsExit", &settings.adsExitRotImpulse, 0.0f, 20.0f, "%.1f deg",
					"Rotation impulse when lowering sights")) { State::hasUnsavedChanges = true; }
				if (SliderFloatWithTooltip("Stiffness##adsExit", &settings.adsExitStiffness, 10.0f, 500.0f, "%.0f",
					"Spring stiffness for ADS exit settle")) { State::hasUnsavedChanges = true; }
				if (SliderFloatWithTooltip("Damping##adsExit", &settings.adsExitDamping, 1.0f, 50.0f, "%.1f",
					"Damping for ADS exit spring")) { State::hasUnsavedChanges = true; }
			}
			ImGuiMCP::TreePop();
		}

		// ---- Reload Impulse ----
		if (ImGuiMCP::TreeNodeEx("Reload Impulse", ImGuiTreeNodeFlags_None)) {
			ImGuiMCP::TextColored(ImVec4(0.7f, 0.7f, 0.7f, 1.0f),
				"Separate impulses for Tactical Reload (rounds still in mag) and Empty Reload (mag empty).");
			ImGuiMCP::Spacing();

			static const char* kReloadEventNames[] = { "ReloadEnd (default)", "InitiateStart", "ReloadComplete" };

			// ---- Tactical Reload ----
			ImGuiMCP::TextColored(ImVec4(0.4f, 0.8f, 1.0f, 1.0f), "Tactical Reload (rounds in magazine)");
			if (CheckboxWithTooltip("Enable##tacReload", &settings.reloadImpulseEnabled,
				"Fire an impulse when reloading with ammo still in the magazine")) { State::hasUnsavedChanges = true; }

			if (settings.reloadImpulseEnabled) {
				ImGuiMCP::Text("Trigger Event:");
				ImGuiMCP::SameLine();
				ImGuiMCP::SetNextItemWidth(180.0f);
				int trigIdx = std::clamp(settings.reloadTriggerEvent, 0, 2);
				if (ImGuiMCP::BeginCombo("##TacReloadTrigger", kReloadEventNames[trigIdx])) {
					for (int i = 0; i < 3; ++i) {
						bool sel = (trigIdx == i);
						if (ImGuiMCP::Selectable(kReloadEventNames[i], sel)) {
							settings.reloadTriggerEvent = i;
							State::hasUnsavedChanges = true;
						}
						if (sel) ImGuiMCP::SetItemDefaultFocus();
					}
					ImGuiMCP::EndCombo();
				}
				if (SliderFloatWithTooltip("Delay##tacReload", &settings.reloadImpulseDelay, 0.0f, 2.0f, "%.2f sec",
					"Delay after trigger before impulse fires")) { State::hasUnsavedChanges = true; }
				if (SliderFloatWithTooltip("Blend-In##tacReload", &settings.reloadImpulseBlendTime, 0.0f, 1.0f, "%.2f sec",
					"Ramp up time for the impulse")) { State::hasUnsavedChanges = true; }
				ImGuiMCP::Spacing();
				if (SliderFloatWithTooltip("X Impulse (Lateral)##tacReload", &settings.reloadImpulseX, -30.0f, 30.0f, "%.1f",
					"Left/right impulse")) { State::hasUnsavedChanges = true; }
				if (SliderFloatWithTooltip("Y Impulse (Depth)##tacReload", &settings.reloadImpulseY, -30.0f, 30.0f, "%.1f",
					"Forward/back impulse")) { State::hasUnsavedChanges = true; }
				if (SliderFloatWithTooltip("Z Impulse (Height)##tacReload", &settings.reloadImpulseZ, -30.0f, 30.0f, "%.1f",
					"Vertical impulse")) { State::hasUnsavedChanges = true; }
				if (SliderFloatWithTooltip("Rotation##tacReload", &settings.reloadRotImpulse, -20.0f, 20.0f, "%.1f deg",
					"Rotation impulse")) { State::hasUnsavedChanges = true; }
				if (SliderFloatWithTooltip("Stiffness##tacReload", &settings.reloadStiffness, 10.0f, 500.0f, "%.0f",
					"Spring stiffness")) { State::hasUnsavedChanges = true; }
				if (SliderFloatWithTooltip("Damping##tacReload", &settings.reloadDamping, 1.0f, 50.0f, "%.1f",
					"Spring damping")) { State::hasUnsavedChanges = true; }
			}

			ImGuiMCP::Spacing();
			ImGuiMCP::Separator();

			// ---- Empty Reload ----
			ImGuiMCP::TextColored(ImVec4(1.0f, 0.6f, 0.3f, 1.0f), "Empty Reload (no rounds in magazine)");
			if (CheckboxWithTooltip("Enable##emptyReload", &settings.emptyReloadImpulseEnabled,
				"Fire an impulse when reloading from empty (no rounds left in magazine)")) { State::hasUnsavedChanges = true; }

			if (settings.emptyReloadImpulseEnabled) {
				ImGuiMCP::Text("Trigger Event:");
				ImGuiMCP::SameLine();
				ImGuiMCP::SetNextItemWidth(180.0f);
				int emptyTrigIdx = std::clamp(settings.emptyReloadTriggerEvent, 0, 2);
				if (ImGuiMCP::BeginCombo("##EmptyReloadTrigger", kReloadEventNames[emptyTrigIdx])) {
					for (int i = 0; i < 3; ++i) {
						bool sel = (emptyTrigIdx == i);
						if (ImGuiMCP::Selectable(kReloadEventNames[i], sel)) {
							settings.emptyReloadTriggerEvent = i;
							State::hasUnsavedChanges = true;
						}
						if (sel) ImGuiMCP::SetItemDefaultFocus();
					}
					ImGuiMCP::EndCombo();
				}
				if (SliderFloatWithTooltip("Delay##emptyReload", &settings.emptyReloadImpulseDelay, 0.0f, 2.0f, "%.2f sec",
					"Delay after trigger before impulse fires")) { State::hasUnsavedChanges = true; }
				if (SliderFloatWithTooltip("Blend-In##emptyReload", &settings.emptyReloadImpulseBlendTime, 0.0f, 1.0f, "%.2f sec",
					"Ramp up time for the impulse")) { State::hasUnsavedChanges = true; }
				ImGuiMCP::Spacing();
				if (SliderFloatWithTooltip("X Impulse (Lateral)##emptyReload", &settings.emptyReloadImpulseX, -30.0f, 30.0f, "%.1f",
					"Left/right impulse")) { State::hasUnsavedChanges = true; }
				if (SliderFloatWithTooltip("Y Impulse (Depth)##emptyReload", &settings.emptyReloadImpulseY, -30.0f, 30.0f, "%.1f",
					"Forward/back impulse")) { State::hasUnsavedChanges = true; }
				if (SliderFloatWithTooltip("Z Impulse (Height)##emptyReload", &settings.emptyReloadImpulseZ, -30.0f, 30.0f, "%.1f",
					"Vertical impulse")) { State::hasUnsavedChanges = true; }
				if (SliderFloatWithTooltip("Rotation##emptyReload", &settings.emptyReloadRotImpulse, -20.0f, 20.0f, "%.1f deg",
					"Rotation impulse")) { State::hasUnsavedChanges = true; }
				if (SliderFloatWithTooltip("Stiffness##emptyReload", &settings.emptyReloadStiffness, 10.0f, 500.0f, "%.0f",
					"Spring stiffness")) { State::hasUnsavedChanges = true; }
				if (SliderFloatWithTooltip("Damping##emptyReload", &settings.emptyReloadDamping, 1.0f, 50.0f, "%.1f",
					"Spring damping")) { State::hasUnsavedChanges = true; }
			}

			ImGuiMCP::TreePop();
		}

		// ---- Fire Recovery Impulse ----
		if (ImGuiMCP::TreeNodeEx("Fire Recovery Impulse", ImGuiTreeNodeFlags_None)) {
			ImGuiMCP::TextColored(ImVec4(0.7f, 0.7f, 0.7f, 1.0f), "Impulse after firing a shot or burst (recoil settle)");

			// Hip-fire
			if (CheckboxWithTooltip("Enable (Hip-Fire)##fireHip", &settings.fireRecoveryImpulseEnabled,
				"Enable post-shot recoil recovery impulse when firing from the hip")) { State::hasUnsavedChanges = true; }

			if (settings.fireRecoveryImpulseEnabled) {
				ImGuiMCP::Text("Hip-Fire Recovery:");
				if (SliderFloatWithTooltip("X Impulse (Lateral)##fireHip", &settings.fireRecoveryImpulseX, -20.0f, 20.0f, "%.1f",
					"Left/right impulse on hip-fire recovery")) { State::hasUnsavedChanges = true; }
				if (SliderFloatWithTooltip("Y Impulse (Depth)##fireHip", &settings.fireRecoveryImpulseY, -20.0f, 20.0f, "%.1f",
					"Forward/back impulse on hip-fire recovery")) { State::hasUnsavedChanges = true; }
				if (SliderFloatWithTooltip("Z Impulse (Height)##fireHip", &settings.fireRecoveryImpulseZ, -20.0f, 20.0f, "%.1f",
					"Vertical impulse on hip-fire recovery")) { State::hasUnsavedChanges = true; }
				if (SliderFloatWithTooltip("Rotation Impulse##fireHip", &settings.fireRecoveryRotImpulse, -20.0f, 20.0f, "%.1f deg",
					"Rotation impulse on hip-fire recovery")) { State::hasUnsavedChanges = true; }
				if (SliderFloatWithTooltip("Stiffness##fireHip", &settings.fireRecoveryStiffness, 10.0f, 500.0f, "%.0f",
					"Spring stiffness for hip-fire recovery settle")) { State::hasUnsavedChanges = true; }
				if (SliderFloatWithTooltip("Damping##fireHip", &settings.fireRecoveryDamping, 1.0f, 50.0f, "%.1f",
					"Damping for hip-fire recovery spring")) { State::hasUnsavedChanges = true; }
				if (SliderFloatWithTooltip("Cooldown##fireHip", &settings.fireRecoveryCooldown, 0.0f, 0.5f, "%.3f sec",
					"Minimum time between hip-fire recovery triggers")) { State::hasUnsavedChanges = true; }
			}

			ImGuiMCP::Spacing();

			// ADS fire recovery
			if (CheckboxWithTooltip("Enable (ADS)##fireADS", &settings.adsFireRecoveryImpulseEnabled,
				"Enable post-shot recoil recovery impulse when firing while ADS")) { State::hasUnsavedChanges = true; }

			if (settings.adsFireRecoveryImpulseEnabled) {
				ImGuiMCP::Text("ADS Recovery:");
				if (SliderFloatWithTooltip("X Impulse (Lateral)##fireADS", &settings.adsFireRecoveryImpulseX, -20.0f, 20.0f, "%.1f",
					"Left/right impulse on ADS fire recovery")) { State::hasUnsavedChanges = true; }
				if (SliderFloatWithTooltip("Y Impulse (Depth)##fireADS", &settings.adsFireRecoveryImpulseY, -20.0f, 20.0f, "%.1f",
					"Forward/back impulse on ADS fire recovery")) { State::hasUnsavedChanges = true; }
				if (SliderFloatWithTooltip("Z Impulse (Height)##fireADS", &settings.adsFireRecoveryImpulseZ, -20.0f, 20.0f, "%.1f",
					"Vertical impulse on ADS fire recovery")) { State::hasUnsavedChanges = true; }
				if (SliderFloatWithTooltip("Rotation Impulse##fireADS", &settings.adsFireRecoveryRotImpulse, -20.0f, 20.0f, "%.1f deg",
					"Rotation impulse on ADS fire recovery")) { State::hasUnsavedChanges = true; }
				if (SliderFloatWithTooltip("Stiffness##fireADS", &settings.adsFireRecoveryStiffness, 10.0f, 500.0f, "%.0f",
					"Spring stiffness for ADS recovery settle")) { State::hasUnsavedChanges = true; }
				if (SliderFloatWithTooltip("Damping##fireADS", &settings.adsFireRecoveryDamping, 1.0f, 50.0f, "%.1f",
					"Damping for ADS recovery spring")) { State::hasUnsavedChanges = true; }
				if (SliderFloatWithTooltip("Cooldown##fireADS", &settings.adsFireRecoveryCooldown, 0.0f, 0.5f, "%.3f sec",
					"Minimum time between ADS fire recovery triggers")) { State::hasUnsavedChanges = true; }
			}

			ImGuiMCP::Spacing();
			ImGuiMCP::TextColored(ImVec4(0.7f, 0.7f, 0.7f, 1.0f), "Sustained Fire (shared, automatic weapons):");
			if (SliderFloatWithTooltip("Build Rate##sustained", &settings.sustainedFireBuildRate, 0.0f, 10.0f, "%.1f/s",
				"How fast the recovery multiplier builds during sustained fire")) { State::hasUnsavedChanges = true; }
			if (SliderFloatWithTooltip("Max Multiplier##sustained", &settings.sustainedFireMax, 1.0f, 10.0f, "%.1fx",
				"Maximum multiplier for recovery impulse after sustained fire")) { State::hasUnsavedChanges = true; }
			if (SliderFloatWithTooltip("Decay Rate##sustained", &settings.sustainedFireDecay, 0.0f, 10.0f, "%.1f/s",
				"How fast the sustained fire multiplier decays between shots")) { State::hasUnsavedChanges = true; }

			ImGuiMCP::TreePop();
		}

		// ---- Weight Scaling ----
		if (ImGuiMCP::TreeNodeEx("Weight-Based Mod Scaling", ImGuiTreeNodeFlags_None)) {
			ImGuiMCP::TextColored(ImVec4(0.7f, 0.7f, 0.7f, 1.0f), "Scale inertia based on weapon weight (mod attachments)");

			if (CheckboxWithTooltip("Enable Weight Scaling##wt", &settings.weightScalingEnabled,
				"Scale inertia based on delta between instance weight (with mods) and base weight")) { State::hasUnsavedChanges = true; }

			if (settings.weightScalingEnabled) {
				if (SliderFloatWithTooltip("Weight Influence##wt", &settings.weightScaleInfluence, 0.0f, 1.0f, "%.2f",
					"How strongly weight delta affects inertia\n0 = no effect, 1 = full effect")) { State::hasUnsavedChanges = true; }
				if (SliderFloatWithTooltip("Min Scale##wt", &settings.weightScaleMin, 0.0f, 1.0f, "%.2f",
					"Minimum inertia scale (lightest configuration)")) { State::hasUnsavedChanges = true; }
				if (SliderFloatWithTooltip("Max Scale##wt", &settings.weightScaleMax, 1.0f, 4.0f, "%.2f",
					"Maximum inertia scale (heaviest configuration)")) { State::hasUnsavedChanges = true; }
			}
			ImGuiMCP::TreePop();
		}

		// ---- Pivot Point ----
		if (ImGuiMCP::TreeNodeEx("Pivot Point", ImGuiTreeNodeFlags_DefaultOpen)) {
			ImGuiMCP::TextColored(ImVec4(0.7f, 0.7f, 0.7f, 1.0f), "Where rotation appears to pivot from");

			const char* pivotNames[] = { "Spine2 / Chest", "Right Hand", "Weapon Node" };
			const char* pivotTooltips[] = {
				"Chest/Spine2 - weapon swings the most, good for heavy weapons",
				"Right Hand - hand is more stable, weapon swings around it",
				"Weapon node - weapon is most stable, tightest feel"
			};

			ImGuiMCP::Text("Hip-Fire Pivot:");
			int pivotIdx = std::clamp(settings.pivotPoint, 0, 2);
			if (ImGuiMCP::BeginCombo("Pivot##hipPivot", pivotNames[pivotIdx])) {
				for (int i = 0; i < 3; ++i) {
					bool isSelected = (settings.pivotPoint == i);
					if (ImGuiMCP::Selectable(pivotNames[i], isSelected)) {
						settings.pivotPoint = i;
						State::hasUnsavedChanges = true;
						s_weaponEditorChanged = true;
					}
					if (ImGuiMCP::IsItemHovered()) {
						ImGuiMCP::SetTooltip("%s", pivotTooltips[i]);
					}
					if (isSelected) ImGuiMCP::SetItemDefaultFocus();
				}
				ImGuiMCP::EndCombo();
			}

			ImGuiMCP::Spacing();
			ImGuiMCP::Text("ADS Pivot:");
			int adsPivotIdx = std::clamp(settings.adsPivotPoint, 0, 2);
			if (ImGuiMCP::BeginCombo("Pivot##adsPivot", pivotNames[adsPivotIdx])) {
				for (int i = 0; i < 3; ++i) {
					bool isSelected = (settings.adsPivotPoint == i);
					if (ImGuiMCP::Selectable(pivotNames[i], isSelected)) {
						settings.adsPivotPoint = i;
						State::hasUnsavedChanges = true;
						s_weaponEditorChanged = true;
					}
					if (ImGuiMCP::IsItemHovered()) {
						ImGuiMCP::SetTooltip("ADS: %s", pivotTooltips[i]);
					}
					if (isSelected) ImGuiMCP::SetItemDefaultFocus();
				}
				ImGuiMCP::EndCombo();
			}

			ImGuiMCP::Spacing();
			if (ImGuiMCP::Checkbox("Use Bind Pose Pivot##bindPosePivot", &settings.useBindPosePivot)) {
				State::hasUnsavedChanges = true;
				s_weaponEditorChanged = true;
			}
			if (ImGuiMCP::IsItemHovered()) {
				ImGuiMCP::SetTooltip(
					"Force the pivot to use the skeleton's rest-pose position\n"
					"instead of the current animated position.\n\n"
					"Enable this for weapons whose idle animations heavily\n"
					"offset the spine from its bind pose, causing inertia\n"
					"to pivot from the wrong point.");
			}

			ImGuiMCP::TreePop();
		}

		// ---- Reset button ----
		ImGuiMCP::Spacing();
		if (ImGuiMCP::Button("Reset to Defaults")) {
			settings = WeaponInertiaSettings{};
			State::hasUnsavedChanges = true;
			s_weaponEditorChanged = true;
		}
		if (ImGuiMCP::IsItemHovered()) {
			ImGuiMCP::SetTooltip("Reset this weapon type to built-in defaults");
		}

		if (!settings.enabled) ImGuiMCP::PopStyleVar();

		// Pivot combo also bypasses helpers — mark if changed
		// (handled above; catch-all: if anything was dirty, also set the global unsaved flag)

		// Live update: bust the settings version cache so InertiaManager picks up changes immediately
		if (s_weaponEditorChanged) {
			InertiaPresets::GetSingleton()->IncrementSettingsVersion();
			InertiaPresets::GetSingleton()->MarkDirty();
		}
		s_inWeaponEditor = false;

		ImGuiMCP::PopID();
	}

	// ============================================================
	// Specific Weapon Presets
	// ============================================================
	void DrawSpecificWeaponSettings()
	{
		auto* presets = InertiaPresets::GetSingleton();
		const auto& types = GetWeaponTypes();
		ImGuiTreeNodeFlags flags = State::specificWeaponExpanded ? ImGuiTreeNodeFlags_DefaultOpen : 0;

		if (ImGuiMCP::CollapsingHeader("Specific Weapon Presets (by EditorID)", flags)) {
			State::specificWeaponExpanded = true;

			ImGuiMCP::TextWrapped(
				"Create custom inertia settings for specific weapons using their EditorID. "
				"These override the weapon type settings above.");
			ImGuiMCP::Spacing();

			// ---- Equipped weapon quick-create ----
			auto* player = RE::PlayerCharacter::GetSingleton();
			if (player) {
				auto* base = GetEquippedWeaponBase(player);
				if (base) {
					std::string weaponID = GetEquippedWeaponEditorID(player);
					auto* fn_full = base->As<RE::TESFullName>();
					const char* name = fn_full ? fn_full->GetFullName() : nullptr;

					ImGuiMCP::TextColored(ImVec4(0.4f, 0.8f, 1.0f, 1.0f),
						"Equipped: %s (%s)", (name && name[0]) ? name : "Unknown", weaponID.c_str());

					if (!presets->HasSpecificWeaponSettings(weaponID)) {
						// Weapon type dropdown for the new preset
						ImGuiMCP::Text("Assign weapon type for new preset:");
						const char* wtPreview = (State::createPresetWeaponTypeIndex >= 0 &&
							State::createPresetWeaponTypeIndex < static_cast<int>(types.size()))
							? types[State::createPresetWeaponTypeIndex].displayName.c_str()
							: "Rifle";
						ImGuiMCP::SetNextItemWidth(180.0f);
						if (ImGuiMCP::BeginCombo("##CreateWTCombo", wtPreview)) {
							for (int i = 0; i < static_cast<int>(types.size()); ++i) {
								bool sel = (State::createPresetWeaponTypeIndex == i);
								if (ImGuiMCP::Selectable(types[i].displayName.c_str(), sel))
									State::createPresetWeaponTypeIndex = i;
								if (sel) ImGuiMCP::SetItemDefaultFocus();
							}
							ImGuiMCP::EndCombo();
						}
						ImGuiMCP::SameLine();
						if (ImGuiMCP::Button("Create Preset for Equipped")) {
							WeaponType wt = (State::createPresetWeaponTypeIndex >= 0 &&
								State::createPresetWeaponTypeIndex < static_cast<int>(types.size()))
								? types[State::createPresetWeaponTypeIndex].type
								: WeaponType::Rifle;
							presets->GetOrCreateSpecificWeaponSettings(weaponID, wt);
							presets->SetWeaponTypeOverride(weaponID, wt);
							presets->SaveSpecificWeaponPreset(weaponID);
							presets->IncrementSettingsVersion();
							State::saveStatusMsg = std::format("Created preset for {} ({})", weaponID,
								InertiaPresets::GetWeaponTypeDisplayName(wt));
							State::saveStatusTimer = 4.0f;
							State::hasUnsavedChanges = true;
						}
						if (ImGuiMCP::IsItemHovered())
							ImGuiMCP::SetTooltip("Create a custom preset for this weapon, copied from the selected weapon type's defaults");
					} else {
						ImGuiMCP::TextColored(ImVec4(0.5f, 1.0f, 0.5f, 1.0f), "[Has custom preset]");
					}
				} else {
					ImGuiMCP::TextColored(ImVec4(0.6f, 0.6f, 0.6f, 1.0f), "No weapon equipped");
				}
			}

			ImGuiMCP::Spacing();
			ImGuiMCP::Separator();

			// ---- Manual EditorID entry ----
			ImGuiMCP::Text("Or enter an EditorID manually:");
			ImGuiMCP::SetNextItemWidth(200.0f);
			ImGuiMCP::InputText("##ManualEID", State::newWeaponEditorID, sizeof(State::newWeaponEditorID));
			ImGuiMCP::SameLine();
			if (ImGuiMCP::Button("Create##manual")) {
				if (strlen(State::newWeaponEditorID) > 0) {
					if (!presets->HasSpecificWeaponSettings(State::newWeaponEditorID)) {
						WeaponType wt = (State::createPresetWeaponTypeIndex >= 0 &&
							State::createPresetWeaponTypeIndex < static_cast<int>(types.size()))
							? types[State::createPresetWeaponTypeIndex].type
							: WeaponType::Rifle;
						presets->GetOrCreateSpecificWeaponSettings(State::newWeaponEditorID, wt);
						presets->SetWeaponTypeOverride(State::newWeaponEditorID, wt);
						presets->SaveSpecificWeaponPreset(State::newWeaponEditorID);
						presets->IncrementSettingsVersion();
						State::saveStatusMsg = std::format("Created preset for {}", State::newWeaponEditorID);
						State::saveStatusTimer = 4.0f;
					}
				}
			}
			if (ImGuiMCP::IsItemHovered())
				ImGuiMCP::SetTooltip("Create a preset for the typed EditorID (uses the same type dropdown above)");

			ImGuiMCP::Spacing();
			ImGuiMCP::Separator();

			// ---- List & edit existing presets ----
			auto savedPresets = presets->GetSavedSpecificWeaponPresets();

			if (savedPresets.empty()) {
				ImGuiMCP::TextColored(ImVec4(0.6f, 0.6f, 0.6f, 1.0f), "No specific weapon presets created yet.");
			} else {
				ImGuiMCP::Text("Existing Presets (%zu):", savedPresets.size());
				ImGuiMCP::BeginChild("SpecificWeaponList", ImVec2(0, 150), ImGuiChildFlags_Border);
				for (int i = 0; i < static_cast<int>(savedPresets.size()); ++i) {
					bool isSelected = (State::selectedSpecificWeaponIndex == i);
					if (ImGuiMCP::Selectable(savedPresets[i].c_str(), isSelected))
						State::selectedSpecificWeaponIndex = i;
					if (ImGuiMCP::IsItemHovered()) {
						auto path = presets->GetSpecificWeaponPresetPath(savedPresets[i]);
						ImGuiMCP::SetTooltip("%s", path.string().c_str());
					}
				}
				ImGuiMCP::EndChild();

				if (State::selectedSpecificWeaponIndex >= 0 &&
					State::selectedSpecificWeaponIndex < static_cast<int>(savedPresets.size())) {

					const std::string& selEID = savedPresets[State::selectedSpecificWeaponIndex];
					ImGuiMCP::Spacing();
					ImGuiMCP::TextColored(ImVec4(0.4f, 0.8f, 1.0f, 1.0f), "Editing: %s", selEID.c_str());
					if (ImGuiMCP::IsItemHovered()) {
						auto path = presets->GetSpecificWeaponPresetPath(selEID);
						ImGuiMCP::SetTooltip("%s", path.string().c_str());
					}

					// Weapon type override for this specific preset
					WeaponType currentOverride = presets->HasWeaponTypeOverride(selEID)
						? presets->GetWeaponTypeOverride(selEID)
						: WeaponType::Rifle;
					const char* curTypeName = InertiaPresets::GetWeaponTypeDisplayName(currentOverride);
					ImGuiMCP::Text("Weapon Type:");
					ImGuiMCP::SameLine();
					ImGuiMCP::SetNextItemWidth(160.0f);
					if (ImGuiMCP::BeginCombo("##SpecWTOverride", curTypeName)) {
						for (int i = 0; i < static_cast<int>(types.size()); ++i) {
							bool sel = (types[i].type == currentOverride && !types[i].isCustomType);
							if (ImGuiMCP::Selectable(types[i].displayName.c_str(), sel)) {
								presets->SetWeaponTypeOverride(selEID, types[i].type);
								presets->IncrementSettingsVersion();
								State::hasUnsavedChanges = true;
							}
							if (sel) ImGuiMCP::SetItemDefaultFocus();
						}
						ImGuiMCP::EndCombo();
					}
					if (ImGuiMCP::IsItemHovered())
						ImGuiMCP::SetTooltip("Override which weapon type category this weapon uses.\nAffects inertia settings when no specific preset value overrides them.");

					ImGuiMCP::Spacing();

					// Full inertia editor
					auto& mutableWS = presets->GetOrCreateSpecificWeaponSettings(selEID, currentOverride);
					ImGuiMCP::PushID(selEID.c_str());
					DrawWeaponInertiaEditor(mutableWS, selEID.c_str());
					ImGuiMCP::PopID();

					if (ImGuiMCP::Button("Save This Preset")) {
						presets->SaveSpecificWeaponPreset(selEID);
						State::saveStatusMsg = std::format("Saved: {}", selEID);
						State::saveStatusTimer = 4.0f;
					}
					if (ImGuiMCP::IsItemHovered()) ImGuiMCP::SetTooltip("Save changes to this specific weapon preset JSON");

					ImGuiMCP::SameLine();
				ImGuiMCP::PushStyleColor(ImGuiCol_Button, ImVec4(0.6f, 0.2f, 0.2f, 1.0f));
				ImGuiMCP::PushStyleColor(ImGuiCol_ButtonHovered, ImVec4(0.8f, 0.3f, 0.3f, 1.0f));
				if (ImGuiMCP::Button("Delete Preset")) {
					presets->RemoveSpecificWeaponSettings(selEID);
					presets->IncrementSettingsVersion();
					State::selectedSpecificWeaponIndex = -1;
					State::saveStatusMsg = std::format("Deleted preset for {}", selEID);
					State::saveStatusTimer = 4.0f;
				}
				ImGuiMCP::PopStyleColor(2);
				if (ImGuiMCP::IsItemHovered()) ImGuiMCP::SetTooltip("Delete this specific weapon preset file");

				// ---- Copy To ----
				ImGuiMCP::Spacing();
				ImGuiMCP::Separator();
				ImGuiMCP::TextColored(ImVec4(1.0f, 0.85f, 0.3f, 1.0f), "Copy Settings From: %s", selEID.c_str());

				// Copy feedback line
				if (State::copyConfirmTimer > 0.0f) {
					auto* io = ImGuiMCP::GetIO();
					if (io) State::copyConfirmTimer -= io->DeltaTime;
					ImGuiMCP::TextColored(ImVec4(0.3f, 1.0f, 0.3f, 1.0f), "%s", State::copyConfirmMsg.c_str());
				}

				// -- Copy to Weapon Type --
				ImGuiMCP::Spacing();
				ImGuiMCP::Text("Copy to Weapon Type:");
				ImGuiMCP::SameLine();
				{
					const char* preview = (State::copyToTypeIndex >= 0 &&
						State::copyToTypeIndex < static_cast<int>(types.size()))
						? types[State::copyToTypeIndex].displayName.c_str() : "Rifle";
					ImGuiMCP::SetNextItemWidth(160.0f);
					if (ImGuiMCP::BeginCombo("##CopyToTypeCombo", preview)) {
						for (int i = 0; i < static_cast<int>(types.size()); ++i) {
							bool sel = (State::copyToTypeIndex == i);
							if (ImGuiMCP::Selectable(types[i].displayName.c_str(), sel))
								State::copyToTypeIndex = i;
							if (sel) ImGuiMCP::SetItemDefaultFocus();
						}
						ImGuiMCP::EndCombo();
					}
				}
				ImGuiMCP::SameLine();
				if (ImGuiMCP::Button("Copy##CopyToType")) {
					const auto* src = presets->GetSpecificWeaponSettings(selEID);
					if (src && State::copyToTypeIndex >= 0 &&
						State::copyToTypeIndex < static_cast<int>(types.size())) {
						const auto& entry = types[State::copyToTypeIndex];
						WeaponInertiaSettings& dst = entry.isCustomType
							? presets->GetCustomWeaponTypeSettingsMutable(entry.internalName)
							: presets->GetWeaponTypeSettingsMutable(entry.type);
						dst = *src;
						presets->SaveWeaponTypePresets();
						presets->IncrementSettingsVersion();
						State::copyConfirmMsg = std::format("Copied to weapon type: {}", entry.displayName);
						State::copyConfirmTimer = 4.0f;
					}
				}
				if (ImGuiMCP::IsItemHovered())
					ImGuiMCP::SetTooltip("Overwrite the selected weapon type's settings with this preset's values.\nSaves immediately.");

				// -- Copy to Another Specific Preset --
				ImGuiMCP::Spacing();
				ImGuiMCP::Text("Copy to Specific Weapon:");
				ImGuiMCP::SetNextItemWidth(180.0f);
				ImGuiMCP::InputText("##CopyToEID", State::copyToEditorBuf, sizeof(State::copyToEditorBuf));
				if (ImGuiMCP::IsItemHovered())
					ImGuiMCP::SetTooltip("EditorID of the target specific weapon preset\n(can be existing or new)");

				// Quick-pick from existing presets
				ImGuiMCP::SameLine();
				if (ImGuiMCP::BeginCombo("##CopyToEIDPick", "Pick...", 0)) {
					for (const auto& eid : savedPresets) {
						if (eid == selEID) continue;
						if (ImGuiMCP::Selectable(eid.c_str(), false))
							std::strncpy(State::copyToEditorBuf, eid.c_str(), sizeof(State::copyToEditorBuf) - 1);
						if (ImGuiMCP::IsItemHovered()) {
							auto path = presets->GetSpecificWeaponPresetPath(eid);
							ImGuiMCP::SetTooltip("%s", path.string().c_str());
						}
					}
					ImGuiMCP::EndCombo();
				}

				ImGuiMCP::SameLine();
				bool targetIsEmpty = (State::copyToEditorBuf[0] == '\0');
				bool targetIsSelf  = (selEID == State::copyToEditorBuf);
				if (targetIsEmpty || targetIsSelf) {
					ImGuiMCP::BeginDisabled();
				}
				if (ImGuiMCP::Button("Copy##CopyToSpecific")) {
					const auto* src = presets->GetSpecificWeaponSettings(selEID);
					if (src) {
						std::string targetEID = State::copyToEditorBuf;
						// Preserve the target's weapon type if it already has one, else use source's
						WeaponType targetWT = presets->HasWeaponTypeOverride(targetEID)
							? presets->GetWeaponTypeOverride(targetEID)
							: (presets->HasWeaponTypeOverride(selEID)
								? presets->GetWeaponTypeOverride(selEID)
								: WeaponType::Rifle);
						WeaponInertiaSettings& dst = presets->GetOrCreateSpecificWeaponSettings(targetEID, targetWT);
						dst = *src;
						presets->SetWeaponTypeOverride(targetEID, targetWT);
						presets->SaveSpecificWeaponPreset(targetEID);
						presets->IncrementSettingsVersion();
						State::copyConfirmMsg = std::format("Copied to: {}", targetEID);
						State::copyConfirmTimer = 4.0f;
						State::copyToEditorBuf[0] = '\0';
					}
				}
				if (targetIsEmpty || targetIsSelf) {
					ImGuiMCP::EndDisabled();
				}
				if (ImGuiMCP::IsItemHovered() && !targetIsEmpty && !targetIsSelf)
					ImGuiMCP::SetTooltip("Copy all settings to the target EditorID preset.\nCreates it if it doesn't exist yet.");
			}
		}
		} else {
			State::specificWeaponExpanded = false;
		}
	}

	// ============================================================
	// Save / Load Buttons
	// ============================================================
	void DrawSaveLoadButtons()
	{
		auto* settings = Settings::GetSingleton();
		auto* presets = InertiaPresets::GetSingleton();

		// Save status message (timed)
		if (State::saveStatusTimer > 0.0f) {
			auto* io = ImGuiMCP::GetIO();
			if (io) State::saveStatusTimer -= io->DeltaTime;
			ImGuiMCP::TextColored(ImVec4(0.3f, 1.0f, 0.3f, 1.0f), "%s", State::saveStatusMsg.c_str());
		}

		ImGuiMCP::Text("INI Configuration (Global Settings):");

		if (ImGuiMCP::Button("Save to INI")) {
			settings->Save();
			State::hasUnsavedChanges = false;
			State::saveStatusMsg = "Global settings saved to INI.";
			State::saveStatusTimer = 4.0f;
		}
		if (ImGuiMCP::IsItemHovered()) {
			ImGuiMCP::SetTooltip("Save global settings to Data\\F4SE\\Plugins\\FPInertia.ini");
		}

		ImGuiMCP::SameLine();

		if (ImGuiMCP::Button("Reload from INI")) {
			settings->Load();
			State::hasUnsavedChanges = false;
			State::saveStatusMsg = "Settings reloaded from INI.";
			State::saveStatusTimer = 4.0f;
		}
		if (ImGuiMCP::IsItemHovered()) {
			ImGuiMCP::SetTooltip("Reload global settings from INI file");
		}

		ImGuiMCP::SameLine();

		if (ImGuiMCP::Button("Reset All to Defaults")) {
			settings->Load();
			presets->ResetToINIValues();
			State::hasUnsavedChanges = true;
			State::saveStatusMsg = "Settings reset to defaults.";
			State::saveStatusTimer = 4.0f;
		}
		if (ImGuiMCP::IsItemHovered()) {
			ImGuiMCP::SetTooltip("Reset all settings to plugin defaults (not saved until you click Save)");
		}

		ImGuiMCP::Spacing();
		ImGuiMCP::Separator();

		// JSON preset save/load
		std::string presetLabel = std::format("Active Preset: {} (Weapon Type Settings)", presets->GetActivePresetName());
		ImGuiMCP::Text("%s", presetLabel.c_str());

		std::string saveLabel = std::format("Save to '{}.json'", presets->GetActivePresetName());
		if (ImGuiMCP::Button(saveLabel.c_str())) {
			presets->SaveWeaponTypePresets();
			presets->ClearDirty();
			State::hasUnsavedChanges = false;
			State::saveStatusMsg = std::format("Preset '{}' saved.", presets->GetActivePresetName());
			State::saveStatusTimer = 4.0f;
		}
		if (ImGuiMCP::IsItemHovered()) {
			ImGuiMCP::SetTooltip("Save current weapon type settings to:\nData\\F4SE\\Plugins\\FPInertia\\%s.json",
				presets->GetActivePresetName().c_str());
		}

		ImGuiMCP::SameLine();

		std::string reloadLabel = std::format("Reload '{}.json'", presets->GetActivePresetName());
		if (ImGuiMCP::Button(reloadLabel.c_str())) {
			presets->LoadAllPresets();
			presets->ClearDirty();
			State::hasUnsavedChanges = false;
			State::saveStatusMsg = std::format("Preset '{}' reloaded.", presets->GetActivePresetName());
			State::saveStatusTimer = 4.0f;
		}
		if (ImGuiMCP::IsItemHovered()) {
			ImGuiMCP::SetTooltip("Reload weapon type settings from:\nData\\F4SE\\Plugins\\FPInertia\\%s.json",
				presets->GetActivePresetName().c_str());
		}

		// Unsaved indicator
		if (State::hasUnsavedChanges || presets->IsDirty()) {
			ImGuiMCP::TextColored(ImVec4(1.0f, 0.8f, 0.2f, 1.0f),
				"You have unsaved changes. Save to keep them after restart.");
		} else {
			ImGuiMCP::TextColored(ImVec4(0.5f, 0.8f, 0.5f, 1.0f), "All settings saved.");
		}

		// Path info
		ImGuiMCP::TextColored(ImVec4(0.6f, 0.6f, 0.6f, 1.0f), "INI: Data\\F4SE\\Plugins\\FPInertia.ini");
		ImGuiMCP::TextColored(ImVec4(0.6f, 0.6f, 0.6f, 1.0f), "Preset: Data\\F4SE\\Plugins\\FPInertia\\%s.json",
			presets->GetActivePresetName().c_str());
	}

	// ============================================================
	// Helper: Weapon Based FOV section (rendered inside Extras)
	// ============================================================
	static void RenderWeaponBasedFOV()
	{
		using namespace ImGuiMCP;
		auto* settings = Settings::GetSingleton();
		auto* mgr = WeaponFOV::Manager::GetSingleton();

		ImGuiMCP::TextWrapped(
			"Force the player viewmodel FOV to a specific value when a weapon is equipped, "
			"so weapons made by different animators can use the FOV their animations were "
			"designed for. When no weapon is equipped (or the equipped weapon has no entry), "
			"the default viewmodel FOV from your installed FOV mod (or 80 if none) is used. "
			"FOV is restored automatically when you holster or unequip the weapon.");
		ImGuiMCP::Spacing();

		// ---- Master toggle ----
		if (ImGuiMCP::Checkbox("Enable Weapon Based FOV##wbfovEnabled", &settings->wbfovEnabled)) {
			State::hasUnsavedChanges = true;
			if (!settings->wbfovEnabled) {
				mgr->RestoreDefault();
			}
		}
		if (ImGuiMCP::IsItemHovered()) {
			ImGuiMCP::SetTooltip(
				"When disabled, the viewmodel FOV is restored to the default and\n"
				"this plugin no longer changes it for any weapon.");
		}

		if (ImGuiMCP::SliderInt("Load Retries##wbfovLoadRetries", &settings->wbfovLoadRetries, 1, 5)) {
			State::hasUnsavedChanges = true;
		}
		if (ImGuiMCP::IsItemHovered()) {
			ImGuiMCP::SetTooltip(
				"How many times to retry applying WBFOV after loading a save.\n"
				"Each attempt uses a longer delay than the last (50ms, 150ms, 300ms, 500ms, 800ms).\n"
				"Retrying stops as soon as the first attempt succeeds.\n"
				"Increase if the FOV doesn't apply cleanly on load; decrease to reduce pops.");
		}

		ImGuiMCP::Spacing();
		ImGuiMCP::Separator();
		ImGuiMCP::Spacing();

		// ---- Default source info ----
		ImGuiMCP::Text("Default Viewmodel FOV: %.1f", mgr->GetDefaultViewmodelFOV());
		ImGuiMCP::SameLine();
		ImGuiMCP::TextColored(ImVec4(0.6f, 0.8f, 1.0f, 1.0f), "(source: %s)", mgr->GetDefaultSourceName());
		if (ImGuiMCP::IsItemHovered()) {
			ImGuiMCP::SetTooltip(
				"Read from your installed FOV mod's INI on plugin load.\n"
				"Reload your save after changing FOV mod settings to refresh.");
		}

		if (ImGuiMCP::SmallButton("Refresh Defaults##wbfovRefresh")) {
			mgr->RefreshDefaults();
			State::saveStatusMsg = std::format("WBFOV defaults refreshed: {:.1f} ({})",
				mgr->GetDefaultViewmodelFOV(), mgr->GetDefaultSourceName());
			State::saveStatusTimer = 4.0f;
		}
		if (ImGuiMCP::IsItemHovered()) {
			ImGuiMCP::SetTooltip("Re-read FOV mod INI files to pick up changes since plugin load.");
		}

		ImGuiMCP::Spacing();
		ImGuiMCP::Separator();
		ImGuiMCP::Spacing();

		// ---- Equipped weapon block ----
		auto* player = RE::PlayerCharacter::GetSingleton();
		auto* base = player ? GetEquippedWeaponBase(player) : nullptr;

		std::string equippedEditorID;
		std::string equippedDisplayName;
		std::uint32_t equippedFormID = 0;
		if (base) {
			equippedFormID = base->GetFormID();
			const char* eid = base->GetFormEditorID();
			equippedEditorID = (eid && eid[0]) ? eid : std::string();
			auto* fn = base->As<RE::TESFullName>();
			equippedDisplayName = (fn && fn->GetFullName() && fn->GetFullName()[0])
				? fn->GetFullName() : equippedEditorID;
		}

		ImGuiMCP::TextColored(ImVec4(1.0f, 0.85f, 0.3f, 1.0f), "Equipped Weapon");

		if (equippedFormID == 0) {
			ImGuiMCP::TextColored(ImVec4(0.6f, 0.6f, 0.6f, 1.0f), "No weapon equipped.");
		} else if (equippedEditorID.empty()) {
			ImGuiMCP::TextColored(ImVec4(0.8f, 0.6f, 0.6f, 1.0f),
				"Equipped weapon has no EditorID — cannot create a WBFOV entry.");
			ImGuiMCP::Text("FormID: 0x%08X (%s)", equippedFormID, equippedDisplayName.c_str());
		} else {
			ImGuiMCP::Text("%s", equippedDisplayName.c_str());
			ImGuiMCP::Text("EditorID: %s", equippedEditorID.c_str());

			const bool hasEntry = mgr->HasEntry(equippedEditorID);
			static float s_newEntryFOV = 80.0f;

			if (!hasEntry) {
				// Pre-fill the slider with the current default
				s_newEntryFOV = mgr->GetDefaultViewmodelFOV();
				ImGuiMCP::SetNextItemWidth(180.0f);
				ImGuiMCP::SliderFloat("FOV##wbfovNew", &s_newEntryFOV, 30.0f, 130.0f, "%.1f");
				ImGuiMCP::SameLine();
				if (ImGuiMCP::Button("Add Entry##wbfovAdd")) {
					mgr->SetEntry(equippedEditorID, equippedDisplayName, s_newEntryFOV);
					State::saveStatusMsg = std::format("WBFOV: added {} -> {:.1f}",
						equippedEditorID, s_newEntryFOV);
					State::saveStatusTimer = 4.0f;
				}
				if (ImGuiMCP::IsItemHovered()) {
					auto path = mgr->GetEntryPath(equippedEditorID);
					ImGuiMCP::SetTooltip("Will save to: %s", path.string().c_str());
				}
			} else {
				float v = mgr->GetEntryFOV(equippedEditorID);
				ImGuiMCP::SetNextItemWidth(180.0f);
				if (ImGuiMCP::SliderFloat("FOV##wbfovEdit", &v, 30.0f, 130.0f, "%.1f")) {
					mgr->SetEntry(equippedEditorID, equippedDisplayName, v);
				}
				ImGuiMCP::SameLine();
				if (ImGuiMCP::Button("Remove Entry##wbfovRemove")) {
					mgr->RemoveEntry(equippedEditorID);
					State::saveStatusMsg = std::format("WBFOV: removed {}", equippedEditorID);
					State::saveStatusTimer = 4.0f;
				}
				if (ImGuiMCP::IsItemHovered()) {
					auto path = mgr->GetEntryPath(equippedEditorID);
					ImGuiMCP::SetTooltip("Deletes: %s", path.string().c_str());
				}
				ImGuiMCP::TextColored(ImVec4(0.4f, 1.0f, 0.4f, 1.0f), "Custom FOV active.");
			}
		}

		ImGuiMCP::Spacing();
		ImGuiMCP::Separator();
		ImGuiMCP::Spacing();

		// ---- All entries list ----
		auto allEntries = mgr->GetEntries();
		ImGuiMCP::TextColored(ImVec4(1.0f, 0.85f, 0.3f, 1.0f),
			"All WBFOV Entries (%d)", static_cast<int>(allEntries.size()));

		if (allEntries.empty()) {
			ImGuiMCP::TextColored(ImVec4(0.6f, 0.6f, 0.6f, 1.0f), "No weapon FOV entries configured.");
		} else {
			static int s_wbfovRemoveIdx = -1;
			for (int i = 0; i < static_cast<int>(allEntries.size()); ++i) {
				auto& e = allEntries[i];

				ImGuiMCP::PushID(i);

				// Display name + editorID
				if (!e.displayName.empty() && e.displayName != e.editorID) {
					ImGuiMCP::Text("%s  ", e.displayName.c_str());
					ImGuiMCP::SameLine();
					ImGuiMCP::TextColored(ImVec4(0.7f, 0.7f, 0.7f, 1.0f), "(%s)", e.editorID.c_str());
				} else {
					ImGuiMCP::Text("%s", e.editorID.c_str());
				}

				// Slider for FOV
				float v = e.viewmodelFOV;
				ImGuiMCP::SetNextItemWidth(160.0f);
				if (ImGuiMCP::SliderFloat("##wbfovListSlider", &v, 30.0f, 130.0f, "%.1f")) {
					mgr->SetEntry(e.editorID, e.displayName, v);
				}
				ImGuiMCP::SameLine();
				if (ImGuiMCP::SmallButton("X##wbfovListDel")) {
					s_wbfovRemoveIdx = i;
				}
				if (ImGuiMCP::IsItemHovered()) {
					auto path = mgr->GetEntryPath(e.editorID);
					ImGuiMCP::SetTooltip("Delete: %s", path.string().c_str());
				}

				ImGuiMCP::PopID();
			}
			if (s_wbfovRemoveIdx >= 0 && s_wbfovRemoveIdx < static_cast<int>(allEntries.size())) {
				mgr->RemoveEntry(allEntries[s_wbfovRemoveIdx].editorID);
				s_wbfovRemoveIdx = -1;
			}
		}
	}

	// ============================================================
	// Extras Tab
	// ============================================================
	void __stdcall RenderExtras()
	{
		using namespace ImGuiMCP;

		auto* presets = InertiaPresets::GetSingleton();
		const auto& types = GetWeaponTypes();

		ImGuiMCP::TextColored(ImVec4(1.0f, 0.85f, 0.3f, 1.0f), "FP Inertia - Extras");
		ImGuiMCP::TextWrapped("Small quality-of-life features that complement the inertia system.");
		ImGuiMCP::Spacing();
		ImGuiMCP::Separator();

		// savedPresets is needed by the save buttons further down
		auto savedPresets = presets->GetSavedSpecificWeaponPresets();

		// ====== EARLY ADS RETURN ======
		if (ImGuiMCP::CollapsingHeader("Early ADS Return", ImGuiTreeNodeFlags_DefaultOpen)) {
			ImGuiMCP::TextWrapped(
				"When you reload while aiming down sights, the game normally waits for the full reload "
				"animation to finish before returning to ADS. This feature triggers the ADS return early "
				"on a configurable animation event, letting you get back on target faster. "
				"Settings here are per-weapon-type (specific weapon presets override).");
			ImGuiMCP::Spacing();

			// ---- Weapon type / specific weapon selector ----
			ImGuiMCP::Text("Weapon Type:");
			ImGuiMCP::SameLine();
			ImGuiMCP::SetNextItemWidth(200.0f);
			{
				const char* preview = (State::extrasWeaponTypeIndex >= 0 &&
					State::extrasWeaponTypeIndex < static_cast<int>(types.size()))
					? types[State::extrasWeaponTypeIndex].displayName.c_str()
					: "Rifle";
				if (ImGuiMCP::BeginCombo("##ExtrasWTCombo", preview)) {
					for (int i = 0; i < static_cast<int>(types.size()); ++i) {
						bool sel = (State::extrasWeaponTypeIndex == i);
						if (ImGuiMCP::Selectable(types[i].displayName.c_str(), sel))
							State::extrasWeaponTypeIndex = i;
						if (sel) ImGuiMCP::SetItemDefaultFocus();
					}
					ImGuiMCP::EndCombo();
				}
			}

			if (!savedPresets.empty()) {
				ImGuiMCP::Text("Or Specific Weapon:");
				ImGuiMCP::SameLine();
				ImGuiMCP::SetNextItemWidth(200.0f);
				const char* specPreview = (State::extrasSpecificWeaponIndex >= 0 &&
					State::extrasSpecificWeaponIndex < static_cast<int>(savedPresets.size()))
					? savedPresets[State::extrasSpecificWeaponIndex].c_str()
					: "(use weapon type)";
				if (ImGuiMCP::BeginCombo("##ExtrasSpecCombo", specPreview)) {
					if (ImGuiMCP::Selectable("(use weapon type)", State::extrasSpecificWeaponIndex < 0))
						State::extrasSpecificWeaponIndex = -1;
					for (int i = 0; i < static_cast<int>(savedPresets.size()); ++i) {
						bool sel = (State::extrasSpecificWeaponIndex == i);
						if (ImGuiMCP::Selectable(savedPresets[i].c_str(), sel))
							State::extrasSpecificWeaponIndex = i;
						if (ImGuiMCP::IsItemHovered()) {
							auto path = presets->GetSpecificWeaponPresetPath(savedPresets[i]);
							ImGuiMCP::SetTooltip("%s", path.string().c_str());
						}
						if (sel) ImGuiMCP::SetItemDefaultFocus();
					}
					ImGuiMCP::EndCombo();
				}
			}

			// Resolve which settings to edit
			WeaponInertiaSettings* editSettings = nullptr;
			std::string editLabel;
			if (State::extrasSpecificWeaponIndex >= 0 &&
				State::extrasSpecificWeaponIndex < static_cast<int>(savedPresets.size())) {
				const auto& eid = savedPresets[State::extrasSpecificWeaponIndex];
				if (presets->HasSpecificWeaponSettings(eid)) {
					WeaponType baseType = presets->HasWeaponTypeOverride(eid)
						? presets->GetWeaponTypeOverride(eid)
						: WeaponType::Rifle;
					editSettings = &presets->GetOrCreateSpecificWeaponSettings(eid, baseType);
					editLabel = eid;
				}
			}
			if (!editSettings && State::extrasWeaponTypeIndex >= 0 &&
				State::extrasWeaponTypeIndex < static_cast<int>(types.size())) {
				const auto& entry = types[State::extrasWeaponTypeIndex];
				editSettings = &GetWeaponSettingsForEditingByEntry(entry);
				editLabel = entry.displayName;
			}

			if (editSettings) {
				auto& ws = *editSettings;

				ImGuiMCP::Spacing();
				ImGuiMCP::Separator();
				ImGuiMCP::TextColored(ImVec4(0.4f, 0.8f, 1.0f, 1.0f), "Editing: %s", editLabel.c_str());
				ImGuiMCP::Spacing();

			static bool s_extrasEditorChanged = false;
			s_extrasEditorChanged = false;

			if (CheckboxWithTooltip("Enable Early ADS Return##earlyAds", &ws.earlyAdsReturnEnabled,
				"If enabled, forces the player back into ADS before the reload animation fully completes "
				"(only if the ADS button is still held)")) {
				s_extrasEditorChanged = true;
			}

			if (ws.earlyAdsReturnEnabled) {
				// Trigger event dropdown
				static const char* kTriggerNames[] = {
					"ReloadComplete (default)", "ReloadEnd", "InitiateStart"
				};
				ImGuiMCP::Text("Trigger Event:");
				ImGuiMCP::SameLine();
				ImGuiMCP::SetNextItemWidth(200.0f);
				int trigIdx = std::clamp(ws.earlyAdsReturnTrigger, 0, 2);
				if (ImGuiMCP::BeginCombo("##EarlyAdsTrigger", kTriggerNames[trigIdx])) {
					for (int i = 0; i < 3; ++i) {
						bool sel = (trigIdx == i);
						if (ImGuiMCP::Selectable(kTriggerNames[i], sel)) {
							ws.earlyAdsReturnTrigger = i;
							s_extrasEditorChanged = true;
						}
						if (sel) ImGuiMCP::SetItemDefaultFocus();
					}
					ImGuiMCP::EndCombo();
				}
				if (ImGuiMCP::IsItemHovered())
					ImGuiMCP::SetTooltip(
						"ReloadComplete – fires when the mechanical reload is done\n"
						"ReloadEnd      – fires when the full reload animation finishes\n"
						"InitiateStart  – fires when the reload begins (earliest possible)");

				if (SliderFloatWithTooltip("Delay##earlyAds", &ws.earlyAdsReturnDelay, 0.0f, 3.0f, "%.2f sec",
					"Seconds to wait after the trigger event before forcing ADS return\n0 = immediate")) {
					s_extrasEditorChanged = true;
				}

				if (SliderFloatWithTooltip("Blend Time##earlyAds", &ws.earlyAdsReturnBlendTime, 0.0f, 2.0f, "%.2f sec",
					"How long the transition from hip to ADS takes\n"
					"Lower = snappier, higher = smoother")) {
					s_extrasEditorChanged = true;
				}

				// Blend type dropdown
				static const char* kBlendTypeNames[] = {
					"Linear", "Ease In", "Ease Out", "Ease In/Out"
				};
				ImGuiMCP::Text("Blend Type:");
				ImGuiMCP::SameLine();
				ImGuiMCP::SetNextItemWidth(160.0f);
				int blendIdx = std::clamp(ws.earlyAdsReturnBlendType, 0, 3);
				if (ImGuiMCP::BeginCombo("##EarlyAdsBlendType", kBlendTypeNames[blendIdx])) {
					for (int i = 0; i < 4; ++i) {
						bool sel = (blendIdx == i);
						if (ImGuiMCP::Selectable(kBlendTypeNames[i], sel)) {
							ws.earlyAdsReturnBlendType = i;
							s_extrasEditorChanged = true;
						}
						if (sel) ImGuiMCP::SetItemDefaultFocus();
					}
					ImGuiMCP::EndCombo();
				}
				if (ImGuiMCP::IsItemHovered())
					ImGuiMCP::SetTooltip(
						"Controls the easing curve of the ADS blend-in\n"
						"Linear    – constant speed\n"
						"Ease In   – starts slow, ends fast\n"
						"Ease Out  – starts fast, ends slow\n"
						"Ease In/Out – slow start and end");
			}

				if (SliderFloatWithTooltip("ADS Enter Impulse Scale", &ws.earlyAdsReturnImpulseScale,
					0.0f, 2.0f, "%.2f",
					"Scales the ADS-enter impulse when triggered by Early ADS Return.\n"
					"0 = no impulse, 1.0 = full impulse, 0.25 = 25% of normal.")) {
					s_extrasEditorChanged = true;
				}

				if (SliderFloatWithTooltip("Fire Block Delay", &ws.earlyAdsFireBlockDelay,
					0.0f, 1.0f, "%.2f s",
					"Extra delay (seconds) after ADS is confirmed before allowing fire input.\n"
					"During this time, the attack handler is suppressed so auto-fire\n"
					"doesn't play animations without actually firing.\n"
					"0 = release immediately when ADS confirms. Default 0.15s.")) {
					s_extrasEditorChanged = true;
				}

				ImGuiMCP::Spacing();
				ImGuiMCP::Separator();
				ImGuiMCP::TextWrapped("Auto-Fire Recovery");
				ImGuiMCP::Spacing();

				if (ImGuiMCP::Checkbox("Enable Auto-Fire Recovery", &ws.earlyAdsAutoFireEnabled)) {
					s_extrasEditorChanged = true;
				}
				if (ImGuiMCP::IsItemHovered())
					ImGuiMCP::SetTooltip(
						"After Early ADS, monitors for stuck automatic fire.\n"
						"If the weapon enters an attack state but never actually fires,\n"
						"sends attackStop to reset the state machine so auto-fire works.\n"
						"Only relevant for automatic weapons.");

				if (ws.earlyAdsAutoFireEnabled) {
					if (SliderFloatWithTooltip("Watch Window", &ws.earlyAdsAutoFireWindow,
						0.1f, 2.0f, "%.2f s",
						"How long (seconds) to monitor for stuck auto-fire after\n"
						"the force-idle sequence completes. Default 0.5s.")) {
						s_extrasEditorChanged = true;
					}

					int maxAttempts = ws.earlyAdsAutoFireMaxAttempts;
					if (ImGuiMCP::SliderInt("Max Reset Attempts", &maxAttempts, 1, 10)) {
						ws.earlyAdsAutoFireMaxAttempts = maxAttempts;
						s_extrasEditorChanged = true;
					}
					if (ImGuiMCP::IsItemHovered())
						ImGuiMCP::SetTooltip(
							"Maximum number of attackStop resets to send within the\n"
							"watch window. Each attempt has a 0.1s grace period.\n"
							"Default 3.");
				}

				ImGuiMCP::Spacing();
				ImGuiMCP::Separator();
				ImGuiMCP::TextColored(ImVec4(0.7f, 0.9f, 1.0f, 1.0f), "Phantom-Fire Audio (Global)");
				ImGuiMCP::Spacing();

				{
					auto* gSettings = Settings::GetSingleton();
					if (CheckboxWithTooltip("Enable Sound Fade-Out##phantomAudio", &gSettings->autoFireSoundFadeEnabled,
						"When enabled, all equipped weapon loop sound handles are faded\n"
						"out when the phantom-fire override exits.\n"
						"Disable if this interferes with other audio mods.")) {
						State::hasUnsavedChanges = true;
					}
					int fadeMs = gSettings->autoFireSoundFadeMs;
					if (gSettings->autoFireSoundFadeEnabled && ImGuiMCP::SliderInt("Sound Fade-Out (ms)", &fadeMs, 0, 1000)) {
						gSettings->autoFireSoundFadeMs = std::clamp(fadeMs, 0, 5000);
						State::hasUnsavedChanges = true;
					}
					if (ImGuiMCP::IsItemHovered())
						ImGuiMCP::SetTooltip(
							"Duration (in milliseconds) over which the equipped weapon's\n"
							"loop sound handles are faded out when the phantom-fire\n"
							"override exits.\n"
							"\n"
							"This applies to all three exit paths:\n"
							"  - Player released the trigger (normal release)\n"
							"  - 5-second safety timeout\n"
							"  - 150ms gap with no weaponFire anim event\n"
							"\n"
							"  0    = instant cut (audible chop)\n"
							"  100  = perceptually clean spin-down (default)\n"
							"  300+ = soft tail (may overlap next shot)\n"
							"\n"
							"Global setting — applies to every weapon. Stored under\n"
							"[AutoFire] iSoundFadeMs in FPInertia.ini.");
				}

				ImGuiMCP::Spacing();
				ImGuiMCP::Separator();
				ImGuiMCP::TextWrapped("Early Fire Cancel");
				ImGuiMCP::Spacing();

				if (ImGuiMCP::Checkbox("Enable Early Fire Cancel", &ws.earlyFireCancelEnabled)) {
					s_extrasEditorChanged = true;
				}
				if (ImGuiMCP::IsItemHovered())
					ImGuiMCP::SetTooltip(
						"Mirrors the Early ADS fix for the case where the player\n"
						"holds the FIRE input mid-reload to cancel-into-fire.\n"
						"Without this, automatic weapons frequently play the fire\n"
						"animation but discharge no ammo (same root cause as the\n"
						"Early ADS auto-fire bug: isReloading stays true and the\n"
						"attack state machine never transitions through idle).\n"
						"\n"
						"When enabled and the fire input is held during a reload,\n"
						"sends ReloadEnd, forces isReloading=false, suppresses the\n"
						"attack handler for 3 frames so gunState passes through\n"
						"idle, then arms the phantom-fire override (which converts\n"
						"the phantom fire animation into real shots via\n"
						"QueueWeaponFire). Default ON.");

				ImGuiMCP::Spacing();
				ImGuiMCP::Separator();
				ImGuiMCP::TextWrapped("Early Equip");
				ImGuiMCP::Spacing();

				if (ImGuiMCP::Checkbox("Enable Early Equip ADS", &ws.earlyEquipAdsEnabled)) {
					s_extrasEditorChanged = true;
				}
				if (ImGuiMCP::IsItemHovered())
					ImGuiMCP::SetTooltip(
						"If the ADS input is held during a weapon equip animation\n"
						"(WPNEquip / WPNEquipFast), trigger ADS early when the\n"
						"InitiateStart anim event fires instead of waiting for\n"
						"the full equip animation to finish.\n"
						"\n"
						"Uses the same delay and blend settings as Early ADS Return.");

				if (ImGuiMCP::Checkbox("Enable Early Equip Fire", &ws.earlyEquipFireEnabled)) {
					s_extrasEditorChanged = true;
				}
				if (ImGuiMCP::IsItemHovered())
					ImGuiMCP::SetTooltip(
						"If the fire input is held during a weapon equip animation\n"
						"(WPNEquip / WPNEquipFast), allow hipfire early when the\n"
						"InitiateStart anim event fires instead of waiting for\n"
						"the full equip animation to finish.\n"
						"\n"
						"Uses the same delay and blend settings as Early ADS Return.");

			if (s_extrasEditorChanged) {
				presets->IncrementSettingsVersion();
				presets->MarkDirty();
				State::hasUnsavedChanges = true;
			}
			} // if (editSettings)
	}

	// ====== WEAPON BASED FOV ======
		ImGuiMCP::Spacing();
		if (ImGuiMCP::CollapsingHeader("Weapon Based FOV")) {
			RenderWeaponBasedFOV();
		}

		// ====== AIR WALK PREVENTION ======
		ImGuiMCP::Spacing();
		if (ImGuiMCP::CollapsingHeader("Air Walk Prevention")) {
			auto* settings = Settings::GetSingleton();
			ImGuiMCP::TextWrapped(
				"Prevents walking and running animations from playing while the player is "
				"in the air (jumping or falling). Without this, legs may visibly cycle "
				"mid-air if you were moving when you jumped or walked off a ledge.");
			ImGuiMCP::Spacing();

			if (ImGuiMCP::Checkbox("Enable Air Walk Prevention##disableAirWalk", &settings->disableAirWalk)) {
				State::hasUnsavedChanges = true;
			}
		}

		// ====== UNEDUCATED RELOAD: CHAMBER EXCLUSION ======
		ImGuiMCP::Spacing();
		if (ImGuiMCP::CollapsingHeader("Uneducated Reload - Chamber Exclusion")) {
			auto* chamberMgr = ChamberExclusion::Manager::GetSingleton();
			const bool urInstalled = chamberMgr->IsInstalled();

			if (!urInstalled) {
				ImGuiMCP::TextColored(ImVec4(0.5f, 0.5f, 0.5f, 1.0f),
					"UneducatedReload.esm not detected. Install the mod to use this feature.");
			} else {
				ImGuiMCP::TextWrapped(
					"Weapons in this list will NOT receive the +1 chambered round from "
					"Uneducated Reload (e.g. revolvers, break-action weapons).");
				ImGuiMCP::Spacing();

				auto* player = RE::PlayerCharacter::GetSingleton();
				auto* base = player ? GetEquippedWeaponBase(player) : nullptr;
				std::uint32_t equippedFormID = 0;
				std::string equippedEditorID;
				std::string equippedDisplayName;

				if (base) {
					equippedFormID = base->GetFormID();
					const char* eid = base->GetFormEditorID();
					equippedEditorID = (eid && eid[0]) ? eid : std::format("0x{:08X}", equippedFormID);
					auto* fn = base->As<RE::TESFullName>();
					equippedDisplayName = (fn && fn->GetFullName() && fn->GetFullName()[0])
						? fn->GetFullName() : equippedEditorID;
				}

				if (equippedFormID != 0) {
					bool alreadyExcluded = chamberMgr->IsWeaponExcluded(equippedFormID);
					ImGuiMCP::Text("Equipped: %s (%s)", equippedDisplayName.c_str(), equippedEditorID.c_str());

					if (alreadyExcluded) {
						if (ImGuiMCP::Button("Remove from Exclusion List##chamberRemoveEquip")) {
							chamberMgr->RemoveWeapon(equippedFormID);
						}
						ImGuiMCP::SameLine();
						ImGuiMCP::TextColored(ImVec4(1.0f, 0.4f, 0.4f, 1.0f), "(excluded)");
					} else {
						if (ImGuiMCP::Button("Add to Exclusion List##chamberAddEquip")) {
							chamberMgr->AddWeapon(equippedFormID, equippedEditorID, equippedDisplayName);
						}
					}
				} else {
					ImGuiMCP::TextColored(ImVec4(0.6f, 0.6f, 0.6f, 1.0f), "No weapon equipped.");
				}

				ImGuiMCP::Spacing();

				auto& excluded = chamberMgr->GetExcludedWeapons();
				if (excluded.empty()) {
					ImGuiMCP::TextColored(ImVec4(0.6f, 0.6f, 0.6f, 1.0f), "No weapons in exclusion list.");
				} else {
					ImGuiMCP::Text("Excluded Weapons (%d):", static_cast<int>(excluded.size()));
					static int s_chamberRemoveIdx = -1;
					for (int i = 0; i < static_cast<int>(excluded.size()); ++i) {
						auto& entry = excluded[i];
						std::string label = std::format("{} ({})", entry.displayName, entry.editorID);
						ImGuiMCP::BulletText("%s", label.c_str());
						if (ImGuiMCP::IsItemHovered()) {
							ImGuiMCP::SetTooltip("FormID: 0x%08X", entry.formID);
						}
						ImGuiMCP::SameLine();
						std::string btnID = std::format("X##chamberDel{}", i);
						if (ImGuiMCP::SmallButton(btnID.c_str())) {
							s_chamberRemoveIdx = i;
						}
					}
					if (s_chamberRemoveIdx >= 0 && s_chamberRemoveIdx < static_cast<int>(excluded.size())) {
						chamberMgr->RemoveWeapon(excluded[s_chamberRemoveIdx].formID);
						s_chamberRemoveIdx = -1;
					}
				}
			}
		}

		// ---- Save Buttons ----
		ImGuiMCP::Spacing();
		ImGuiMCP::Separator();

		if (State::saveStatusTimer > 0.0f) {
			auto* io = ImGuiMCP::GetIO();
			if (io) State::saveStatusTimer -= io->DeltaTime;
			ImGuiMCP::TextColored(ImVec4(0.3f, 1.0f, 0.3f, 1.0f), "%s", State::saveStatusMsg.c_str());
		}

		if (ImGuiMCP::Button("Save to INI")) {
			Settings::GetSingleton()->Save();
			State::hasUnsavedChanges = false;
			State::saveStatusMsg = "Global settings saved to INI.";
			State::saveStatusTimer = 4.0f;
		}
		ImGuiMCP::SameLine();
		if (ImGuiMCP::Button("Save Weapon Type Presets")) {
			presets->SaveWeaponTypePresets();
			State::saveStatusMsg = "Weapon type presets saved.";
			State::saveStatusTimer = 4.0f;
		}
		if (State::extrasSpecificWeaponIndex >= 0 &&
			State::extrasSpecificWeaponIndex < static_cast<int>(savedPresets.size())) {
			ImGuiMCP::SameLine();
			const auto& eid = savedPresets[State::extrasSpecificWeaponIndex];
			if (ImGuiMCP::Button("Save Specific Preset")) {
				presets->SaveSpecificWeaponPreset(eid);
				State::saveStatusMsg = std::format("Saved: {}", eid);
				State::saveStatusTimer = 4.0f;
			}
		}
	}

	// ============================================================
	// Helper: draw one ADSTransitionSettings block (enter or exit)
	// ============================================================
	static bool DrawADSTransitionBlock(const char* header, const char* idPrefix,
		ADSTransitionSettings& t, bool& changed)
	{
		using namespace ImGuiMCP;
		bool anyChanged = false;
		if (ImGuiMCP::CollapsingHeader(header)) {
			static const char* kCurveNames[] = { "Sine", "EaseInOut", "Bounce", "Overshoot" };

			auto togId  = std::string("##") + idPrefix + "_en";
			auto crvId  = std::string("##") + idPrefix + "_curve";
			auto pxId   = std::string("##") + idPrefix + "_px";
			auto pyId   = std::string("##") + idPrefix + "_py";
			auto pzId   = std::string("##") + idPrefix + "_pz";
			auto rPitId = std::string("##") + idPrefix + "_rpit";
			auto rYawId = std::string("##") + idPrefix + "_ryaw";
			auto rRolId = std::string("##") + idPrefix + "_rrol";
			auto ppId   = std::string("##") + idPrefix + "_pp";
			auto asymId = std::string("##") + idPrefix + "_asy";
			auto blnId  = std::string("##") + idPrefix + "_bln";

			if (ImGuiMCP::Checkbox(togId.c_str(), &t.enabled)) { anyChanged = true; }
			ImGuiMCP::SameLine();
			ImGuiMCP::Text("Enabled");
			if (ImGuiMCP::IsItemHovered())
				ImGuiMCP::SetTooltip("Enable this procedural motion during the ADS transition.");

			ImGuiMCP::Text("Curve Type:");
			ImGuiMCP::SameLine();
			ImGuiMCP::SetNextItemWidth(150.0f);
			if (ImGuiMCP::BeginCombo(crvId.c_str(), kCurveNames[std::clamp(t.curveType, 0, 3)])) {
				for (int c = 0; c < 4; c++) {
					bool sel = (t.curveType == c);
					if (ImGuiMCP::Selectable(kCurveNames[c], sel)) { t.curveType = c; anyChanged = true; }
					if (sel) ImGuiMCP::SetItemDefaultFocus();
				}
				ImGuiMCP::EndCombo();
			}
			if (ImGuiMCP::IsItemHovered())
				ImGuiMCP::SetTooltip("Shape of the transition curve.\nSine: smooth bell. EaseInOut: cubic. Bounce: slight overshoot. Overshoot: peaks high.");

			ImGuiMCP::Spacing();
			ImGuiMCP::Text("Peak Offset (X=vertical, Y=forward, Z=lateral):");
			ImGuiMCP::SetNextItemWidth(120.0f);
			if (ImGuiMCP::SliderFloat(pxId.c_str(), &t.peakOffsetX, -20.0f, 20.0f, "X %.2f")) anyChanged = true;
			if (ImGuiMCP::IsItemHovered()) ImGuiMCP::SetTooltip("Vertical additive offset at the peak of the transition.");
			ImGuiMCP::SameLine();
			ImGuiMCP::SetNextItemWidth(120.0f);
			if (ImGuiMCP::SliderFloat(pyId.c_str(), &t.peakOffsetY, -20.0f, 20.0f, "Y %.2f")) anyChanged = true;
			if (ImGuiMCP::IsItemHovered()) ImGuiMCP::SetTooltip("Forward/back additive offset at the peak.");
			ImGuiMCP::SameLine();
			ImGuiMCP::SetNextItemWidth(120.0f);
			if (ImGuiMCP::SliderFloat(pzId.c_str(), &t.peakOffsetZ, -20.0f, 20.0f, "Z %.2f")) anyChanged = true;
			if (ImGuiMCP::IsItemHovered()) ImGuiMCP::SetTooltip("Lateral additive offset at the peak.");

			ImGuiMCP::Text("Peak Rotation (Pitch / Yaw / Roll deg):");
			ImGuiMCP::SetNextItemWidth(120.0f);
			if (ImGuiMCP::SliderFloat(rPitId.c_str(), &t.peakRotPitch, -30.0f, 30.0f, "P %.1f")) anyChanged = true;
			if (ImGuiMCP::IsItemHovered()) ImGuiMCP::SetTooltip("Additive pitch rotation at the peak.");
			ImGuiMCP::SameLine();
			ImGuiMCP::SetNextItemWidth(120.0f);
			if (ImGuiMCP::SliderFloat(rYawId.c_str(), &t.peakRotYaw, -30.0f, 30.0f, "Y %.1f")) anyChanged = true;
			if (ImGuiMCP::IsItemHovered()) ImGuiMCP::SetTooltip("Additive yaw rotation at the peak.");
			ImGuiMCP::SameLine();
			ImGuiMCP::SetNextItemWidth(120.0f);
			if (ImGuiMCP::SliderFloat(rRolId.c_str(), &t.peakRotRoll, -30.0f, 30.0f, "R %.1f")) anyChanged = true;
			if (ImGuiMCP::IsItemHovered()) ImGuiMCP::SetTooltip("Additive roll rotation at the peak.");

			ImGuiMCP::Spacing();
			ImGuiMCP::SetNextItemWidth(180.0f);
			if (SliderFloatWithTooltip(ppId.c_str(), &t.peakPosition, 0.01f, 0.99f, "Peak pos %.2f",
				"Where in the transition the peak occurs (0=start, 0.5=midpoint, 1=end)."))
				anyChanged = true;
			ImGuiMCP::SetNextItemWidth(180.0f);
			if (SliderFloatWithTooltip(asymId.c_str(), &t.asymmetry, -1.0f, 1.0f, "Asymmetry %.2f",
				"Positive = peak arrives earlier (front-loaded). Negative = peak arrives later."))
				anyChanged = true;
			ImGuiMCP::SetNextItemWidth(180.0f);
			if (SliderFloatWithTooltip(blnId.c_str(), &t.impulseBlendFactor, 0.0f, 1.0f, "Impulse blend %.2f",
				"How strongly the transition curve aligns with the ADS enter/exit spring impulse at endpoints."))
				anyChanged = true;
		}
		if (anyChanged) changed = true;
		return anyChanged;
	}

	// ============================================================
	void __stdcall RenderADSTransitions()
	{
		using namespace ImGuiMCP;

		auto* presets = InertiaPresets::GetSingleton();
		const auto& types = GetWeaponTypes();

		ImGuiMCP::TextColored(ImVec4(1.0f, 0.85f, 0.3f, 1.0f), "FP Inertia - ADS Transitions");
		ImGuiMCP::TextWrapped(
			"Procedural secondary motion added on top of the vanilla linear ADS transition. "
			"Settings are per-weapon-type (specific weapon presets override). "
			"Spring dampening applies to all residual spring motion when entering/exiting ADS.");
		ImGuiMCP::Spacing();
		ImGuiMCP::Separator();

		// ---- Weapon type selector ----
		ImGuiMCP::Text("Weapon Type:");
		ImGuiMCP::SameLine();
		ImGuiMCP::SetNextItemWidth(200.0f);
		{
			const char* preview = (State::adsTransWeaponTypeIndex >= 0 &&
				State::adsTransWeaponTypeIndex < static_cast<int>(types.size()))
				? types[State::adsTransWeaponTypeIndex].displayName.c_str()
				: "Rifle";
			if (ImGuiMCP::BeginCombo("##AdsTransWTCombo", preview)) {
				for (int i = 0; i < static_cast<int>(types.size()); ++i) {
					bool sel = (State::adsTransWeaponTypeIndex == i);
					if (ImGuiMCP::Selectable(types[i].displayName.c_str(), sel))
						State::adsTransWeaponTypeIndex = i;
					if (sel) ImGuiMCP::SetItemDefaultFocus();
				}
				ImGuiMCP::EndCombo();
			}
		}

		// Specific weapon override
		auto savedPresets = presets->GetSavedSpecificWeaponPresets();
		if (!savedPresets.empty()) {
			ImGuiMCP::Text("Or Specific Weapon:");
			ImGuiMCP::SameLine();
			ImGuiMCP::SetNextItemWidth(200.0f);
			const char* specPreview = (State::adsTransSpecificWeaponIndex >= 0 &&
				State::adsTransSpecificWeaponIndex < static_cast<int>(savedPresets.size()))
				? savedPresets[State::adsTransSpecificWeaponIndex].c_str()
				: "(use weapon type)";
			if (ImGuiMCP::BeginCombo("##AdsTransSpecCombo", specPreview)) {
				if (ImGuiMCP::Selectable("(use weapon type)", State::adsTransSpecificWeaponIndex < 0))
					State::adsTransSpecificWeaponIndex = -1;
				for (int i = 0; i < static_cast<int>(savedPresets.size()); ++i) {
					bool sel = (State::adsTransSpecificWeaponIndex == i);
					if (ImGuiMCP::Selectable(savedPresets[i].c_str(), sel))
						State::adsTransSpecificWeaponIndex = i;
					if (ImGuiMCP::IsItemHovered()) {
						auto path = presets->GetSpecificWeaponPresetPath(savedPresets[i]);
						ImGuiMCP::SetTooltip("%s", path.string().c_str());
					}
					if (sel) ImGuiMCP::SetItemDefaultFocus();
				}
				ImGuiMCP::EndCombo();
			}
		}

		// Resolve which settings to edit
		WeaponInertiaSettings* editSettings = nullptr;
		std::string editLabel;
		if (State::adsTransSpecificWeaponIndex >= 0 &&
			State::adsTransSpecificWeaponIndex < static_cast<int>(savedPresets.size())) {
			const auto& eid = savedPresets[State::adsTransSpecificWeaponIndex];
			if (presets->HasSpecificWeaponSettings(eid)) {
				WeaponType baseType = presets->HasWeaponTypeOverride(eid)
					? presets->GetWeaponTypeOverride(eid) : WeaponType::Rifle;
				editSettings = &presets->GetOrCreateSpecificWeaponSettings(eid, baseType);
				editLabel = eid;
			}
		}
		if (!editSettings && State::adsTransWeaponTypeIndex >= 0 &&
			State::adsTransWeaponTypeIndex < static_cast<int>(types.size())) {
			const auto& entry = types[State::adsTransWeaponTypeIndex];
			editSettings = &GetWeaponSettingsForEditingByEntry(entry);
			editLabel = entry.displayName;
		}
		if (!editSettings) return;

		auto& ws = *editSettings;

		ImGuiMCP::Spacing();
		ImGuiMCP::Separator();
		ImGuiMCP::TextColored(ImVec4(0.4f, 0.8f, 1.0f, 1.0f), "Editing: %s", editLabel.c_str());

		// Show equipped weapon's sightedTransitionSeconds for reference
		{
			auto* player = RE::PlayerCharacter::GetSingleton();
			if (player && player->currentProcess && player->currentProcess->middleHigh) {
				auto* mh = player->currentProcess->middleHigh;
				RE::BSAutoLock lock{ mh->equippedItemsLock };
				for (auto& equipped : mh->equippedItems) {
					auto* form = equipped.item.object;
					if (form && form->formType == RE::ENUM_FORM_ID::kWEAP && equipped.item.instanceData) {
						auto* wid = static_cast<RE::TESObjectWEAP::InstanceData*>(equipped.item.instanceData.get());
						if (wid && wid->rangedData) {
							ImGuiMCP::SameLine();
							ImGuiMCP::TextDisabled("  (Equipped ADS time: %.3fs)", wid->rangedData->sightedTransitionSeconds);
						}
						break;
					}
				}
			}
		}
		ImGuiMCP::Spacing();

		bool changed = false;

		// ====== ADS SPRING DAMPENING ======
		if (ImGuiMCP::CollapsingHeader("ADS Spring Dampening", ImGuiTreeNodeFlags_DefaultOpen)) {
			ImGuiMCP::TextWrapped(
				"When entering or exiting ADS, multiply residual spring velocities (camera, movement, sprint, jump) "
				"by this factor. Lower values reduce jitter during the transition.");
			ImGuiMCP::Spacing();

			if (CheckboxWithTooltip("Enable Dampening##adsDampen", &ws.adsTransitionDampenEnabled,
				"When enabled, all continuous spring velocities are multiplied by the factor below on ADS transitions."))
				changed = true;

			if (ws.adsTransitionDampenEnabled) {
				ImGuiMCP::SetNextItemWidth(220.0f);
				if (SliderFloatWithTooltip("Dampen Factor##adsDampenFactor",
					&ws.adsTransitionDampenFactor, 0.0f, 1.0f, "%.2f",
					"Fraction of spring velocity to keep (0=kill all, 0.25=keep 25%, 1=no dampening)."))
					changed = true;
			}
		}

		// ====== ADS ENTER TRANSITION ======
		DrawADSTransitionBlock("ADS Enter Transition (Hip -> ADS)##adsEnter", "adsEnter",
			ws.adsEnterTransition, changed);

		// ====== ADS EXIT TRANSITION ======
		DrawADSTransitionBlock("ADS Exit Transition (ADS -> Hip)##adsExit", "adsExit",
			ws.adsExitTransition, changed);

		if (changed) {
			presets->IncrementSettingsVersion();
			presets->MarkDirty();
			State::hasUnsavedChanges = true;
		}

		// ---- Save Buttons ----
		ImGuiMCP::Spacing();
		ImGuiMCP::Separator();

		if (State::saveStatusTimer > 0.0f) {
			auto* io = ImGuiMCP::GetIO();
			if (io) State::saveStatusTimer -= io->DeltaTime;
			ImGuiMCP::TextColored(ImVec4(0.3f, 1.0f, 0.3f, 1.0f), "%s", State::saveStatusMsg.c_str());
		}

		if (ImGuiMCP::Button("Save Weapon Type Presets##adsTrans")) {
			presets->SaveWeaponTypePresets();
			State::saveStatusMsg = "ADS transition presets saved.";
			State::saveStatusTimer = 4.0f;
		}
		if (State::adsTransSpecificWeaponIndex >= 0 &&
			State::adsTransSpecificWeaponIndex < static_cast<int>(savedPresets.size())) {
			ImGuiMCP::SameLine();
			const auto& eid = savedPresets[State::adsTransSpecificWeaponIndex];
			if (ImGuiMCP::Button("Save Specific Preset##adsTrans")) {
				presets->SaveSpecificWeaponPreset(eid);
				State::saveStatusMsg = std::format("Saved: {}", eid);
				State::saveStatusTimer = 4.0f;
			}
		}
	}

} // namespace Menu
