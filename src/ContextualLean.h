#pragma once

// ============================================================
// Contextual Lean (Extras feature)
// ------------------------------------------------------------
// Automatically triggers UneducatedShooter's lean while the player
// is aiming down sights near cover. Forward raycasts (center + two
// side probes) detect an obstacle in front of the camera; when one
// side is open and the other blocked, the player is leaned toward
// the OPEN side so they can see around the obstacle.
//
// UneducatedShooter (US) remains the lean implementation — this
// module only *drives* it by dispatching synthetic ButtonEvents for
// the user's own US lean keybinds through the input receiver US
// hooks (PlayerCamera's BSInputEventReceiver base). That way US's
// blending (fleanTimeCost), collision checks (no leaning through
// walls), and all of its user settings apply unchanged.
//
// The feature is automatically disabled when UneducatedShooter.dll
// is not loaded.
// ============================================================

namespace RE
{
	class PlayerCharacter;
	enum class INPUT_DEVICE : std::int32_t;
}

namespace ContextualLean
{
	class Manager
	{
	public:
		static Manager* GetSingleton()
		{
			static Manager singleton;
			return &singleton;
		}

		// kGameDataReady: detect UneducatedShooter.dll and read its
		// MCM settings + keybinds.
		void Init();

		// kPostLoadGame / kNewGame: re-read US config (mirrors US's own
		// reload points).
		void OnGameLoaded();

		// Per-frame update (main thread, called from InertiaManager::Update
		// in the EXTRAS section). Handles detection, the lean state
		// machine, and synthetic input injection.
		void Update(RE::PlayerCharacter* a_player, float a_delta);

		// Record the device of real button input (called from the
		// AttackInput hook). Used for the KB+M / gamepad enable options.
		void NotifyInputDevice(RE::INPUT_DEVICE a_device);

		// ---- Status queries (for the F4SE Menu Framework page) ----
		bool IsUSInstalled() const { return usInstalled; }
		bool IsUSLeanDisabled() const { return usDisableLean; }
		bool IsToggleMode() const { return usToggleLean; }
		bool IsUSADSOnly() const { return usADSOnly; }
		bool AreKeysUsable() const;
		std::uint32_t GetLeanLeftKey() const { return usLeanLeftKey; }
		std::uint32_t GetLeanRightKey() const { return usLeanRightKey; }
		// -1 = leaning right, 0 = idle, 1 = leaning left (US convention)
		int  GetCurrentLeanDir() const { return ourLeanDir; }
		bool IsLastInputGamepad() const { return lastInputWasGamepad; }

		// ---- Live debug view (read by the menu's Debug View panel) ----
		// Plain fields written on the game thread each Update and read by
		// the render thread for display only — benign data races, same
		// policy as InertiaManager's DebugSnapshot.
		struct DebugView
		{
			// Gating
			bool  featureAvailable{ false };
			bool  inFP{ false };
			bool  inADS{ false };
			bool  deviceAllowed{ false };
			bool  menuBlocked{ false };
			std::uint32_t gunState{ 0 };

			// Last engage evaluation (only meaningful while idle)
			bool  evalRan{ false };       // pattern evaluation ran this frame
			bool  pitchGated{ false };    // skipped because aiming steeply down
			float distC{ 0.0f }, distL{ 0.0f }, distR{ 0.0f };
			bool  hitC{ false }, hitL{ false }, hitR{ false };
			bool  blockedC{ false }, blockedL{ false }, blockedR{ false };
			// Scene-graph name of each hit object (empty when no hit) —
			// identifies invisible colliders behind mystery engages.
			char  hitNameC[48]{};
			char  hitNameL[48]{};
			char  hitNameR[48]{};
			bool  usedNarrow{ false };    // thin-obstacle narrow probes ran
			float distNL{ 0.0f }, distNR{ 0.0f };
			bool  narrowBlockedL{ false }, narrowBlockedR{ false };
			int   desired{ 0 };           // pattern verdict: 1 left, -1 right, 0 none
			float desiredHold{ 0.0f };    // stability timer

			// State machine
			float leanMagnitude{ 0.0f };  // live readback from US's camera bone
			float cooldown{ 0.0f };
			float backoff{ 0.0f };

			// While leaning
			float moveDist{ 0.0f };       // distance from engage anchor
			float yawDeltaDeg{ 0.0f };    // aim yaw change since engage
			bool  anchoredValid{ false }; // anchored cover rays still see the obstacle
			float minHold{ 0.0f };
			float patternFail{ 0.0f };

			// Last release reason (string literal, or nullptr when none yet)
			const char* lastReleaseReason{ nullptr };
		};
		const DebugView& GetDebugView() const { return dbg; }

	private:
		Manager() = default;
		~Manager() = default;
		Manager(const Manager&) = delete;
		Manager(Manager&&) = delete;
		Manager& operator=(const Manager&) = delete;
		Manager& operator=(Manager&&) = delete;

		// Read US's effective settings the same way US itself does
		// (Data\MCM\Config\...\settings.ini presence selects the
		// Data\MCM\Settings\UneducatedShooter.ini override file), plus
		// the lean keybinds from Data\MCM\Settings\Keybinds.json.
		void LoadUSConfig();

		// Throttled config re-read: every couple of seconds, stat the US
		// config files and reload when their write time changed (US applies
		// MCM changes on PauseMenu close — this catches the same edits).
		void MaybeReloadUSConfig(float a_delta);

		// ENGAGE evaluation (only run while NOT leaning, from the un-leaned
		// viewpoint): returns the lean direction the ray pattern justifies
		// (1 = left, -1 = right) or 0 when the situation is ambiguous.
		// Deliberately strict — see .cpp for the exact conditions.
		int EvaluateEngageDirection(RE::PlayerCharacter* a_player);

		// RELEASE evaluation (only run while leaning): re-checks the
		// obstacle from the engage-time ANCHOR (translated by the player's
		// movement since engage, which the lean itself barely affects).
		// Returns true while the cover evidence is still present.
		bool AnchoredPatternStillValid(RE::PlayerCharacter* a_player);

		// Live lean amount readback from the CameraInserted1st bone US
		// inserts: |sin(leanAngle)| normalized by sin(leanMax) -> 0..1.
		// Sign is NOT trusted (the bone's sign convention is unverified);
		// all uses are magnitude-only.
		float ReadLeanMagnitude(RE::PlayerCharacter* a_player) const;

		// Synthetic input: deterministically force US's lean state.
		// Both work from ANY current US state (see .cpp for the toggle-
		// mode key sequences that make this stateless).
		bool InjectForceDirection(int a_dir);
		bool InjectForceNone();

		// Disengage bookkeeping shared by all "stop leaning" paths.
		// a_inject = false when US is known to have cleared the lean
		// itself (e.g. bADSOnly on ADS exit).
		// a_patternFail = true only for "cover gone" releases: those get a
		// longer re-engage cooldown and count toward the rapid-cycle
		// guard. User-intent releases (ADS exit, moving/turning away) get
		// a minimal cooldown so deliberate re-triggering stays responsive.
		void Disengage(bool a_inject, bool a_patternFail = false);

		// ---- UneducatedShooter integration state ----
		bool usInstalled{ false };
		bool usConfigLoaded{ false };
		bool usDisableLean{ false };   // bleanDisable
		bool usToggleLean{ false };    // bToggleLean
		bool usADSOnly{ false };       // bADSOnly
		float usLeanTimeCost{ 1.0f };  // fleanTimeCost (lean blend seconds)
		float usLeanMax{ 15.0f };      // fleanMax (degrees, first person)
		std::uint32_t usLeanLeftKey{ 0xFFFF };   // unified id space (see .cpp)
		std::uint32_t usLeanRightKey{ 0xFFFF };
		float configRecheckTimer{ 0.0f };
		std::int64_t settingsFileTime{ 0 };   // last-write times for change detection
		std::int64_t keybindsFileTime{ 0 };

		// ---- Input device tracking ----
		bool lastInputWasGamepad{ false };

		// ---- Lean state machine ----
		// The lean is LATCHED: once engaged it never re-picks a direction.
		// It only ends via the release conditions (movement, turning,
		// cover gone, gating lost), after which a fresh engage evaluation
		// may start a new lean. This makes left/right ping-pong and
		// engage/release flicker structurally impossible.
		int   ourLeanDir{ 0 };            // 0 idle, 1 left, -1 right (what we told US)
		float desiredHoldTimer{ 0.0f };   // continuous time the engage direction has held
		int   lastDesired{ 0 };           // engage direction from the previous frame
		float patternFailTimer{ 0.0f };   // continuous time the anchored pattern has been gone
		float minHoldTimer{ 0.0f };       // time since engage (blocks early pattern-fail release)
		float cooldownTimer{ 0.0f };      // block re-engage right after a release
		float overrideCheckTimer{ 0.0f }; // after engage: verify the lean actually appeared
		float backoffTimer{ 0.0f };       // external cancel / rapid cycling -> stand down

		// ---- Engage-time anchor (valid while ourLeanDir != 0) ----
		RE::NiPoint3 anchorEyePos{};      // eye position at engage (world)
		RE::NiPoint3 anchorPlayerPos{};   // player data.location at engage
		float anchorYaw{ 0.0f };          // player aim yaw at engage (radians)

		// ---- Rapid-cycle guard ----
		// If engage -> cover-gone-release -> engage keeps cycling
		// back-to-back (geometry that defeats the hysteresis), stop trying
		// for a few seconds instead of strobing the lean. Releases the
		// player caused on purpose (ADS exit, moving/turning away) never
		// count toward this.
		float sinceLastEngage{ 999.0f };
		int   quickEngageCount{ 0 };
		bool  lastReleaseWasPatternFail{ false };

		// ---- Live debug view (display only) ----
		DebugView dbg{};
	};
}
