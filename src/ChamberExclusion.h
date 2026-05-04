#pragma once

#include <set>
#include <string>
#include <vector>
#include <mutex>

namespace ChamberExclusion
{
	struct ExcludedWeaponEntry
	{
		std::uint32_t formID{ 0 };
		std::string   editorID;
		std::string   displayName;
	};

	class Manager
	{
	public:
		static Manager* GetSingleton()
		{
			static Manager singleton;
			return &singleton;
		}

		void Init();

		bool IsInstalled() const { return installed; }
		bool IsKeywordResolved() const { return chamberExclusionKW != nullptr; }

		bool IsWeaponExcluded(std::uint32_t formID) const;
		void AddWeapon(std::uint32_t formID, const std::string& editorID, const std::string& displayName);
		void RemoveWeapon(std::uint32_t formID);

		const std::vector<ExcludedWeaponEntry>& GetExcludedWeapons() const { return excludedWeapons; }

		void ApplyKeywordToEquippedInstance(RE::PlayerCharacter* player);
		void ReapplyAllKeywords(RE::PlayerCharacter* player);

	private:
		Manager() = default;
		~Manager() = default;
		Manager(const Manager&) = delete;
		Manager(Manager&&) = delete;
		Manager& operator=(const Manager&) = delete;
		Manager& operator=(Manager&&) = delete;

		RE::TESForm* GetFormFromMod(const char* modName, std::uint32_t localFormID);
		void ApplyKeywordToBaseForm(std::uint32_t formID);
		void RemoveKeywordFromBaseForm(std::uint32_t formID);
		void AddKeywordToForm(RE::BGSKeywordForm* form, RE::BGSKeyword* kw);
		void RemoveKeywordFromForm(RE::BGSKeywordForm* form, RE::BGSKeyword* kw);

		void LoadJSON();
		void SaveJSON();
		std::filesystem::path GetJSONPath() const;

		bool installed{ false };
		RE::BGSKeyword* chamberExclusionKW{ nullptr };
		std::vector<ExcludedWeaponEntry> excludedWeapons;
		mutable std::mutex mtx;
	};
}
