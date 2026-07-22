#pragma once

#include "Settings.h"
#include "InertiaPresets.h"

namespace RE
{
	struct BSAnimationGraphEvent
	{
		TESObjectREFR* refr;
		BSFixedString animEvent;
		BSFixedString argument;
	};

	namespace BGSAnimationSystemUtils
	{
		inline bool GetEventSourcePointersFromGraph(
			const TESObjectREFR* a_refr,
			BSScrapArray<BSTEventSource<BSAnimationGraphEvent>*>& a_sourcesOut)
		{
			using func_t = decltype(&GetEventSourcePointersFromGraph);
			REL::Relocation<func_t> func{ REL::ID(897074) };
			return func(a_refr, a_sourcesOut);
		}
	}
}

namespace Inertia
{
	// Ring buffer entry for impulse trigger events
	struct DebugEvent
	{
		float timestamp{ 0.0f };  // time since plugin started (seconds)
		char  description[64]{};
	};
	static constexpr int kMaxDebugEvents = 20;

	// Snapshot of InertiaManager runtime state for debug display (read-only, best-effort - no lock)
	struct DebugSnapshot
	{
		// Player / camera state
		bool inFirstPerson{ false };
		bool weaponDrawn{ false };
		bool isADS{ false };
		bool isScoped{ false };
		bool isFiring{ false };
		bool isReloading{ false };
		bool isInMeleeAction{ false };
		bool isInPowerArmor{ false };
		bool isSprinting{ false };
		bool isSuperSprinting{ false };
		bool isJumping{ false };
		bool isInAir{ false };
		bool isFalling{ false };

		// Havok character state (from bhkCharacterController)
		std::int32_t havokCharState{ -1 };  // hknpCharacterStateType enum value

		// Detected weapon
		WeaponType detectedWeaponType{ WeaponType::Rifle };
		std::string equippedEditorID;
		std::string equippedDisplayName;
		float baseWeight{ 0.0f };
		bool hasSpecificPreset{ false };

		// Active inertia parameters
		float cachedWeightMult{ 1.0f };
		float actionBlendFactor{ 1.0f };
		float settlingFactor{ 0.0f };
		float timeSinceMovement{ 0.0f };

		// Spring velocities (magnitude gives sense of activity)
		RE::NiPoint3 camPosVel{};
		RE::NiPoint3 camRotVel{};
		RE::NiPoint3 movePosVel{};
		RE::NiPoint3 moveRotVel{};

		// Applied offset (from deferred frame-gen buffer)
		RE::NiPoint3 appliedPosOffset{};
		RE::NiPoint3 appliedRotOffset{};

		// Raw bitfield values for debugging
		std::uint32_t gunStateRaw{ 0 };
		std::uint32_t recoilRaw{ 0 };
		std::uint32_t moveModeRaw{ 0 };
		std::uint32_t flyStateRaw{ 0 };
		std::uint32_t meleeAttackStateRaw{ 0 };
		float sustainedFireTime{ 0.0f };
		float airTimeVal{ 0.0f };
		float globalTimeMult{ 1.0f };

		// Recent impulse events
		DebugEvent recentEvents[kMaxDebugEvents]{};
		int eventCount{ 0 };
	};


	// Spring state tracking position and rotation offsets
	struct SpringState
	{
		RE::NiPoint3 positionOffset{ 0.0f, 0.0f, 0.0f };
		RE::NiPoint3 positionVelocity{ 0.0f, 0.0f, 0.0f };
		RE::NiPoint3 rotationOffset{ 0.0f, 0.0f, 0.0f };
		RE::NiPoint3 rotationVelocity{ 0.0f, 0.0f, 0.0f };

		void Reset()
		{
			positionOffset  = { 0.0f, 0.0f, 0.0f };
			positionVelocity = { 0.0f, 0.0f, 0.0f };
			rotationOffset  = { 0.0f, 0.0f, 0.0f };
			rotationVelocity = { 0.0f, 0.0f, 0.0f };
		}
	};

	// Generic single-fire impulse spring helper
	struct ImpulseSpring
	{
		SpringState state;
		float blendProgress{ 1.0f };   // 1.0 = fully applied / idle
		float blendDuration{ 0.0f };
		RE::NiPoint3 pendingPosImpulse{ 0.0f, 0.0f, 0.0f };
		RE::NiPoint3 pendingRotImpulse{ 0.0f, 0.0f, 0.0f };

		void Reset()
		{
			state.Reset();
			blendProgress    = 1.0f;
			blendDuration    = 0.0f;
			pendingPosImpulse = { 0.0f, 0.0f, 0.0f };
			pendingRotImpulse = { 0.0f, 0.0f, 0.0f };
		}

		// Fire an impulse; blendTime=0 applies instantly
		void Fire(const RE::NiPoint3& posImpulse, const RE::NiPoint3& rotImpulse, float blendTime)
		{
			pendingPosImpulse = posImpulse;
			pendingRotImpulse = rotImpulse;
			blendDuration     = blendTime;
			blendProgress     = 0.0f;
			if (blendTime <= 0.001f) {
				state.positionVelocity.x += posImpulse.x;
				state.positionVelocity.y += posImpulse.y;
				state.positionVelocity.z += posImpulse.z;
				state.rotationVelocity.x += rotImpulse.x;
				state.rotationVelocity.y += rotImpulse.y;
				state.rotationVelocity.z += rotImpulse.z;
				pendingPosImpulse = { 0, 0, 0 };
				pendingRotImpulse = { 0, 0, 0 };
				blendProgress     = 1.0f;
			}
		}

		// Advance blend; call every frame
		void AdvanceBlend(float delta)
		{
			if (blendProgress >= 1.0f || blendDuration <= 0.001f) return;
			float prev = blendProgress;
			blendProgress = std::min(1.0f, blendProgress + delta / blendDuration);
			float dp = blendProgress - prev;
			state.positionVelocity.x += pendingPosImpulse.x * dp;
			state.positionVelocity.y += pendingPosImpulse.y * dp;
			state.positionVelocity.z += pendingPosImpulse.z * dp;
			state.rotationVelocity.x += pendingRotImpulse.x * dp;
			state.rotationVelocity.y += pendingRotImpulse.y * dp;
			state.rotationVelocity.z += pendingRotImpulse.z * dp;
			if (blendProgress >= 1.0f) {
				pendingPosImpulse = { 0, 0, 0 };
				pendingRotImpulse = { 0, 0, 0 };
			}
		}
	};

	// Animation graph event sink for reliable fire/reload detection
	class AnimEventSink : public RE::BSTEventSink<RE::BSAnimationGraphEvent>
	{
	public:
		RE::BSEventNotifyControl ProcessEvent(
			const RE::BSAnimationGraphEvent& a_event,
			RE::BSTEventSource<RE::BSAnimationGraphEvent>* a_source) override;

		std::atomic<bool> firedThisFrame{ false };
		std::atomic<bool> reloadingThisFrame{ false };
		// Per-event atomics used to select which event triggers the reload impulse
		std::atomic<bool> reloadStartThisFrame{ false };     // reloadStateEnter
		std::atomic<bool> reloadEndThisFrame{ false };       // reloadEnd / ReloadEnd
		std::atomic<bool> reloadCompleteThisFrame{ false };  // reloadComplete / ReloadComplete
		std::atomic<bool> initiateStartThisFrame{ false };   // InitiateStart / initiateStart
		std::atomic<bool> sightedExitThisFrame{ false };     // SightedStateExit (tracks ADS exit for early return)
		std::atomic<bool> sneakStartedThisFrame{ false };    // sneakStateEnter / sneakStart etc.
		std::atomic<bool> sneakStoppedThisFrame{ false };    // sneakStateExit / sneakStop etc.
		// Holster / draw window — Fire on Empty must not run over these
		// clips (same failure mode as mid-equip: sound plays against the
		// wrong animation). Events from FPWeaponAdjust / in-game log:
		// weaponSheathe, BeginWeaponSheathe, BeginWeaponDraw.
		std::atomic<bool> sheatheStartedThisFrame{ false };
		std::atomic<bool> beginWeaponDrawThisFrame{ false };
		// Repeatable Gun Bash: HitFrame is the common "impact" annotation
		// present in melee/bash animations (the engine itself registers a
		// HitFrameHandler functor for this exact event name). The combo
		// window opens a configurable delay after this event.
		std::atomic<bool> hitFrameThisFrame{ false };
		bool registered{ false };
	};

	class InertiaManager
	{
	public:
		static InertiaManager* GetSingleton()
		{
			static InertiaManager singleton;
			return &singleton;
		}

		void Update(float delta, float realDelta);
		void Reset();
		void OnEnterFirstPerson();
		void OnExitFirstPerson();

		// Apply deferred offsets in the first-person update hook (frame-gen safe)
		void OnFirstPersonUpdate(RE::NiAVObject* firstPersonObject);

		// Register/unregister anim event sink on the player
		void RegisterAnimEventSink();
		void UnregisterAnimEventSink();

		// Call after save/new game to allow form lookups
		void OnGameLoaded();

		// Menu helpers — get the currently equipped weapon base form and EditorID
		// Safe to call from render/game thread.
		static RE::TESBoundObject* GetEquippedWeaponBaseStatic(RE::PlayerCharacter* a_player);
		static std::string GetEquippedWeaponEditorIDStatic(RE::PlayerCharacter* a_player);

		// Fill a debug snapshot with current live state (best-effort, no lock — for display only)
		void FillDebugSnapshot(DebugSnapshot& snap);

		// Super Sprint — create the runtime keyword + cache actor value pointers
		void InitSuperSprint();

		// Whether super sprint is currently engaged (read by menu for status display)
		bool IsSuperSprintActive() const { return superSprintActive; }

	private:
		InertiaManager() = default;
		~InertiaManager() = default;
		InertiaManager(const InertiaManager&) = delete;
		InertiaManager(InertiaManager&&) = delete;
		InertiaManager& operator=(const InertiaManager&) = delete;
		InertiaManager& operator=(InertiaManager&&) = delete;

		// Snapshot of equipped weapon pointers captured under a single lock
		struct EquippedWeaponSnapshot {
			RE::TESBoundObject*   base{ nullptr };
			RE::TBO_InstanceData* idata{ nullptr };
			std::uint32_t         formID{ 0 };
			std::string           editorID;
		};

		// ---- Weapon detection ----
		EquippedWeaponSnapshot FetchEquippedWeapon(RE::PlayerCharacter* player) const;
		WeaponType DetectWeaponType(const EquippedWeaponSnapshot& snap, bool inPowerArmor) const;
		bool IsInPowerArmor(RE::PlayerCharacter* player) const;
		bool IsADS(RE::PlayerCamera* camera) const;
		bool IsScoped(RE::PlayerCamera* camera) const;
		bool IsFiring(RE::PlayerCharacter* player) const;
		bool IsReloading(RE::PlayerCharacter* player) const;
		bool IsInMeleeAction(RE::PlayerCharacter* player) const;

		float GetWeightScaleMult(const EquippedWeaponSnapshot& snap, const WeaponInertiaSettings& ws) const;
		RE::TESBoundObject* GetEquippedWeaponBase(RE::PlayerCharacter* player) const;
		RE::TBO_InstanceData* GetEquippedWeaponInstanceData(RE::PlayerCharacter* player) const;
		std::string GetEquippedWeaponEditorID(RE::PlayerCharacter* player) const;

		bool IsWeaponAutomatic(RE::PlayerCharacter* player) const;

		// Cached weapon type settings (checks EditorID/keywords)
		const WeaponInertiaSettings& GetCurrentWeaponSettings(RE::PlayerCharacter* player);
		bool WeaponHasKeyword(RE::TESBoundObject* base, RE::TBO_InstanceData* idata, const char* kwEditorID) const;

		// ---- Spring update helpers ----
		void UpdateCameraSpring(SpringState& state, const WeaponInertiaSettings& ws,
			const RE::NiPoint3& camVel, float delta, float mult);
		void UpdateMovementSpring(SpringState& state, const WeaponInertiaSettings& ws,
			const RE::NiPoint3& localMove, float delta, float mult);
		void UpdateImpulseSpring(ImpulseSpring& spring, float stiffness, float damping, float delta);

		// Springs, impulses, walk/lean carryover, ADS transition motion, and camera
		// velocity smoothing — used while Pip-Boy is open and as the first stage of
		// Reset(). Does not clear reload / EarlyADS / weapon cache / bone cache.
		void ResetSpringPhysicsState();

		void ApplyOffset(RE::NiNode* node, const SpringState& combined,
			const WeaponInertiaSettings& ws);
		RE::NiNode* FindTargetNode(RE::NiNode* fpRoot, const WeaponInertiaSettings& ws, bool isADS = false);
		RE::NiNode* GetOrInsertInertiaBone(RE::NiNode* fpRoot, RE::NiNode* pivotBone);

		RE::NiPoint3 CalculateCameraVelocity(float delta);
		RE::NiPoint3 CalculateLocalMovement(RE::PlayerCharacter* player);

		// ---- State tracking ----
		bool isInFirstPerson{ false };
		bool initialized{ false };
		// Previous frame's springsActive value (inertia visuals allowed to
		// run). Used to drain spring energy exactly once on the falling
		// edge so no stale viewmodel offset persists while inertia is off.
		bool springsWereActive{ false };

		// Camera velocity
		float lastCameraYaw{ 0.0f };
		float lastCameraPitch{ 0.0f };
		RE::NiPoint3 smoothedCameraVelocity{};

		// Movement
		RE::NiPoint3 smoothedLocalMovement{};

		// Springs
		SpringState cameraSpring;
		SpringState movementSpring;
		SpringState sprintSpring;
		SpringState jumpSpring;

		// Impulse springs (F4-new)
		ImpulseSpring equipImpulse;
		ImpulseSpring adsEnterImpulse;
		ImpulseSpring adsExitImpulse;
		ImpulseSpring fireRecoveryImpulse;
		ImpulseSpring adsFireRecoveryImpulse;
		ImpulseSpring reloadImpulse;       // tactical reload
		ImpulseSpring emptyReloadImpulse;  // empty reload
		ImpulseSpring leanImpulse;         // UneducatedShooter lean transitions
		ImpulseSpring sneakImpulse;        // sneak enter/exit

		// Sprint tracking
		bool isSprinting{ false };
		bool wasSprinting{ false };
		float sprintBlendProgress{ 0.0f };
		float sprintBlendDuration{ 0.0f };
		RE::NiPoint3 sprintPendingPos{};
		RE::NiPoint3 sprintPendingRot{};

		// Super Sprint — double-tap sprint for speed/AP/anim boost + OAR keyword
		bool  superSprintActive{ false };          // currently in super sprint mode
		bool  superSprintWindowActive{ false };    // activation window is open (waiting for second tap)
		float superSprintWindowStart{ 0.0f };      // elapsedTime when the activation window opened
		float superSprintSpeedBoost{ 0.0f };       // SpeedMult delta currently applied (to remove cleanly)
		float superSprintAnimBoost{ 0.0f };        // AnimationMult delta currently applied (to remove cleanly)
		float superSprintPrevAP{ -1.0f };          // AP value at end of previous frame (for multiplicative extra drain)
		RE::BGSKeyword*    superSprintKeyword{ nullptr };   // runtime keyword added/removed from player NPC base form
		RE::ActorValueInfo* avifSpeedMult{ nullptr };        // cached SpeedMult AVIF
		RE::ActorValueInfo* avifAnimMult{ nullptr };         // cached AnimationMult AVIF (controls animation playback speed)
		RE::ActorValueInfo* avifActionPoints{ nullptr };     // cached ActionPoints AVIF

		// Walk direction offset blending (4 directional weights, 0..1)
		float walkWeightFwd{ 0.0f };
		float walkWeightBack{ 0.0f };
		float walkWeightLeft{ 0.0f };
		float walkWeightRight{ 0.0f };

		// Jump tracking
		bool isInAir{ false };
		bool wasInAir{ false };
		bool confirmedInAir{ false };
		bool didJump{ false };
		float airTime{ 0.0f };
		float landingCooldown{ 0.0f };
		// Delayed fall impulse: only fires after the player has been falling for > 0.25 s
		// to avoid false triggers from ledge-edge collision jitter.
		bool pendingFallImpulse{ false };
		float pendingFallTimer{ 0.0f };
		float movementAirBlend{ 1.0f };
		float cameraAirBlend{ 1.0f };
		float currentJumpStiffness{ 40.0f };
		float currentJumpDamping{ 3.0f };

		// Lean tracking (UneducatedShooter integration)
		// currentLeanWeight: smoothed [-1,+1] derived from CameraInserted1st bone rotation
		// (positive = lean right, negative = lean left, 0 = not leaning / no plugin)
		float currentLeanWeight{ 0.0f };
		float prevLeanDir{ 0.0f };         // sign of lean weight last frame for transition detection
		RE::NiPoint3 leanAdditiveOffset{}; // current blended positional offset

		// Sneak tracking
		bool wasSneaking{ false };

		// ADS tracking
		bool wasADS{ false };
		bool wasScoped{ false };

		// ADS procedural transition tracking
		bool  adsTransitionActive{ false };
		bool  adsTransitionIsEnter{ true };   // true = hip->ADS, false = ADS->hip
		float adsTransitionProgress{ 0.0f };  // [0,1] normalized
		float adsTransitionDuration{ 0.0f };  // seconds (from weapon sightedTransitionSeconds)
		float adsTransitionTimer{ 0.0f };     // accumulated time

		// Early ADS return tracking
		bool isCurrentlyReloading{ false };   // tracked via anim events (reloadStart → reloadEnd)
		bool isCurrentlyEquipping{ false };   // true from justEquipped / BeginWeaponDraw until equip finishes
		float equipAnimTimer{ 0.0f };         // safety timeout for equip state (cleared after ~1.5s)
		// Holster/sheathe window — true from weaponSheathe / BeginWeaponSheathe
		// until drawn goes false or the safety timer expires. Gates Fire on Empty
		// the same way isCurrentlyEquipping gates the draw.
		bool  isCurrentlyHolstering{ false };
		float holsterAnimTimer{ 0.0f };

		// Early Equip — ADS/fire during equip animations (InitiateStart trigger)
		bool  earlyEquipAdsArmed{ false };
		bool  earlyEquipFireArmed{ false };
		bool  earlyEquipPending{ false };
		float earlyEquipTimer{ 0.0f };

		bool earlyAdsArmed{ false };          // armed when ADS input held during reload
		bool earlyAdsReturnPending{ false };  // true once the trigger event fires
		float earlyAdsReturnTimer{ 0.0f };    // counts down from delay to 0
		bool earlyAdsTriggered{ false };            // set when we end reload early; dampens next ADS enter impulse
		int earlyAdsPostDiagFrames{ 0 };            // diagnostic: log attack vars for N frames after trigger
		int earlyAdsForceIdleFrames{ 0 };           // suppress ADS handler for N frames to force idle transition
		int earlyAdsForceIdleCountdown{ 0 };        // initial value for frame logging

		// After EarlyADS / EarlyFireCancel / EarlyEquip: replay UnCullBone dispatch
		// for a few frames — NotifyAnimationGraph can be ignored if fired while
		// the graph is still transitioning; MiddleHighProcessData cull flags may
		// also need clearing so parts are not left hidden.
		std::uint8_t uncullBoneReplayFrames{ 0 };

		// Early Fire Cancel tracking — mirrors Early ADS but driven by FIRE input
		// held during a reload (reload-cancel-into-fire).  Same downstream flow:
		// ReloadEnd → force isReloading=false → force idle frames → arm phantom
		// override.  Tracked separately so both paths can be armed independently.
		bool  earlyFireCancelArmed{ false };
		bool  earlyFireCancelPending{ false };
		float earlyFireCancelTimer{ 0.0f };

		// Phantom-fire override: drive QueueWeaponFire off each weaponFire
		// anim event until the engine state machine engages or the player
		// releases the trigger.  Auto-arms after EarlyADS *and* whenever
		// we detect a phantom fire condition shortly after a reload.
		bool  earlyAdsAutoFireWatching{ false };     // true while phantom-fire override is active
		float earlyAdsAutoFireTimer{ 0.0f };         // safety timer (max duration of override)
		int   earlyAdsAutoFireAttempts{ 0 };         // how many real shots forced through QueueWeaponFire
		float earlyAdsAutoFireGraceTimer{ 0.0f };    // min spacing between forced shots (rate-limit)
		float earlyAdsAutoFirePhantomGap{ 0.0f };    // time since last weaponFire anim event (used to detect trigger release)
		bool  earlyAdsAutoFireSeenGS8{ false };      // true if we've seen gunState reach 8 (attacking)
		float recentlyReloadedTimer{ 0.0f };         // ticks down from 2.0s after isReloading transitions to false; used to auto-arm phantom override

		// Engine-discharge detection.  Tracks EquippedWeaponData::ammoCount
		// (offset 0x18) between frames.  When the engine actually fires a
		// shot the magazine count decrements; when our phantom override
		// queues a shot it ALSO decrements (QueueWeaponFire is the same
		// path the engine uses).  We use this to:
		//   1) Detect when the engine is firing on its own (so the phantom
		//      override can stay armed but skip duplicate shots).
		//   2) Confirm that our forced QueueWeaponFire calls actually
		//      consumed ammo (real discharge) vs. were silently rejected.
		std::uint32_t lastEquippedAmmoCount{ 0 };    // ammoCount as of last frame
		bool          ammoCountInitialized{ false }; // false until first read

		// Equip tracking
		std::uint32_t cachedEquippedFormID{ 0 };
		bool wasWeaponDrawn{ false };

		// Fire tracking (driven by anim event sink)
		AnimEventSink animEventSink;
		bool wasFiring{ false };
		float fireRecoveryCooldownTimer{ 0.0f };
		float adsFireRecoveryCooldownTimer{ 0.0f };
		float reloadImpulseDelayTimer{ 0.0f };   // counts down after trigger event fires
		float emptyReloadImpulseDelayTimer{ 0.0f };
		bool lastReloadWasEmpty{ false };         // set on reloadStart based on ammoCount
		std::uint32_t lastKnownAmmoCount{ 0 };    // tracked each frame for reliable empty-reload detection
		float sustainedFireTime{ 0.0f };
		float recentlyFiredTimer{ 0.0f };
		bool recentlyFiredADS{ false };

		// Fire on Empty — input tracking.
		// fireOnEmptyAnimActive: a dry-fire idle (the 1st-person fire idle
		// played as a special idle, MSF burst-mode style) is in flight and
		// owes the engine a StopCurrentIdle — vanilla fire clips loop
		// inside the idle indefinitely (verified in-game 2026-07-16).
		// fireOnEmptyStopTimer: countdown (one weapon fire interval) to
		// that stop; released earlier on trigger release / ammo return.
		bool  fireOnEmptyAnimActive{ false };
		float fireOnEmptyStopTimer{ 0.0f };
		// fireOnEmptySuppressGrace: keeps the FireAnnotationGuard window
		// (which swallows the engine's weaponFire annotation so a vanilla
		// fire clip can't discharge real rounds during a dry-fire) open
		// through the idle's blend-out after the stop.
		float fireOnEmptySuppressGrace{ 0.0f };
		// fireOnEmptyVerifyTimer: safety net armed on each trigger. The
		// idle path never touches the attack state machine, so if gunState
		// nonetheless reads as firing on an empty magazine when this
		// expires, force a graph base-state reset (the wedged-loop failure
		// mode of the abandoned attackStart injection, verified in-game
		// 2026-07-16: WeaponFire looped ~15s until a weapon swap).
		float fireOnEmptyVerifyTimer{ 0.0f };
		// prevFireInputHeld: rising-edge detection for the diagnostic log.
		// fireOnEmptyLatched: one-shot latch so the forced fire action runs
		// exactly once per trigger hold. Covers BOTH cases: a fresh press on
		// an already-empty magazine AND holding the trigger while an
		// automatic weapon runs dry (no rising edge there — the latch
		// releases when the trigger is released or ammo returns).
		bool prevFireInputHeld{ false };
		bool fireOnEmptyLatched{ false };
		// ADS preservation across the dry-fire stop. When the dry-fire
		// starts in iron-sights (gunState 6/8), the stop re-asserts or
		// releases sighted as needed. All normal stops are soft
		// (StopCurrentIdle only) — InitializeToBaseState is reserved for
		// the wedged-graph safety net (hard-stop every tap wedged ADS /
		// holster so only PlayIdle still worked — verified 2026-07-21).
		bool fireOnEmptyWasADS{ false };
		bool fireOnEmptyAdsHeldAtTrigger{ false };
		// Retired (2026-07-21): a per-frame forced-SightedRelease retry
		// after ADS soft-stop. Verified never to succeed (every retry
		// returned false) and unnecessary once the stop always hard-resets
		// — kept as a dead flag only so old call sites compile unchanged.
		bool fireOnEmptyGuardAdsRelease{ false };
		// FOE-only draw/holster lock. Separate from isCurrentlyEquipping so
		// dry-fire stops never arm Early Equip (which disables the attack
		// handler). Refreshed on BeginWeaponDraw / sheathe / drawn edges.
		float fireOnEmptyMotionLock{ 0.0f };
		// NOTE (2026-07-22): the "ADS soft-stop" (parking the graph in the
		// special idle while ADS was held) and its rescue watchdog were
		// removed after three failed in-game rounds — see the soft-stop
		// post-mortem comment above StopEmptyFireAnimation in Inertia.cpp.
		// All stops hard-reset; ADS re-entry is a synthetic input tap
		// (AttackInput::SimulateTap).
		// Deferred OAR duration query (2026-07-22). The fire clip is only
		// SOMETIMES visible to OAR's Clips API on the trigger frame
		// (verified in the 00:39 session log: one same-frame hit, several
		// misses), so the query retries each frame while this timer runs.
		// fireOnEmptyAnimElapsed tracks time since the trigger so a late
		// hit can set the remaining stop time to (duration - elapsed).
		float fireOnEmptyDurationQueryTimer{ 0.0f };
		float fireOnEmptyAnimElapsed{ 0.0f };

		// Repeatable Gun Bash — combo/queue state.
		// A bash is "active" while ActorState::meleeAttackState != 0 with a
		// gun equipped. During an active bash:
		//   * every fresh Melee press queues a follow-up (capped by settings),
		//   * the HitFrame anim event arms bashComboDelayTimer,
		//   * when that timer expires the combo window opens, and the next
		//     queued bash fires via RunActionOnActor(kActionMelee) (with a
		//     synthetic Melee input tap as fallback if the action layer
		//     refuses).
		// If the bash ends naturally with the queue non-empty, the next
		// queued bash fires immediately on the falling edge.
		bool  wasBashActive{ false };        // meleeAttackState != 0 last frame (gun equipped)
		int   bashQueuedCount{ 0 };          // follow-up bashes waiting to fire
		float bashComboDelayTimer{ 0.0f };   // counts down from settings delay after HitFrame
		bool  bashComboWindowOpen{ false };  // HitFrame + delay elapsed for the CURRENT bash
		float bashRetriggerCooldown{ 0.0f }; // min spacing between injected bashes (re-entry guard)

		// Action blending
		float actionBlendFactor{ 1.0f };
		float equipBlendFactor{ 0.0f };
		float reloadElapsedTime{ 0.0f };
		float lastMeasuredReloadDuration{ 0.0f };
		float meleeElapsedTime{ 0.0f };
		float lastMeasuredMeleeDuration{ 0.0f };
		bool  wasInMelee{ false };

		// Settling
		float settlingFactor{ 0.0f };
		float timeSinceMovement{ 0.0f };

		// Simultaneous blend smoothing
		float smoothedCamSimultaneousMult{ 1.0f };
		float smoothedMovSimultaneousMult{ 1.0f };

		// Weight scale (cached per weapon change)
		float cachedWeightMult{ 1.0f };

		// Cached weapon settings (refreshed every frame from presets)
		std::uint32_t cachedWeaponSettingsFormID{ 0 };
		bool cachedWeaponSettingsValid{ false };
		WeaponInertiaSettings cachedWeaponSettingsCopy;

		float chamberExclusionTimer{ 0.0f };

		// Cached nodes (avoid per-frame string search)
		RE::NiNode* cachedTargetNode{ nullptr };
		RE::NiNode* cachedInsertedBone{ nullptr };
		int cachedPivotIndex{ -1 };
		float pivotWarmupTimer{ 0.0f };       // forces spine pivot briefly after load/equip
		RE::NiPoint3 refPosePivotTranslate{ 0.0f, 0.0f, 0.0f };
		bool hasRefPosePivot{ false };

		// Deferred offsets (frame-gen compatible)
		struct DeferredOffsets {
			bool hasOffsets{ false };
			bool isADS{ false };
			SpringState combined;
			WeaponInertiaSettings settings;
		};
		DeferredOffsets deferredOffsets;

		// Debug
		int debugFrameCounter{ 0 };
		static bool hasLoggedSkeleton;
		static bool hasLoggedGraphVars;

		// Event log ring buffer
		DebugEvent eventLog[kMaxDebugEvents]{};
		int eventLogHead{ 0 };
		int eventLogCount{ 0 };
		float elapsedTime{ 0.0f };

		void PushEvent(const char* desc);
	};

	void Install();

} // namespace Inertia
