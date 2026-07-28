#include "PCH.h"
#include "WeaponFOV.h"
#include "Settings.h"
#include "SimpleIni.h"

#include <fstream>
#include <nlohmann/json.hpp>
#include <spdlog/spdlog.h>

namespace WeaponFOV
{
	static constexpr float kHardcodedDefaultFOV = 80.0f;

	// ---- FOV mod INI paths and keys ----
	// FOV Slider F4SE — native F4SE plugin replacement for Simple FOV Slider.
	// Highest priority because it owns the `Display` section live in the
	// engine's INI collections; if it's installed, the values it writes are
	// the user's intent.
	static constexpr const char* kFOVSliderF4SE_INI            = "Data\\F4SE\\Plugins\\FOV Slider F4SE.ini";
	static constexpr const char* kFOVSliderF4SE_Section        = "Display";
	static constexpr const char* kFOVSliderF4SE_VMKey          = "fViewmodelFOV";
	static constexpr const char* kFOVSliderF4SE_FirstPersonKey = "fFirstPersonFOV";

	// Simple FOV Slider — uses MCM Display section.
	static constexpr const char* kSimpleFOV_UserINI       = "Data\\MCM\\Settings\\SimpleFOVSlider.ini";
	static constexpr const char* kSimpleFOV_DefaultINI    = "Data\\MCM\\Config\\SimpleFOVSlider\\settings.ini";
	static constexpr const char* kSimpleFOV_Section       = "Display";
	static constexpr const char* kSimpleFOV_VMKey         = "fViewmodelFOV";
	static constexpr const char* kSimpleFOV_FirstPersonKey= "fFirstPersonFOV";

	// FOV Slider and Player Height — uses MCM Main section.
	static constexpr const char* kFOVSlider_UserINI       = "Data\\MCM\\Settings\\FOVSlider.ini";
	static constexpr const char* kFOVSlider_DefaultINI    = "Data\\MCM\\Config\\FOVSlider\\settings.ini";
	static constexpr const char* kFOVSlider_Section       = "Main";
	static constexpr const char* kFOVSlider_VMKey         = "fViewModelFOVSlider";
	static constexpr const char* kFOVSlider_FirstPersonKey= "fFirstPersonFOVSlider";

	// Vanilla Fallout 4 defaults when nothing else has overridden them.
	static constexpr float kHardcodedCameraFOV     = 90.0f;
	static constexpr float kHardcodedThirdPersonFOV = 80.0f;

	// INI keys for 3rd-person FOV (needed to undo the `fov X Y` side-effect).
	static constexpr const char* kFOVSliderF4SE_ThirdPersonKey = "fThirdPersonFOV";
	static constexpr const char* kSimpleFOV_ThirdPersonKey     = "fThirdPersonFOV";
	static constexpr const char* kFOVSlider_ThirdPersonKey     = "fThirdPersonFOVSlider";

	// Forward — used in init/refresh logging before its definition.
	static const char* SourceName(FOVDefaultSource src);

	// ---- Helpers ----

	// Read a float from one of the candidate INI files (returns true on hit).
	static bool TryReadFOVFromINI(const char* iniPath, const char* section, const char* key, float& outValue)
	{
		if (!std::filesystem::exists(iniPath)) return false;

		CSimpleIniA ini;
		ini.SetUnicode();
		if (ini.LoadFile(iniPath) < 0) return false;

		const char* raw = ini.GetValue(section, key, nullptr);
		if (!raw) return false;

		try {
			outValue = std::stof(raw);
			return true;
		} catch (...) {
			return false;
		}
	}

	// Look up a float setting from the engine's in-memory INI / GameSetting
	// collections. Tries INIPrefSettingCollection (Fallout4Prefs.ini),
	// INISettingCollection (Fallout4.ini / Custom.ini), then
	// GameSettingCollection (.esm-defined defaults). Returns true on hit.
	static bool TryReadFloatFromGameINI(const char* a_key, float& a_outValue)
	{
		if (auto* prefs = RE::INIPrefSettingCollection::GetSingleton()) {
			if (auto* s = prefs->GetSetting(a_key); s && s->GetType() == RE::Setting::SETTING_TYPE::kFloat) {
				a_outValue = s->GetFloat();
				return true;
			}
		}
		if (auto* ini = RE::INISettingCollection::GetSingleton()) {
			if (auto* s = ini->GetSetting(a_key); s && s->GetType() == RE::Setting::SETTING_TYPE::kFloat) {
				a_outValue = s->GetFloat();
				return true;
			}
		}
		// GameSettingCollection uses the BTree variant; iterate via GetSetting on the parent.
		if (auto* gs = RE::GameSettingCollection::GetSingleton()) {
			for (auto& kv : gs->settings) {
				if (kv.second && kv.second->GetKey() == a_key &&
					kv.second->GetType() == RE::Setting::SETTING_TYPE::kFloat) {
					a_outValue = kv.second->GetFloat();
					return true;
				}
			}
		}
		return false;
	}

	// Write a float setting into the engine's live in-memory INI collections.
	// Equivalent to Papyrus Utility::SetINIFloat — changes apply instantly
	// because the engine reads these values every frame.
	static bool SetEngineFloatSetting(const char* a_key, float a_value)
	{
		if (auto* prefs = RE::INIPrefSettingCollection::GetSingleton()) {
			if (auto* s = prefs->GetSetting(a_key); s && s->GetType() == RE::Setting::SETTING_TYPE::kFloat) {
				s->SetFloat(a_value);
				return true;
			}
		}
		if (auto* ini = RE::INISettingCollection::GetSingleton()) {
			if (auto* s = ini->GetSetting(a_key); s && s->GetType() == RE::Setting::SETTING_TYPE::kFloat) {
				s->SetFloat(a_value);
				return true;
			}
		}
		if (auto* gs = RE::GameSettingCollection::GetSingleton()) {
			for (auto& kv : gs->settings) {
				if (kv.second && kv.second->GetKey() == a_key &&
				    kv.second->GetType() == RE::Setting::SETTING_TYPE::kFloat) {
					kv.second->SetFloat(a_value);
					return true;
				}
			}
		}
		return false;
	}

	// Run a console command via the engine's script compiler. This is the
	// same pipeline Papyrus mods use through ConsoleUtilF4 — and the only
	// reliable way to change the viewmodel FOV in Fallout 4. The `fov X Y`
	// command is the canonical setter:
	//     X = viewmodel FOV AND PlayerCamera::firstPersonFOV AND worldFOV
	//     Y = updates INI fDefault1stPersonFOV:Display only
	//
	// firstPersonFOV controls both the 1st-person camera AND the viewmodel
	// projection, so we cannot write it back to the camera value without
	// undoing the viewmodel change. We fix worldFOV (3rd-person only) and
	// the INI settings in ApplyViewmodelFOV; the 1st-person camera drift
	// is corrected by FOV Slider F4SE's drift watcher.
	//
	// Critical implementation detail: you MUST pass a real ScriptCompiler
	// instance and use COMPILER_NAME::kSystemWindow. Passing nullptr or
	// kDefault makes CompileAndRun silently bail before evaluating.
	// Reference: LucaDotGit/PapyrusCommonLibraryF4 -> CommandRunner::Execute.
	static bool ExecuteConsoleCommand(std::string_view a_command)
	{
		auto* factory = RE::ConcreteFormFactory<RE::Script>::GetFormFactory();
		if (!factory) {
			logger::warn("[WeaponFOV] Script factory unavailable — cannot run '{}'", a_command);
			return false;
		}

		auto* script = factory->Create();
		if (!script) {
			logger::warn("[WeaponFOV] Script create failed — cannot run '{}'", a_command);
			return false;
		}

		// Suppress the "fov 90 95" line that would normally show up in the
		// console history every time we re-apply.
		auto* log = RE::ConsoleLog::GetSingleton();
		std::remove_cvref_t<decltype(log->buffer)> savedBuffer{};
		if (log) savedBuffer = log->buffer;

		RE::ScriptCompiler compiler;
		script->SetText(a_command);
		script->CompileAndRun(&compiler, RE::COMPILER_NAME::kSystemWindow, nullptr);

		const bool compiled = script->header.isCompiled;

		if (log) log->buffer = std::move(savedBuffer);
		delete script;

		if (!compiled) {
			logger::warn("[WeaponFOV] Failed to compile '{}'", a_command);
		}
		return compiled;
	}

	// ============================================================
	// Init / refresh
	// ============================================================
	void Manager::Init()
	{
		DetectAndLoadDefaultFOV();
		LoadAllEntries();

		logger::info("[WeaponFOV] Initialized. VM default={:.2f} ({}), 1P camera={:.2f} ({}), 3P={:.2f}, {} entries loaded.",
			defaultViewmodelFOV.load(), SourceName(defaultSource.load()),
			cameraFOV.load(),           SourceName(cameraSource.load()),
			thirdPersonFOV.load(),
			entries.size());
	}

	void Manager::RefreshDefaults()
	{
		// Refresh can be requested from the F4SE Menu Framework render
		// callback. Queue the complete refresh, including engine setting
		// reads, game-object reads, and FOV writes, to the game thread.
		const auto* taskInterface = F4SE::GetTaskInterface();
		if (!taskInterface) {
			logger::error("[WeaponFOV] Cannot refresh defaults: F4SE task interface unavailable");
			return;
		}

		taskInterface->AddTask([this]() {
			DetectAndLoadDefaultFOV();
			logger::info("[WeaponFOV] Defaults refreshed. VM={:.2f} ({}), 1P camera={:.2f} ({}), 3P={:.2f}.",
				defaultViewmodelFOV.load(), SourceName(defaultSource.load()),
				cameraFOV.load(), SourceName(cameraSource.load()),
				thirdPersonFOV.load());

			// Feature disabled: re-reading the INI defaults above is harmless
			// and keeps the menu display fresh, but the plugin must not issue
			// any `fov` command. bEnabled=0 means FOV-inert on EVERY path —
			// a save load or FOV Slider refresh must not clobber a FOV the
			// user set themselves (AE bug report 2026-07-28: console
			// `fov 100 100` was reset on load/VATS/dialogue with bEnabled=0).
			if (!Settings::GetSingleton()->wbfovEnabled) {
				logger::info("[WeaponFOV] WBFOV disabled — skipping apply after refresh");
				return;
			}

			auto* player = RE::PlayerCharacter::GetSingleton();
			if (!player || !player->Get3D()) {
				// Player 3D is not ready yet. Force the next Update tick or
				// scheduled load retry to perform the apply.
				lastAppliedFOV = -1.0f;
				return;
			}

			std::string curEditorID;
			if (player->currentProcess && player->currentProcess->middleHigh) {
				auto* mh = player->currentProcess->middleHigh;
				RE::BSAutoLock lock{ mh->equippedItemsLock };
				for (auto& equipped : mh->equippedItems) {
					auto* form = equipped.item.object;
					if (!form || form->formType != RE::ENUM_FORM_ID::kWEAP) continue;
					const char* eid = form->GetFormEditorID();
					if (eid && eid[0]) curEditorID = eid;
					break;
				}
			}

			// Ownership model: WBFOV only owns the FOV while the equipped
			// weapon has a per-weapon entry. With no entry we must not
			// write ANYTHING — `fov X Y` clobbers the runtime camera FOV
			// to X as an engine side effect, and users without FOV Slider
			// F4SE have no drift watcher to undo it. This was the AE bug
			// report: no entries configured, console `fov 100 100`, and
			// every load applied the hardcoded default and reset it.
			float targetFOV       = 0.0f;
			bool  haveWeaponEntry = false;
			if (!curEditorID.empty()) {
				std::lock_guard lock(entriesMtx);
				auto it = entries.find(curEditorID);
				if (it != entries.end()) {
					targetFOV       = it->second.viewmodelFOV;
					haveWeaponEntry = true;
				}
			}

			if (!haveWeaponEntry) {
				// Hand FOV back to the engine/user. Reset ownership state
				// so a stale pre-load `hadWeaponEntry` can't fire a
				// spurious entryLost apply on the first Update tick.
				lastAppliedFOV = -1.0f;
				hadWeaponEntry = false;
				logger::info("[WeaponFOV] Refresh: no weapon entry (weapon='{}') — leaving FOV untouched",
					curEditorID.empty() ? "<none>" : curEditorID.c_str());
				return;
			}

			ApplyViewmodelFOV(targetFOV);
			lastAppliedFOV = targetFOV;
			lastApplyTime = std::chrono::steady_clock::now();
			logger::info("[WeaponFOV] Main-thread apply after refresh: vm={:.1f}", targetFOV);
		});
	}

	void Manager::DetectAndLoadDefaultFOV()
	{
		// ---- Viewmodel FOV (default when no weapon entry matches) ----
		{
			float            v   = kHardcodedDefaultFOV;
			FOVDefaultSource src = FOVDefaultSource::HardcodedDefault;

			// Priority order:
			//   1. FOV Slider F4SE  (native F4SE plugin replacement; if
			//      installed, it owns the engine's INI settings live)
			//   2. Simple FOV Slider (Papyrus / MCM, legacy)
			//   3. FOV Slider and Player Height (Papyrus / MCM, legacy)
			//   4. Engine fDefault1stPersonFOV:Display — with no slider mod
			//      installed, vanilla couples the viewmodel to this setting
			//      (the engine copies it into PlayerCamera::firstPersonFOV
			//      on camera transitions). A user who ran `fov 100 100` has
			//      100 here; reverting to that IS vanilla behavior for them,
			//      whereas the old hardcoded 80 stomped their choice.
			//   5. Hardcoded 80
			if (TryReadFOVFromINI(kFOVSliderF4SE_INI, kFOVSliderF4SE_Section, kFOVSliderF4SE_VMKey, v))
			{
				src = FOVDefaultSource::FOVSliderF4SE;
			}
			else if (TryReadFOVFromINI(kSimpleFOV_UserINI, kSimpleFOV_Section, kSimpleFOV_VMKey, v) ||
			         TryReadFOVFromINI(kSimpleFOV_DefaultINI, kSimpleFOV_Section, kSimpleFOV_VMKey, v))
			{
				src = FOVDefaultSource::SimpleFOVSlider;
			}
			else if (TryReadFOVFromINI(kFOVSlider_UserINI, kFOVSlider_Section, kFOVSlider_VMKey, v) ||
			         TryReadFOVFromINI(kFOVSlider_DefaultINI, kFOVSlider_Section, kFOVSlider_VMKey, v))
			{
				src = FOVDefaultSource::FOVSlider;
			}
			else if (TryReadFloatFromGameINI("fDefault1stPersonFOV:Display", v))
			{
				src = FOVDefaultSource::UserINI;
			}

			// Sanity clamp
			if (v < 30.0f || v > 160.0f) v = kHardcodedDefaultFOV;
			defaultViewmodelFOV.store(v);
			defaultSource = src;
		}

		// ---- Camera FOV (for the X arg of `fov X Y`) ----
		// Per the user's directive: NEVER read this from the live PlayerCamera
		// state (other code/mods may have moved it). Always resolve from the
		// FOV slider mod INIs first, then the user's Fallout4Prefs.ini.
		{
			float            v   = kHardcodedCameraFOV;
			FOVDefaultSource src = FOVDefaultSource::HardcodedDefault;

			if (TryReadFOVFromINI(kFOVSliderF4SE_INI, kFOVSliderF4SE_Section, kFOVSliderF4SE_FirstPersonKey, v))
			{
				src = FOVDefaultSource::FOVSliderF4SE;
			}
			else if (TryReadFOVFromINI(kSimpleFOV_UserINI, kSimpleFOV_Section, kSimpleFOV_FirstPersonKey, v) ||
			         TryReadFOVFromINI(kSimpleFOV_DefaultINI, kSimpleFOV_Section, kSimpleFOV_FirstPersonKey, v))
			{
				src = FOVDefaultSource::SimpleFOVSlider;
			}
			else if (TryReadFOVFromINI(kFOVSlider_UserINI, kFOVSlider_Section, kFOVSlider_FirstPersonKey, v) ||
			         TryReadFOVFromINI(kFOVSlider_DefaultINI, kFOVSlider_Section, kFOVSlider_FirstPersonKey, v))
			{
				src = FOVDefaultSource::FOVSlider;
			}
			else if (TryReadFloatFromGameINI("fDefault1stPersonFOV:Display", v))
			{
				src = FOVDefaultSource::UserINI;
			}

			if (v < 30.0f || v > 160.0f) v = kHardcodedCameraFOV;
			cameraFOV.store(v);
			cameraSource = src;
		}

		// ---- 3rd-person FOV (to fix `fov X Y` side-effect) ----
		{
			float v = kHardcodedThirdPersonFOV;
			if (TryReadFOVFromINI(kFOVSliderF4SE_INI, kFOVSliderF4SE_Section, kFOVSliderF4SE_ThirdPersonKey, v))
			{ /* best source */ }
			else if (TryReadFOVFromINI(kSimpleFOV_UserINI, kSimpleFOV_Section, kSimpleFOV_ThirdPersonKey, v) ||
			         TryReadFOVFromINI(kSimpleFOV_DefaultINI, kSimpleFOV_Section, kSimpleFOV_ThirdPersonKey, v))
			{ /* legacy */ }
			else if (TryReadFOVFromINI(kFOVSlider_UserINI, kFOVSlider_Section, kFOVSlider_ThirdPersonKey, v) ||
			         TryReadFOVFromINI(kFOVSlider_DefaultINI, kFOVSlider_Section, kFOVSlider_ThirdPersonKey, v))
			{ /* legacy */ }
			else if (TryReadFloatFromGameINI("fDefaultWorldFOV:Display", v))
			{ /* user INI */ }

			if (v < 30.0f || v > 160.0f) v = kHardcodedThirdPersonFOV;
			thirdPersonFOV.store(v);
		}
	}

	void Manager::RefreshLiveCameraFOV()
	{
		// FOV slider mods (Simple FOV Slider, FOV Slider and Player Height)
		// both write the user's live first-person camera FOV to
		// `fDefault1stPersonFOV:Display` via Papyrus `SetINIFloat`, which
		// updates the engine's in-memory `INIPrefSettingCollection`. Reading
		// from that collection therefore always gives us whatever value the
		// FOV slider mod most recently set — no disk INI IO required, no
		// stale-cache problem.
		//
		// Without this refresh, our 1.5s periodic re-apply would feed a
		// startup-cached Y into `fov X Y`, briefly snapping the camera away
		// from the user's actual current setting (the "jumping around"
		// behavior reported by users running FOV slider mods alongside).
		float v;
		if (TryReadFloatFromGameINI("fDefault1stPersonFOV:Display", v)) {
			if (v >= 30.0f && v <= 160.0f) {
				cameraFOV.store(v);
			}
		}
		// If the engine settings collection isn't ready yet (very early
		// boot), keep the previously cached value — it was sourced from the
		// FOV slider mod INIs at Init().

		// Note: we intentionally do NOT live-refresh thirdPersonFOV from the
		// engine's fDefaultWorldFOV:Display here. Our own `fov X Y` clobbers
		// that setting to the viewmodel value every time we apply, so reading
		// it back would just give us our own clobbered value. The 3rd-person
		// FOV is sourced exclusively from FOV mod INIs at Init/RefreshDefaults.
	}

	static const char* SourceName(FOVDefaultSource src)
	{
		switch (src) {
		case FOVDefaultSource::FOVSliderF4SE:   return "FOV Slider F4SE";
		case FOVDefaultSource::SimpleFOVSlider: return "Simple FOV Slider";
		case FOVDefaultSource::FOVSlider:       return "FOV Slider and Player Height";
		case FOVDefaultSource::UserINI:         return "User INI (Fallout4Prefs)";
		default:                                return "Hardcoded default";
		}
	}

	const char* Manager::GetDefaultSourceName() const
	{
		return SourceName(defaultSource.load());
	}

	// ============================================================
	// External FOV change adoption
	// ------------------------------------------------------------
	// The user can change their camera FOV at any time with the console
	// (`fov X Y` writes Y into fDefault1stPersonFOV:Display and updates
	// fDefaultWorldFOV:Display). Our cameraFOV/thirdPersonFOV caches are
	// resolved at load, so without this the next timedReapply would pass
	// the STALE cached Y and erase the user's change within 1.5 s
	// (bug: "weapon fov was set accordingly but it was not respecting
	// my camera fov").
	//
	// Detection: ApplyViewmodelFOV always writes our caches back into
	// these INI settings, so an engine value differing from the cache
	// can only have come from outside (console, another mod).
	//
	// Debounce: the engine transiently resets fDefault1stPersonFOV
	// around load screens (the feedback loop the old "never re-read
	// per-frame" comment feared). A candidate must stay stable for
	// kAdoptDebounceSec before adoption. While a candidate is pending
	// the caller must NOT apply — one apply would overwrite the user's
	// value with the stale cache and destroy the evidence.
	// ============================================================
	bool Manager::AdoptExternalFOVChanges(bool a_force)
	{
		using clock = std::chrono::steady_clock;
		constexpr double kPollSec         = 0.4;
		constexpr double kAdoptDebounceSec = 0.9;
		constexpr float  kEps             = 0.05f;

		const auto now = clock::now();
		const bool anyPending = pendingExternalCam >= 0.0f || pendingExternalWorld >= 0.0f;
		if (!a_force &&
			std::chrono::duration<double>(now - lastExternalPollTime).count() < kPollSec) {
			return anyPending;
		}
		lastExternalPollTime = now;

		const auto track = [&](const char* a_key, std::atomic<float>& a_cache,
		                       float& a_pending, clock::time_point& a_since,
		                       const char* a_what) {
			float v = 0.0f;
			if (!TryReadFloatFromGameINI(a_key, v) || v < 30.0f || v > 160.0f) {
				a_pending = -1.0f;
				return;
			}
			if (std::fabs(v - a_cache.load()) <= kEps) {
				// Engine agrees with our cache — nothing external happened
				// (or a previously pending transient resolved back).
				a_pending = -1.0f;
				return;
			}
			if (a_pending >= 0.0f && std::fabs(v - a_pending) <= kEps) {
				if (std::chrono::duration<double>(now - a_since).count() >= kAdoptDebounceSec) {
					logger::info("[WeaponFOV] Adopting external {} FOV change {:.1f} -> {:.1f} (console or other mod)",
						a_what, a_cache.load(), v);
					a_cache.store(v);
					a_pending = -1.0f;
				}
			} else {
				// New candidate — start (or restart) the stability clock.
				a_pending = v;
				a_since   = now;
			}
		};

		track("fDefault1stPersonFOV:Display", cameraFOV,      pendingExternalCam,   pendingExternalCamSince,   "camera");
		track("fDefaultWorldFOV:Display",     thirdPersonFOV, pendingExternalWorld, pendingExternalWorldSince, "3rd-person");

		return pendingExternalCam >= 0.0f || pendingExternalWorld >= 0.0f;
	}

	// ============================================================
	// Per-frame update
	// ============================================================
	void Manager::SetExternalOverride(bool a_locked)
	{
		const bool prev = externalOverride.exchange(a_locked);
		if (prev != a_locked) {
			logger::info("[WeaponFOV] External override {} (FOV Slider F4SE handshake)",
				a_locked ? "LOCKED — pausing WBFOV applies" : "UNLOCKED — resuming WBFOV applies");
			if (!a_locked) {
				lastAppliedFOV = -1.0f;
				hadWeaponEntry = false;
			}
		}
	}

	void Manager::Update(RE::PlayerCharacter* player, bool weaponDrawn)
	{
		// Belt-and-suspenders enabled gate. The per-frame caller already
		// checks wbfovEnabled, but the flag was historically checked in one
		// caller and forgotten in others (load refresh, load retry, FSRF) —
		// centralizing the invariant here makes that regression impossible.
		if (!Settings::GetSingleton()->wbfovEnabled) {
			return;
		}

		// FOV Slider F4SE has the conch — don't touch the viewmodel
		// while it's running a Pip-Boy / Terminal / Aiming override.
		if (externalOverride.load()) {
			return;
		}

		// Pick up console / external camera FOV changes BEFORE anything
		// else. While a change is being debounced, do nothing this tick:
		// an apply now would feed the stale cached Y into `fov X Y` and
		// erase the user's edit before adoption confirms it. Placed ahead
		// of the camera-state guard so wasCameraBlocked isn't consumed
		// (the resume-from-block transition survives the pending window).
		if (AdoptExternalFOVChanges()) {
			return;
		}

		// Camera-state guard: `fov X Y` has a side-effect of setting 3rd-
		// person FOV to X. In first-person you never see it; the moment the
		// camera is anything else (furniture/workbench, VATS, free-cam,
		// manual 3rd-person toggle, etc.) the user DOES see the clobber.
		// Skip the apply entirely when outside first-person / iron-sights.
		auto* camera = RE::PlayerCamera::GetSingleton();
		if (camera && camera->currentState) {
			const auto state = camera->currentState->id.get();
			if (state != RE::CameraStates::kFirstPerson &&
			    state != RE::CameraStates::kIronSights)
			{
				if (!wasCameraBlocked) {
					logger::info("[WeaponFOV] Camera state {} — blocking WBFOV applies (furniture/3rd-person/VATS)",
						static_cast<unsigned>(state));
				}
				wasCameraBlocked = true;
				return;
			}
		}
		const bool resumingFromBlock = wasCameraBlocked;
		wasCameraBlocked = false;
		if (resumingFromBlock) {
			logger::info("[WeaponFOV] Camera returned to first-person — resuming WBFOV applies");
		}

		// Camera/3rd-person FOV baselines come from the FOV mod INIs,
		// resolved at Init() and RefreshDefaults() (called on game load and
		// when FOV Slider F4SE sends an FSRF refresh message). On top of
		// that, AdoptExternalFOVChanges() (called above) folds in changes
		// made mid-session via the console or other mods. Raw per-frame
		// re-reads of fDefault1stPersonFOV:Display would be unreliable
		// (engine post-load re-init transiently resets it, and our own
		// writes echo back), which is why adoption is debounced instead.

		// Read FormID and editorID of currently equipped weapon (if any).
		// Use equipped-items (inventory data) rather than weaponDrawn so
		// we see the weapon the instant it enters the equip slot — well
		// before the engine reports it as "drawn" (which only happens
		// once the equip animation finishes). This eliminates the visible
		// FOV pop mid-equip-animation when exiting Pip-Boy.
		std::uint32_t curFormID = 0;
		std::string   curEditorID;

		if (player && player->currentProcess && player->currentProcess->middleHigh) {
			auto* mh = player->currentProcess->middleHigh;
			RE::BSAutoLock lock{ mh->equippedItemsLock };
			for (auto& equipped : mh->equippedItems) {
				auto* form = equipped.item.object;
				if (!form) continue;
				if (form->formType != RE::ENUM_FORM_ID::kWEAP) continue;
				curFormID = form->GetFormID();
				const char* eid = form->GetFormEditorID();
				if (eid && eid[0]) curEditorID = eid;
				break;
			}
		}

		// Determine whether the weapon is visually present. We consider
		// it present if EITHER the engine says "drawn" OR we still have
		// a weapon form in the equip slot (covers the equip-animation
		// window where GetWeaponMagicDrawn() hasn't flipped yet).
		const bool weaponPresent = weaponDrawn || (curFormID != 0);

		// Look up weapon-specific FOV entry
		float entryFOV = 0.0f;
		bool  haveWeaponEntry = false;
		if (weaponPresent && !curEditorID.empty()) {
			std::lock_guard lock(entriesMtx);
			auto it = entries.find(curEditorID);
			if (it != entries.end()) {
				entryFOV = it->second.viewmodelFOV;
				haveWeaponEntry = true;
			}
		}

		// Detect transitions
		const bool weaponChanged = (curFormID != prevWeaponFormID);
		const bool drawChanged   = (weaponPresent != prevWeaponDrawn);
		const bool entryGained   = (haveWeaponEntry && !hadWeaponEntry);
		const bool entryLost     = (!haveWeaponEntry && hadWeaponEntry);

		// Ownership model (FOV Slider F4SE handshake):
		// ----------------------------------------------------------
		//   - WHEN AN ENTRY EXISTS: WBFOV owns the viewmodel FOV. We
		//     apply periodically (1.5s) so other mods that re-issue
		//     `fov` on menu close can't permanently undo us.
		//   - WHEN NO ENTRY EXISTS: we hand the viewmodel FOV back to
		//     FOV Slider F4SE entirely. We do ONE re-apply on the
		//     entry-lost transition (so dropping a per-weapon override
		//     instantly reverts to the default) and then stop touching
		//     it.
		// ----------------------------------------------------------

		const auto   now       = std::chrono::steady_clock::now();
		const double sinceLast = std::chrono::duration<double>(now - lastApplyTime).count();

		bool  needApply = false;
		float targetFOV = 0.0f;

		if (haveWeaponEntry) {
			targetFOV = entryFOV;
			const bool fovDrifted    = std::fabs(targetFOV - lastAppliedFOV) > 0.001f;
			const bool timedReapply  = sinceLast > 1.5;
			needApply = weaponChanged || drawChanged || entryGained ||
			            fovDrifted    || timedReapply ||
			            resumingFromBlock ||
			            lastAppliedFOV < 0.0f;
		} else if (entryLost) {
			// One-shot revert to default when a per-weapon override goes
			// away, then hands off. Deliberately NOT `|| resumingFromBlock`:
			// with no entry we own nothing, and applying the default after
			// every VATS/dialogue/furniture exit clobbered the runtime
			// camera FOV (the `fov X Y` side effect) for users who never
			// configured an entry at all (AE bug report 2026-07-28).
			targetFOV = defaultViewmodelFOV.load();
			needApply = true;
		}

		// Final race guard: force a fresh INI poll right before writing.
		// A console `fov` typed between the throttled polls above would
		// otherwise be erased by this apply (our Y re-asserts the stale
		// cache into the INI, destroying the evidence). If the fresh poll
		// finds a candidate, bail without touching transition state so
		// this same apply re-fires once adoption resolves (~1 s).
		if (needApply && AdoptExternalFOVChanges(true)) {
			return;
		}

		if (needApply) {
			auto*       infoLogger = spdlog::default_logger().get();
			const bool log_info    = infoLogger && infoLogger->should_log(spdlog::level::info);

			std::string reason;
			if (log_info) {
				reason.reserve(96);
				if (resumingFromBlock) reason += "resumeFromBlock ";
				if (weaponChanged) reason += "weaponChanged ";
				if (drawChanged) reason += "drawChanged ";
				if (entryGained) reason += "entryGained ";
				if (entryLost) reason += "entryLost ";
				if (haveWeaponEntry) {
					if (std::fabs(targetFOV - lastAppliedFOV) > 0.001f) reason += "fovDrifted ";
					if (sinceLast > 1.5) reason += "timedReapply ";
					if (lastAppliedFOV < 0) reason += "firstApply ";
				}
			}

			if (resumingFromBlock && haveWeaponEntry) {
				const float fromFOV = defaultViewmodelFOV.load();
				if (log_info) {
					logger::info("[WeaponFOV] LERP vm {:.1f}->{:.1f} (weapon='{}' cam={:.1f} 3p={:.1f}) reason: {}",
						fromFOV, targetFOV,
						curEditorID.empty() ? "<none>" : curEditorID.c_str(),
						cameraFOV.load(), thirdPersonFOV.load(), reason);
				}
				InterpolateViewmodelFOV(fromFOV, targetFOV, 12);
			} else {
				if (log_info) {
					logger::info("[WeaponFOV] APPLY vm={:.1f} (weapon='{}' cam={:.1f} 3p={:.1f} prev={:.1f}) reason: {}",
						targetFOV,
						curEditorID.empty() ? "<none>" : curEditorID.c_str(),
						cameraFOV.load(), thirdPersonFOV.load(),
						lastAppliedFOV, reason);
				}
				ApplyViewmodelFOV(targetFOV);
			}
			lastAppliedFOV = targetFOV;
			lastApplyTime  = now;
		}

		// Log weapon/entry transitions even when no apply was needed
		if ((weaponChanged || drawChanged || entryGained || entryLost) && !needApply) {
			auto* infoLogger = spdlog::default_logger().get();
			if (infoLogger && infoLogger->should_log(spdlog::level::info)) {
				logger::info("[WeaponFOV] Transition (no apply): weapon='{}' drawn={} present={} entry={} lastVM={:.1f}",
					curEditorID.empty() ? "<none>" : curEditorID.c_str(),
					weaponDrawn ? "yes" : "no",
					weaponPresent ? "yes" : "no",
					haveWeaponEntry ? "yes" : "no",
					lastAppliedFOV);
			}
		}

		hadWeaponEntry   = haveWeaponEntry;
		prevWeaponFormID = curFormID;
		prevWeaponDrawn  = weaponPresent;
	}

	void Manager::RestoreDefault()
	{
		const float def = defaultViewmodelFOV.load();
		logger::info("[WeaponFOV] RestoreDefault — WBFOV disabled, reverting vm to {:.1f} (source: {})",
			def, SourceName(defaultSource.load()));

		const auto* taskInterface = F4SE::GetTaskInterface();
		if (!taskInterface) {
			logger::error("[WeaponFOV] Cannot restore default: F4SE task interface unavailable");
			return;
		}

		taskInterface->AddTask([this, def]() {
			// Nothing applied this session -> nothing to restore. Issuing
			// `fov X Y` anyway would clobber a FOV the user set themselves
			// (checked on the game thread, where lastAppliedFOV is owned).
			if (lastAppliedFOV < 0.0f) {
				logger::info("[WeaponFOV] RestoreDefault skipped — no FOV was applied this session");
				return;
			}
			ApplyViewmodelFOV(def);
			lastAppliedFOV = -1.0f;
			lastApplyTime = std::chrono::steady_clock::now();
			hadWeaponEntry = false;
		});
	}

	void Manager::ScheduleLoadRetry()
	{
		// Feature disabled: no load-time applies at all. The retry loop's
		// entire purpose is to force WBFOV's value onto a fresh save; when
		// bEnabled=0 the plugin must stay FOV-inert (see RefreshDefaults).
		if (!Settings::GetSingleton()->wbfovEnabled) {
			logger::info("[WeaponFOV] WBFOV disabled — skipping load retries");
			return;
		}

		const std::uint32_t gen = ++loadRetryGeneration;
		loadRetryCompletedGeneration.store(0);

		int maxAttempts = Settings::GetSingleton()->wbfovLoadRetries;
		if (maxAttempts < 1) maxAttempts = 1;
		if (maxAttempts > 5) maxAttempts = 5;

		const auto* taskInterface = F4SE::GetTaskInterface();
		if (!taskInterface) {
			logger::error("[WeaponFOV] Cannot schedule load retries: F4SE task interface unavailable");
			return;
		}

		std::thread([this, taskInterface, gen, maxAttempts]() {
			// Increasing delays: try quickly, then back off. Stop as soon
			// as one main-thread attempt succeeds so we don't cause repeated
			// pops. The timer thread never reads or writes live game state.
			constexpr std::chrono::milliseconds kDelays[5] = {
				std::chrono::milliseconds(50),
				std::chrono::milliseconds(150),
				std::chrono::milliseconds(300),
				std::chrono::milliseconds(500),
				std::chrono::milliseconds(800),
			};

			for (int i = 0; i < maxAttempts; ++i) {
				if (loadRetryGeneration.load() != gen) return;
				std::this_thread::sleep_for(kDelays[i]);
				if (loadRetryGeneration.load() != gen) return;
				if (loadRetryCompletedGeneration.load() == gen) return;

				taskInterface->AddTask([this, gen, attempt = i + 1, maxAttempts]() {
					if (loadRetryGeneration.load() != gen ||
						loadRetryCompletedGeneration.load() == gen) {
						return;
					}
					// User may have disabled WBFOV between scheduling and
					// this attempt firing (menu toggle) — stay inert.
					if (!Settings::GetSingleton()->wbfovEnabled) {
						return;
					}

					auto* player = RE::PlayerCharacter::GetSingleton();
					if (!player || !player->Get3D()) return;

					auto* cam = RE::PlayerCamera::GetSingleton();
					if (cam && cam->currentState) {
						const auto state = cam->currentState->id.get();
						if (state != RE::CameraStates::kFirstPerson &&
							state != RE::CameraStates::kIronSights) {
							return;
						}
					}

					std::string curEditorID;
					if (player->currentProcess && player->currentProcess->middleHigh) {
						auto* mh = player->currentProcess->middleHigh;
						RE::BSAutoLock lock{ mh->equippedItemsLock };
						for (auto& equipped : mh->equippedItems) {
							auto* form = equipped.item.object;
							if (!form || form->formType != RE::ENUM_FORM_ID::kWEAP) continue;
							const char* eid = form->GetFormEditorID();
							if (eid && eid[0]) curEditorID = eid;
							break;
						}
					}

					// Same ownership rule as RefreshDefaults: only force an
					// apply when the equipped weapon has a per-weapon entry.
					// No entry -> complete the generation WITHOUT touching
					// FOV; if an entry weapon equips later, Update()'s
					// entryGained transition applies it naturally.
					float targetFOV       = 0.0f;
					bool  haveWeaponEntry = false;
					if (!curEditorID.empty()) {
						std::lock_guard lock(entriesMtx);
						auto it = entries.find(curEditorID);
						if (it != entries.end()) {
							targetFOV       = it->second.viewmodelFOV;
							haveWeaponEntry = true;
						}
					}

					if (!haveWeaponEntry) {
						loadRetryCompletedGeneration.store(gen);
						logger::info("[WeaponFOV] LoadRetry attempt {}/{}: no weapon entry (weapon='{}') — leaving FOV untouched",
							attempt, maxAttempts,
							curEditorID.empty() ? "<none>" : curEditorID.c_str());
						return;
					}

					logger::info("[WeaponFOV] LoadRetry attempt {}/{}: vm={:.1f} weapon='{}' cam={:.1f}",
						attempt, maxAttempts, targetFOV,
						curEditorID.empty() ? "<none>" : curEditorID.c_str(),
						cameraFOV.load());
					ApplyViewmodelFOV(targetFOV);
					lastAppliedFOV = targetFOV;
					lastApplyTime = std::chrono::steady_clock::now();
					loadRetryCompletedGeneration.store(gen);
					logger::info("[WeaponFOV] LoadRetry succeeded on attempt {} — stopping", attempt);
				});
			}
		}).detach();
	}

	void Manager::ApplyViewmodelFOV(float vmFov)
	{
		if (vmFov < 30.0f)  vmFov = 30.0f;
		if (vmFov > 160.0f) vmFov = 160.0f;

		// `fov X Y` sets the viewmodel projection to X and writes Y into
		// fDefault1stPersonFOV:Display (INI). However it also clobbers:
		//   PlayerCamera::firstPersonFOV = X  (runtime, renderer reads this)
		//   PlayerCamera::worldFOV       = X  (runtime, renderer reads this)
		// Y does NOT update the runtime fields — only the INI setting.
		// The renderer reads the runtime fields every frame, so without a
		// post-fix the user sees their world camera at the viewmodel value.
		const float camFov  = cameraFOV.load();
		const float tpFov   = thirdPersonFOV.load();

		const std::string cmd = std::format("fov {:.4f} {:.4f}", vmFov, camFov);
		ExecuteConsoleCommand(cmd);

		// Undo the runtime 3rd-person clobber immediately — worldFOV only
		// affects the 3rd-person camera, safe to write same-frame.
		if (auto* camera = RE::PlayerCamera::GetSingleton()) {
			camera->worldFOV = tpFov;
		}

		// firstPersonFOV is also clobbered to X (viewmodel value), but it
		// cannot be written back at any point — the engine uses this field
		// for the viewmodel projection every frame, so writing camFov here
		// would revert the viewmodel to the camera FOV. The camera-side
		// correction (firstPersonFOV drifting from vmFov back to camFov)
		// happens naturally via the engine reading fDefault1stPersonFOV:Display
		// on camera-mode transitions (FaderMenu close, furniture enter/exit).

		// Re-assert the INI-level settings so engine camera-mode
		// transitions (save/load, furniture enter/exit) read correct values.
		SetEngineFloatSetting("fDefaultWorldFOV:Display", tpFov);
		SetEngineFloatSetting("fDefault1stPersonFOV:Display", camFov);

		logger::trace("[WeaponFOV] post-fov fixup: worldFOV={:.2f} INI(1stP={:.2f} 3rdP={:.2f}) vm={:.2f}",
			tpFov, camFov, tpFov, vmFov);
	}

	void Manager::InterpolateViewmodelFOV(float from, float to, int frames)
	{
		if (frames <= 1 || std::fabs(to - from) < 0.05f) {
			ApplyViewmodelFOV(to);
			return;
		}

		const std::uint64_t myGen = ++interpGeneration;
		const auto* taskInterface = F4SE::GetTaskInterface();
		if (!taskInterface) {
			logger::error("[WeaponFOV] Cannot interpolate: F4SE task interface unavailable");
			ApplyViewmodelFOV(to);
			return;
		}

		std::thread([this, taskInterface, from, to, frames, myGen]() {
			const float delta = to - from;
			const float step  = delta / static_cast<float>(frames);
			float       cur   = from;

			for (int i = 0; i < frames; ++i) {
				if (interpGeneration.load() != myGen) {
					logger::info("[WeaponFOV] Lerp gen={} superseded at step {}/{}", myGen, i, frames);
					return;
				}
				cur += step;
				const float frameFOV = (i + 1 == frames) ? to : cur;
				taskInterface->AddTask([this, frameFOV, to, myGen, stepIndex = i + 1, frames]() {
					if (interpGeneration.load() != myGen) return;

					ApplyViewmodelFOV(frameFOV);
					if (stepIndex == frames) {
						logger::info("[WeaponFOV] Lerp complete: vm={:.1f} (gen={})", to, myGen);
					}
				});
				std::this_thread::sleep_for(std::chrono::milliseconds(8));
			}
		}).detach();
	}

	// ============================================================
	// Query / mutation
	// ============================================================
	bool Manager::HasEntry(const std::string& editorID) const
	{
		std::lock_guard lock(entriesMtx);
		return entries.find(editorID) != entries.end();
	}

	float Manager::GetEntryFOV(const std::string& editorID) const
	{
		std::lock_guard lock(entriesMtx);
		auto it = entries.find(editorID);
		return (it != entries.end()) ? it->second.viewmodelFOV : defaultViewmodelFOV.load();
	}

	// Returns the editorID of the currently equipped weapon, or empty string if none.
	static std::string GetEquippedEditorID()
	{
		auto* player = RE::PlayerCharacter::GetSingleton();
		if (!player || !player->currentProcess || !player->currentProcess->middleHigh)
			return {};
		auto* mh = player->currentProcess->middleHigh;
		RE::BSAutoLock lock{ mh->equippedItemsLock };
		for (auto& equipped : mh->equippedItems) {
			auto* form = equipped.item.object;
			if (!form || form->formType != RE::ENUM_FORM_ID::kWEAP) continue;
			const char* eid = form->GetFormEditorID();
			return (eid && eid[0]) ? std::string(eid) : std::string{};
		}
		return {};
	}

	void Manager::SetEntry(const std::string& editorID, const std::string& displayName, float fov)
	{
		if (editorID.empty()) return;
		logger::info("[WeaponFOV] SetEntry '{}' ('{}') vm={:.1f}", editorID, displayName, fov);
		{
			std::lock_guard lock(entriesMtx);
			auto& e         = entries[editorID];
			e.editorID      = editorID;
			e.displayName   = displayName;
			e.viewmodelFOV  = fov;
		}
		SaveEntry(editorID);

		// Menu callbacks can run on the render thread. Defer the equipped
		// weapon read and engine apply to F4SE's game-thread queue.
		const auto* taskInterface = F4SE::GetTaskInterface();
		if (!taskInterface) {
			logger::error("[WeaponFOV] Cannot apply entry '{}': F4SE task interface unavailable", editorID);
			return;
		}

		taskInterface->AddTask([this, editorID, fov]() {
			if (GetEquippedEditorID() == editorID) {
				logger::info("[WeaponFOV] SetEntry main-thread apply: vm={:.1f} (weapon='{}')", fov, editorID);
				ApplyViewmodelFOV(fov);
				lastAppliedFOV = fov;
				lastApplyTime = std::chrono::steady_clock::now();
			} else {
				lastAppliedFOV = -1.0f;
			}
		});
	}

	void Manager::RemoveEntry(const std::string& editorID)
	{
		logger::info("[WeaponFOV] RemoveEntry '{}' — reverting to default vm={:.1f}", editorID, defaultViewmodelFOV.load());
		{
			std::lock_guard lock(entriesMtx);
			entries.erase(editorID);
		}
		std::error_code ec;
		std::filesystem::remove(GetEntryPath(editorID), ec);

		const auto* taskInterface = F4SE::GetTaskInterface();
		if (!taskInterface) {
			logger::error("[WeaponFOV] Cannot apply removed entry '{}': F4SE task interface unavailable", editorID);
			return;
		}

		taskInterface->AddTask([this, editorID]() {
			if (GetEquippedEditorID() == editorID) {
				const float def = defaultViewmodelFOV.load();
				logger::info("[WeaponFOV] RemoveEntry main-thread revert: vm={:.1f} (weapon='{}')", def, editorID);
				ApplyViewmodelFOV(def);
				lastAppliedFOV = def;
				lastApplyTime = std::chrono::steady_clock::now();
			} else {
				lastAppliedFOV = -1.0f;
			}
		});
	}

	std::vector<WBFOVEntry> Manager::GetEntries() const
	{
		std::vector<WBFOVEntry> out;
		std::lock_guard lock(entriesMtx);
		out.reserve(entries.size());
		for (auto& kv : entries) out.push_back(kv.second);
		std::sort(out.begin(), out.end(), [](const WBFOVEntry& a, const WBFOVEntry& b) {
			return a.editorID < b.editorID;
		});
		return out;
	}

	// ============================================================
	// JSON persistence (one file per weapon)
	// ============================================================
	std::filesystem::path Manager::GetWBFOVFolder() const
	{
		return std::filesystem::current_path() / "Data" / "F4SE" / "Plugins" / "FPGunplayOverhaul" / "WBFOV";
	}

	std::filesystem::path Manager::GetEntryPath(const std::string& editorID) const
	{
		return GetWBFOVFolder() / (editorID + ".json");
	}

	void Manager::SaveEntry(const std::string& editorID)
	{
		WBFOVEntry copy;
		{
			std::lock_guard lock(entriesMtx);
			auto it = entries.find(editorID);
			if (it == entries.end()) return;
			copy = it->second;
		}

		auto folder = GetWBFOVFolder();
		std::error_code ec;
		std::filesystem::create_directories(folder, ec);

		nlohmann::json j;
		j["editorID"]     = copy.editorID;
		j["displayName"]  = copy.displayName;
		j["viewmodelFOV"] = copy.viewmodelFOV;

		try {
			std::ofstream ofs(GetEntryPath(editorID));
			ofs << j.dump(4);
			logger::info("[WeaponFOV] Saved {} -> {:.2f}", editorID, copy.viewmodelFOV);
		} catch (const std::exception& ex) {
			logger::error("[WeaponFOV] Failed to save '{}': {}", editorID, ex.what());
		}
	}

	void Manager::LoadAllEntries()
	{
		std::lock_guard lock(entriesMtx);
		entries.clear();

		auto folder = GetWBFOVFolder();
		std::error_code ec;
		if (!std::filesystem::exists(folder, ec)) return;

		for (auto const& it : std::filesystem::directory_iterator(folder, ec)) {
			if (!it.is_regular_file()) continue;
			if (it.path().extension() != ".json") continue;

			try {
				std::ifstream ifs(it.path());
				auto j = nlohmann::json::parse(ifs);

				WBFOVEntry e;
				e.editorID     = j.value("editorID", it.path().stem().string());
				e.displayName  = j.value("displayName", e.editorID);
				e.viewmodelFOV = j.value("viewmodelFOV", 80.0f);

				if (!e.editorID.empty()) {
					entries[e.editorID] = std::move(e);
				}
			} catch (const std::exception& ex) {
				logger::error("[WeaponFOV] Failed to parse {}: {}", it.path().string(), ex.what());
			}
		}
	}
}
