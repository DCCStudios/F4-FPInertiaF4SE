#pragma once

#include "Settings.h"
#include <nlohmann/json.hpp>

using json = nlohmann::json;

// Hash/equality for WeaponType enum keys
struct WeaponTypeKeyHash
{
	std::size_t operator()(WeaponType k) const noexcept
	{
		return std::hash<int>()(static_cast<int>(k));
	}
};

// Keyword-to-weapon-type mapping (loaded from WeaponTypeMappings/*.txt)
struct KeywordMapping
{
	std::vector<std::string> keywords;
	std::vector<RE::BGSKeyword*> resolvedKeywords;
	std::string weaponTypeName;
	bool keywordsResolved{ false };

	bool operator<(const KeywordMapping& other) const
	{
		return keywords.size() > other.keywords.size(); // most-specific first
	}
};

// JSON serialization declarations
void to_json(json& j, const WeaponInertiaSettings& s);
void from_json(const json& j, WeaponInertiaSettings& s);

class InertiaPresets
{
public:
	static InertiaPresets* GetSingleton()
	{
		static InertiaPresets singleton;
		return &singleton;
	}

	void Init();

	// Get settings (priority: specific weapon -> keyword-based -> type-based)
	const WeaponInertiaSettings& GetWeaponSettings(const std::string& a_editorID, WeaponType a_type) const;
	const WeaponInertiaSettings& GetWeaponSettingsWithKeywords(
		const std::string& a_editorID, RE::TESObjectWEAP* a_weapon, WeaponType a_type) const;
	const WeaponInertiaSettings& GetWeaponTypeSettings(WeaponType a_type) const;
	WeaponInertiaSettings& GetWeaponTypeSettingsMutable(WeaponType a_type);

	// Specific weapon presets (by EditorID)
	const WeaponInertiaSettings* GetSpecificWeaponSettings(const std::string& a_editorID) const;
	WeaponInertiaSettings& GetOrCreateSpecificWeaponSettings(const std::string& a_editorID, WeaponType a_baseType);
	bool HasSpecificWeaponSettings(const std::string& a_editorID) const;
	void RemoveSpecificWeaponSettings(const std::string& a_editorID);
	std::vector<std::string> GetSavedSpecificWeaponPresets() const;

	// Per-weapon type overrides (from JSON presets)
	bool HasWeaponTypeOverride(const std::string& a_editorID) const;
	WeaponType GetWeaponTypeOverride(const std::string& a_editorID) const;
	void SetWeaponTypeOverride(const std::string& a_editorID, WeaponType a_type);

	// Custom keyword-based types
	const WeaponInertiaSettings* GetCustomWeaponTypeSettings(const std::string& a_name) const;
	WeaponInertiaSettings& GetCustomWeaponTypeSettingsMutable(const std::string& a_name);
	const std::vector<std::string>& GetCustomWeaponTypes() const { return customWeaponTypeNames; }
	bool IsCustomWeaponType(const std::string& a_name) const;

	// Preset file management
	void SaveWeaponTypePresets();
	void LoadWeaponTypePresets();
	void SaveSpecificWeaponPreset(const std::string& a_editorID);
	void LoadSpecificWeaponPreset(const std::string& a_editorID);
	void LoadAllPresets();
	void ResetToINIValues();

	// Preset profile management (multiple named presets)
	std::vector<std::string> GetAvailablePresets() const;
	const std::string& GetActivePresetName() const { return activePresetName; }
	void SetActivePreset(const std::string& a_name);
	void CreateNewPreset(const std::string& a_name);
	void DuplicatePreset(const std::string& a_src, const std::string& a_dst);
	void DeletePreset(const std::string& a_name);
	void RenamePreset(const std::string& a_old, const std::string& a_new);
	void SaveActivePresetSetting();
	void LoadActivePresetSetting();

	// Path helpers
	std::filesystem::path GetPresetFolderPath() const;
	std::filesystem::path GetWeaponTypePresetsPath() const;
	std::filesystem::path GetPresetPath(const std::string& a_name) const;
	std::filesystem::path GetSpecificWeaponPresetPath(const std::string& a_editorID) const;
	std::filesystem::path GetKeywordMappingsFolderPath() const;

	// Keyword mappings
	void LoadKeywordMappings();
	void ResolveKeywordPointers();
	std::string GetBestKeywordMatch(RE::TESObjectWEAP* a_weapon) const;
	void EnsureCustomTypesInPreset();

	// Dirty tracking / version
	bool IsDirty() const { return isDirty; }
	void MarkDirty() { isDirty = true; }
	void ClearDirty() { isDirty = false; }
	uint32_t GetSettingsVersion() const { return settingsVersion; }
	void IncrementSettingsVersion() { settingsVersion++; }

	// Type name helpers
	static const char* GetWeaponTypeName(WeaponType a_type);
	static const char* GetWeaponTypeDisplayName(WeaponType a_type);
	static WeaponType ParseWeaponTypeName(const std::string& a_name);

private:
	InertiaPresets() = default;
	~InertiaPresets() = default;
	InertiaPresets(const InertiaPresets&) = delete;
	InertiaPresets(InertiaPresets&&) = delete;
	InertiaPresets& operator=(const InertiaPresets&) = delete;
	InertiaPresets& operator=(InertiaPresets&&) = delete;

	std::unordered_map<WeaponType, WeaponInertiaSettings, WeaponTypeKeyHash> weaponTypeSettings;
	std::unordered_map<std::string, WeaponInertiaSettings> specificWeaponSettings;
	std::unordered_map<std::string, WeaponInertiaSettings> customWeaponTypeSettings;

	// Per-weapon type overrides (EditorID -> WeaponType), populated from JSON presets
	std::unordered_map<std::string, WeaponType> weaponTypeOverrides;

	std::vector<KeywordMapping> keywordMappings;
	std::vector<std::string> customWeaponTypeNames;

	WeaponInertiaSettings defaultSettings{};
	bool isDirty{ false };
	uint32_t settingsVersion{ 0 };
	std::string activePresetName{ "WeaponTypes" };

	mutable std::shared_mutex presetMutex;

	void EnsurePresetFolderExists();
	void InitializeDefaultSettings();
};
