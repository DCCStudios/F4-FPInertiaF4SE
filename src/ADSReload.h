#pragma once

#include <cstdint>

namespace RE
{
	class PlayerCamera;
	class PlayerCharacter;
	class BGSKeyword;
}

namespace ADSReload
{
	// ============================================================
	// ADS Reloads (Extras feature)
	// ============================================================
	//
	// Goal: when the player triggers a reload while HOLDING the aim input,
	// do not visually kick them out of ADS. The reload animation still
	// plays; releasing the aim input at any point exits the state normally.
	//
	// How Fallout 4 actually "exits ADS" on reload (verified in this
	// workspace: Inertia.cpp gunState notes + Fake Through Scope's use of
	// PlayerCamera::fovAdjustCurrent):
	//
	//   1. The behavior graph leaves the sighted state (gunState 6 -> 4,
	//      SightedStateExit fires the same frame as reloadStateEnter) and
	//      plays the non-sighted reload animation. The sighted POSE is
	//      animation-driven, so keeping it requires replacement
	//      animations, not engine state.
	//   2. The engine zooms the camera back out by interpolating
	//      PlayerCamera::fovAdjustCurrent toward fovAdjustTarget (reset to
	//      0 on sighted exit) at fovAdjustPerSec. This is the visible
	//      "kicked out of ADS" zoom-out.
	//
	// This feature therefore:
	//   * pins fovAdjustCurrent/fovAdjustTarget at the sighted zoom value
	//     for the duration of the reload while aim stays held (the engine
	//     re-enters sighted on its own afterwards because the aim button
	//     is still down, so the hand-off is seamless), and
	//   * keeps the runtime keyword "AnimsADSReloadKeyword" on the player
	//     while an ADS reload could start (sighted + aim held) and through
	//     the hold itself, so Open Animation Replacer conditions
	//     (HasKeyword) can swap in a sighted-pose reload animation. The
	//     keyword must be PRE-ARMED rather than applied at reload start:
	//     OAR evaluates conditions at clip activation, earlier in the
	//     frame than this feature's per-frame hook (verified in-game
	//     2026-08-03). OAR's IsADS condition reads gunState 6/8 and is
	//     false during a reload (gunState 4), so the keyword is the
	//     supported detection path — same pattern as Super Sprint /
	//     Crouch Slide.
	//
	// Multi-runtime: everything goes through CommonLibF4 struct members
	// and anim events already consumed by InertiaManager — no REL::ID
	// hooks, no per-runtime offsets. OG behavior is unchanged when the
	// feature is disabled (pure additive per-frame writes).
	class Manager
	{
	public:
		static Manager* GetSingleton()
		{
			static Manager singleton;
			return &singleton;
		}

		// Create (or find) the runtime OAR keyword. Call at kGameDataReady.
		void Init();

		// Per-frame driver. Called from InertiaManager::Update on the main
		// thread, inside the first-person gate, AFTER the reload anim-event
		// flags are consumed and BEFORE the ADS transition detection (so an
		// active hold can mask the gunState 6 -> 4 flip).
		//
		//   a_gunState      masked ActorState::gunState nibble (0..8)
		//   a_adsAimHeld    genuine SecondaryAttack held (AttackInput hook)
		//   a_wasADS        previous frame's ADS state (gunState 6/8)
		//   a_wasScoped     previous frame's scope-overlay state
		//   a_reloadStarted reloadStateEnter anim event fired this frame
		//   a_sightedTransitionSeconds
		//                   equipped weapon's aim-enter blend duration
		//                   (RangedData::sightedTransitionSeconds); used to
		//                   fast-forward the engine's re-sight blend when
		//                   the hold hands off (Dry Fire technique)
		void Update(RE::PlayerCharacter* a_player,
			RE::PlayerCamera*            a_camera,
			float                        a_delta,
			std::uint32_t                a_gunState,
			bool                         a_adsAimHeld,
			bool                         a_wasADS,
			bool                         a_wasScoped,
			bool                         a_reloadStarted,
			float                        a_sightedTransitionSeconds);

		// True while the zoom hold is engaged. InertiaManager treats this
		// as "still ADS" (no exit/enter impulses, ADS inertia profile), and
		// suppresses Early ADS Return arming (which would cut the reload
		// short — this feature wants the full animation).
		bool IsHoldActive() const { return m_holdActive; }

		// Force-release the hold (FP camera exit, game load). Removes the
		// keyword and, unless the engine is sighted again, restores the
		// zoom-out target so the FOV cannot stay stuck.
		void ForceRelease(const char* a_reason);

		// Clear state after a save load / new game (fresh camera + graph).
		void OnGameLoaded();

	private:
		Manager() = default;
		~Manager() = default;
		Manager(const Manager&) = delete;
		Manager(Manager&&) = delete;
		Manager& operator=(const Manager&) = delete;
		Manager& operator=(Manager&&) = delete;

		void AddKeyword(RE::PlayerCharacter* a_player);
		void RemoveKeyword();

		// a_engineSighted: gunState is 6/8 again — the engine owns the zoom
		// values (they already equal the sighted adjust), so leave them.
		void Disengage(RE::PlayerCamera* a_camera, bool a_engineSighted, const char* a_reason);

		// Runtime keyword for OAR animation conditions
		RE::BGSKeyword*      m_keyword{ nullptr };
		bool                 m_keywordAdded{ false };
		RE::PlayerCharacter* m_keywordTarget{ nullptr };  // player the keyword was added to

		// Hold state
		bool  m_holdActive{ false };
		float m_holdFov{ 0.0f };        // pinned fovAdjust value (sighted zoom)
		float m_regainTimer{ 0.0f };    // time spent outside gunState 4 waiting for re-sight
		float m_safetyTimer{ 0.0f };    // absolute cap since engage

		// Continuous capture of the sighted zoom adjust, sampled while the
		// player is genuinely sighted (gunState 6/8). At reload start the
		// engine has already reset fovAdjustTarget, so the value must be
		// remembered from before the reload.
		float m_lastAdsFovAdjust{ 0.0f };
		// Last non-trivial transition rate seen while sighted; used to
		// restore a sane zoom-out speed if the engine's own rate reads 0.
		float m_lastAdsFovPerSec{ 0.0f };
	};
}
