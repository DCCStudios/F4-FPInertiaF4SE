#pragma once

#include <atomic>
#include <chrono>
#include <filesystem>
#include <mutex>
#include <string>
#include <unordered_map>
#include <vector>

namespace RE
{
	class PlayerCharacter;
	class TESForm;
	class TESBoundObject;
}

namespace WeaponFOV
{
	// Source of the default viewmodel FOV when no weapon-specific entry exists.
	enum class FOVDefaultSource
	{
		HardcodedDefault,    // No FOV mod installed — use 80
		FOVSliderF4SE,       // From FOV Slider F4SE.ini (native F4SE plugin, highest priority)
		SimpleFOVSlider,     // From SimpleFOVSlider.ini
		FOVSlider,           // From FOVSlider.ini
		UserINI,             // From the user's Fallout4Prefs/Custom INI
	};

	struct WBFOVEntry
	{
		std::string editorID;     // Weapon EditorID — also the JSON filename stem
		std::string displayName;  // Cached friendly name (best-effort)
		float       viewmodelFOV{ 80.0f };
	};

	class Manager
	{
	public:
		static Manager* GetSingleton()
		{
			static Manager singleton;
			return &singleton;
		}

		// Initialize: detect FOV mods, load default values, scan WBFOV folder
		void Init();

		// Re-detect FOV mod defaults (call on game load — user might have changed
		// FOV mod settings since last session). Immediately applies the correct
		// viewmodel FOV if the player is ready.
		void RefreshDefaults();

		// After a game load, schedule a short burst of re-applies on a
		// background thread. The engine's script compiler and FOV state
		// aren't always ready on the very first frame after load, so
		// retrying a few times over the first ~1s catches any race.
		void ScheduleLoadRetry();

		// External-override lock. When set, WBFOV's per-frame Update is a
		// no-op so FOV Slider F4SE can own the viewmodel FOV during
		// Pip-Boy / Terminal / Aiming contexts without us fighting it.
		void SetExternalOverride(bool a_locked);
		bool IsExternalOverride() const { return externalOverride.load(); }

		// Per-frame update — applies the correct FOV based on equipped weapon
		void Update(RE::PlayerCharacter* player, bool weaponDrawn);

		// Force-restore the default viewmodel FOV (called when feature disabled)
		void RestoreDefault();

		// ---- Query API ----
		float            GetDefaultViewmodelFOV() const { return defaultViewmodelFOV.load(); }
		FOVDefaultSource GetDefaultSource() const { return defaultSource; }
		const char*      GetDefaultSourceName() const;

		bool  HasEntry(const std::string& editorID) const;
		float GetEntryFOV(const std::string& editorID) const;

		// ---- Mutation API (used by menu) ----
		void SetEntry(const std::string& editorID, const std::string& displayName, float fov);
		void RemoveEntry(const std::string& editorID);

		// Returns a snapshot of all entries (for menu display)
		std::vector<WBFOVEntry> GetEntries() const;

		// Save/load individual JSON files
		void SaveEntry(const std::string& editorID);
		void LoadAllEntries();

		// Path helpers
		std::filesystem::path GetWBFOVFolder() const;
		std::filesystem::path GetEntryPath(const std::string& editorID) const;

	private:
		Manager() = default;
		~Manager() = default;
		Manager(const Manager&) = delete;
		Manager(Manager&&) = delete;
		Manager& operator=(const Manager&) = delete;
		Manager& operator=(Manager&&) = delete;

		// Apply a viewmodel FOV value via the engine's `fov X Y` console
		// command — X = viewmodel target, Y = camera FOV (from INI cache).
		// After the command, directly writes PlayerCamera::worldFOV to undo
		// the 3rd-person clobber. firstPersonFOV is NOT written same-frame
		// because the engine hasn't latched the viewmodel yet — doing so
		// would undo the viewmodel change. The INI re-assert handles
		// firstPersonFOV correction a few frames later.
		void ApplyViewmodelFOV(float fov);

		// Smooth interpolation from `from` to `to` over `frames` steps (~8ms
		// each). Generation-guarded so a newer transition supersedes any
		// in-flight one. Modeled after FOV Slider F4SE's InterpolateViewmodelFOV.
		void InterpolateViewmodelFOV(float from, float to, int frames);

		// Detect installed FOV mods and read their default viewmodel FOV setting.
		// Sets defaultViewmodelFOV and defaultSource.
		void DetectAndLoadDefaultFOV();

		// Refresh the cached camera (1st-person) FOV from the engine's live
		// in-memory `fDefault1stPersonFOV:Display` setting. The FOV slider
		// mods write to this exact setting via Papyrus `SetINIFloat` whenever
		// the user adjusts their MCM, so reading it back gives us the live
		// current value with no INI file IO. Called every Update tick to
		// avoid feeding a stale Y arg into `fov X Y`, which would otherwise
		// cause a visible camera-FOV jump every time we re-apply.
		void RefreshLiveCameraFOV();

		// Tracking
		std::atomic<float>  defaultViewmodelFOV{ 80.0f };
		FOVDefaultSource    defaultSource{ FOVDefaultSource::HardcodedDefault };

		// Cached camera (1st-person world) FOV — sourced from FOV slider mods
		// then the user's INIs. Used as the Y argument of `fov X Y` so we
		// only ever set the viewmodel and never disturb the camera FOV.
		std::atomic<float>  cameraFOV{ 90.0f };
		FOVDefaultSource    cameraSource{ FOVDefaultSource::HardcodedDefault };

		// Cached 3rd-person FOV — re-written to fDefaultWorldFOV:Display
		// after every `fov X Y` to undo the command's side-effect of
		// clobbering 3rd-person FOV to X (the viewmodel value).
		std::atomic<float>  thirdPersonFOV{ 80.0f };

		// Last applied viewmodel FOV — used to suppress redundant re-applies
		// (executing the script compiler every frame is unnecessary work).
		float               lastAppliedFOV{ -1.0f };

		// Time of last applied command — drives a periodic re-apply (every
		// ~1.5s while an override is active) so FOV-slider mods can't
		// permanently undo us when their MCM/menu close handlers re-issue
		// `fov`.
		std::chrono::steady_clock::time_point lastApplyTime{};

		// Set by FOV Slider F4SE during Pip-Boy / Terminal / Aiming
		// contexts. While true, Update() does nothing so FOV Slider F4SE
		// owns the viewmodel FOV without us clobbering its overrides.
		std::atomic<bool> externalOverride{ false };

		// Track previous override state so we can detect entry-gain / entry-loss
		// transitions for immediate re-application.
		bool                hadWeaponEntry{ false };

		// Track previous equipped weapon / draw state so we can detect transitions
		std::uint32_t       prevWeaponFormID{ 0 };
		bool                prevWeaponDrawn{ false };
		bool                lastEnabled{ true };

		// Generation counter for ScheduleLoadRetry — incremented each call,
		// so a prior thread that's still sleeping bails out.
		std::atomic<std::uint32_t> loadRetryGeneration{ 0 };

		// Generation counter for InterpolateViewmodelFOV — a newer
		// transition supersedes any in-flight one.
		std::atomic<std::uint64_t> interpGeneration{ 0 };

		// True while the camera-state guard is blocking applies (furniture,
		// 3rd-person, VATS, etc.). Used to detect the transition back to
		// first-person so we can smooth-lerp rather than hard-snap.
		bool wasCameraBlocked{ false };

		// editorID -> entry map (case-sensitive, matches preset system convention)
		mutable std::mutex                          entriesMtx;
		std::unordered_map<std::string, WBFOVEntry> entries;
	};
}
