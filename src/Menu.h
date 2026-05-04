#pragma once

#include "F4SEMenuFramework.h"
#include "Settings.h"
#include "InertiaPresets.h"

namespace Menu
{
	// Register with F4SE Menu Framework
	void Register();

	// Main render callback for F4SE Menu Framework
	void __stdcall Render();

	// Debug Info tab render callback
	void __stdcall RenderDebugInfo();

	// Debug popout window render callback (non-pausing, managed by framework)
	void __stdcall RenderDebugPopout();

	// Extras tab render callback
	void __stdcall RenderExtras();

	// ADS Transitions tab render callback
	void __stdcall RenderADSTransitions();

	// Internal state for menu
	namespace State
	{
		// Editing state
		inline bool initialized{ false };
		inline bool hasUnsavedChanges{ false };

		// Preset profile management
		inline int selectedPresetIndex{ 0 };
		inline char newPresetName[128]{ "" };
		inline bool showCreatePresetPopup{ false };
		inline bool showDeletePresetConfirm{ false };
		inline std::vector<std::string> cachedPresetList;
		inline bool presetListAttemptedRefresh{ false };

		// Selected weapon type for per-weapon settings
		inline int selectedWeaponTypeIndex{ 0 };

		// Collapsible section states
		inline bool generalExpanded{ true };
		inline bool settlingExpanded{ false };
		inline bool movementExpanded{ false };
		inline bool actionBlendExpanded{ false };
		inline bool powerArmorExpanded{ false };
		inline bool debugExpanded{ false };
		inline bool weaponSettingsExpanded{ true };
		inline bool specificWeaponExpanded{ false };

		// Specific weapon preset management
		inline char newWeaponEditorID[256]{ "" };
		inline int selectedSpecificWeaponIndex{ -1 };

		// Weapon type override when creating a per-weapon preset
		inline int createPresetWeaponTypeIndex{ 3 };  // default = Rifle (index 3 in s_weaponTypes)

		// Copy to preset dialog state
		inline bool showCopyToPresetPopup{ false };
		inline int copyTargetPresetIndex{ 0 };
		inline std::string copySourceWeaponType;
		inline std::string copySourcePreset;

		// Copy from specific weapon preset
		inline int copyToTypeIndex{ 3 };                    // target weapon type index
		inline char copyToEditorBuf[256]{ "" };             // target EditorID for copy-to-specific
		inline std::string copyConfirmMsg;                  // feedback line shown after copy
		inline float copyConfirmTimer{ 0.0f };

		// Save status feedback (shown briefly after save)
		inline std::string saveStatusMsg;
		inline float saveStatusTimer{ 0.0f };

		// Debug popout window (framework-managed)
		inline MENU_WINDOW debugPopoutWindow{ nullptr };

		// Extras tab state
		inline int extrasWeaponTypeIndex{ 0 };
		inline int extrasSpecificWeaponIndex{ -1 };

		// ADS Transitions tab state
		inline int adsTransWeaponTypeIndex{ 3 };   // default to Rifle
		inline int adsTransSpecificWeaponIndex{ -1 };
	}

	// Weapon type info for dropdown (supports both standard and custom types)
	struct WeaponTypeEntry
	{
		std::string displayName;
		std::string internalName;  // INI section name or custom type name
		WeaponType type;           // Enum for standard types (ignored for custom)
		bool isCustomType;         // True if this is a keyword-based custom type

		WeaponTypeEntry(const char* display, const char* internal, WeaponType t)
			: displayName(display), internalName(internal), type(t), isCustomType(false) {}

		explicit WeaponTypeEntry(const std::string& customTypeName)
			: displayName(customTypeName), internalName(customTypeName), type(WeaponType::Unarmed), isCustomType(true) {}
	};

	// Get array of weapon types for dropdown (refreshes to include custom types)
	const std::vector<WeaponTypeEntry>& GetWeaponTypes();
	void RefreshWeaponTypesList();

	// Get mutable settings for a weapon type entry (from presets)
	WeaponInertiaSettings& GetWeaponSettingsForEditingByEntry(const WeaponTypeEntry& entry);

	// Draw helper functions
	void DrawHeader();
	void DrawPresetSelector();
	void DrawGeneralSettings();
	void DrawSettlingSettings();
	void DrawMovementInertiaSettings();
	void DrawActionBlendSettings();
	void DrawPowerArmorSettings();
	void DrawDebugSettings();
	void DrawWeaponTypeSettings();
	void DrawSpecificWeaponSettings();
	void DrawWeaponInertiaEditor(WeaponInertiaSettings& settings, const char* label);
	void DrawSaveLoadButtons();

	// Refresh cached preset list
	void RefreshPresetList();

	// Helper for sliders with tooltips
	bool SliderFloatWithTooltip(const char* label, float* value, float min, float max, const char* format, const char* tooltip);
	bool CheckboxWithTooltip(const char* label, bool* value, const char* tooltip);
	bool SliderIntWithTooltip(const char* label, int* value, int min, int max, const char* format, const char* tooltip);
}
