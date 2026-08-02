#pragma once

#include <atomic>

namespace RE
{
	class PlayerCharacter;
	class BGSKeyword;
	class ActorValueInfo;
	class BGSAction;
}

namespace CrouchSlide
{
	// Raised by the F4SE Menu Framework hotkey callback (see Menu::Register)
	// when the Crouch Slide activation key is pressed. Consumed once per frame
	// by Manager::Update on the game thread. Atomic because the framework
	// dispatches hotkeys from the game window's WndProc / XInput poll.
	inline std::atomic<bool> g_hotkeyPressed{ false };

	// ============================================================
	// Crouch Slide manager
	// ------------------------------------------------------------
	// Owns the slide state machine, the runtime OAR keyword, the sound
	// handle, and the temporary actor-value / actor-flag modifications used
	// while a slide is active. Driven every frame from InertiaManager::Update
	// (the PlayerCharacter::UpdateAnimation vfunc hook) which already resolves
	// the sprint / airborne / landing state we need.
	// ============================================================
	class Manager
	{
	public:
		static Manager* GetSingleton()
		{
			static Manager singleton;
			return &singleton;
		}

		// Resolve AVIF pointers, create/reuse the runtime keyword, and install
		// the SneakHandler input hook. Safe to call once at kGameDataReady.
		void Init();

		// Per-frame state handed down from InertiaManager::Update. All values
		// are computed on the game thread by the caller, so nothing here needs
		// to re-read engine state that the caller already resolved.
		struct FrameState
		{
			RE::PlayerCharacter* player{ nullptr };
			float delta{ 0.0f };            // game-time delta (seconds)
			bool  sprinting{ false };       // moveMode & 0x100 this frame
			bool  confirmedLanding{ false };// filtered landing edge (real jump/fall -> ground)
			bool  inAir{ false };           // airborne this frame
			bool  firstPerson{ false };     // camera is in first person
			bool  sneaking{ false };        // in the crouch/sneak pose (anim-event latched)
		};

		// Advance the slide state machine and apply/clear all borrowed state.
		void Update(const FrameState& a_fs);

		// Cancel any active slide and restore every borrowed value. Called from
		// InertiaManager::Reset (cell change, save load, first-person exit).
		void Reset();

		// True while a slide is actively driving the player (for menu status).
		bool IsSliding() const { return m_state == State::kSliding; }

	private:
		Manager() = default;
		~Manager() = default;
		Manager(const Manager&) = delete;
		Manager(Manager&&) = delete;
		Manager& operator=(const Manager&) = delete;
		Manager& operator=(Manager&&) = delete;

		enum class State
		{
			kIdle,          // not sliding
			kPendingCrouch, // trigger accepted; waiting for the crouch pose before driving velocity
			kSliding,       // committed to a slide, driving velocity
			kRampUp         // slide finished, easing SpeedMult back to crouch-walk
		};

		// Arm a slide: commit AP, crouch the player, and enter the
		// pending-crouch wait. Velocity does not start until the player is
		// actually crouched (so it reads as a slide, not a standing lunge).
		// a_crouchNow: issue a synthetic Sneak toggle immediately (hotkey /
		// landing). False for the crouch-key path, whose real press the engine
		// is already processing this frame (a synthetic toggle on top would
		// cancel it out).
		void StartSlide(RE::PlayerCharacter* a_player, bool a_crouchNow, const char* a_reason);
		// Actually begin driving the slide once crouched: capture heading, add
		// the OAR keyword, fire gun-down, play the sound, and start velocity.
		void BeginSlideMotion(RE::PlayerCharacter* a_player);
		// Finish a slide normally (duration elapsed). Enters kRampUp if the
		// player is still crouching, otherwise returns straight to idle.
		void EndSlide(RE::PlayerCharacter* a_player);
		// Abort a slide (jump, ragdoll, feature disabled, etc.) with full cleanup.
		void CancelSlide(RE::PlayerCharacter* a_player, const char* a_reason);
		// Remove the OAR keyword + i-frames + any live velocity ownership.
		// a_releaseForceSneak: cancels release the pin immediately; a normal
		// EndSlide passes false and defers the release to the aligned-state
		// handler in Update (see m_pinReleasePending).
		void ClearSlideEffects(RE::PlayerCharacter* a_player, bool a_releaseForceSneak = true);
		// Restore ActorState::forceSneak to its pre-slide value.
		void ReleaseForceSneakPin(RE::PlayerCharacter* a_player);

		// One-shot slide sound (best-effort; failure is logged, slide continues).
		void PlaySound(RE::PlayerCharacter* a_player);
		void StopSound();

		// Toggle i-frames (invulnerable actor-base flag) with save/restore.
		void SetIFrames(RE::PlayerCharacter* a_player, bool a_on);

		// Send a synthetic Sneak press so the hotkey / landing paths end the
		// player crouched (the crouch-key path is handled by the engine itself).
		void ForceCrouch(RE::PlayerCharacter* a_player);

		// ---- cached engine handles (resolved in Init) ----
		RE::BGSKeyword*     m_keyword{ nullptr };        // "AnimsCrouchSlideKeyword"
		RE::ActorValueInfo* m_avSpeedMult{ nullptr };
		RE::ActorValueInfo* m_avActionPoints{ nullptr };
		RE::BGSAction*      m_gunDownAction{ nullptr };  // resolved lazily on first slide

		// ---- slide state ----
		State m_state{ State::kIdle };
		bool  m_prevSprinting{ false }; // sprint bit last frame (for the hotkey fallback)
		float m_elapsed{ 0.0f };        // time in the current slide
		float m_duration{ 1.6f };       // captured from settings at start
		float m_peakSpeed{ 0.0f };      // units/s peak of the ease envelope
		float m_dirX{ 0.0f };           // world slide direction (unit vector)
		float m_dirY{ 1.0f };
		float m_initialHeading{ 0.0f }; // slide heading at start (radians)
		float m_startYaw{ 0.0f };       // camera yaw at start (radians)
		float m_maxSteer{ 0.0f };       // captured steer clamp (radians)
		bool  m_keywordAdded{ false };
		bool  m_iframesActive{ false };
		bool  m_iframesWerePrev{ false }; // prior invulnerable flag state (to restore)
		float m_iframesRemaining{ 0.0f };

		// Throttle for the per-frame velocity-drive diagnostics (seconds).
		float m_diagTimer{ 0.0f };

		// Time spent waiting for the crouch pose after a trigger (seconds).
		// Velocity starts when the player is crouched or this passes a short
		// timeout (whichever comes first).
		float m_crouchWaitTime{ 0.0f };

		// The ActorState::forceSneak pin (the engine bit behind the
		// SetForceSneak console/script function) is the SOLE in-slide crouch
		// enforcement: pinned at StartSlide (so the pending wait is covered
		// too), it holds the pose against ANY input scheme - toggle sneak,
		// hold-to-crouch mods releasing on key-up, jump-land recovery.
		// Deliberately NO synthetic-toggle re-issues while pinned: each press
		// takes several frames to reflect in IsSneaking(), so a retry loop
		// keyed on that bool double-fires and visibly bobs the player up and
		// down mid-slide (observed in-game). Previous value saved so we never
		// clobber a force-sneak someone else (e.g. a quest) set.
		bool          m_forceSneakPinned{ false };
		std::uint32_t m_forceSneakPrev{ 0 };

		// Deferred pin release after a normal slide end. The pose is held by
		// forceSneak, but the persistent sneak TOGGLE state may still read
		// standing (e.g. a hotkey/landing slide where the initial press was
		// swallowed). Releasing the pin in that state would stand the player
		// up, so EndSlide sends one aligning synthetic press if needed and the
		// pin is released from Update once IsSneaking() reads true (or a short
		// timeout passes). Cancels release immediately instead - standing is
		// the correct outcome for an aborted slide.
		bool  m_pinReleasePending{ false };
		float m_pinReleaseTimer{ 0.0f };

		// Gun-down (weapon-lowered) visual. PerformAction is refused while the
		// animation graph is mid-transition (the sprint -> crouch change at
		// slide start is exactly such a moment), so we retry across the first
		// part of the slide until it takes. See StartSlide / Update.
		bool  m_gunDownApplied{ false };
		float m_gunDownRetryTime{ 0.0f };

		// ---- ramp-up (post-slide crouch-walk ease) ----
		float m_rampElapsed{ 0.0f };
		float m_rampDuration{ 0.0f };
		float m_rampInitialReduction{ 0.0f }; // SpeedMult reduction applied at ramp start (negative)
		float m_rampSpeedDelta{ 0.0f };       // currently-applied SpeedMult reduction (negative)

		// The live BSSoundHandle is a file-static in the cpp (single manager,
		// one slide at a time) so this header stays free of engine audio
		// includes.
	};
}
