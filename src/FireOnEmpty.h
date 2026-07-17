#pragma once

#include <filesystem>
#include <mutex>
#include <string>
#include <unordered_map>
#include <vector>

namespace FireOnEmpty
{
	// One per-weapon opt-in record. The JSON filename stem is the weapon
	// EditorID (matching this mod's other per-weapon systems, e.g. WBFOV).
	//
	// The feature is OPT-IN per weapon: a weapon only forces the fire
	// animation on an empty magazine if it has an entry here AND that
	// entry is enabled. The intent is that mod authors ship these JSONs
	// alongside an Open Animation Replacer set that swaps the forced fire
	// animation for a dry-fire animation under their own conditions.
	struct FOEEntry
	{
		std::string editorID;      // weapon EditorID — also the JSON filename stem
		std::string displayName;   // cached friendly name (best-effort)
		bool        enabled{ false };  // opt-in toggle (default off)
		bool        autoFire{ false }; // false = single-fire action, true = auto-fire action
	};

	class Manager
	{
	public:
		static Manager* GetSingleton()
		{
			static Manager singleton;
			return &singleton;
		}

		// Scan the FireOnEmpty folder and load every *.json entry.
		void Init();

		// ---- Query API (called from the per-frame runtime hook) ----
		// Returns true and fills a_out if an entry exists for a_editorID.
		bool HasEntry(const std::string& a_editorID) const;
		bool GetEntry(const std::string& a_editorID, FOEEntry& a_out) const;

		// ---- Mutation API (used by the in-game menu) ----
		// Creates the JSON on demand if it does not already exist.
		void SetEntry(const std::string& a_editorID, const std::string& a_displayName,
			bool a_enabled, bool a_autoFire);
		void RemoveEntry(const std::string& a_editorID);

		// Snapshot of all entries, sorted by EditorID (for menu display).
		std::vector<FOEEntry> GetEntries() const;

		// ---- Path helpers ----
		std::filesystem::path GetFolder() const;
		std::filesystem::path GetEntryPath(const std::string& a_editorID) const;

	private:
		Manager() = default;
		~Manager() = default;
		Manager(const Manager&) = delete;
		Manager(Manager&&) = delete;
		Manager& operator=(const Manager&) = delete;
		Manager& operator=(Manager&&) = delete;

		void SaveEntry(const std::string& a_editorID);
		void LoadAllEntries();

		// editorID -> entry (case-sensitive, matching the preset system convention)
		mutable std::mutex                        entriesMtx;
		std::unordered_map<std::string, FOEEntry> entries;
	};
}
