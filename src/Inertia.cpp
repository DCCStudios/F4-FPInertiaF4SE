#include "Inertia.h"
#include "ChamberExclusion.h"
#include "WeaponFOV.h"
#include "FireOnEmpty.h"
#include "OpenAnimationReplacerAPI-Clips.h"

// ============================================================
// Static members
// ============================================================
bool Inertia::InertiaManager::hasLoggedSkeleton = false;
bool Inertia::InertiaManager::hasLoggedGraphVars = false;

// ============================================================
// Fallout 4 keyword editor IDs for weapon type detection
// These are the standard F4 weapon keywords from vanilla
// ============================================================
static constexpr const char* kKW_WeapTypePistol    = "WeapTypePistol";
static constexpr const char* kKW_WeapTypeRifle     = "WeapTypeRifle";
static constexpr const char* kKW_WeapTypeMG        = "WeapTypeMG";
static constexpr const char* kKW_WeapTypeNonAutomatic = "WeaponTypeNonAutomatic";
static constexpr const char* kKW_WeapTypeLauncher  = "WeaponTypeLauncher";
static constexpr const char* kKW_WeapTypeMissile   = "WeaponTypeMissile";
static constexpr const char* kKW_WeapTypeFlamer    = "WeaponTypeFlamer";
static constexpr const char* kKW_WeapTypeGun       = "WeaponTypeGun";
static constexpr const char* kKW_IsMelee           = "WeaponTypeMelee";
static constexpr const char* kKW_IsUnarmed         = "WeaponTypeUnarmed";

// WEAPON_FLAGS from TESObjectWEAP::InstanceData::flags (offset 0x110)
static constexpr std::uint32_t kWeapFlag_Automatic = 0x00008000;

// Global time multiplier set by the `sgtm` console command. This is a raw
// engine global (not an INI setting); its address is resolved through the
// address library. ID 388442 is post-NG — verified against
// CommonLibSSE's BSTimer::QGlobalTimeMultiplier (RELOCATION_ID(511882, 388442))
// and Open Animation Replacer's Offsets.h.
static float GetGlobalTimeMult()
{
	static REL::Relocation<float*> sgtmValue{ REL::ID(388442) };
	float v = *sgtmValue;
	if (v <= 0.0f || v > 100.0f) v = 1.0f;
	return v;
}

// Local static: BSFixedString must not be constructed at DLL load (string pool).
static bool IsPipboyMenuOpen()
{
	static const RE::BSFixedString kPipboyMenu{ "PipboyMenu" };
	static const RE::BSFixedString kPipboyHolotapeMenu{ "PipboyHolotapeMenu" };
	const auto* ui = RE::UI::GetSingleton();
	return ui && (ui->GetMenuOpen(kPipboyMenu) || ui->GetMenuOpen(kPipboyHolotapeMenu));
}

// ============================================================
// TaskQueueInterface — minimal declaration for QueueWeaponFire
// ============================================================
class TaskQueueInterfaceLocal
{
public:
	static TaskQueueInterfaceLocal* GetSingleton()
	{
		REL::Relocation<TaskQueueInterfaceLocal**> singleton{ REL::ID(7491) };
		return *singleton;
	}

	void QueueWeaponFire(RE::TESObjectWEAP* a_weapon, RE::TESObjectREFR* a_refObject, RE::BGSEquipIndex a_equipIndex, RE::TESAmmo* a_ammo)
	{
		using func_t = decltype(&TaskQueueInterfaceLocal::QueueWeaponFire);
		REL::Relocation<func_t> func{ REL::ID(15449) };
		return func(this, a_weapon, a_refObject, a_equipIndex, a_ammo);
	}
};

// ============================================================
// Action-system declarations (kept for reference / future use)
//
// FIRST ATTEMPT for "Fire on Empty" was the vanilla fire ACTION
// (BGSAction default object) via BGSAnimationSystemUtils::
// RunActionOnActor — the layer Mod Switch Framework's FireGunTask
// uses to force a shot. VERIFIED IN-GAME (2026-07-16) that the
// engine REJECTS both kActionFireAuto and kActionFireEmpty on an
// empty magazine (`RunActionOnActor(...) -> false` on every dry
// press) — the action layer validates ammo before running. The
// behavior graph itself accepts `attackStart` at the same moment
// (probe returned true), so Fire on Empty now injects the graph
// event directly (see TriggerEmptyFireAction) and skips the
// action layer entirely.
//
// The declarations below stay because RunActionOnActor is the
// correct tool for OTHER forced actions (reload, holster, etc.)
// where ammo validation is not in the way.
//
// This CommonLibF4 build does not vendor ActionInput / TESActionData
// (only their RTTI / VTABLE IDs), so we declare the minimal layouts
// here. Layout verified against:
//   - CommonLibF4 (Fell63 fork) RE/A/ActionInput.h  (0x28, member offsets)
//   - Native Animation Framework RE/TESActionData.h (extra members,
//     RunActionOnActor at REL::ID(22408))
// stl::emplace_vtable installs the engine's real vtable so the object
// behaves as a genuine TESActionData when RunActionOnActor uses it.
// ============================================================
namespace RE
{
	class ActionQueue;

	class __declspec(novtable) ActionInput
	{
	public:
		static constexpr auto RTTI{ RTTI::ActionInput };
		static constexpr auto VTABLE{ VTABLE::ActionInput };

		enum class ACTIONPRIORITY
		{
			kImperative = 0x0,
			kQueue      = 0x1,
			kTry        = 0x2
		};

		class Data
		{
		public:
			union
			{
				float         f;
				std::int32_t  i;
				std::uint32_t ui;
			};  // 00
		};

		virtual ~ActionInput() {}  // 00 (body irrelevant — emplace_vtable installs the engine vtable)

		// members
		NiPointer<TESObjectREFR> ref;         // 08
		NiPointer<TESObjectREFR> targetRef;   // 10
		BGSAction*               action;      // 18
		std::uint32_t            priority;    // 20 (ACTIONPRIORITY stored as u32)
		Data                     actionData;  // 24
	};
	static_assert(sizeof(ActionInput) == 0x28);

	class __declspec(novtable) TESActionData :
		public ActionInput
	{
	public:
		static constexpr auto RTTI{ RTTI::TESActionData };
		static constexpr auto VTABLE{ VTABLE::TESActionData };

		TESActionData(ACTIONPRIORITY a_priority, TESObjectREFR* a_refr, BGSAction* a_action)
		{
			stl::emplace_vtable(this);
			priority = static_cast<std::uint32_t>(a_priority);
			ref      = NiPointer<TESObjectREFR>(a_refr);
			action   = a_action;
		}

		virtual ~TESActionData() {}

		virtual ActorState*            GetActorState() { return nullptr; }
		virtual ActionQueue*           GetActionQueue() { return nullptr; }
		virtual BGSAnimationSequencer* GetAnimationSequencer() { return nullptr; }
		virtual TESActionData*         CreateCopy() { return nullptr; }
		virtual bool                   DoIt() { return false; }

		// members (opaque tail — sized to match the engine object so
		// RunActionOnActor can freely write into it)
		BSFixedString unkStr01;
		BSFixedString unkStr02;
		std::int32_t  unk01 = 0;
		std::int64_t  unk02 = 0;
		std::int64_t  unk03 = 0;
		std::int32_t  unk04 = 0;
		std::int32_t  unk05 = 0;
		std::uint8_t  padding[32]{};
	};

	namespace BGSAnimationSystemUtils
	{
		inline bool RunActionOnActor(Actor* a_actor, TESActionData& a_action, bool a_unk = false)
		{
			using func_t = decltype(&RunActionOnActor);
			REL::Relocation<func_t> func{ REL::ID(22408) };
			return func(a_actor, a_action, a_unk);
		}

		// The "InitializeToBaseState" BGSAction — running it via
		// RunActionOnActor resets the actor's behavior graph to its base
		// state. Native Animation Framework uses exactly this to recover
		// a graph after custom animations (SmartIdle.h), which is the
		// proven precedent; here it is the last-resort unstick for a
		// dry-fire attack state that ignored every stop stimulus.
		// (REL::ID verified against NAF's BGSAnimationSystemUtils.h.)
		inline BGSAction* GetDefaultObjectForActionInitializeToBaseState()
		{
			using func_t = decltype(&GetDefaultObjectForActionInitializeToBaseState);
			REL::Relocation<func_t> func{ REL::ID(639576) };
			return func();
		}

		// Read-only query: would sending this anim event to the actor's
		// graph cause a state transition right now? Used to diagnose which
		// fire stimuli the graph accepts on an empty magazine without
		// actually disturbing the graph. (REL::ID verified against NAF's
		// BGSAnimationSystemUtils.h.)
		inline bool WillEventChangeState(const TESObjectREFR& a_ref, const BSFixedString& a_event)
		{
			using func_t = decltype(&WillEventChangeState);
			REL::Relocation<func_t> func{ REL::ID(35074) };
			return func(a_ref, a_event);
		}

		// Instantly re-initialize the actor's behavior graph (no blend).
		// SeamlessInspect calls this on the IdleStop graph event to make
		// the engine's mandatory post-special-idle re-equip invisible
		// (paired with a small UpdateAnimation step to advance past it).
		// (Signature + REL::ID(672857) verified against NAF's
		// BGSAnimationSystemUtils.h; same usage in SeamlessInspect.)
		inline bool InitializeActorInstant(Actor* a_actor, bool a_initializeNodes)
		{
			using func_t = decltype(&InitializeActorInstant);
			REL::Relocation<func_t> func{ REL::ID(672857) };
			return func(a_actor, a_initializeNodes);
		}
	}

	// AIProcess::SetupSpecialIdle — the engine function that plays an
	// action's associated idle on an actor. This is what the Papyrus
	// Actor.PlayIdleAction native (Mod Switch Framework's fire/release
	// mechanism) calls internally, and what SeamlessInspect / idlestopfix
	// hook in working plugins.
	//
	// HISTORY: the first attempt called the Papyrus native itself via a
	// derived address (REL::ID(760592), inferred from MSF's raw-offset
	// comment) and CRASHED in-game (2026-07-17 02:36: garbage indirect
	// call during TESConditionItem::IsTrue with ActionFireAuto on the
	// stack) — the derived ID was wrong. SetupSpecialIdle's ID needs no
	// derivation: REL::ID(1446774) is declared verbatim in TWO vendored
	// CommonLibF4 copies in PluginTemplate (fo4test CommonLibF4PreNG
	// Actor.h and GunMover IDs.h `SetupSpecialIdle{ 1446774, 2231704 }`),
	// with this exact signature.
	inline bool AIProcess_SetupSpecialIdle(
		AIProcess*     a_process,
		Actor&         a_actor,
		DEFAULT_OBJECT a_action,
		TESIdleForm*   a_idle,
		bool           a_testConditions,
		TESObjectREFR* a_targetOverride)
	{
		if (!a_process) return false;
		using func_t = bool (*)(AIProcess*, Actor&, DEFAULT_OBJECT, TESIdleForm*, bool, TESObjectREFR*);
		REL::Relocation<func_t> func{ REL::ID(1446774) };
		return func(a_process, a_actor, a_action, a_idle, a_testConditions, a_targetOverride);
	}

	// AIProcess::StopCurrentIdle — ends the special idle SetupSpecialIdle
	// started. Signature and pre-NG ID from GunMover's vendored
	// CommonLibF4 (AIProcess.h + IDs.h `StopCurrentIdle{ 434460, 2231705 }`).
	inline void AIProcess_StopCurrentIdle(
		AIProcess* a_process,
		Actor*     a_actor,
		bool       a_instant,
		bool       a_killFlavor)
	{
		if (!a_process) return;
		using func_t = void (*)(AIProcess*, Actor*, bool, bool);
		REL::Relocation<func_t> func{ REL::ID(434460) };
		return func(a_process, a_actor, a_instant, a_killFlavor);
	}
}

// Force the equipped weapon's fire animation to play even with an empty
// magazine, by playing the first-person FIRE IDLE through the regular
// idle system — Mod Switch Framework's mechanism for showing fire
// animations on its forced burst shots (MSF_BurstMode.cpp FireWeapon:
// PlayIdle(player, fireIdle1stP), where fireIdle1stP is Fallout4.esm
// form 0x4AE1, MSF_Data.cpp line 719). AIProcess::PlayIdle ==
// SetupSpecialIdle(kActionIdle, idle, true, target) per GunMover's
// vendored CommonLibF4 AIProcess.h.
//
// Why NOT the attack state machine (all verified in-game 2026-07-16/17):
//   * RunActionOnActor(kActionFireAuto/Single/Empty): refused on empty
//     magazine (action layer validates ammo).
//   * SetupSpecialIdle(kActionFireAuto): refused the same way.
//   * Raw graph attackStart: PLAYS, but always the single-fire branch
//     (OAR log: SCAR played wpnfiresingleready), and it wedges the
//     engine in a self-sustaining fire loop: the attackState-Enter
//     annotation sets gunState to firing, the engine then re-syncs
//     isFiring=1 into the graph every frame, and the auto-fire loop
//     state never exits — attackStop / release actions / variable
//     writes all bounce (log 22:49: vars written false, still true
//     next dump) until a full graph base-state reset (which visually
//     re-equips the weapon).
// The idle avoids the feedback trap because StopCurrentIdle actually
// terminates it (unlike attackStop, which the wedged attack state
// ignored). It is NOT free of the attack state though — in-game trace
// (2026-07-16 23:29) shows the idle drives attackState Enter/gunState 7
// while it plays, the weapon's own fire clip loops inside it (SCAR
// resolved to its AUTO loop — the right clip at last), and every
// WeaponFire annotation in the clip genuinely discharges the weapon,
// ammo or not. Hence the caller MUST stop it after one fire cycle (see
// the lifecycle block in Update), and the feature's real dry-fire use
// requires an OAR replacement clip without WeaponFire annotations.
//
// NOTE: with this path the entry's autoFire flag no longer selects the
// animation — the idle tree resolves the weapon's own fire anim. The
// flag is kept in the JSON/UI and logged for OAR authors' reference.
static bool TriggerEmptyFireAction(RE::PlayerCharacter* a_player, bool a_autoFire)
{
	if (!a_player || !a_player->currentProcess) return false;

	auto* fireIdle = RE::TESForm::GetFormByID<RE::TESIdleForm>(0x0004AE1);
	if (!fireIdle) {
		logger::warn("[FireOnEmpty] 1st-person fire idle (Fallout4.esm|0004AE1) not found");
		return false;
	}

	const bool ok = RE::AIProcess_SetupSpecialIdle(
		a_player->currentProcess, *a_player,
		RE::DEFAULT_OBJECT::kActionIdle, fireIdle, true, nullptr);
	logger::info("[FireOnEmpty] PlayIdle(fireIdle1stP 0x4AE1, autoFlag={}) -> {}",
		a_autoFire, ok);
	return ok;
}

// ---- OAR Clips API: real fire-idle duration (replaces the tuned floor) ----
// fireIdle1stP resolves through the graph's idle tree to whichever clip the
// weapon/behavior actually binds (see TriggerEmptyFireAction comment), so
// there's no fixed clip we can look up a duration for ahead of time — but
// once SetupSpecialIdle has activated it, OAR's Clips API can read the real
// bound hkaAnimation's duration off the live clip generator (verified via
// OAR's own Conditions.cpp: GetAnimDuration() reads clipGen->GetAnimation()->
// duration, an offset-checked field — this is the same mechanism, just
// exposed to us). This also means an OAR-authored dry-fire replacement clip's
// real (likely shorter, non-looping) duration is picked up automatically,
// no re-tuning needed on our side.
//
// The API has no "get the actor's active special-idle clip" query — only
// "list every active clip" on the actor's first-person graph, which can
// include several concurrently-active layers (weapon idle, breathing/idle
// sway, prop layers, etc).
//
// Two prior approaches failed in-game:
//   1. Handle-diff before/after the trigger (2026-07-22 early): picked
//      unrelated clips whose generators happened to be recreated the same
//      frame, and missed reused generators entirely.
//   2. Same-frame "freshest clip" pick (2026-07-22 later): the fire clip
//      is only SOMETIMES visible to OAR on the trigger frame — the 00:39
//      session log shows one trigger where WPNFireSingleSighted appeared
//      at localTime=0 (picked correctly, 0.767s) and several where the
//      candidate list contained no fire clip at all yet, so hipfire
//      grabbed a freshly-restarted 5.8s WPNIdleReady instead.
//
// Current approach: the caller RETRIES this query each frame for a short
// window after the trigger (fireOnEmptyDurationQueryTimer), and we only
// accept a clip whose name contains "fire" — vanilla weapon fire clips
// are consistently named this way (wpnfiresingleready, wpnfiresinglesighted,
// wpnfireauto…; confirmed in the session log) and fire clips don't play
// outside firing, so a recently-started fire-named clip during a dry-fire
// window is ours. Ambient false positives (WPNIdleReady etc.) never
// name-match, so they can no longer be picked at all.
namespace OARFireIdleQuery
{
	static bool NameLooksLikeFire(const OAR::Clips::ClipInfo& a_clip)
	{
		auto contains = [](const char* a_hay) {
			if (!a_hay || !a_hay[0]) return false;
			std::string lower(a_hay);
			std::transform(lower.begin(), lower.end(), lower.begin(),
				[](unsigned char c) { return static_cast<char>(std::tolower(c)); });
			return lower.find("fire") != std::string::npos;
		};
		return contains(a_clip.suffix) || contains(a_clip.animationName) || contains(a_clip.resolvedPath);
	}

	// One retry attempt. a_maxLocalTime bounds how far into playback the
	// clip may already be (grows with time since trigger, so a clip that
	// activated a couple frames ago still qualifies). a_logCandidates
	// dumps the full candidate list — used on the final attempt only, so
	// a persistent failure is diagnosable without per-frame log spam.
	// Returns the clip's real duration, or 0.0f if no fire-named,
	// recently-started first-person clip is visible yet.
	static float QueryFireClipDuration(float a_maxLocalTime, bool a_logCandidates)
	{
		auto* api = OAR::Clips::GetAPI();
		if (!api) return 0.0f;

		OAR::Clips::ClipInfo clips[32];
		const std::uint32_t total = api->GetActorClips(0, clips, 32);

		int   bestIdx = -1;
		float bestLocalTime = 0.0f;

		for (std::uint32_t i = 0; i < total && i < 32; ++i) {
			const auto& c = clips[i];
			if (c.perspective != OAR::Clips::kPerspectiveFirstPerson) continue;
			if (c.duration <= 0.0f) continue;
			const bool fresh = c.localTime <= a_maxLocalTime;
			const bool fireName = NameLooksLikeFire(c);
			if (a_logCandidates) {
				logger::info("[FireOnEmpty] OAR clip candidate: name='{}' duration={:.3f} "
					"localTime={:.3f} fresh={} fireName={}",
					c.animationName, c.duration, c.localTime, fresh, fireName);
			}
			if (!fresh || !fireName) continue;
			if (bestIdx < 0 || c.localTime < bestLocalTime) {
				bestIdx = static_cast<int>(i);
				bestLocalTime = c.localTime;
			}
		}

		if (bestIdx < 0) return 0.0f;
		logger::info("[FireOnEmpty] OAR fire clip found: name='{}' duration={:.3f} localTime={:.3f}",
			clips[bestIdx].animationName, clips[bestIdx].duration, clips[bestIdx].localTime);
		return clips[bestIdx].duration;
	}
}

// Return the graph's attack state to idle after a forced dry-fire, and
// (if that fails) force-reset the graph. Defined after the HavokVar
// namespace below — the stop needs to clear behavior variables directly.
// Returns true if the hard-stop (base-state reset) ran.
static bool StopEmptyFireAnimation(RE::PlayerCharacter* a_player, const char* a_reason);
static void ForceGraphBaseStateReset(RE::PlayerCharacter* a_player);

// ============================================================
// BSSoundHandle::FadeOutAndRelease — Pre-NG REL::ID(260328).
// Used to fade out and release a looping fire sound that the
// engine starts via BSAudioManager but whose stop annotation
// never fires (because our QueueWeaponFire bypasses the proper
// attack state machine).  Pre-NG version of CommonLibF4 in this
// project doesn't expose the method, so we declare it locally.
// ============================================================
namespace LocalSound
{
	inline bool FadeOutAndRelease(RE::BSSoundHandle& a_handle, std::uint16_t a_milliseconds)
	{
		using func_t = bool (RE::BSSoundHandle::*)(std::uint16_t);
		REL::Relocation<func_t> func{ REL::ID(260328) };
		return func(&a_handle, a_milliseconds);
	}

	// Fades out every sound handle owned by the player's currently equipped
	// weapon AND the player's generic soundHand handle.  Returns the number
	// of handles that returned `true` from FadeOutAndRelease.
	//
	// The auto-fire LOOP sound for a weapon is held on
	// EquippedWeaponData::attackSound (offset 0x78); when our phantom-fire
	// override drives QueueWeaponFire directly, the engine never emits the
	// natural attackStateExit cleanup that would normally fade this handle,
	// so we have to do it ourselves on phantom-override exit.  We also fade
	// idleSound/reverbSound/prevAttack/prevReverb and player.soundHand as
	// belt-and-suspenders coverage for any other looping audio the engine
	// may have started during the phantom run.
	inline int FadeOutAllPlayerWeaponSounds(RE::PlayerCharacter* a_player, std::uint16_t a_milliseconds)
	{
		int faded = 0;
		if (!a_player) return faded;

		// Player generic sound handle (covers various non-weapon loops).
		if (FadeOutAndRelease(a_player->soundHand, a_milliseconds)) ++faded;

		// Equipped weapon sound handles — the actual gunfire loop lives here.
		if (a_player->currentProcess && a_player->currentProcess->middleHigh) {
			RE::BSAutoLock lock{ a_player->currentProcess->middleHigh->equippedItemsLock };
			for (auto& eq : a_player->currentProcess->middleHigh->equippedItems) {
				auto* wd = static_cast<RE::EquippedWeaponData*>(eq.data.get());
				if (!wd) continue;
				if (FadeOutAndRelease(wd->attackSound, a_milliseconds))  ++faded;
				if (FadeOutAndRelease(wd->idleSound,   a_milliseconds))  ++faded;
				if (FadeOutAndRelease(wd->reverbSound, a_milliseconds))  ++faded;
				if (FadeOutAndRelease(wd->prevAttack,  a_milliseconds))  ++faded;
				if (FadeOutAndRelease(wd->prevReverb,  a_milliseconds))  ++faded;
			}
		}
		return faded;
	}
}

// ============================================================
// Weapon-bone uncull helper
// When EarlyADS or EarlyFireCancel sends ReloadEnd to cut a reload short,
// bones that are normally un-culled by later annotations in the reload
// animation may remain hidden.  Sending all known UnCullBone.* events
// ensures every weapon part is visible regardless of how far through
// the reload animation we were when we interrupted it.
//
// The engine also tracks a coarse weapon cull on MiddleHighProcessData
// (animWeaponCull / weaponCullCounter).  If those stay set after we cut
// the reload short, parts can remain culled even when UnCullBone events
// are accepted by the graph — clear them whenever we repair visibility.
//
// NotifyAnimationGraphImpl() feeds Havok / the behavior graph directly.
// It does NOT reliably fan out through BSTEventSource<BSAnimationGraphEvent>,
// so registered BSTEventSinks (F4SE plugins, OAR, etc.) never see those
// events.  Clip-driven annotations do hit that path.  After each batch of
// NotifyAnimationGraphImpl calls we therefore synthesize BSAnimationGraphEvent
// and call BSTEventSource::Notify on every source returned by
// BGSAnimationSystemUtils::GetEventSourcePointersFromGraph (same sources
// FPGunplayOverhaul's AnimEventSink registers on).
// ============================================================
static void ClearEngineWeaponCullFlags(RE::Actor* a_actor)
{
	if (!a_actor || !a_actor->currentProcess || !a_actor->currentProcess->middleHigh) {
		return;
	}
	auto* mh = a_actor->currentProcess->middleHigh;
	mh->animWeaponCull     = false;
	mh->weaponCullCounter  = 0;
}

// Sends all UncullBone events as an ordered batch, then a_triggerEvent (if non-null).
//
//   Phase 1 — NotifyAnimationGraphImpl (Havok / behavior graph):
//             UncullBone × 13  →  a_triggerEvent
//   Phase 2 — BSTEventSource::Notify (F4SE plugin sinks, AnimationEventLog…):
//             UncullBone × 13  →  a_triggerEvent
//
// This mirrors how a native HKX clip annotation batch works: the graph sees
// every event before any BSTEventSink observer does.  UncullBone events are
// placed before a_triggerEvent in both phases so the reload sub-graph can
// accept them while still active — prior to the state-transition event.
//
// CRASH NOTE: BSFixedString{} default-constructs with _data = nullptr.
// AnimationEventLog.dll and ActorMediator::ProcessEvent both call
// argument.c_str() which then crashes on the null ptr.  Always pass
// BSFixedString{""} for the argument field (interns the empty string,
// guarantees a valid non-null _data pointer).
//
// Returns the NotifyAnimationGraphImpl result for a_triggerEvent
// (false when a_triggerEvent is nullptr).
static bool SendUncullBatch(RE::PlayerCharacter* a_player,
	const RE::BSFixedString* a_triggerEvent = nullptr)
{
	if (!a_player) return false;

	// Local static: BSFixedString constructors call BSStringPool which must be
	// initialized by the engine first.  A local static is constructed on the
	// first call to this function (well into gameplay, engine fully up),
	// not at DLL-load time — avoiding the startup crash that file-scope
	// RE::BSFixedString statics cause.
	static const RE::BSFixedString kUnCullEvents[] = {
		"UncullBone.WeaponBolt",
		"UncullBone.WeaponExtra1",
		"UncullBone.WeaponExtra2",
		"UncullBone.WeaponExtra3",
		"UncullBone.WeaponTrigger",
		"UncullBone.WeaponMagazine",
		"UncullBone.WeaponMagazineChild1",
		"UncullBone.WeaponMagazineChild2",
		"UncullBone.WeaponMagazineChild3",
		"UncullBone.WeaponMagazineChild4",
		"UncullBone.WeaponMagazineChild5",
		"UncullBone.WeaponOptics1",
		"UncullBone.WeaponOptics2",
	};

	// Phase 1: Havok / behavior graph
	for (const auto& evt : kUnCullEvents) {
		a_player->NotifyAnimationGraphImpl(evt);
	}
	bool triggerResult = false;
	if (a_triggerEvent) {
		triggerResult = a_player->NotifyAnimationGraphImpl(*a_triggerEvent);
	}

	// Phase 2: BSTEventSource sinks (same event order as Phase 1)
	auto* refr = static_cast<RE::TESObjectREFR*>(a_player);
	RE::BSScrapArray<RE::BSTEventSource<RE::BSAnimationGraphEvent>*> sources;
	if (!RE::BGSAnimationSystemUtils::GetEventSourcePointersFromGraph(a_player, sources)) {
		return triggerResult;
	}
	const RE::BSFixedString emptyArg{ "" };
	for (auto* src : sources) {
		if (!src) continue;
		for (const auto& evt : kUnCullEvents) {
			RE::BSAnimationGraphEvent ge{};
			ge.refr      = refr;
			ge.animEvent = evt;
			ge.argument  = emptyArg;
			src->Notify(ge);
		}
		if (a_triggerEvent) {
			RE::BSAnimationGraphEvent ge{};
			ge.refr      = refr;
			ge.animEvent = *a_triggerEvent;
			ge.argument  = emptyArg;
			src->Notify(ge);
		}
	}

	return triggerResult;
}

// Clear engine-side cull flags and send all UncullBone events (no trigger event).
// Used for the periodic replay frames and EarlyEquip.
static void DispatchWeaponUncullRepair(RE::PlayerCharacter* a_player)
{
	ClearEngineWeaponCullFlags(a_player);
	SendUncullBatch(a_player);
}

// ============================================================
// GUN_STATE enum mirror — the CommonLibF4 build vendored with this
// project exposes `Actor::gunState` as a raw `uint32_t : 4` bitfield
// without the named enum.  These values are taken from the CommonLibF4
// PreNG headers (RE::GUN_STATE) and verified against the engine's own
// behavior (also documented in F4SE_Plugin_Development_Reference.md).
//
//   0 kDrawn   1 kRelaxed  2 kBlocked    3 kAlert
//   4 kReloading   5 kThrowing   6 kSighted   <- ADS hold (NOT firing)
//   7 kFire        8 kFireSighted              <- ACTUAL FIRING STATES
//
// Use IsFiringGunState(gs) anywhere we want to know "is the player
// actually trying to discharge a round?" — anything else is noise.
// ============================================================
namespace GunStateLocal
{
	constexpr std::uint32_t kDrawn        = 0;
	constexpr std::uint32_t kRelaxed      = 1;
	constexpr std::uint32_t kBlocked      = 2;
	constexpr std::uint32_t kAlert        = 3;
	constexpr std::uint32_t kReloading    = 4;
	constexpr std::uint32_t kThrowing     = 5;
	constexpr std::uint32_t kSighted      = 6;
	constexpr std::uint32_t kFire         = 7;
	constexpr std::uint32_t kFireSighted  = 8;

	inline bool IsFiringGunState(std::uint32_t gs) noexcept
	{
		return gs == kFire || gs == kFireSighted;
	}
}

// ============================================================
// Equipped-weapon helpers — read live magazine ammoCount
// (EquippedWeaponData::ammoCount, offset 0x18).  This is the
// "rounds in mag" count that decrements on every real discharge,
// whether triggered by the engine's normal attack pipeline OR by
// our phantom override's QueueWeaponFire calls (same code path).
// We use it to tell whether the engine is firing on its own and
// to confirm our forced shots actually consumed ammo.
// ============================================================
namespace EquippedWeapon
{
	inline std::uint32_t GetMagazineAmmoCount(RE::PlayerCharacter* a_player)
	{
		if (!a_player || !a_player->currentProcess || !a_player->currentProcess->middleHigh) return 0;
		auto* mh = a_player->currentProcess->middleHigh;
		RE::BSAutoLock lock{ mh->equippedItemsLock };
		for (auto& eq : mh->equippedItems) {
			auto* wd = static_cast<RE::EquippedWeaponData*>(eq.data.get());
			if (!wd) continue;
			if (wd->ammo) return wd->ammoCount;
		}
		return 0;
	}

	// Returns the per-shot interval in seconds for the equipped weapon,
	// derived from RangedData::fireSeconds and InstanceData::speed
	// (the same values F4 uses to compute its actual auto-fire rate).
	// Used to pace forced phantom shots so we don't outrun the
	// auto-fire WAV's loop_end marker.
	//
	// Falls back to 0.10s (10 RPS) if any value is unavailable.
	inline float GetWeaponFireInterval(RE::PlayerCharacter* a_player)
	{
		constexpr float kFallback = 0.10f;
		if (!a_player || !a_player->currentProcess || !a_player->currentProcess->middleHigh) return kFallback;
		auto* mh = a_player->currentProcess->middleHigh;
		RE::BSAutoLock lock{ mh->equippedItemsLock };
		for (auto& eq : mh->equippedItems) {
			auto* form = eq.item.object;
			if (!form || form->formType != RE::ENUM_FORM_ID::kWEAP) continue;
			auto* weap = static_cast<RE::TESObjectWEAP*>(form);
			auto* idata = static_cast<RE::TESObjectWEAP::InstanceData*>(eq.item.instanceData.get());
			float fireSeconds = (idata && idata->rangedData) ? idata->rangedData->fireSeconds
			                  : (weap->weaponData.rangedData ? weap->weaponData.rangedData->fireSeconds : 0.0f);
			float speed = idata ? idata->speed : weap->weaponData.speed;
			if (speed <= 0.0001f) speed = 1.0f;
			if (fireSeconds <= 0.0001f) return kFallback;
			float interval = fireSeconds / speed;
			// Sanity clamp: 20ms (50 RPS) min, 1.0s max.
			if (interval < 0.020f) interval = 0.020f;
			if (interval > 1.000f) interval = 1.000f;
			return interval;
		}
		return kFallback;
	}

	// True when the equipped weapon is a gun (WEAPON_TYPE::kGun). Used to
	// gate the Repeatable Gun Bash combo — a melee weapon's swings also
	// set meleeAttackState, and combo-ing those is out of scope.
	inline bool IsGunEquipped(RE::PlayerCharacter* a_player)
	{
		if (!a_player || !a_player->currentProcess || !a_player->currentProcess->middleHigh) return false;
		auto* mh = a_player->currentProcess->middleHigh;
		RE::BSAutoLock lock{ mh->equippedItemsLock };
		for (auto& eq : mh->equippedItems) {
			auto* form = eq.item.object;
			if (!form || form->formType != RE::ENUM_FORM_ID::kWEAP) continue;
			auto* weap = static_cast<RE::TESObjectWEAP*>(form);
			auto* idata = static_cast<RE::TESObjectWEAP::InstanceData*>(eq.item.instanceData.get());
			const auto type = idata ? idata->type : weap->weaponData.type;
			return type == RE::WEAPON_TYPE::kGun;
		}
		return false;
	}
}

// ============================================================
// BSAnimationGraphManager — minimal definition for
// BSTSmartPointer ref-counting (BSIntrusiveRefCounted).
// ============================================================
class RE::BSAnimationGraphManager :
	public RE::BSTEventSink<RE::BSAnimationGraphEvent>,
	public RE::BSIntrusiveRefCounted
{
public:
	char _pad[0xD0];
};

// ============================================================
// Direct Havok variable access — bypasses the broken
// BSAnimationGraphVariableCache entirely.
//
// Path: BSAnimationGraphManager → BShkbAnimationGraph (0xC0)
//       → hkbCharacter (0x1C8) → hkbBehaviorGraph (0x80)
//       → hkbVariableValueSet (0x110)
//       → hkArray<hkbVariableValue>.m_data (0x10)
//
// Variable indices from rootbehavior.xml:
//   28=isFiring  31=isReloading  36=iAttackState
//   37=IsAttackReady  47=isAttackNotReady  52=isAttacking
// ============================================================
union HkbVarValue {
	bool         b;
	std::int32_t i;
	float        f;
};
static_assert(sizeof(HkbVarValue) == 4);

namespace HavokVar {
	// Root behavior variable indices (from rootbehavior.xml)
	static constexpr int kIsFiring        = 28;
	static constexpr int kIsReloading     = 31;
	static constexpr int kIAttackState    = 36;
	static constexpr int kIsAttackReady   = 37;
	static constexpr int kIsAttackNotReady= 47;
	static constexpr int kIsAttacking     = 52;

	// Verification variables
	static constexpr int kSpeed           = 2;
	static constexpr int kDirection       = 3;

	struct VarArray {
		HkbVarValue* data;
		std::int32_t size;
		std::int32_t capacityAndFlags;
	};

	// Full pointer walk (expensive). Used once per Update frame via BeginFrame cache.
	inline VarArray* ResolveVariableArray(RE::Actor* a_actor)
	{
		RE::BSTSmartPointer<RE::BSAnimationGraphManager> mgr;
		if (!a_actor->GetAnimationGraphManagerImpl(mgr) || !mgr) return nullptr;
		auto mgrAddr = reinterpret_cast<std::uintptr_t>(mgr.get());

		// graphToCacheFor at offset 0xC0 (BSAnimationGraphVariableCache.graphToCacheFor)
		auto* graphPtr = *reinterpret_cast<void**>(mgrAddr + 0xC0);
		if (!graphPtr) return nullptr;
		auto graphAddr = reinterpret_cast<std::uintptr_t>(graphPtr);

		// hkbCharacter at 0x1C8, hkbCharacter::behaviorGraph at 0x80
		auto* behaviorGraph = *reinterpret_cast<void**>(graphAddr + 0x1C8 + 0x80);
		if (!behaviorGraph) return nullptr;
		auto bgAddr = reinterpret_cast<std::uintptr_t>(behaviorGraph);

		// variableValueSet at 0x110
		auto* varSet = *reinterpret_cast<void**>(bgAddr + 0x110);
		if (!varSet) return nullptr;
		auto vsAddr = reinterpret_cast<std::uintptr_t>(varSet);

		// hkArray<hkbVariableValue> at 0x10 (after hkReferencedObject base)
		auto* arr = reinterpret_cast<VarArray*>(vsAddr + 0x10);
		if (!arr->data || arr->size <= 0) return nullptr;

		return arr;
	}

	// One-frame cache: BeginFrame(player) at start of InertiaManager::Update;
	// all HavokVar reads/writes for that actor reuse the resolved array pointer.
	// EndFrame clears state (RAII FrameGuard). Never held across frames.
	inline thread_local RE::Actor* tls_frameActor = nullptr;
	inline thread_local VarArray* tls_frameArray = nullptr;

	inline void BeginFrame(RE::Actor* a_actor)
	{
		tls_frameActor = a_actor;
		tls_frameArray = a_actor ? ResolveVariableArray(a_actor) : nullptr;
	}

	inline void EndFrame()
	{
		tls_frameActor = nullptr;
		tls_frameArray = nullptr;
	}

	struct FrameGuard
	{
		~FrameGuard() { EndFrame(); }
	};

	inline VarArray* GetVariableArray(RE::Actor* a_actor)
	{
		if (tls_frameActor && a_actor == tls_frameActor) {
			return tls_frameArray;  // may be nullptr (cached miss)
		}
		return ResolveVariableArray(a_actor);
	}

	inline HkbVarValue* GetVar(RE::Actor* a_actor, int a_index) {
		auto* arr = GetVariableArray(a_actor);
		if (!arr || a_index < 0 || a_index >= arr->size) return nullptr;
		return &arr->data[a_index];
	}

	inline bool SetBool(RE::Actor* a_actor, int a_index, bool a_val) {
		auto* v = GetVar(a_actor, a_index);
		if (!v) return false;
		v->i = a_val ? 1 : 0;
		return true;
	}

	inline bool SetInt(RE::Actor* a_actor, int a_index, std::int32_t a_val) {
		auto* v = GetVar(a_actor, a_index);
		if (!v) return false;
		v->i = a_val;
		return true;
	}

	inline bool GetBool(RE::Actor* a_actor, int a_index, bool& a_out) {
		auto* v = GetVar(a_actor, a_index);
		if (!v) return false;
		a_out = (v->i != 0);
		return true;
	}

	inline bool GetInt(RE::Actor* a_actor, int a_index, std::int32_t& a_out) {
		auto* v = GetVar(a_actor, a_index);
		if (!v) return false;
		a_out = v->i;
		return true;
	}

	inline bool SetFloat(RE::Actor* a_actor, int a_index, float a_val) {
		auto* v = GetVar(a_actor, a_index);
		if (!v) return false;
		v->f = a_val;
		return true;
	}

	inline bool GetFloat(RE::Actor* a_actor, int a_index, float& a_out) {
		auto* v = GetVar(a_actor, a_index);
		if (!v) return false;
		a_out = v->f;
		return true;
	}

	inline void LogAllAttackVars(RE::Actor* a_actor) {
		auto* arr = GetVariableArray(a_actor);
		if (!arr) {
			logger::trace("[HavokVar] Could not reach variableValueSet");
			return;
		}
		logger::trace("[HavokVar] variableValueSet has {} variables", arr->size);
		auto logVar = [&](const char* name, int idx) {
			if (idx < arr->size)
				logger::trace("[HavokVar]   [{}] {} = int:{} float:{:.4f}",
					idx, name, arr->data[idx].i, arr->data[idx].f);
			else
				logger::trace("[HavokVar]   [{}] {} = OUT OF RANGE", idx, name);
		};
		logVar("Speed", kSpeed);
		logVar("Direction", kDirection);
		logVar("isFiring", kIsFiring);
		logVar("isReloading", kIsReloading);
		logVar("iAttackState", kIAttackState);
		logVar("IsAttackReady", kIsAttackReady);
		logVar("isAttackNotReady", kIsAttackNotReady);
		logVar("isAttacking", kIsAttacking);
	}
}

// ============================================================
// Fire on Empty — stop (declared above, near TriggerEmptyFireAction).
// ------------------------------------------------------------
// Ends the dry-fire fire idle. Always called after one fire cycle
// (vanilla fire clips LOOP inside the idle — verified 2026-07-16 23:29,
// SCAR auto loop ran the full 1.5s safety window), or earlier on
// trigger release / ammo return.
//
// THE EXIT STIMULUS — a full graph base-state reset, hidden by
// fallout4-idlestopfix's fast-forward.
//
// Why nothing gentler works (all verified in-game 2026-07-17):
//   * StopCurrentIdle instant (03:32) / non-instant (03:07): the fire
//     clip keeps looping — weaponFire annotations continue every
//     ~130ms after the call.
//   * attackStop graph event: NotifyAnimationGraph returns false —
//     the fire loop state is not listening for it.
//   * MSF's ActionRightRelease through RunActionOnActor (03:48):
//     returns false. The action system's conditions read the ENGINE's
//     attack state — and FireAnnotationGuard keeps the engine blind
//     (attackState suppressed) precisely so it can't discharge rounds.
//     MSF can use the release action because its engine SAW the burst
//     begin; ours by design never does. Engine blind → every vanilla
//     exit path is condition-gated off. Deadlock by construction.
//   * Direct isFiring/isAttacking/iAttackState writes: succeed but the
//     loop continues — the special idle drives the clip regardless.
//
// The ONE stimulus that provably runs while the engine is blind is the
// InitializeToBaseState action (kTry, no attack-state conditions —
// every 'forced base-state reset (ran=true)' line in the session
// logs). Its downside was purely cosmetic: the engine follows the
// graph re-init with a visible weapon re-draw. fallout4-idlestopfix
// hides exactly that re-draw with one call — UpdateAnimation(1000.0f),
// a single graph update with a huge delta that completes the re-draw
// before it renders a frame (idlestopfix Hooks.cpp ProcessEvent). So:
// reset to base state, then fast-forward through the draw.
//
// For a non-looping OAR dry-fire replacement clip the reset lands
// after the clip already finished, and the fast-forward makes the
// return to base pose invisible either way.
//
// ADS (2026-07-21, two rounds of testing):
//   Round 1 — hard reset + blind re-assert kActionSighted afterward:
//     ADS dry-fire played correctly, but releasing ADS afterward no
//     longer exited — the forced re-assert (bypassing normal input)
//     left the action layer unable to process the real button-release.
//   Round 2 — soft-stop only (StopCurrentIdle, no reset), preserving
//     gunState, with a per-frame TryPlayerAction(kActionSightedRelease)
//     retry once ADS was released: EVERY retry returned false (dozens
//     of consecutive attempts in the log), and gunState only recovered
//     1-3s later on its own — fire-effect anim events kept firing the
//     whole window, proving the idle was still looping internally.
//     That multi-second window is what blocked ADS/sprint ("locked").
//   Conclusion: hard reset is the only reliable stop (confirmed again
//     across every hipfire cycle this session — zero stuck frames).
//     Do NOT force any action afterward. If ADS is still held, let the
//     normal per-frame SecondaryAttack dispatch (AttackInput always
//     chains to the engine's real handler) re-enter sighted through
//     the vanilla input path, exactly like hipfire resumes after its
//     reset. Cost: a brief idle->ADS blend is visible on ADS dry-fire.
//
// ADS round 3 (2026-07-22): the synthetic-press re-entry itself became
//   the prime suspect for a session-long "cannot enter ADS" input wedge
//   (survived a full weapon re-equip → not graph state; see
//   StopEmptyFireAnimation's REMOVED note). ADS dry-fires now use
//   SoftStopEmptyFireADS (no reset, no synthetic input) with a bounded
//   weaponFire-annotation rescue in Update(); the hard reset here is
//   for hipfire and reload interrupts only.
// ============================================================

// Set while the hard-stop fast-forwards the post-reset re-draw so
// FireAnnotationGuard can swallow re-equip SoundPlay annotations
// without muting the dry-fire clip's own sounds earlier in the idle.
static std::atomic<bool> g_suppressEquipSounds{ false };

// HARD stop: full base-state reset. Used for hipfire dry-fires and any
// reload-driven interrupt. Soft-stop (StopCurrentIdle alone) was verified
// in-game (2026-07-21, gs=6 trace) to NOT reliably kill the special
// idle: fire-effect anim events (jiggleAfterFireEffectSingleSighted)
// kept firing for 1-3 more seconds after the "stop", and every single
// TryPlayerAction(kActionSightedRelease) attempt during that window
// returned false — the eventual recovery came from the idle's own
// internal timing, not from us. That multi-second window is what
// blocked ADS / sprint ("locked weapon state"). InitializeToBaseState
// is the only stop that has ever reliably killed the idle same-frame
// (2026-07-16/17 findings) — verified again across every hipfire
// cycle in-session, zero stuck frames.
//
// REMOVED (2026-07-22): the post-reset synthetic SecondaryAttack press
// (SimulateFreshPress) that re-entered ADS after an ADS dry-fire's hard
// stop. Prime suspect for the "cannot enter ADS at all anymore" wedge in
// the 00:39 session: after three resimulated presses ADS entry stopped
// working for the rest of the session, and a FULL weapon re-equip did
// not fix it — a graph-level wedge cannot survive a re-equip, so the
// stuck state must live in the input/controls layer, which is exactly
// the layer the synthetic ButtonEvent injected into (an extra "just
// pressed" edge per stop with no matching release). Inferred, not
// proven — but ADS dry-fires no longer hard-stop at all (see the soft
// stop below), so nothing needs the resimulation anymore either way.
static bool StopEmptyFireAnimation(RE::PlayerCharacter* a_player, const char* a_reason)
{
	if (!a_player || !a_player->currentProcess) return false;

	RE::AIProcess_StopCurrentIdle(a_player->currentProcess, a_player, true, false);
	HavokVar::SetBool(a_player, HavokVar::kIsFiring, false);
	HavokVar::SetBool(a_player, HavokVar::kIsAttacking, false);
	HavokVar::SetInt(a_player, HavokVar::kIAttackState, 0);
	static const RE::BSFixedString kEvtAttackStop{ "attackStop" };
	a_player->NotifyAnimationGraphImpl(kEvtAttackStop);

	bool resetRan = false;
	if (auto* resetAction = RE::BGSAnimationSystemUtils::GetDefaultObjectForActionInitializeToBaseState()) {
		RE::TESActionData action(RE::ActionInput::ACTIONPRIORITY::kTry, a_player, resetAction);
		resetRan = RE::BGSAnimationSystemUtils::RunActionOnActor(a_player, action);
	} else {
		logger::warn("[FireOnEmpty] InitializeToBaseState default object missing");
	}

	g_suppressEquipSounds.store(true, std::memory_order_relaxed);
	a_player->UpdateAnimation(1000.0f);
	g_suppressEquipSounds.store(false, std::memory_order_relaxed);

	const auto gsAfter = static_cast<std::uint32_t>(a_player->gunState);
	logger::info("[FireOnEmpty] Stopped dry-fire ({}) hard-stop baseStateReset={} + fast-forward (gs={})",
		a_reason, resetRan, gsAfter);
	return resetRan;
}

// SOFT stop: used for ALL ADS dry-fire stops (2026-07-22) — both the
// natural end of the fire cycle with ADS still held, and the ADS-released
// interrupt. Same cleanup as the top of StopEmptyFireAnimation
// (StopCurrentIdle + attack-state variable/event reset) but deliberately
// WITHOUT InitializeToBaseState or the fast-forward:
//   * ADS still held  -> the sighted state simply persists; no exit, no
//     re-enter, no synthetic input needed at all.
//   * ADS released    -> the engine's own ADS-exit blend plays in real
//     time off the real button release, instead of snapping to idle.
//
// SOFT-STOP POST-MORTEM (2026-07-22, three in-game rounds — do not retry):
// "park the graph in the special idle while ADS is held" failed on every
// axis. (1) 01:02 session: after every soft stop the engine never
// processed the ADS release — gunState stayed 6 indefinitely. (2) 01:14
// session: ActionIdleStop (the engine's own idle interrupt, the last
// untried exit stimulus after StopCurrentIdle / attackStop /
// ActionRightRelease / direct variable writes) returned false from
// RunActionOnActor on EVERY attempt — the annotation guard keeps the
// engine blind to the attack by design, and every conditioned action
// reads that blind state. (3) 01:28 report: the parked idle owns the
// whole first-person graph, so locomotion and sway froze until release.
// Conclusion: the graph must ALWAYS be hard-reset out of the idle, and
// ADS is re-entered through the input layer (AttackInput::SimulateTap).

// Forward declaration — defined in the MeleeInput namespace below
// (near the other input hooks); needed here by TriggerGunBashAction.
namespace MeleeInput
{
	static bool SimulateTap();
}

// ============================================================
// Repeatable Gun Bash — visual blend across the hard graph reset
// ------------------------------------------------------------
// The follow-up bash needs InitializeToBaseState to yank the graph out
// of the previous bash (V3 findings above), and that reset snaps the
// viewmodel: the pose jumps from mid-swing to the new bash's first
// frame in one rendered frame.
//
// This masks the snap GunMover-style: a decaying corrective transform
// applied on top of the animation via an inserted bone, never touching
// the animation data itself.  No OAR dependency.
//
// V1 FINDING (2026-07-22 in-game): routing the correction through the
// shared inertia pivot (FPInertia_Node, Spine2 by default) was wrong —
// a rotation measured AT THE HAND applied AROUND THE SPINE swings the
// hand/weapon along a completely different arc (offset ~= R*(hand-pivot)
// -(hand-pivot), tens of units for mid-swing deltas), sending the
// weapon off camera through poses that exist in neither animation.
//
// V2 FINDING (in-game): solving a dedicated bone DIRECTLY above
// RArm_Hand placed the hand/weapon exactly on the blend path, but the
// forearm and elbow stayed where the NEW animation put them — the
// skinned wrist/forearm visibly stretched between the displaced hand
// and the un-displaced elbow during larger corrections.
//
// V3 (this version — GunMover's aesthetic): GunMover never corrects a
// single joint; it shifts the WHOLE viewmodel rigidly (the a_loc
// anchor in Set1stPersonCameraLocation + the 1st-person camera node's
// world rotation), which is why its offsets blend in and out of any
// animation with zero distortion. The node-space equivalent: the
// blend bone (FPBashBlendNode) is inserted above the SPINE anchor
// (Spine2 — the subtree carrying both arms and the weapon, the same
// one the inertia sway moves), and each frame its local transform is
// SOLVED so the HAND's world pose lands exactly on the blend path:
//
//     target(w) = geodesic-lerp( live animated pose,
//                                captured pre-reset pose,  w )
//
// recomputed EVERY FRAME against the live animation. This is the
// load-bearing design decision, learned the hard way: the first
// implementation measured the pose delta ONCE (right after the
// reset) and then composed that frozen offset onto the animation
// while it kept swinging. Adding a stale ~90-degree offset to a fast
// moving pose lands on orientations that belong to NEITHER endpoint
// — the "blend looks completely random" report (2026-07-22 17:38).
// With the lerp target recomputed per frame, the visible pose is
// always BETWEEN the old on-screen pose (w=1) and the current
// animation frame (w=0): it starts exactly where the screen was and
// converges onto the moving swing — a real visual lerp into the
// next bash. The whole upper body moves rigidly (no wrist stretch —
// the seam is the spine junction, off-screen in first person), and
// the solve collapses to the identity exactly at w=0. Correct
// regardless of the inertia pivot setting, of inertia being enabled
// at all, and of FPInertia_Node sitting anywhere above or below —
// the chain from our bone down to the hand is composed live.
//
// This also masks the trigger frame itself: the skeleton shows the
// fast-forwarded weapon-DRAW pose there (the InitializeToBaseState
// re-draw; ~34 units / ~134 deg from the captured pose in testing),
// and at w~1 the lerp pulls the hand back onto the captured pose,
// hiding the one-frame draw flash that used to slip through.
//
// Flow: 1. CaptureBeforeReset (visible hand pose, FP-root frame —
// the root is placed from the camera, not animated by the bash, so
// it is stable across the reset; "visible" includes any residual
// offset from a still-decaying previous blend, so chained combos
// hand over without a pop)  2. ArmAfterReset starts the decay clock
// 3. Apply re-solves the bone local every frame until it expires.
//
// All state is main-thread only (poked from Update and from
// TriggerGunBashAction, which Update calls).
// ============================================================
namespace BashBlend
{
	// -- capture + active blend --
	// The captured pose stays live for the whole blend: Apply re-derives
	// the (capture minus current-animation) delta fresh each frame and
	// weights it, so there is no frozen offset anywhere. The per-frame
	// rotation delta is taken as AXIS-ANGLE and the angle scaled by w —
	// the exact geodesic (shortest-arc) between the two orientations.
	static bool          s_captureValid = false;  // s_capPos/s_capRot hold a usable pose
	static RE::NiPoint3  s_capPos{};              // hand position, FP-root frame (before reset)
	static RE::NiMatrix3 s_capRot;                // hand orientation, FP-root frame (before reset)
	static float         s_timer    = 0.0f;       // counts down from s_duration
	static float         s_duration = 0.0f;

	// -- dedicated inserted bone (above the spine anchor) --
	static constexpr const char* kBashBoneName = "FPBashBlendNode";
	// Anchor candidates, tried in order. Must carry both arms and the
	// weapon (whole-viewmodel rigid move, GunMover-style) but NOT the
	// Camera bone — moving that would rotate the player's view.
	static constexpr const char* kAnchorNames[] = {
		"Spine2", "Spine1b", "Chest", "Spine1", nullptr
	};
	static RE::NiNode* s_bone      = nullptr;     // cached; revalidated by name each use
	static bool        s_boneDirty = false;       // bone local holds a non-identity offset

	// 3x3 helpers — this vendored CommonLibF4's NiMatrix3 has no operators.
	static RE::NiMatrix3 Mul(const RE::NiMatrix3& a, const RE::NiMatrix3& b)
	{
		RE::NiMatrix3 r;
		for (int i = 0; i < 3; ++i)
			for (int j = 0; j < 3; ++j)
				r.entry[i].pt[j] =
					a.entry[i].pt[0] * b.entry[0].pt[j] +
					a.entry[i].pt[1] * b.entry[1].pt[j] +
					a.entry[i].pt[2] * b.entry[2].pt[j];
		return r;
	}

	static RE::NiMatrix3 Transpose(const RE::NiMatrix3& a)
	{
		RE::NiMatrix3 r;
		for (int i = 0; i < 3; ++i)
			for (int j = 0; j < 3; ++j)
				r.entry[i].pt[j] = a.entry[j].pt[i];
		return r;
	}

	static RE::NiPoint3 MulP(const RE::NiMatrix3& m, const RE::NiPoint3& p)
	{
		return {
			m.entry[0].pt[0] * p.x + m.entry[0].pt[1] * p.y + m.entry[0].pt[2] * p.z,
			m.entry[1].pt[0] * p.x + m.entry[1].pt[1] * p.y + m.entry[1].pt[2] * p.z,
			m.entry[2].pt[0] * p.x + m.entry[2].pt[1] * p.y + m.entry[2].pt[2] * p.z
		};
	}

	// Rodrigues' rotation formula: matrix for a rotation of a_angle
	// radians about the unit axis a_axis. R = I + sin(t)K + (1-cos(t))K^2.
	static RE::NiMatrix3 FromAxisAngle(const RE::NiPoint3& a_axis, float a_angle)
	{
		const float s = std::sin(a_angle);
		const float c = std::cos(a_angle);
		const float t = 1.0f - c;
		const float x = a_axis.x, y = a_axis.y, z = a_axis.z;

		RE::NiMatrix3 r;
		r.entry[0].pt[0] = t * x * x + c;
		r.entry[0].pt[1] = t * x * y - s * z;
		r.entry[0].pt[2] = t * x * z + s * y;
		r.entry[1].pt[0] = t * x * y + s * z;
		r.entry[1].pt[1] = t * y * y + c;
		r.entry[1].pt[2] = t * y * z - s * x;
		r.entry[2].pt[0] = t * x * z - s * y;
		r.entry[2].pt[1] = t * y * z + s * x;
		r.entry[2].pt[2] = t * z * z + c;
		return r;
	}

	static RE::NiAVObject* FindHand(RE::NiAVObject* a_fpRoot)
	{
		static const RE::BSFixedString kHand{ "RArm_Hand" };
		return a_fpRoot ? a_fpRoot->GetObjectByName(kHand) : nullptr;
	}

	// Compose the local transforms from (exclusive) a_top down to
	// (inclusive) a_node: the pose of a_node in a_top's frame. Returns
	// false when a_top is not an ancestor of a_node.
	static bool ComposeChain(RE::NiAVObject* a_node, RE::NiAVObject* a_top,
		RE::NiMatrix3& a_outRot, RE::NiPoint3& a_outPos)
	{
		RE::NiMatrix3 rot;
		rot.MakeIdentity();
		RE::NiPoint3 pos{ 0.0f, 0.0f, 0.0f };

		auto* node = a_node;
		while (node && node != a_top) {
			// prepend node->local:  chain = local ∘ chain
			pos = {
				node->local.translate.x + (node->local.rotate.entry[0].pt[0] * pos.x + node->local.rotate.entry[0].pt[1] * pos.y + node->local.rotate.entry[0].pt[2] * pos.z),
				node->local.translate.y + (node->local.rotate.entry[1].pt[0] * pos.x + node->local.rotate.entry[1].pt[1] * pos.y + node->local.rotate.entry[1].pt[2] * pos.z),
				node->local.translate.z + (node->local.rotate.entry[2].pt[0] * pos.x + node->local.rotate.entry[2].pt[1] * pos.y + node->local.rotate.entry[2].pt[2] * pos.z)
			};
			rot = Mul(node->local.rotate, rot);
			node = node->parent;
		}
		if (node != a_top) return false;

		a_outRot = rot;
		a_outPos = pos;
		return true;
	}

	// VISIBLE hand pose (everything applied, including our own bone) in
	// the FP root's frame. Used for the "before" capture so the blend
	// starts from exactly what is on screen — even when a previous
	// combo's blend is still mid-decay. Returns false when the FP rig
	// isn't available (3rd person, power armor rig without RArm_Hand,
	// mid cell transition) or the values are non-finite.
	static bool ReadHandPoseInRootFrame(RE::PlayerCharacter* a_player,
		RE::NiPoint3& a_outPos, RE::NiMatrix3& a_outRot)
	{
		auto* fpRoot = a_player ? a_player->Get3D(true) : nullptr;
		auto* hand = FindHand(fpRoot);
		if (!hand) return false;

		const RE::NiMatrix3 rootRotT = Transpose(fpRoot->world.rotate);
		const RE::NiPoint3 rel{
			hand->world.translate.x - fpRoot->world.translate.x,
			hand->world.translate.y - fpRoot->world.translate.y,
			hand->world.translate.z - fpRoot->world.translate.z
		};
		a_outPos = MulP(rootRotT, rel);
		a_outRot = Mul(rootRotT, hand->world.rotate);

		return std::isfinite(a_outPos.x) && std::isfinite(a_outPos.y) && std::isfinite(a_outPos.z);
	}

	// Abort the blend entirely (bad capture, unloading rig, cell move).
	static void CancelBlend()
	{
		s_captureValid = false;
		s_timer        = 0.0f;
		s_duration     = 0.0f;
	}

	// Step 1 — TriggerGunBashAction, right before the graph interrupt.
	// Records the VISIBLE pose (including a still-decaying previous
	// blend's offset) so the new blend starts from exactly what is on
	// screen.
	static void CaptureBeforeReset(RE::PlayerCharacter* a_player)
	{
		s_captureValid = ReadHandPoseInRootFrame(a_player, s_capPos, s_capRot);
	}

	// Step 2 — TriggerGunBashAction, after the reset ran and the
	// follow-up bash was requested: start the blend immediately. No
	// delta is measured here — Apply re-derives it fresh every frame
	// against the live animation (see the namespace comment for why a
	// frozen one-shot delta produced garbage poses).
	static void ArmAfterReset(float a_blendTime)
	{
		if (!s_captureValid) return;
		s_duration = std::max(a_blendTime, 0.01f);
		s_timer    = s_duration;
		logger::info("[GunBash] Blend started — {:.2f}s", s_duration);
	}

	// Kept for the trigger fallback path: a synthetic tap that failed
	// means no follow-up bash is coming, so nothing needs masking.
	static void CancelPending() { CancelBlend(); }

	// Per-frame decay clock (runs from the bash block, before Apply).
	static void Tick(float a_delta)
	{
		if (s_timer > 0.0f) {
			s_timer -= a_delta;
			if (s_timer <= 0.0f) {
				s_timer = 0.0f;
				s_captureValid = false;  // capture consumed
			}
		}
	}

	static void ResetState()
	{
		CancelPending();
		s_timer    = 0.0f;
		s_duration = 0.0f;
		// The cached bone may belong to an unloading skeleton; drop the
		// pointer (Apply revalidates by name) and let s_boneDirty make
		// the next Apply restore the identity if the rig survives.
		s_bone = nullptr;
	}

	// Find the spine anchor: the highest candidate that is an ancestor
	// of the hand (so the rigid correction carries the whole arm rig).
	static RE::NiNode* FindAnchor(RE::NiAVObject* a_fpRoot, RE::NiAVObject* a_hand)
	{
		for (int i = 0; kAnchorNames[i]; ++i) {
			if (auto* cand = a_fpRoot->GetObjectByName(kAnchorNames[i])) {
				if (auto* candNode = cand->IsNode()) {
					if (candNode->GetObjectByName(a_hand->name))
						return candNode;
				}
			}
		}
		return nullptr;
	}

	// Get (or crash-safe insert) the blend bone above the spine anchor.
	// Same insertion pattern as GetOrInsertInertiaBone: pre-detach the
	// child via the NiPointer overload so AttachChild reuses the freed
	// slot without growing the parent's child array.
	static RE::NiNode* EnsureBone(RE::NiAVObject* a_fpRoot, RE::NiAVObject* a_hand)
	{
		static const RE::BSFixedString kName{ kBashBoneName };

		// Cached pointer still wired into this skeleton?
		if (s_bone) {
			if (a_fpRoot->GetObjectByName(kName) == s_bone && s_bone->GetObjectByName(a_hand->name))
				return s_bone;
			s_bone = nullptr;
		}

		// A node by our name already exists (e.g. re-entry after Reset
		// dropped the cache): adopt it when the hand hangs under it.
		if (auto* existing = a_fpRoot->GetObjectByName(kName)) {
			if (auto* existingNode = existing->IsNode()) {
				if (existingNode->GetObjectByName(a_hand->name)) {
					s_bone = existingNode;
					return s_bone;
				}
				// Structure mismatch — detach the orphan and reinsert.
				if (auto* staleParent = existingNode->parent) {
					staleParent->DetachChild(existingNode);
				}
			}
		}

		auto* anchor = FindAnchor(a_fpRoot, a_hand);
		RE::NiNode* parent = anchor ? anchor->parent : nullptr;
		if (!anchor || !parent) return nullptr;

		auto* inserted = new RE::NiNode(1);
		if (!inserted) return nullptr;
		inserted->name = kName;
		inserted->local.MakeIdentity();

		RE::NiPointer<RE::NiAVObject> anchorRef;
		parent->DetachChild(anchor, anchorRef);  // detach + keep alive
		anchor->parent = nullptr;                // prevent double-detach in AttachChild

		parent->AttachChild(inserted, true);     // reuses freed slot
		inserted->parent = parent;
		inserted->AttachChild(anchor, true);

		s_bone = inserted;
		logger::info("[GunBash] Inserted blend bone '{}' above '{}' (parent '{}')",
			kBashBoneName, anchor->name.c_str(), parent->name.c_str());
		return s_bone;
	}

	// Solve and write the blend bone's local transform for this frame.
	// Runs every frame from Update's tail (after the engine's world
	// update, alongside the inertia ApplyOffset — the engine consumes
	// these locals when building the render transforms).
	//
	// Let P = the bone's parent world transform, K = the composed
	// animation-authored chain from the bone down to the hand. The
	// clean animated hand pose is  H_anim = P ∘ K  (our bone treated as
	// identity — the compose stops below it). The captured pose is
	// re-expressed in world via the CURRENT root transform (so it
	// survives the player turning), the delta  capture ∘ H_animᵀ  is
	// taken FRESH THIS FRAME, and the target is the geodesic lerp:
	//     H_tgt.R = R(axis, w·angle) · H_anim.R      (axis, angle from the fresh delta)
	//     H_tgt.T = H_anim.T + w·(capture.T − H_anim.T)
	// Then the bone local B satisfying  P ∘ B ∘ K = H_tgt  is
	//     B.R = P.Rᵀ · H_tgt.R · K.Rᵀ
	//     B.T = P.Rᵀ · (H_tgt.T − P.T) − B.R·K.T
	// At w=0 this collapses to the identity exactly; at w=1 the hand
	// sits exactly on the captured pose, whatever the animation is
	// doing underneath.
	static void Apply(RE::PlayerCharacter* a_player)
	{
		if (s_timer <= 0.0f || s_duration <= 0.0f || !s_captureValid) {
			// Blend over — restore the identity once so no stale offset
			// stays frozen on the hand.
			if (s_boneDirty && s_bone) {
				s_bone->local.MakeIdentity();
				s_boneDirty = false;
			}
			return;
		}

		auto* fpRoot = a_player ? a_player->Get3D(true) : nullptr;
		auto* hand = FindHand(fpRoot);
		if (!hand || !hand->parent) return;

		auto* bone = EnsureBone(fpRoot, hand);
		if (!bone || !bone->parent) return;

		RE::NiMatrix3 chainRot;
		RE::NiPoint3  chainPos;
		if (!ComposeChain(hand, bone, chainRot, chainPos)) return;

		auto* p = bone->parent;
		const RE::NiMatrix3& pRot = p->world.rotate;
		const RE::NiMatrix3 pRotT = Transpose(pRot);

		// Clean animated hand pose in world.
		const RE::NiMatrix3 animRot = Mul(pRot, chainRot);
		const RE::NiPoint3  pk = MulP(pRot, chainPos);
		const RE::NiPoint3  animPos{
			p->world.translate.x + pk.x,
			p->world.translate.y + pk.y,
			p->world.translate.z + pk.z
		};

		// Captured pose, FP-root frame -> world (current root transform).
		const RE::NiMatrix3& rootRot = fpRoot->world.rotate;
		const RE::NiMatrix3 capRotW = Mul(rootRot, s_capRot);
		const RE::NiPoint3  capOff = MulP(rootRot, s_capPos);
		const RE::NiPoint3  capPosW{
			fpRoot->world.translate.x + capOff.x,
			fpRoot->world.translate.y + capOff.y,
			fpRoot->world.translate.z + capOff.z
		};

		// Fresh delta for THIS frame.
		const RE::NiPoint3 dP{ capPosW.x - animPos.x, capPosW.y - animPos.y, capPosW.z - animPos.z };
		const RE::NiMatrix3 dR = Mul(capRotW, Transpose(animRot));
		const float mag = std::sqrt(dP.x * dP.x + dP.y * dP.y + dP.z * dP.z);
		const float trace = dR.entry[0].pt[0] + dR.entry[1].pt[1] + dR.entry[2].pt[2];
		const float cosAng = std::clamp((trace - 1.0f) * 0.5f, -1.0f, 1.0f);

		// Pathological gap (bone re-parented, cell transition mid blend):
		// dropping the blend beats flinging the viewmodel. A large-but-
		// real gap (the trigger frame's fast-forwarded draw pose sits
		// ~34 units / ~134 deg away) stays IN: the lerp path is always
		// between two genuine poses, so big is fine — only absurd or
		// axis-degenerate (angle -> 180 deg) gaps bail.
		if (mag > 60.0f || cosAng < -0.94f) {  // ~160 degrees
			logger::warn("[GunBash] Blend dropped mid-flight (posDelta={:.1f}, cosAng={:.2f})", mag, cosAng);
			CancelBlend();
			if (s_boneDirty) {
				bone->local.MakeIdentity();
				s_boneDirty = false;
			}
			return;
		}

		// Quadratic ease-out: full pull toward the captured pose at the
		// snap, decaying with zero-ish velocity into the live animation.
		const float t = s_timer / s_duration;
		const float w = t * t;

		// Axis-angle of the fresh delta (skew-symmetric part of dR:
		// (R32-R23, R13-R31, R21-R12) = 2*sin(angle)*axis; the 160-degree
		// bail above keeps sin(angle) well away from zero at large angles).
		const float angle = std::acos(cosAng);
		RE::NiPoint3 axis{
			dR.entry[2].pt[1] - dR.entry[1].pt[2],
			dR.entry[0].pt[2] - dR.entry[2].pt[0],
			dR.entry[1].pt[0] - dR.entry[0].pt[1]
		};
		const float axisLen = std::sqrt(axis.x * axis.x + axis.y * axis.y + axis.z * axis.z);
		RE::NiMatrix3 dRotW;
		if (axisLen > 1.0e-6f && angle > 1.0e-4f) {
			axis.x /= axisLen;
			axis.y /= axisLen;
			axis.z /= axisLen;
			dRotW = FromAxisAngle(axis, angle * w);
		} else {
			dRotW.MakeIdentity();
		}

		const RE::NiMatrix3 tgtRot = Mul(dRotW, animRot);
		const RE::NiPoint3  tgtPos{
			animPos.x + dP.x * w,
			animPos.y + dP.y * w,
			animPos.z + dP.z * w
		};

		// Solve the bone local.
		const RE::NiMatrix3 boneRot = Mul(Mul(pRotT, tgtRot), Transpose(chainRot));
		const RE::NiPoint3 relTgt{
			tgtPos.x - p->world.translate.x,
			tgtPos.y - p->world.translate.y,
			tgtPos.z - p->world.translate.z
		};
		const RE::NiPoint3 relLocal = MulP(pRotT, relTgt);
		const RE::NiPoint3 rk = MulP(boneRot, chainPos);
		const RE::NiPoint3 boneT{ relLocal.x - rk.x, relLocal.y - rk.y, relLocal.z - rk.z };

		if (!std::isfinite(boneT.x) || !std::isfinite(boneT.y) || !std::isfinite(boneT.z))
			return;

		bone->local.rotate = boneRot;
		bone->local.translate = boneT;
		s_boneDirty = true;
	}
}

// Repeatable Gun Bash — fire a follow-up bash while the previous bash's
// animation is still playing.
//
// V1 FINDINGS (2026-07-22 11:14 in-game session): with the previous bash
// still in flight, RunActionOnActor(kActionMelee) was REFUSED on every
// single attempt, and the fallback synthetic Melee tap through the real
// MeleeThrowHandler dispatched fine but produced no bash either — the
// handler funnels into the same conditioned action, and the action layer
// will not start a melee attack while the actor still reads as attacking
// (meleeAttackState was 2 at every HitFrame in the log). Same failure
// class as kActionFireAuto refusing on an empty magazine: the action's
// conditions read actor state we consider stale.
//
// V2 FINDINGS (2026-07-22 11:59 session): clearing the attack bookkeeping
// (attackStop event + Havok kIsAttacking/kIAttackState + the
// meleeAttackState bitfield, all confirmed written: "meleeState 3->0" in
// every log line) did NOT change the refusal — kActionMelee was still
// refused on every attempt, and the synthetic tap still produced nothing.
// Conclusion: the action's conditions read the LIVE behavior graph state
// (the graph is still inside the bash attack sub-graph), not the actor's
// bookkeeping fields. No variable write can fix that; the graph itself
// must leave the attack state before a new melee action can start.
//
// V3 (this version): force the graph out first, with the one interrupt
// PROVEN (across every Fire on Empty stop this week) to kill any
// in-flight state same-frame: InitializeToBaseState via RunActionOnActor
// + a big UpdateAnimation step to fast-forward the mandatory re-draw the
// reset causes. From the resulting clean idle state, kActionMelee is a
// bog-standard bash request — the same call the engine makes from idle
// on every vanilla bash.
//
// Order of attempts (cheapest first):
//   1. kActionMelee directly — covers the bash-already-ended chain case
//      (falling-edge refire) where no interrupt is needed at all.
//   2. Hard graph interrupt (attackStop + Havok clears +
//      InitializeToBaseState + fast-forward), then kActionMelee again.
//   3. Synthetic Melee tap as the last resort.
// The visual cost of the reset (viewmodel snap) is masked by the new
// bash animation starting the same frame — same masking that made the
// FOE hard-stop acceptable for ADS dry-fires.
static bool TriggerGunBashAction(RE::PlayerCharacter* a_player)
{
	if (!a_player || !a_player->currentProcess) return false;

	auto* dom = RE::BGSDefaultObjectManager::GetSingleton();
	auto* meleeAction = dom ? dom->GetDefaultObject<RE::BGSAction>(RE::DEFAULT_OBJECT::kActionMelee) : nullptr;
	if (!meleeAction) {
		logger::warn("[GunBash] ActionMelee default object missing");
		return false;
	}

	const auto meleeStateBefore = static_cast<std::uint32_t>(a_player->meleeAttackState);

	// -- attempt 1: direct request (works when the graph is already idle) --
	{
		RE::TESActionData action(RE::ActionInput::ACTIONPRIORITY::kTry, a_player, meleeAction);
		if (RE::BGSAnimationSystemUtils::RunActionOnActor(a_player, action)) {
			logger::info("[GunBash] Follow-up bash via kActionMelee, no interrupt needed (meleeState={})",
				meleeStateBefore);
			return true;
		}
	}

	// -- attempt 2: hard graph interrupt, then re-request --
	// V2 proved the refusal comes from live graph state, so yank the graph
	// to its base state (the FOE-proven same-frame interrupt) and retry.

	// The interrupt snaps the viewmodel pose; record the current hand pose
	// so the decaying blend offset can mask the discontinuity (BashBlend).
	if (Settings::GetSingleton()->bashComboBlendEnabled) {
		BashBlend::CaptureBeforeReset(a_player);
	}

	static const RE::BSFixedString kEvtAttackStop{ "attackStop" };
	a_player->NotifyAnimationGraphImpl(kEvtAttackStop);
	HavokVar::SetBool(a_player, HavokVar::kIsAttacking, false);
	HavokVar::SetInt(a_player, HavokVar::kIAttackState, 0);

	bool resetRan = false;
	if (auto* resetAction = RE::BGSAnimationSystemUtils::GetDefaultObjectForActionInitializeToBaseState()) {
		RE::TESActionData reset(RE::ActionInput::ACTIONPRIORITY::kTry, a_player, resetAction);
		resetRan = RE::BGSAnimationSystemUtils::RunActionOnActor(a_player, reset);
	}

	// Fast-forward the reset's mandatory re-draw so the follow-up bash can
	// start immediately (and its equip sounds stay silent) — the exact
	// mechanism the FOE hard-stop uses.
	g_suppressEquipSounds.store(true, std::memory_order_relaxed);
	a_player->UpdateAnimation(1000.0f);
	g_suppressEquipSounds.store(false, std::memory_order_relaxed);

	{
		RE::TESActionData action(RE::ActionInput::ACTIONPRIORITY::kTry, a_player, meleeAction);
		if (RE::BGSAnimationSystemUtils::RunActionOnActor(a_player, action)) {
			logger::info("[GunBash] Follow-up bash via kActionMelee after graph reset (resetRan={}, meleeState {}->{})",
				resetRan, meleeStateBefore, static_cast<std::uint32_t>(a_player->meleeAttackState));
			BashBlend::ArmAfterReset(Settings::GetSingleton()->bashComboBlendTime);
			return true;
		}
	}

	// -- attempt 3: action refused even from base state — input-level tap --
	const bool tapped = MeleeInput::SimulateTap();
	logger::info("[GunBash] kActionMelee refused even after graph reset (resetRan={}); synthetic tap -> {} (meleeState {}->{})",
		resetRan, tapped, meleeStateBefore, static_cast<std::uint32_t>(a_player->meleeAttackState));
	if (tapped) {
		BashBlend::ArmAfterReset(Settings::GetSingleton()->bashComboBlendTime);
	} else {
		BashBlend::CancelPending();
	}
	return tapped;
}

// Last-resort unstick: reset the behavior graph to its base state via the
// InitializeToBaseState action (NAF's proven recovery for a stuck graph).
// Only called when, ~0.75s after the stop above, gunState still reads as
// firing on an empty magazine — i.e. every stop stimulus was ignored.
// May cause a brief visual reset of the viewmodel; preferable to an
// attack state stuck until weapon swap (engine-side gunState wedged at 7,
// WeaponFire annotations looping, Fire on Empty unable to re-arm).
static void ForceGraphBaseStateReset(RE::PlayerCharacter* a_player)
{
	if (!a_player) return;

	auto* resetAction = RE::BGSAnimationSystemUtils::GetDefaultObjectForActionInitializeToBaseState();
	if (!resetAction) {
		logger::warn("[FireOnEmpty] InitializeToBaseState default object missing — cannot force reset");
		return;
	}

	// kTry priority mirrors NAF's SmartIdle stop path exactly.
	RE::TESActionData action(RE::ActionInput::ACTIONPRIORITY::kTry, a_player, resetAction);
	const bool ran = RE::BGSAnimationSystemUtils::RunActionOnActor(a_player, action);

	static const RE::BSFixedString kEvtAttackStop{ "attackStop" };
	a_player->NotifyAnimationGraphImpl(kEvtAttackStop);

	// Fast-forward the reset's mandatory re-draw (equip sounds muted),
	// exactly like the hard stop and the bash trigger do. Before this,
	// the net's reset was the ONE reset that played its re-draw visibly —
	// the "fast equip after every few dry fires" report (2026-07-22).
	g_suppressEquipSounds.store(true, std::memory_order_relaxed);
	a_player->UpdateAnimation(1000.0f);
	g_suppressEquipSounds.store(false, std::memory_order_relaxed);

	logger::warn("[FireOnEmpty] Graph stuck in fire loop after stop — forced base-state reset (ran={})", ran);
}

// Bone names to try in order for the FP skeleton pivot node
static constexpr const char* kInsertedBoneName = "FPInertia_Node";

// Pivot 0: Chest/Spine — highest in the hierarchy, most sway
static constexpr const char* kBoneNames_Spine[] = {
	"Spine2", "Spine1b", "Chest", "Spine1", "COM", "Camera", nullptr
};
// Pivot 1: Right hand — mid-level
static constexpr const char* kBoneNames_Hand[] = {
	"RArm_Hand", "RArm_ForeArm3", "RArm_ForeArm2", nullptr
};
// Pivot 2: Weapon — tightest, least sway.
// Must NOT use literal Weapon/WeaponNode bones because they're below the
// arm bones in the hierarchy; inserting there would only transform the
// weapon mesh, not the arms. RArm_Hand is the lowest bone that still
// encompasses both the hand rendering and the weapon attachment.
static constexpr const char* kBoneNames_Weapon[] = {
	"RArm_Hand", "RArm_ForeArm3", "Spine2", nullptr
};

static const char* const* GetBoneListForPivot(int pivot)
{
	switch (pivot) {
	case 1:  return kBoneNames_Hand;
	case 2:  return kBoneNames_Weapon;
	default: return kBoneNames_Spine;
	}
}

// Skeleton reference (bind) pose local translates.
// Source: F4_skeleton_transforms.txt (3ds Max scene export of HumanRig.max).
// These are the bone-local positions in the skeleton's rest pose before any
// animation. Used when useBindPosePivot is enabled to override the live
// animated position with the skeleton's authored rest-pose value.
// No coordinate conversion is needed — 3ds Max local transforms map directly
// to NiNode::local.translate (both Z-up).
struct BoneRefPose { const char* name; float x, y, z; };
static constexpr BoneRefPose kSkeletonRefPose[] = {
	{ "Spine2",         8.70466f,   0.0f,        0.0f       },
	{ "Spine1b",        8.70466f,   0.0f,        0.0f       },  // alias of Spine2
	{ "Chest",          9.95628f,   0.0f,        0.0f       },
	{ "Spine1",         3.79199f,  -0.00294795f, 0.0f       },
	{ "COM",            0.0f,       0.0f,       68.9113f    },
	{ "Camera",         0.0f,       0.0328195f, 120.483f    },
	{ "RArm_Hand",      6.15264f,   0.0f,        0.0f       },
	{ "RArm_ForeArm3",  6.15263f,   0.0f,        0.0f       },
	{ "RArm_ForeArm2",  6.15264f,   0.0f,        0.0f       },
	{ nullptr,          0.0f,       0.0f,        0.0f       },
};

static RE::NiPoint3 GetRefPoseTranslate(const char* boneName)
{
	for (int i = 0; kSkeletonRefPose[i].name; ++i) {
		if (_stricmp(kSkeletonRefPose[i].name, boneName) == 0) {
			return { kSkeletonRefPose[i].x, kSkeletonRefPose[i].y, kSkeletonRefPose[i].z };
		}
	}
	return { 0.0f, 0.0f, 0.0f };
}

// ============================================================
// Spring math helpers
// ============================================================
namespace
{
	// ---- Analytically exact spring integration ----
	// Solves m*x'' + d*x' + k*(x - target) = 0 for one time step.
	// Fully framerate-independent: behavior is identical at any fps.
	// See: Erin Catto, "Numerical Methods" GDC 2015.
	float SpringStep(float pos, float& vel, float target, float stiffness, float damping, float mass, float dt)
	{
		// Clamp dt to prevent runaway on frame hitches (>200ms is pathological)
		dt = std::min(dt, 0.2f);

		float k  = stiffness / mass;          // k/m
		float d  = damping / (2.0f * mass);   // d/(2m) = ζω₀

		float w0 = std::sqrtf(k);             // natural frequency ω₀ = sqrt(k/m)
		float zeta = d / w0;                  // damping ratio ζ = d/(2m) / ω₀

		float y0 = pos - target;              // displacement from target
		float v0 = vel;

		float new_pos, new_vel;

		if (zeta < 0.9999f) {
			// ---- Underdamped (ζ < 1) — oscillates, most springs in this mod ----
			float wd   = w0 * std::sqrtf(1.0f - zeta * zeta);  // damped frequency
			float edt  = std::expf(-zeta * w0 * dt);
			float c    = std::cosf(wd * dt);
			float s    = std::sinf(wd * dt);
			float s_wd = (wd > 1e-6f) ? s / wd : dt;           // sin(wd*dt)/wd, stable near wd→0

			new_pos = target + edt * (y0 * c + (v0 + zeta * w0 * y0) * s_wd);
			// Velocity: d/dt of above expression
			new_vel = edt * (v0 * c - (y0 * w0 * w0 + zeta * w0 * v0) * s_wd);

		} else if (zeta < 1.0001f) {
			// ---- Critically damped (ζ ≈ 1) — fastest settle without overshoot ----
			float edt = std::expf(-w0 * dt);
			float b   = v0 + w0 * y0;

			new_pos = target + edt * (y0 + b * dt);
			new_vel = edt * (b * (1.0f - w0 * dt) - w0 * y0);

		} else {
			// ---- Overdamped (ζ > 1) — sluggish, rarely used ----
			float sqz = std::sqrtf(zeta * zeta - 1.0f);
			float r1  = (-zeta + sqz) * w0;
			float r2  = (-zeta - sqz) * w0;
			float c1  = (v0 - r2 * y0) / (r1 - r2);
			float c2  = y0 - c1;
			float e1  = std::expf(r1 * dt);
			float e2  = std::expf(r2 * dt);

			new_pos = target + c1 * e1 + c2 * e2;
			new_vel = c1 * r1 * e1 + c2 * r2 * e2;
		}

		if (!std::isfinite(new_pos) || !std::isfinite(new_vel)) {
			vel = 0.0f;
			return target;
		}
		vel = new_vel;
		return new_pos;
	}

	// After clamping position, kill velocity that pushes past the clamp
	void ClampSpringAxis(float& pos, float& vel, float lo, float hi)
	{
		if (pos >= hi)  { pos = hi; if (vel > 0) vel = 0; }
		if (pos <= lo)  { pos = lo; if (vel < 0) vel = 0; }
	}

	RE::NiPoint3 SpringStep3(RE::NiPoint3& pos, RE::NiPoint3& vel, RE::NiPoint3 target,
		float stiffness, float damping, float mass, float dt)
	{
		RE::NiPoint3 result;
		result.x = SpringStep(pos.x, vel.x, target.x, stiffness, damping, mass, dt);
		result.y = SpringStep(pos.y, vel.y, target.y, stiffness, damping, mass, dt);
		result.z = SpringStep(pos.z, vel.z, target.z, stiffness, damping, mass, dt);
		return result;
	}

	float Clampf(float v, float lo, float hi) { return std::clamp(v, lo, hi); }

	float Lerp(float a, float b, float t) { return a + (b - a) * t; }
} // namespace

// ============================================================
// Weapon detection helpers
// ============================================================

// ============================================================
// Player state detection â€” ALL via direct ActorState bitfields
// ============================================================
//
// NEVER use GetGraphVariableImpl from the actor update hook thread.
// BSAnimationGraphVariableCache::GetGraphVariable uses the string hash
// as a direct array index. During save load, cell transitions, or graph
// rebuild, the cache is uninitialized/partial. The hash value (~800M-2B)
// is used as an array index â†’ OOB read â†’ crash.
//
// All state detection uses direct struct members instead:
//   gunState    â€” firing, ADS
//   moveMode    â€” sprinting
//   meleeAttackState â€” melee/bash
//   recoil      â€” reloading
//   flyState    â€” airborne
//   weaponState â€” weapon drawn
//   biped       â€” Power Armor
//
// Pattern sourced from UneducatedShooter, LighthousePapyrusExtender.

// ============================================================
// ADS transition helpers
// ============================================================

// Reads the weapon's base sighted-transition duration in seconds.
// Returns 0.0 for melee/unarmed (no rangedData) or if unavailable.
static float GetSightedTransitionSeconds(RE::TBO_InstanceData* idata)
{
	if (!idata) return 0.0f;
	auto* wid = static_cast<RE::TESObjectWEAP::InstanceData*>(idata);
	if (!wid || !wid->rangedData) return 0.0f;
	float t = wid->rangedData->sightedTransitionSeconds;
	// Guard against degenerate values
	return (t > 0.005f && t < 10.0f) ? t : 0.2f;
}

// Maps asymmetry [-1,1] so that the peak time is remapped:
//   asymmetry =  0 -> peak stays at peakPosition
//   asymmetry >0 -> peak shifts earlier (front-loaded)
//   asymmetry <0 -> peak shifts later (back-loaded)
static float RemapWithAsymmetry(float t, float peakPos, float asym)
{
	// Split t into before-peak and after-peak segments and stretch each
	if (t <= peakPos) {
		float newPeak = peakPos * (1.0f - asym * 0.5f);
		newPeak = std::clamp(newPeak, 0.01f, 0.99f);
		return (newPeak > 0.0f) ? (t / peakPos) * newPeak : 0.0f;
	} else {
		float newPeak = peakPos * (1.0f - asym * 0.5f);
		newPeak = std::clamp(newPeak, 0.01f, 0.99f);
		float rem = 1.0f - peakPos;
		float remNew = 1.0f - newPeak;
		return (rem > 0.0f) ? newPeak + ((t - peakPos) / rem) * remNew : newPeak;
	}
}

// Evaluates a bell-shaped envelope at normalized time t in [0,1],
// returning a scalar in [0,1] that peaks at 1.0.
static float EvaluateTransitionEnvelope(const ADSTransitionSettings& cfg, float t)
{
	// Remap t through peak position and asymmetry
	float tp = RemapWithAsymmetry(t, cfg.peakPosition, cfg.asymmetry);

	float env = 0.0f;
	static constexpr float kPi = 3.14159265358979323846f;
	switch (cfg.curveType) {
	case 1:  // EaseInOut: cubic hermite bell
	{
		// Build a symmetric bell with easeInOut on each half
		float half = (tp < 0.5f) ? tp * 2.0f : (1.0f - tp) * 2.0f;
		env = half * half * (3.0f - 2.0f * half);
		break;
	}
	case 2:  // Bounce: rises, overshoots slightly at peak, settles back to 0
	{
		float s = std::sinf(kPi * tp);
		float bounce = std::sinf(kPi * tp * 2.5f) * 0.15f * (1.0f - tp);
		env = std::clamp(s + bounce, -0.3f, 1.3f);
		break;
	}
	case 3:  // Overshoot: peaks above 1 then returns below 0 before settling
	{
		float s = std::sinf(kPi * tp);
		float over = std::sinf(kPi * tp * 2.0f) * 0.3f;
		env = s + over;
		break;
	}
	default: // 0 = Sine
		env = std::sinf(kPi * tp);
		break;
	}
	return env;
}

bool Inertia::InertiaManager::IsInPowerArmor(RE::PlayerCharacter* player) const
{
	if (!player) return false;
	if (player->biped) {
		constexpr std::uint32_t kPAFrameSlot = 40;
		if (player->biped->object[kPAFrameSlot].parent.object != nullptr) return true;
	}
	return false;
}

bool Inertia::InertiaManager::IsADS(RE::PlayerCamera* camera) const
{
	// Camera state kIronSights works on some setups but F4 often stays in
	// kFirstPerson during ADS. gunState is the reliable source:
	//   6 = sighted/ADS, 8 = firing while in ADS (UneducatedShooter pattern).
	auto* player = RE::PlayerCharacter::GetSingleton();
	if (player) {
		auto gs = static_cast<std::uint32_t>(player->gunState);
		if (gs == 6 || gs == 8) return true;
	}
	// Fallback: camera state (in case some mod changes gunState behavior)
	if (camera && camera->currentState) {
		if (camera->currentState->id.get() == RE::CameraStates::kIronSights) return true;
	}
	return false;
}

bool Inertia::InertiaManager::IsScoped(RE::PlayerCamera* camera) const
{
	if (!IsADS(camera)) return false;
	auto* player = RE::PlayerCharacter::GetSingleton();
	if (!player) return false;
	auto* idata = GetEquippedWeaponInstanceData(player);
	if (!idata) return false;
	auto* wid = static_cast<RE::TESObjectWEAP::InstanceData*>(idata);
	if (!wid || !wid->zoomData) return false;
	// All ADS weapons have zoomData (iron sights included).
	// Scoped weapons have a non-zero overlay form ID (the scope overlay texture).
	return wid->zoomData->zoomData.overlay != 0;
}

bool Inertia::InertiaManager::IsFiring(RE::PlayerCharacter*) const
{
	// Driven by weaponFire animation event (set by AnimEventSink::ProcessEvent).
	// The flag is consumed each frame in Update() after being read here.
	return animEventSink.firedThisFrame.load(std::memory_order_relaxed);
}

bool Inertia::InertiaManager::IsReloading(RE::PlayerCharacter* player) const
{
	if (!player) return false;
	// No reliable non-graph-variable bitfield for reload state in ActorState.
	// The recoil field (2 bits) tracks weapon recoil animation, not reloading.
	// We use recoil != 0 as a rough heuristic â€” it correlates with weapon activity
	// that includes the tail end of firing and may overlap with reload transitions.
	// This is conservative: worst case, inertia blend-down triggers slightly too
	// broadly during weapon animations, which is barely noticeable.
	// No clean bitfield for reload in ActorState; return false to avoid false positives.
	return false;
}

bool Inertia::InertiaManager::IsInMeleeAction(RE::PlayerCharacter* player) const
{
	if (!player) return false;
	// meleeAttackState: 3-bit field on ActorState â€” non-zero during bash/melee.
	return player->meleeAttackState != 0;
}

// ============================================================
// Animation graph event sink
// ============================================================
RE::BSEventNotifyControl Inertia::AnimEventSink::ProcessEvent(
	const RE::BSAnimationGraphEvent& a_event,
	RE::BSTEventSource<RE::BSAnimationGraphEvent>*)
{
	auto* player = RE::PlayerCharacter::GetSingleton();
	std::uint32_t gs = player ? static_cast<std::uint32_t>(player->gunState) : 99;

	// Engine quirk (verified): the player's animation graph emits `weaponFire`
	// annotations from background idle/transition loops, especially right
	// after a save load (~7-8 Hz at gunState=0) and during ADS-but-not-firing
	// transitions (gunState=6 / kSighted).  These are NOT real fire events.
	// Other plugins (FPCameraOverhaul, etc.) see the same noise because
	// it's the engine emitting them from compiled .hkx annotations.
	//
	// CommonLibF4's RE::GUN_STATE enum:
	//   0 kDrawn   1 kRelaxed  2 kBlocked    3 kAlert
	//   4 kReloading  5 kThrowing  6 kSighted   <- ADS hold (NOT firing)
	//   7 kFire        8 kFireSighted          <- THE ACTUAL FIRING STATES
	//
	// Anything outside {kFire, kFireSighted} is noise from our perspective,
	// so we drop it at the sink.  Every downstream consumer of
	// `firedThisFrame` already gates on these same values, but filtering at
	// the source means we also stop polluting the trace log.
	const bool isWeaponFire = (a_event.animEvent == "weaponFire" || a_event.animEvent == "WeaponFire");
	if (isWeaponFire && !GunStateLocal::IsFiringGunState(gs)) {
		// Drop entirely — no log spam, no firedThisFrame.
		return RE::BSEventNotifyControl::kContinue;
	}

	const char* arg = a_event.argument.c_str();
	if (arg && arg[0])
		logger::trace("[AnimEvent] {} arg=\"{}\" gunState={}", a_event.animEvent.c_str(), arg, gs);
	else
		logger::trace("[AnimEvent] {} gunState={}", a_event.animEvent.c_str(), gs);

	if (isWeaponFire) {
		firedThisFrame.store(true, std::memory_order_relaxed);
		logger::trace("[AnimEvent] weaponFire");
	}
	if (a_event.animEvent == "reloadComplete" || a_event.animEvent == "ReloadComplete" ||
	    a_event.animEvent == "reloadStart"    || a_event.animEvent == "ReloadStart") {
		reloadingThisFrame.store(true, std::memory_order_relaxed);
	}
	if (a_event.animEvent == "reloadStart" || a_event.animEvent == "ReloadStart" ||
	    a_event.animEvent == "reloadStateEnter") {
		reloadStartThisFrame.store(true, std::memory_order_relaxed);
		logger::trace("[AnimEvent] reloadStart (event={})", a_event.animEvent.c_str());
	}
	if (a_event.animEvent == "reloadEnd" || a_event.animEvent == "ReloadEnd" ||
	    a_event.animEvent == "reloadStateExit") {
		reloadEndThisFrame.store(true, std::memory_order_relaxed);
		logger::trace("[AnimEvent] reloadEnd (event={})", a_event.animEvent.c_str());
	}
	if (a_event.animEvent == "reloadComplete" || a_event.animEvent == "ReloadComplete") {
		reloadCompleteThisFrame.store(true, std::memory_order_relaxed);
		logger::trace("[AnimEvent] reloadComplete");
	}
	if (a_event.animEvent == "InitiateStart" || a_event.animEvent == "initiateStart") {
		initiateStartThisFrame.store(true, std::memory_order_relaxed);
		logger::trace("[AnimEvent] InitiateStart");
	}
	if (a_event.animEvent == "weaponSheathe" || a_event.animEvent == "BeginWeaponSheathe") {
		sheatheStartedThisFrame.store(true, std::memory_order_relaxed);
		logger::trace("[AnimEvent] weaponSheathe (event={})", a_event.animEvent.c_str());
	}
	if (a_event.animEvent == "BeginWeaponDraw") {
		beginWeaponDrawThisFrame.store(true, std::memory_order_relaxed);
		logger::trace("[AnimEvent] BeginWeaponDraw");
	}
	if (a_event.animEvent == "SightedStateExit") {
		sightedExitThisFrame.store(true, std::memory_order_relaxed);
	}
	// Repeatable Gun Bash: HitFrame is the engine's own melee-impact
	// annotation (a HitFrameHandler functor is registered for this exact
	// name engine-side). Consumed by the bash combo block in Update.
	if (a_event.animEvent == "HitFrame" || a_event.animEvent == "hitFrame") {
		hitFrameThisFrame.store(true, std::memory_order_relaxed);
		logger::trace("[AnimEvent] HitFrame (meleeState={})",
			player ? static_cast<std::uint32_t>(player->meleeAttackState) : 99);
	}
	if (a_event.animEvent == "sneakStateEnter" || a_event.animEvent == "sneakStart" ||
	    a_event.animEvent == "SneakStart" || a_event.animEvent == "tagSneakStart" ||
	    a_event.animEvent == "tagCrouchStart" || a_event.animEvent == "crouchStart" ||
	    a_event.animEvent == "tagCrouchEnter") {
		sneakStartedThisFrame.store(true, std::memory_order_relaxed);
	}
	if (a_event.animEvent == "sneakStateExit" || a_event.animEvent == "sneakStop" ||
	    a_event.animEvent == "SneakStop" || a_event.animEvent == "tagSneakStop" ||
	    a_event.animEvent == "tagCrouchStop" || a_event.animEvent == "crouchStop" ||
	    a_event.animEvent == "tagCrouchExit") {
		sneakStoppedThisFrame.store(true, std::memory_order_relaxed);
	}
	return RE::BSEventNotifyControl::kContinue;
}

void Inertia::InertiaManager::RegisterAnimEventSink()
{
	if (animEventSink.registered) return;
	auto* player = RE::PlayerCharacter::GetSingleton();
	if (!player) return;

	RE::BSScrapArray<RE::BSTEventSource<RE::BSAnimationGraphEvent>*> sources;
	if (RE::BGSAnimationSystemUtils::GetEventSourcePointersFromGraph(player, sources)) {
		for (auto* src : sources) {
			if (src) src->RegisterSink(&animEventSink);
		}
		animEventSink.registered = true;
		logger::info("[FPGunplayOverhaul] Registered animation event sink ({} sources)", sources.size());
	}
}

void Inertia::InertiaManager::UnregisterAnimEventSink()
{
	if (!animEventSink.registered) return;
	auto* player = RE::PlayerCharacter::GetSingleton();
	if (!player) return;

	RE::BSScrapArray<RE::BSTEventSource<RE::BSAnimationGraphEvent>*> sources;
	if (RE::BGSAnimationSystemUtils::GetEventSourcePointersFromGraph(player, sources)) {
		for (auto* src : sources) {
			if (src) src->UnregisterSink(&animEventSink);
		}
	}
	animEventSink.registered = false;
}

RE::TESBoundObject* Inertia::InertiaManager::GetEquippedWeaponBaseStatic(RE::PlayerCharacter* player)
{
	if (!player || !player->currentProcess || !player->currentProcess->middleHigh) return nullptr;
	auto* mh = player->currentProcess->middleHigh;
	RE::BSAutoLock lock{ mh->equippedItemsLock };
	for (auto& equipped : mh->equippedItems) {
		auto* form = equipped.item.object;
		if (form && form->GetFormID() != 0 && form->formType == RE::ENUM_FORM_ID::kWEAP) {
			return static_cast<RE::TESBoundObject*>(form);
		}
	}
	return nullptr;
}

std::string Inertia::InertiaManager::GetEquippedWeaponEditorIDStatic(RE::PlayerCharacter* player)
{
	auto* base = GetEquippedWeaponBaseStatic(player);
	if (!base) return {};
	const char* eid = base->GetFormEditorID();
	if (eid && eid[0]) return eid;
	return std::format("0x{:08X}", base->GetFormID());
}

void Inertia::InertiaManager::PushEvent(const char* desc)
{
	auto& evt = eventLog[eventLogHead];
	evt.timestamp = elapsedTime;
	strncpy_s(evt.description, desc, _TRUNCATE);
	eventLogHead = (eventLogHead + 1) % kMaxDebugEvents;
	if (eventLogCount < kMaxDebugEvents) eventLogCount++;
}

void Inertia::InertiaManager::FillDebugSnapshot(DebugSnapshot& snap)
{
	auto* player = RE::PlayerCharacter::GetSingleton();
	auto* camera = RE::PlayerCamera::GetSingleton();

	snap.inFirstPerson    = isInFirstPerson;
	snap.isSprinting      = isSprinting;
	snap.isSuperSprinting = superSprintActive;

	if (player) {
		snap.weaponDrawn     = player->currentProcess ? player->GetWeaponMagicDrawn() : false;
		snap.isInPowerArmor  = IsInPowerArmor(player);
		snap.isFiring        = recentlyFiredTimer > 0.0f;  // Held for ~0.3s so debug can see it
		snap.isReloading     = isCurrentlyReloading;
		snap.isInMeleeAction = IsInMeleeAction(player);
		auto weap = FetchEquippedWeapon(player);
		snap.detectedWeaponType = DetectWeaponType(weap, snap.isInPowerArmor);
		snap.equippedEditorID = weap.editorID;

		if (weap.base) {
			auto* fn = weap.base->As<RE::TESFullName>();
			if (fn) snap.equippedDisplayName = fn->GetFullName();

			auto* weapObj = weap.base->As<RE::TESObjectWEAP>();
			if (weapObj) snap.baseWeight = weapObj->weaponData.weight;
		}

		auto* presets = InertiaPresets::GetSingleton();
		snap.hasSpecificPreset = !snap.equippedEditorID.empty() &&
		                         presets->HasSpecificWeaponSettings(snap.equippedEditorID);
	}

	if (camera) {
		snap.isADS    = IsADS(camera);
		snap.isScoped = IsScoped(camera);
	}

	// Havok character controller state (most reliable for airborne/jump/fall)
	if (player && player->currentProcess && player->currentProcess->middleHigh &&
	    player->currentProcess->middleHigh->charController) {
		auto* cc = player->currentProcess->middleHigh->charController.get();
		auto havokState = cc->context.currentState.underlying();
		snap.havokCharState = havokState;
		using HkStateType = RE::hknpCharacterState::hknpCharacterStateType;
		snap.isJumping = (cc->context.currentState.get() == HkStateType::kJumping);
		snap.isInAir   = (cc->context.currentState.get() == HkStateType::kInAir);
		snap.isFalling = snap.isInAir && (cc->fallTime > 0.1f);
	}

	// Live inertia parameters
	snap.cachedWeightMult   = cachedWeightMult;
	snap.actionBlendFactor  = actionBlendFactor;
	snap.settlingFactor     = settlingFactor;
	snap.timeSinceMovement  = timeSinceMovement;

	// Spring velocities
	snap.camPosVel  = cameraSpring.positionVelocity;
	snap.camRotVel  = cameraSpring.rotationVelocity;
	snap.movePosVel = movementSpring.positionVelocity;
	snap.moveRotVel = movementSpring.rotationVelocity;

	// Current applied offset (from deferred buffer)
	if (deferredOffsets.hasOffsets) {
		snap.appliedPosOffset = deferredOffsets.combined.positionOffset;
		snap.appliedRotOffset = deferredOffsets.combined.rotationOffset;
	} else {
		snap.appliedPosOffset = {};
		snap.appliedRotOffset = {};
	}

	// Raw bitfield values
	if (player) {
		snap.gunStateRaw         = static_cast<std::uint32_t>(player->gunState);
		snap.recoilRaw           = player->recoil;
		snap.moveModeRaw         = player->moveMode;
		snap.flyStateRaw         = player->flyState;
		snap.meleeAttackStateRaw = player->meleeAttackState;
	}
	snap.sustainedFireTime = sustainedFireTime;
	snap.airTimeVal        = airTime;
	snap.globalTimeMult    = GetGlobalTimeMult();

	// Copy event log (newest first)
	snap.eventCount = eventLogCount;
	for (int i = 0; i < eventLogCount; ++i) {
		int idx = (eventLogHead - 1 - i + kMaxDebugEvents) % kMaxDebugEvents;
		snap.recentEvents[i] = eventLog[idx];
	}
}

RE::TESBoundObject* Inertia::InertiaManager::GetEquippedWeaponBase(RE::PlayerCharacter* player) const
{
	if (!player || !player->currentProcess || !player->currentProcess->middleHigh) return nullptr;
	auto* mh = player->currentProcess->middleHigh;
	RE::BSAutoLock lock{ mh->equippedItemsLock };
	for (auto& equipped : mh->equippedItems) {
		auto* form = equipped.item.object;
		if (form && form->GetFormID() != 0 && form->formType == RE::ENUM_FORM_ID::kWEAP) {
			return static_cast<RE::TESBoundObject*>(form);
		}
	}
	return nullptr;
}

RE::TBO_InstanceData* Inertia::InertiaManager::GetEquippedWeaponInstanceData(RE::PlayerCharacter* player) const
{
	if (!player || !player->currentProcess || !player->currentProcess->middleHigh) return nullptr;
	auto* mh = player->currentProcess->middleHigh;
	RE::BSAutoLock lock{ mh->equippedItemsLock };
	for (auto& equipped : mh->equippedItems) {
		auto* form = equipped.item.object;
		if (form && form->formType == RE::ENUM_FORM_ID::kWEAP && equipped.item.instanceData) {
			return equipped.item.instanceData.get();
		}
	}
	return nullptr;
}

Inertia::InertiaManager::EquippedWeaponSnapshot
Inertia::InertiaManager::FetchEquippedWeapon(RE::PlayerCharacter* player) const
{
	EquippedWeaponSnapshot snap;
	if (!player || !player->currentProcess || !player->currentProcess->middleHigh)
		return snap;

	auto* mh = player->currentProcess->middleHigh;
	RE::BSAutoLock lock{ mh->equippedItemsLock };
	for (auto& equipped : mh->equippedItems) {
		auto* form = equipped.item.object;
		if (form && form->GetFormID() != 0 && form->formType == RE::ENUM_FORM_ID::kWEAP) {
			snap.base   = static_cast<RE::TESBoundObject*>(form);
			snap.formID = form->GetFormID();
			snap.idata  = equipped.item.instanceData ? equipped.item.instanceData.get() : nullptr;
			const char* eid = snap.base->GetFormEditorID();
			if (eid && eid[0]) snap.editorID = eid;
			break;
		}
	}
	return snap;
}

bool Inertia::InertiaManager::IsWeaponAutomatic(RE::PlayerCharacter* player) const
{
	auto* idata = GetEquippedWeaponInstanceData(player);
	if (!idata) return false;
	auto* wid = static_cast<RE::TESObjectWEAP::InstanceData*>(idata);
	if (!wid) return false;
	return (wid->flags & kWeapFlag_Automatic) != 0;
}

// Check if the equipped weapon has a given keyword (checks base form keywords AND
// instance-level keywords from attached mods, per the Lighthouse pattern)
bool Inertia::InertiaManager::WeaponHasKeyword(
	RE::TESBoundObject* base, RE::TBO_InstanceData* idata, const char* kwEditorID) const
{
	auto* kw = RE::TESForm::GetFormByEditorID<RE::BGSKeyword>(kwEditorID);
	if (!kw) return false;

	auto* weap = base ? base->As<RE::TESObjectWEAP>() : nullptr;
	if (!weap) return false;

	// 1. Check base weapon's own keyword form (standard BGSKeywordForm component)
	if (weap->HasKeyword(kw, idata)) return true;

	// 2. Check instance-level keywords (mod-applied keywords live in InstanceData::keywords)
	//    This is separate from the base form keywords â€” pattern from LighthousePapyrusExtender
	if (idata) {
		auto* instKwForm = idata->GetKeywordData();
		if (instKwForm && instKwForm->HasKeyword(kw, nullptr)) return true;
	}
	return false;
}

std::string Inertia::InertiaManager::GetEquippedWeaponEditorID(RE::PlayerCharacter* player) const
{
	auto* base = GetEquippedWeaponBase(player);
	if (!base) return "";
	const char* eid = base->GetFormEditorID();
	return eid ? std::string(eid) : "";
}

float Inertia::InertiaManager::GetWeightScaleMult(
	const EquippedWeaponSnapshot& weap, const WeaponInertiaSettings& ws) const
{
	if (!ws.weightScalingEnabled) return 1.0f;
	if (!weap.base) return 1.0f;

	auto* weapObj = weap.base->As<RE::TESObjectWEAP>();
	if (!weapObj) return 1.0f;

	float baseWeight = weapObj->weaponData.weight;
	if (baseWeight <= 0.01f) return 1.0f;

	float instanceWeight = baseWeight;
	if (weap.idata) {
		auto* wid = static_cast<RE::TESObjectWEAP::InstanceData*>(weap.idata);
		if (wid) instanceWeight = wid->weight;
	}

	float weightRatio = instanceWeight / baseWeight;
	float weightMult  = Lerp(1.0f, weightRatio, ws.weightScaleInfluence);
	return Clampf(weightMult, ws.weightScaleMin, ws.weightScaleMax);
}

WeaponType Inertia::InertiaManager::DetectWeaponType(
	const EquippedWeaponSnapshot& weap, bool inPowerArmor) const
{
	auto* base  = weap.base;
	auto* idata = weap.idata;

	if (!base) {
		return inPowerArmor ? WeaponType::PA_Unarmed : WeaponType::Unarmed;
	}

	auto* presets = InertiaPresets::GetSingleton();
	const std::string& eid = weap.editorID;
	if (!eid.empty() && presets->HasWeaponTypeOverride(eid)) {
		WeaponType overrideType = presets->GetWeaponTypeOverride(eid);
		if (!inPowerArmor) return overrideType;
		switch (overrideType) {
		case WeaponType::Unarmed: return WeaponType::PA_Unarmed;
		case WeaponType::Melee:   return WeaponType::PA_Melee;
		case WeaponType::Pistol:  return WeaponType::PA_Pistol;
		case WeaponType::Heavy:   return WeaponType::PA_Heavy;
		case WeaponType::Energy:  return WeaponType::PA_Energy;
		default:                  return WeaponType::PA_Rifle;
		}
	}

	auto* weapObj = base->As<RE::TESObjectWEAP>();

	WeaponType baseType = WeaponType::Rifle;

	// Keyword-based detection below replaces primitive type for initial classification.
	if (false) {
		// weaponData.type is an integer field in CommonLibF4
		// Values: 0=HandToHand, 1=OneHandSword, 2=OneHandDagger, 3=OneHandAxe,
		//         4=OneHandMace, 5=TwoHandSword, 6=TwoHandAxe, 7=Bow, 8=Staff,
		//         9=Gun, 10=GrenadeLauncher, 11=MissileLauncher, 12=Mine,
		//         13=Bomb, 14=Thrown, 15=Melee(?), (F4 may differ)
		// In F4, the main division is: 0=HandToHand(Unarmed), 1=OneHand/Melee, 9=Gun
		// We just use keywords for everything â€” they are more reliable in F4.
		// Keep type check only for hand-to-hand / melee base category.
	}

	// Keyword-based detection (most reliable in F4)
	auto hasKW = [&](const char* editorID) -> bool {
		return WeaponHasKeyword(base, idata, editorID);
	};

	// Melee / unarmed first (these rarely have gun keywords)
	if (hasKW(kKW_IsUnarmed))          { baseType = WeaponType::Unarmed;  goto pa_map; }
	if (hasKW(kKW_IsMelee))            { baseType = WeaponType::Melee;    goto pa_map; }

	// Gun subtypes (order matters: more specific first)
	if (hasKW(kKW_WeapTypePistol))     { baseType = WeaponType::Pistol;   goto pa_map; }
	if (hasKW(kKW_WeapTypeMG))         { baseType = WeaponType::Heavy;    goto pa_map; }
	if (hasKW(kKW_WeapTypeFlamer))     { baseType = WeaponType::Heavy;    goto pa_map; }
	if (hasKW(kKW_WeapTypeLauncher))   { baseType = WeaponType::Heavy;    goto pa_map; }
	if (hasKW(kKW_WeapTypeMissile))    { baseType = WeaponType::Heavy;    goto pa_map; }
	if (hasKW(kKW_WeapTypeRifle))      { baseType = WeaponType::Rifle;    goto pa_map; }

	// For weapons without clear keyword, check ammo type to detect energy weapons
	if (weapObj && weapObj->weaponData.ammo) {
		const char* ammoEID = weapObj->weaponData.ammo->GetFormEditorID();
		if (ammoEID) {
			std::string_view sv(ammoEID);
			if (sv.find("FusionCell") != sv.npos ||
			    sv.find("PlasmaCartridge") != sv.npos ||
			    sv.find("ElectronCharge") != sv.npos ||
			    sv.find("GammaRound") != sv.npos) {
				baseType = WeaponType::Energy;
				goto pa_map;
			}
		}
	}

	// Check primitive type for melee/unarmed without keywords
	// weaponData.type is std::int8_t: 0=HandToHand, 1-6=Melee varieties, 14=Thrown
	if (weapObj) {
		auto t = static_cast<std::int8_t>(weapObj->weaponData.type);
		if (t == 0) { baseType = WeaponType::Unarmed;   goto pa_map; }
		if (t >= 1 && t <= 6) { baseType = WeaponType::Melee; goto pa_map; }
		if (t == 14) { baseType = WeaponType::Throwable; goto pa_map; }
	}

	// Default fallback
	baseType = WeaponType::Rifle;

pa_map:
	if (!inPowerArmor) return baseType;

	switch (baseType) {
	case WeaponType::Unarmed:   return WeaponType::PA_Unarmed;
	case WeaponType::Melee:     return WeaponType::PA_Melee;
	case WeaponType::Pistol:    return WeaponType::PA_Pistol;
	case WeaponType::Heavy:     return WeaponType::PA_Heavy;
	case WeaponType::Energy:    return WeaponType::PA_Energy;
	default:                    return WeaponType::PA_Rifle;
	}
}

const WeaponInertiaSettings& Inertia::InertiaManager::GetCurrentWeaponSettings(RE::PlayerCharacter* player)
{
	auto* presets = InertiaPresets::GetSingleton();

	// Single atomic fetch: base, idata, formID, editorID all captured
	// under one lock so they are guaranteed to be from the same weapon.
	EquippedWeaponSnapshot weap = FetchEquippedWeapon(player);

	// Guard against transient nullptr from equippedItems during reloads,
	// holster/draw animations, or cell transitions.  If the weapon is drawn
	// but the engine briefly reports nothing equipped, keep the cached
	// settings rather than falling through to Unarmed/default.
	if (weap.formID == 0 && cachedWeaponSettingsValid && cachedWeaponSettingsFormID != 0) {
		bool weaponDrawn = player->GetWeaponMagicDrawn();
		if (weaponDrawn) {
			return cachedWeaponSettingsCopy;
		}
	}

	bool weaponChanged = (weap.formID != cachedWeaponSettingsFormID);

	cachedWeaponSettingsFormID = weap.formID;
	if (weaponChanged)
		cachedTargetNode = nullptr;

	bool inPA = IsInPowerArmor(player);
	WeaponType wt = DetectWeaponType(weap, inPA);

	if (weap.formID != 0) {
		auto* weapBase = weap.base ? weap.base->As<RE::TESObjectWEAP>() : nullptr;
		cachedWeaponSettingsCopy = presets->GetWeaponSettingsWithKeywords(weap.editorID, weapBase, wt);
	} else {
		cachedWeaponSettingsCopy = presets->GetWeaponTypeSettings(wt);
	}
	cachedWeaponSettingsValid = true;

	cachedWeightMult = GetWeightScaleMult(weap, cachedWeaponSettingsCopy);

	if (weaponChanged) {
		logger::debug("[FPGunplayOverhaul] Settings refreshed: formID=0x{:08X} eid='{}' type={} invert={}",
			weap.formID, weap.editorID, static_cast<int>(wt), cachedWeaponSettingsCopy.invertCameraPitch);
	}

	return cachedWeaponSettingsCopy;
}

// ============================================================
// Node finding
// ============================================================
RE::NiNode* Inertia::InertiaManager::FindTargetNode(
	RE::NiNode* fpRoot, const WeaponInertiaSettings& ws, bool isADS)
{
	if (!fpRoot) return nullptr;

	int requestedPivot = isADS ? ws.adsPivotPoint : ws.pivotPoint;

	// During warmup period after load/equip, force spine pivot (index 0)
	// so the FP skeleton is fully assembled before we try lower bones.
	int activePivot = (pivotWarmupTimer > 0.0f) ? 0 : requestedPivot;

	// If the pivot changed (e.g. ADS transition or warmup ending), invalidate cache
	if (cachedInsertedBone && cachedPivotIndex != activePivot) {
		cachedInsertedBone = nullptr;
		cachedTargetNode = nullptr;
		hasRefPosePivot = false;
	}

	// If we have a cached inserted bone that's still valid, use it
	if (cachedInsertedBone) {
		if (fpRoot->GetObjectByName(kInsertedBoneName) == cachedInsertedBone)
			return cachedInsertedBone;
		cachedInsertedBone = nullptr;
		cachedTargetNode = nullptr;
		hasRefPosePivot = false;
	}

	cachedPivotIndex = activePivot;

	// Select bone list based on pivot setting
	const char* const* boneList = GetBoneListForPivot(activePivot);

	// Find the pivot bone in the skeleton
	RE::NiNode* pivotBone = nullptr;
	const char* foundName = nullptr;
	for (int i = 0; boneList[i] != nullptr; ++i) {
		if (auto* bone = fpRoot->GetObjectByName(boneList[i])) {
			pivotBone = static_cast<RE::NiNode*>(bone);
			foundName = boneList[i];
			break;
		}
	}

	if (!pivotBone) {
		if (!hasLoggedSkeleton) {
			logger::warn("[FPGunplayOverhaul] No suitable pivot bone found, using FP root");
			std::function<void(RE::NiAVObject*, int)> logBones = [&](RE::NiAVObject* obj, int depth) {
				if (!obj || depth > 5) return;
				logger::info("[FPGunplayOverhaul] Bone[{}]: {}", depth, obj->name.c_str() ? obj->name.c_str() : "<null>");
				auto* asNode = obj->IsNode();
				if (asNode) {
					for (auto& child : asNode->children) {
						if (child) logBones(child.get(), depth + 1);
					}
				}
			};
			logBones(fpRoot, 0);
			hasLoggedSkeleton = true;
		}
		return nullptr;
	}

	if (!hasLoggedSkeleton) {
		logger::info("[FPGunplayOverhaul] Using bone '{}' as pivot node", foundName);
		hasLoggedSkeleton = true;
	}

	// Insert our custom bone above the pivot bone.
	// The animation system overwrites the pivot bone each frame,
	// but our inserted bone is unknown to animations and persists.
	auto* inserted = GetOrInsertInertiaBone(fpRoot, pivotBone);
	if (inserted) {
		cachedTargetNode = pivotBone;
		refPosePivotTranslate = GetRefPoseTranslate(foundName);
		hasRefPosePivot = true;
		return inserted;
	}

	return nullptr;
}

// ============================================================
// Inserted bone â€” animation system doesn't know about this
// node so it won't overwrite our transforms each frame.
// ============================================================
RE::NiNode* Inertia::InertiaManager::GetOrInsertInertiaBone(
	RE::NiNode* fpRoot, RE::NiNode* pivotBone)
{
	if (!fpRoot || !pivotBone) return nullptr;

	// Check cache first
	if (cachedInsertedBone) {
		// Verify it's still valid (still in the skeleton)
		if (fpRoot->GetObjectByName(kInsertedBoneName) == cachedInsertedBone)
			return cachedInsertedBone;
		cachedInsertedBone = nullptr;
	}

	// Look for an existing inserted bone
	auto* existing = fpRoot->GetObjectByName(kInsertedBoneName);
	if (existing) {
		auto* existingNode = existing->IsNode();
		if (existingNode) {
			// Verify the pivot is a child of our inserted bone
			if (existingNode->GetObjectByName(pivotBone->name)) {
				cachedInsertedBone = existingNode;
				return existingNode;
			}
			// Structure mismatch: detach the stale node so it doesn't remain as an
			// orphan with no children in the skeleton, then fall through to reinsert.
			logger::warn("[FPGunplayOverhaul] Inserted bone structure mismatch, reinserting");
			if (auto* staleParent = existingNode->parent) {
				staleParent->DetachChild(existingNode);
			}
		}
	}

	// Create a new NiNode
	auto* inserted = new RE::NiNode(1);
	if (!inserted) return nullptr;

	inserted->name = kInsertedBoneName;
	inserted->local.translate = RE::NiPoint3{ 0.0f, 0.0f, 0.0f };
	inserted->local.rotate.MakeIdentity();
	inserted->local.scale = 1.0f;

	// Crash-safe insertion between pivotBone and its parent:
	//   Before: parent -> pivotBone
	//   After:  parent -> inserted -> pivotBone
	//
	// Pre-detach pivotBone via the NiPointer overload to keep it alive
	// (refcount >= 1) while freeing its slot in parent's array. AttachChild
	// can then reuse the freed slot without growing/reallocating the array,
	// which eliminates the null-deref that occurred when another mod (e.g. FBA)
	// had already expanded the same parent array in the same frame.
	// Clearing pivotBone->parent prevents the implicit DetachFromParent inside
	// the subsequent AttachChild(pivotBone) from double-detaching.
	RE::NiNode* parent = pivotBone->parent;
	if (parent) {
		RE::NiPointer<RE::NiAVObject> pivotRef;
		parent->DetachChild(pivotBone, pivotRef);  // detach + keep alive via pivotRef
		pivotBone->parent = nullptr;               // prevent double-detach in AttachChild

		parent->AttachChild(inserted, true);       // reuses freed slot; no reallocation
		inserted->parent = parent;
	}
	// pivotBone->parent is null; AttachChild skips the implicit DetachFromParent chain
	inserted->AttachChild(pivotBone, true);

	cachedInsertedBone = inserted;
	logger::info("[FPGunplayOverhaul] Inserted bone '{}' above '{}'",
		kInsertedBoneName, pivotBone->name.c_str());

	return inserted;
}

// ============================================================
// Camera velocity
// ============================================================
RE::NiPoint3 Inertia::InertiaManager::CalculateCameraVelocity(float delta)
{
	if (delta <= 0.0001f) return { 0.0f, 0.0f, 0.0f };

	// Use player input angles directly instead of extracting from the camera
	// rotation matrix.  The camera root's world.rotate can include our own
	// bone modifications (pitch rotation on the inserted bone propagates
	// through the skeleton to the camera), creating a feedback loop that
	// intermittently dampens pitch inertia.  player->data.angle is the raw
	// input-driven look direction, free from bone feedback.
	auto* player = RE::PlayerCharacter::GetSingleton();
	if (!player) return { 0.0f, 0.0f, 0.0f };

	float currentPitch = player->data.angle.x;
	float currentYaw   = player->data.angle.z;

	if (!initialized) {
		lastCameraYaw   = currentYaw;
		lastCameraPitch = currentPitch;
		smoothedCameraVelocity = { 0.0f, 0.0f, 0.0f };
		initialized = true;
		return { 0.0f, 0.0f, 0.0f };
	}

	static constexpr float kPi = 3.14159265358979323846f;
	// Normalize angle deltas to [-π, π] to handle yaw wrap-around at ±180°
	auto normalizeAngle = [](float a) -> float {
		while (a >  kPi) a -= 2.0f * kPi;
		while (a < -kPi) a += 2.0f * kPi;
		return a;
	};

	// Cap delta to avoid huge velocity values during frame hitches
	float cappedDelta = std::max(delta, 0.004f);  // never divide by less than 4ms

	float rawYaw   = normalizeAngle(currentYaw   - lastCameraYaw)   / cappedDelta;
	float rawPitch = normalizeAngle(currentPitch - lastCameraPitch) / cappedDelta;

	lastCameraYaw   = currentYaw;
	lastCameraPitch = currentPitch;

	// Hard clamp: camera angular velocity beyond ~4 rad/s (≈230°/s) is physically
	// implausible for deliberate aiming and indicates wrap or hitch artifacts.
	static constexpr float kMaxCamVel = 4.0f;
	rawYaw   = std::clamp(rawYaw,   -kMaxCamVel, kMaxCamVel);
	rawPitch = std::clamp(rawPitch, -kMaxCamVel, kMaxCamVel);

	RE::NiPoint3 rawVel = { rawPitch, 0.0f, rawYaw };

	auto* gs = Settings::GetSingleton();
	// Frame-rate-independent exponential smoothing: alpha = 1 - e^(-dt/τ)
	// smoothingFactor is treated as the time constant τ in seconds (higher = smoother).
	float tau   = std::max(gs->smoothingFactor * 0.05f, 0.001f);  // map [0..1] → τ [0..50ms]
	float alpha = 1.0f - std::expf(-delta / tau);
	smoothedCameraVelocity.x += (rawVel.x - smoothedCameraVelocity.x) * alpha;
	smoothedCameraVelocity.y += (rawVel.y - smoothedCameraVelocity.y) * alpha;
	smoothedCameraVelocity.z += (rawVel.z - smoothedCameraVelocity.z) * alpha;

	return smoothedCameraVelocity;
}

// ============================================================
// Movement vector in local space
// ============================================================
RE::NiPoint3 Inertia::InertiaManager::CalculateLocalMovement(RE::PlayerCharacter* player)
{
	if (!player) return { 0.0f, 0.0f, 0.0f };
	auto* controls = RE::PlayerControls::GetSingleton();
	if (!controls) return { 0.0f, 0.0f, 0.0f };
	auto& mv = controls->data.moveInputVec;
	return { mv.x, mv.y, 0.0f };
}

// ============================================================
// Super Sprint keyword helpers (add/remove from BGSKeywordForm)
// ============================================================
namespace SuperSprintHelpers
{
	static void AddKeyword(RE::BGSKeywordForm* form, RE::BGSKeyword* kw)
	{
		if (!form || !kw) return;
		for (std::uint32_t i = 0; i < form->numKeywords; ++i) {
			if (form->keywords[i] == kw) return;
		}
		auto newCount = form->numKeywords + 1;
		auto** newArr = new RE::BGSKeyword*[newCount];
		for (std::uint32_t i = 0; i < form->numKeywords; ++i) {
			newArr[i] = form->keywords[i];
		}
		newArr[form->numKeywords] = kw;
		form->keywords    = newArr;
		form->numKeywords = newCount;
	}

	static void RemoveKeyword(RE::BGSKeywordForm* form, RE::BGSKeyword* kw)
	{
		if (!form || !kw) return;
		std::uint32_t idx = UINT32_MAX;
		for (std::uint32_t i = 0; i < form->numKeywords; ++i) {
			if (form->keywords[i] == kw) { idx = i; break; }
		}
		if (idx == UINT32_MAX) return;
		for (std::uint32_t i = idx; i < form->numKeywords - 1; ++i) {
			form->keywords[i] = form->keywords[i + 1];
		}
		form->numKeywords--;
	}
}

// ============================================================
// Super Sprint input hook — prevents the engine from toggling
// sprint off when the player double-taps the sprint key.
// We hook SprintHandler::HandleEvent(ButtonEvent*) at vtable
// slot 8 (byte offset 0x40), the same layout confirmed by
// UneducatedShooter's offsets for MouseMoveEvent (0x30 / slot 6)
// and ThumbstickEvent (0x20 / slot 4).
// ============================================================
namespace SuperSprintInput
{
	// Signature of BSInputEventUser::HandleEvent(ButtonEvent*)
	using FnHandleButton = void(*)(void*, const RE::ButtonEvent*);

	// Original (unhooked) SprintHandler::HandleEvent(ButtonEvent*)
	static FnHandleButton s_originalHandleButton = nullptr;

	// Set by Update when the activation window is open.
	// While true, the hook eats JustPressed events so the engine's
	// sprint toggle never flips.
	static bool s_eatEnabled = false;

	// Set by the hook when it eats a sprint press.  Cleared by Update
	// after it reads it and activates super sprint.
	static bool s_eatTriggered = false;

	// Whether the hook is installed (prevents double-install)
	static bool s_installed = false;

	static void HookedSprintHandleButton(void* self, const RE::ButtonEvent* event)
	{
		if (s_eatEnabled && event && event->JustPressed()) {
			// Eat the press — don't let the engine toggle sprint off
			s_eatTriggered = true;
			return;
		}
		// Pass all other events (releases, held) to the original handler
		if (s_originalHandleButton) {
			s_originalHandleButton(self, event);
		}
	}

	// Patch SprintHandler's vtable entry for HandleEvent(ButtonEvent*).
	// Uses the same SafeWrite pattern as UneducatedShooter.
	static bool Install()
	{
		auto* pc = RE::PlayerControls::GetSingleton();
		if (!pc || !pc->sprintHandler) {
			logger::error("[SuperSprintInput] PlayerControls or SprintHandler is null");
			return false;
		}

		// SprintHandler inherits BSInputEventUser single-inheritance chain.
		// Vtable pointer is at offset 0 of the object.
		uintptr_t vtable = *reinterpret_cast<uintptr_t*>(pc->sprintHandler);

		// HandleEvent(ButtonEvent*) is vtable slot 8 → byte offset 0x40
		constexpr uintptr_t kSlotOffset = 8 * sizeof(void*);
		uintptr_t addr = vtable + kSlotOffset;

		// Read original function pointer
		memcpy(&s_originalHandleButton, reinterpret_cast<void*>(addr), sizeof(void*));

		// Write our hook
		uintptr_t hookAddr = reinterpret_cast<uintptr_t>(&HookedSprintHandleButton);
		DWORD oldProtect = 0;
		if (!VirtualProtect(reinterpret_cast<void*>(addr), sizeof(void*),
				PAGE_EXECUTE_READWRITE, &oldProtect)) {
			logger::error("[SuperSprintInput] VirtualProtect failed");
			return false;
		}
		memcpy(reinterpret_cast<void*>(addr), &hookAddr, sizeof(void*));
		VirtualProtect(reinterpret_cast<void*>(addr), sizeof(void*), oldProtect, &oldProtect);

		s_installed = true;
		logger::info("[SuperSprintInput] Hooked SprintHandler::HandleEvent(ButtonEvent*) — "
			"vtable=0x{:X}, slot=8, original=0x{:X}",
			vtable, reinterpret_cast<uintptr_t>(s_originalHandleButton));
		return true;
	}
}

// ============================================================
// Attack input hook — ground-truth fire/ADS button state.
// ------------------------------------------------------------
// The previous approach peeked AttackBlockHandler's raw bytes
// (leftAttackButtonHeld @ 0x72) to detect the fire button. Verified
// unreliable in-game: a full session of SCAR bursts and 1911 shots
// produced zero reads of that byte as non-zero, so features gated on it
// (Fire on Empty, Early Fire Cancel) silently never armed.
//
// Instead, vtable-hook AttackBlockHandler::HandleEvent(ButtonEvent*)
// (BSInputEventUser slot 8 — the same slot/pattern as the proven
// SuperSprintInput hook, and the same slot ExtendedWeaponSystem patches
// for this handler) and track the button state straight from the
// engine's dispatched events. FO4 user-event names, confirmed in F4SE's
// CustomControlMap.txt and HaBCR: "PrimaryAttack" = fire,
// "SecondaryAttack" = ADS. ButtonEvent::value != 0 while held, == 0 on
// release.
// ============================================================
namespace AttackInput
{
	using FnHandleButton = void(*)(void*, const RE::ButtonEvent*);

	// Original (unhooked) AttackBlockHandler::HandleEvent(ButtonEvent*)
	static FnHandleButton s_originalHandleButton = nullptr;

	// Whether the hook is installed (prevents double-install)
	static bool s_installed = false;

	// Live input state, updated by the hook as the engine dispatches
	// button events. Read by InertiaManager::Update on the main thread;
	// input dispatch also happens on the main thread, so plain bools are
	// sufficient (same reasoning as SuperSprintInput's flags).
	static bool s_fireHeld = false;  // "PrimaryAttack" currently held
	static bool s_adsHeld  = false;  // "SecondaryAttack" currently held

	static void HookedAttackHandleButton(void* self, const RE::ButtonEvent* event)
	{
		if (event) {
			const RE::BSFixedString& userEvent = event->QUserEvent();
			if (userEvent == "PrimaryAttack"sv) {
				s_fireHeld = (event->value != 0.0f);
				if (event->JustPressed()) {
					logger::trace("[AttackInput] PrimaryAttack pressed");
				}
			} else if (userEvent == "SecondaryAttack"sv) {
				const bool nowHeld = (event->value != 0.0f);
				if (nowHeld != s_adsHeld) {
					// Edge trace: if ADS entry ever wedges engine-side
					// again, these lines show real presses arriving while
					// gunState never leaves 0 — pinpointing the ignore.
					logger::trace("[AttackInput] SecondaryAttack {}", nowHeld ? "pressed" : "released");
				}
				s_adsHeld = nowHeld;
			}
		}
		// Always pass through to the engine's handler unchanged.
		if (s_originalHandleButton) {
			s_originalHandleButton(self, event);
		}
	}

	// Synthesize a full release+press TAP and dispatch both events
	// straight to the engine's real AttackBlockHandler::HandleEvent.
	// Purpose: re-enter ADS after a dry-fire hard stop while the player
	// is still physically holding the ADS button — the engine's aim-
	// enter logic is edge-triggered off this input path (verified
	// 2026-07-21: a continuously-held button never re-enters on its
	// own, action-layer requests all return false, but a synthetic
	// press reliably re-entered sighted every cycle).
	//
	// Why a TAP and not a bare press: the first synthetic-press build
	// (00:39 session) wedged ADS entry for the rest of the session
	// after a few cycles, and the wedge survived a weapon re-equip —
	// so it lived in this input layer's bookkeeping, not the graph.
	// A bare press injects a second "pressed" edge while the handler
	// already holds a pressed state from the REAL button, leaving its
	// press/release pairing unbalanced when the single real release
	// arrives. Dispatching a release FIRST returns the handler to a
	// consistent released state, and the following press re-arms it —
	// after which the real release pairs with our synthetic press.
	// INFERRED from the wedge's survival pattern, not from disassembly
	// of the handler; if ADS entry ever session-wedges again, this
	// theory is wrong (the SecondaryAttack edge trace in the hook
	// above will show real presses arriving while gunState stays 0).
	// Dispatch ONE synthetic ButtonEvent straight to the engine's real
	// AttackBlockHandler::HandleEvent. value 1 + heldSecs 0 = a fresh
	// press (JustPressed() == true); value 0 + heldSecs > 0 = a release.
	static bool DispatchButton(const char* a_userEvent, float a_value, float a_heldSecs)
	{
		auto* pc = RE::PlayerControls::GetSingleton();
		if (!s_originalHandleButton || !pc || !pc->attackHandler) return false;

		RE::ButtonEvent evt{};
		RE::stl::emplace_vtable(&evt);
		evt.device       = RE::INPUT_DEVICE::kKeyboard;
		evt.deviceID     = 0;
		evt.eventType    = RE::INPUT_EVENT_TYPE::kButton;
		evt.next         = nullptr;
		evt.timeCode     = 0;
		evt.handled      = RE::InputEvent::HANDLED_RESULT::kUnhandled;
		evt.strUserEvent = RE::BSFixedString(a_userEvent);
		evt.idCode       = -1;
		evt.disabled     = false;
		evt.value        = a_value;
		evt.heldDownSecs = a_heldSecs;
		s_originalHandleButton(pc->attackHandler, &evt);
		return true;
	}

	static bool SimulateTap(const char* a_userEvent)
	{
		// Release first (returns the handler to a consistent released
		// state — see the wedge post-mortem above), then a fresh press.
		if (!DispatchButton(a_userEvent, 0.0f, 0.5f)) return false;
		return DispatchButton(a_userEvent, 1.0f, 0.0f);
	}

	// Patch AttackBlockHandler's vtable entry for HandleEvent(ButtonEvent*).
	static bool Install()
	{
		auto* pc = RE::PlayerControls::GetSingleton();
		if (!pc || !pc->attackHandler) {
			logger::error("[AttackInput] PlayerControls or AttackBlockHandler is null");
			return false;
		}

		uintptr_t vtable = *reinterpret_cast<uintptr_t*>(pc->attackHandler);

		// HandleEvent(ButtonEvent*) is vtable slot 8 → byte offset 0x40
		constexpr uintptr_t kSlotOffset = 8 * sizeof(void*);
		uintptr_t addr = vtable + kSlotOffset;

		memcpy(&s_originalHandleButton, reinterpret_cast<void*>(addr), sizeof(void*));

		uintptr_t hookAddr = reinterpret_cast<uintptr_t>(&HookedAttackHandleButton);
		DWORD oldProtect = 0;
		if (!VirtualProtect(reinterpret_cast<void*>(addr), sizeof(void*),
				PAGE_EXECUTE_READWRITE, &oldProtect)) {
			logger::error("[AttackInput] VirtualProtect failed");
			return false;
		}
		memcpy(reinterpret_cast<void*>(addr), &hookAddr, sizeof(void*));
		VirtualProtect(reinterpret_cast<void*>(addr), sizeof(void*), oldProtect, &oldProtect);

		s_installed = true;
		logger::info("[AttackInput] Hooked AttackBlockHandler::HandleEvent(ButtonEvent*) — "
			"vtable=0x{:X}, slot=8, original=0x{:X}",
			vtable, reinterpret_cast<uintptr_t>(s_originalHandleButton));
		return true;
	}
}

// ============================================================
// Melee input hook — ground-truth gun-bash button presses.
// ------------------------------------------------------------
// Same BSInputEventUser slot-8 vtable patch as SuperSprintInput /
// AttackInput, applied to MeleeThrowHandler (PlayerControls +0x240).
// The FO4 user event for gun bash is "Melee" (confirmed in F4SE's
// CustomControlMap.txt). The handler also drives grenade throws on
// long-hold of the same key; we only observe events and always pass
// them through, so throw behavior is untouched.
//
// Used by Repeatable Gun Bash: presses that land while a bash is
// already playing are queued (the engine ignores them natively), and
// the combo block in Update fires the follow-up bash once the
// HitFrame + delay window opens.
// ============================================================
namespace MeleeInput
{
	using FnHandleButton = void(*)(void*, const RE::ButtonEvent*);

	// Original (unhooked) MeleeThrowHandler::HandleEvent(ButtonEvent*)
	static FnHandleButton s_originalHandleButton = nullptr;

	// Whether the hook is installed (prevents double-install)
	static bool s_installed = false;

	// Rising-edge press flag, set by the hook, consumed (cleared) by the
	// bash combo block in Update. Input dispatch and Update both run on
	// the main thread (same reasoning as AttackInput's plain bools).
	static bool s_meleePressedEdge = false;

	static void HookedMeleeHandleButton(void* self, const RE::ButtonEvent* event)
	{
		if (event && event->QUserEvent() == "Melee"sv && event->JustPressed()) {
			s_meleePressedEdge = true;
			logger::trace("[MeleeInput] Melee pressed");
		}
		// Always pass through to the engine's handler unchanged.
		if (s_originalHandleButton) {
			s_originalHandleButton(self, event);
		}
	}

	// Synthesize a release+press tap on the Melee user event, dispatched
	// straight to the engine's real MeleeThrowHandler::HandleEvent.
	// Fallback bash trigger for when RunActionOnActor(kActionMelee)
	// refuses mid-bash (conditioned actions can read the in-flight
	// attack state and bail — the same failure class as kActionFireAuto
	// on an empty magazine). Release-first for the same input-layer
	// bookkeeping reason as AttackInput::SimulateTap.
	static bool SimulateTap()
	{
		auto* pc = RE::PlayerControls::GetSingleton();
		if (!s_originalHandleButton || !pc || !pc->meleeThrowHandler) return false;

		auto makeEvent = [&](RE::ButtonEvent& a_evt, float a_value, float a_heldSecs) {
			RE::stl::emplace_vtable(&a_evt);
			a_evt.device       = RE::INPUT_DEVICE::kKeyboard;
			a_evt.deviceID     = 0;
			a_evt.eventType    = RE::INPUT_EVENT_TYPE::kButton;
			a_evt.next         = nullptr;
			a_evt.timeCode     = 0;
			a_evt.handled      = RE::InputEvent::HANDLED_RESULT::kUnhandled;
			a_evt.strUserEvent = RE::BSFixedString("Melee");
			a_evt.idCode       = -1;
			a_evt.disabled     = false;
			a_evt.value        = a_value;
			a_evt.heldDownSecs = a_heldSecs;
		};

		RE::ButtonEvent release{};
		makeEvent(release, 0.0f, 0.1f);
		s_originalHandleButton(pc->meleeThrowHandler, &release);

		RE::ButtonEvent press{};
		makeEvent(press, 1.0f, 0.0f);
		s_originalHandleButton(pc->meleeThrowHandler, &press);

		// The synthetic press must not read back as a queue request.
		s_meleePressedEdge = false;
		return true;
	}

	// Patch MeleeThrowHandler's vtable entry for HandleEvent(ButtonEvent*).
	static bool Install()
	{
		auto* pc = RE::PlayerControls::GetSingleton();
		if (!pc || !pc->meleeThrowHandler) {
			logger::error("[MeleeInput] PlayerControls or MeleeThrowHandler is null");
			return false;
		}

		uintptr_t vtable = *reinterpret_cast<uintptr_t*>(pc->meleeThrowHandler);

		// HandleEvent(ButtonEvent*) is vtable slot 8 → byte offset 0x40
		constexpr uintptr_t kSlotOffset = 8 * sizeof(void*);
		uintptr_t addr = vtable + kSlotOffset;

		memcpy(&s_originalHandleButton, reinterpret_cast<void*>(addr), sizeof(void*));

		uintptr_t hookAddr = reinterpret_cast<uintptr_t>(&HookedMeleeHandleButton);
		DWORD oldProtect = 0;
		if (!VirtualProtect(reinterpret_cast<void*>(addr), sizeof(void*),
				PAGE_EXECUTE_READWRITE, &oldProtect)) {
			logger::error("[MeleeInput] VirtualProtect failed");
			return false;
		}
		memcpy(reinterpret_cast<void*>(addr), &hookAddr, sizeof(void*));
		VirtualProtect(reinterpret_cast<void*>(addr), sizeof(void*), oldProtect, &oldProtect);

		s_installed = true;
		logger::info("[MeleeInput] Hooked MeleeThrowHandler::HandleEvent(ButtonEvent*) — "
			"vtable=0x{:X}, slot=8, original=0x{:X}",
			vtable, reinterpret_cast<uintptr_t>(s_originalHandleButton));
		return true;
	}
}

// ============================================================
// Fire annotation guard — swallow `weaponFire` during dry-fire,
// and skip the engine's post-special-idle re-equip on IdleStop.
// ------------------------------------------------------------
// The WeaponFire annotation in a fire clip is what makes the engine
// actually discharge the weapon, and it does NOT check ammo (verified
// in-game 2026-07-16 23:29: a Fire on Empty dry-fire on the SCAR fired
// real projectiles from a 0-round magazine; our phantom-fire feature is
// built on the same fact). So while a Fire on Empty dry-fire idle is
// playing a VANILLA fire clip, its annotations must be kept away from
// the engine.
//
// Mechanism (same pattern as HaBCR's AnimationGraphEventWatcher and
// ExtendedWeaponSystem's PlayerAnimationGraphEventHandler, both proven
// in-game): the player object is itself the engine's sink for its
// graph events — TESObjectREFR inherits BSTEventSink<
// BSAnimationGraphEvent> at offset 0x38 (CommonLibF4 TESObjectREFRs.h),
// and ProcessEvent is vtable slot 1 (+0x8, BSTEvent.h: slot 0 is the
// dtor). We patch that slot, and while the suppression window is open
// (managed per-frame by the Fire on Empty block in Update: idle in
// flight or blend-out grace, AND magazine still empty) we return
// kContinue for weaponFire without calling the engine's handler.
// Everything else — and all normal firing — passes straight through.
//
// INFERRED (not yet verified): that the discharge happens inside this
// handler. Reference plugins intercept weapon anim events here, but
// none blocks weaponFire specifically. If the inference is wrong the
// failure mode is benign: bullets keep firing exactly as without this
// hook, nothing else changes.
// ============================================================
namespace FireAnnotationGuard
{
	using FnProcessEvent = RE::BSEventNotifyControl (*)(
		void*, const RE::BSAnimationGraphEvent&,
		RE::BSTEventSource<RE::BSAnimationGraphEvent>*);

	// Original (unhooked) PlayerCharacter ProcessEvent for graph events
	static FnProcessEvent s_original = nullptr;

	// Whether the hook is installed (prevents double-install)
	static bool s_installed = false;

	// Suppression windows. Written by InertiaManager::Update (main
	// thread). weaponFire and attackState are split: after an ADS
	// soft-stop we must NOT keep swallowing attackState — that was
	// implicated in SightedRelease no longer working on button-up.
	static std::atomic<bool> s_suppressWeaponFire{ false };
	static std::atomic<bool> s_suppressAttackState{ false };

	// Back-compat alias written by Reset() and older call sites.
	static std::atomic<bool> s_suppress{ false };


	static RE::BSEventNotifyControl HookedProcessEvent(void* a_self,
		const RE::BSAnimationGraphEvent& a_event,
		RE::BSTEventSource<RE::BSAnimationGraphEvent>* a_source)
	{
		// Re-equip SoundPlay from the stop's UpdateAnimation(1000) —
		// draw/bolt/foley annotations (WPNSCARDrawFoley, BoltBack, …).
		if (g_suppressEquipSounds.load(std::memory_order_relaxed)) {
			if (a_event.animEvent == "SoundPlay" || a_event.animEvent == "SoundPlay3D") {
				logger::info("[FireOnEmpty] Suppressed re-equip {} '{}' (stop fast-forward)",
					a_event.animEvent.c_str(), a_event.argument.c_str());
				return RE::BSEventNotifyControl::kContinue;
			}
		}

		const bool suppressWF = s_suppressWeaponFire.load(std::memory_order_relaxed) ||
		                        s_suppress.load(std::memory_order_relaxed);
		const bool suppressAS = s_suppressAttackState.load(std::memory_order_relaxed) ||
		                        s_suppress.load(std::memory_order_relaxed);

		if (suppressWF || suppressAS) {
			if (suppressWF && (a_event.animEvent == "weaponFire" || a_event.animEvent == "WeaponFire")) {
				// Swallow: the engine's handler never sees the event, so no
				// discharge. kContinue (not kStop) so OTHER registered sinks
				// (OAR's log, our own AnimEventSink) still observe it.
				logger::info("[FireOnEmpty] Suppressed weaponFire annotation (dry-fire window)");
				return RE::BSEventNotifyControl::kContinue;
			}
			if (suppressAS && a_event.animEvent == "attackState") {
				// Swallow only while the dry-fire idle is in flight — this
				// is the loop-breaker (attackState Enter → gunState 7 →
				// isFiring re-sync every frame). Cleared on ADS soft-stop
				// so SightedRelease can proceed.
				logger::info("[FireOnEmpty] Suppressed attackState '{}' annotation (dry-fire window)",
					a_event.argument.c_str());
				return RE::BSEventNotifyControl::kContinue;
			}
			if (a_event.animEvent == "IdleStop") {
				// Intentionally NOT calling UpdateAnimation(1000) here.
				// idlestopfix uses that to hide the post-idle re-draw, but
				// combined with our former InitializeToBaseState hard-stop
				// it stormed InitiateStart and wedged ADS/holster so only
				// dry-fire PlayIdle still worked (2026-07-21). Fall through
				// to the engine; g_suppressEquipSounds (if set by the stop)
				// still mutes re-equip foley.
			}
		}
		return s_original ? s_original(a_self, a_event, a_source) :
		                    RE::BSEventNotifyControl::kContinue;
	}

	// Patch the ProcessEvent slot of the player's
	// BSTEventSink<BSAnimationGraphEvent> sub-vtable.
	static bool Install()
	{
		auto* player = RE::PlayerCharacter::GetSingleton();
		if (!player) {
			logger::error("[FireAnnotationGuard] PlayerCharacter singleton is null");
			return false;
		}

		// BSTEventSink<BSAnimationGraphEvent> base subobject (offset 0x38
		// per CommonLibF4 TESObjectREFRs.h — same offset HaBCR uses).
		auto* sink = reinterpret_cast<void*>(
			reinterpret_cast<std::uintptr_t>(player) + 0x38);
		uintptr_t vtable = *reinterpret_cast<uintptr_t*>(sink);

		// ProcessEvent is slot 1 (slot 0 = virtual dtor) → byte offset 0x8.
		uintptr_t addr = vtable + 0x8;

		memcpy(&s_original, reinterpret_cast<void*>(addr), sizeof(void*));

		uintptr_t hookAddr = reinterpret_cast<uintptr_t>(&HookedProcessEvent);
		DWORD oldProtect = 0;
		if (!VirtualProtect(reinterpret_cast<void*>(addr), sizeof(void*),
				PAGE_EXECUTE_READWRITE, &oldProtect)) {
			logger::error("[FireAnnotationGuard] VirtualProtect failed");
			return false;
		}
		memcpy(reinterpret_cast<void*>(addr), &hookAddr, sizeof(void*));
		VirtualProtect(reinterpret_cast<void*>(addr), sizeof(void*), oldProtect, &oldProtect);

		s_installed = true;
		logger::info("[FireAnnotationGuard] Hooked player BSTEventSink<BSAnimationGraphEvent>::ProcessEvent — "
			"vtable=0x{:X}, slot=1, original=0x{:X}",
			vtable, reinterpret_cast<uintptr_t>(s_original));
		return true;
	}
}

// ============================================================
// Spring update helpers
// ============================================================
void Inertia::InertiaManager::UpdateCameraSpring(
	SpringState& state, const WeaponInertiaSettings& ws,
	const RE::NiPoint3& camVel, float delta, float mult)
{
	auto* gs = Settings::GetSingleton();
	if (!gs->enablePosition && !gs->enableRotation) return;

	float settling  = 1.0f + settlingFactor * (gs->settleDampingMult - 1.0f);
	float effDamp   = ws.damping * settling;

	float pitchForce = camVel.x * ws.pitchMultiplier * ws.cameraPitchMult * mult;
	float yawForce   = camVel.z * mult;
	float rollForce  = camVel.z * ws.rollMultiplier * mult;

	if (ws.invertCameraPitch) pitchForce = -pitchForce;
	if (ws.invertCameraYaw)   yawForce   = -yawForce;

	// F4 FP skeleton bone-local axes: X = vertical, Y = forward, Z = lateral
	if (gs->enablePosition) {
		state.positionOffset.z = SpringStep(state.positionOffset.z, state.positionVelocity.z,
			yawForce * 0.01f, ws.stiffness, effDamp, ws.mass, delta);
		state.positionOffset.x = SpringStep(state.positionOffset.x, state.positionVelocity.x,
			pitchForce * 0.01f, ws.stiffness, effDamp, ws.mass, delta);
		ClampSpringAxis(state.positionOffset.z, state.positionVelocity.z, -ws.maxOffset, ws.maxOffset);
		ClampSpringAxis(state.positionOffset.x, state.positionVelocity.x, -ws.maxOffset, ws.maxOffset);
	}

	// Rotation: pitch (up/down look) → tilt around Z (lateral axis),
	//           roll (yaw-based lean) → roll around Y (forward axis)
	if (gs->enableRotation) {
		state.rotationOffset.z = SpringStep(state.rotationOffset.z, state.rotationVelocity.z,
			pitchForce, ws.stiffness, effDamp, ws.mass, delta);
		state.rotationOffset.y = SpringStep(state.rotationOffset.y, state.rotationVelocity.y,
			rollForce, ws.stiffness, effDamp, ws.mass, delta);
		ClampSpringAxis(state.rotationOffset.z, state.rotationVelocity.z, -ws.maxRotation, ws.maxRotation);
		ClampSpringAxis(state.rotationOffset.y, state.rotationVelocity.y, -ws.maxRotation, ws.maxRotation);
	}
}

void Inertia::InertiaManager::UpdateMovementSpring(
	SpringState& state, const WeaponInertiaSettings& ws,
	const RE::NiPoint3& localMove, float delta, float mult)
{
	auto* gs = Settings::GetSingleton();
	if (!ws.movementInertiaEnabled) return;
	if (!gs->movementInertiaEnabled) return;

	float str = gs->movementInertiaStrength;
	float thr = gs->movementInertiaThreshold;
	float strength = std::sqrt(localMove.x * localMove.x + localMove.y * localMove.y);

	if (strength < thr * 0.001f) {
		state.positionOffset = SpringStep3(state.positionOffset, state.positionVelocity,
			{ 0, 0, 0 }, ws.movementStiffness, ws.movementDamping, 1.0f, delta);
		state.rotationOffset = SpringStep3(state.rotationOffset, state.rotationVelocity,
			{ 0, 0, 0 }, ws.movementStiffness, ws.movementDamping, 1.0f, delta);
		return;
	}

	float lateral  = localMove.x * str * mult;
	float forward  = localMove.y * str * mult;

	if (localMove.x > 0) lateral *= ws.movementRightMult;
	else lateral *= ws.movementLeftMult;

	if (localMove.y > 0) forward *= ws.movementForwardMult;
	else forward *= ws.movementBackwardMult;

	if (ws.invertMovementLateral)    lateral  = -lateral;
	if (ws.invertMovementForwardBack) forward = -forward;

	// F4 FP skeleton bone-local axes: X = vertical, Y = forward, Z = lateral
	if (gs->enablePosition) {
		state.positionOffset = SpringStep3(state.positionOffset, state.positionVelocity,
			{ 0.0f, forward, lateral },
			ws.movementStiffness, ws.movementDamping, 1.0f, delta);
		ClampSpringAxis(state.positionOffset.z, state.positionVelocity.z, -ws.movementMaxOffset, ws.movementMaxOffset);
		ClampSpringAxis(state.positionOffset.y, state.positionVelocity.y, -ws.movementMaxOffset, ws.movementMaxOffset);
	}
	// Rotation lean: lateral sway rolls around Y (forward axis)
	if (gs->enableRotation) {
		state.rotationOffset = SpringStep3(state.rotationOffset, state.rotationVelocity,
			{ 0.0f, lateral * 0.5f, 0.0f },
			ws.movementStiffness, ws.movementDamping, 1.0f, delta);
		ClampSpringAxis(state.rotationOffset.y, state.rotationVelocity.y, -ws.movementMaxRotation, ws.movementMaxRotation);
	}
}

void Inertia::InertiaManager::UpdateImpulseSpring(
	ImpulseSpring& spring, float stiffness, float damping, float delta)
{
	spring.AdvanceBlend(delta);
	spring.state.positionOffset = SpringStep3(
		spring.state.positionOffset, spring.state.positionVelocity,
		{ 0, 0, 0 }, stiffness, damping, 1.0f, delta);
	spring.state.rotationOffset = SpringStep3(
		spring.state.rotationOffset, spring.state.rotationVelocity,
		{ 0, 0, 0 }, stiffness, damping, 1.0f, delta);
}

// ============================================================
// Apply combined offset to node
// ============================================================
static RE::NiPoint3 MatMulPoint(const RE::NiMatrix3& m, const RE::NiPoint3& p)
{
	return {
		m.entry[0].pt[0] * p.x + m.entry[0].pt[1] * p.y + m.entry[0].pt[2] * p.z,
		m.entry[1].pt[0] * p.x + m.entry[1].pt[1] * p.y + m.entry[1].pt[2] * p.z,
		m.entry[2].pt[0] * p.x + m.entry[2].pt[1] * p.y + m.entry[2].pt[2] * p.z
	};
}

void Inertia::InertiaManager::ApplyOffset(
	RE::NiNode* node, const SpringState& combined, const WeaponInertiaSettings& ws)
{
	if (!node) return;

	auto* gs = Settings::GetSingleton();

	// Build rotation matrix from spring offsets
	RE::NiMatrix3 rot;
	if (gs->enableRotation) {
		float rx = combined.rotationOffset.x * 0.01745329f;
		float ry = combined.rotationOffset.y * 0.01745329f;
		float rz = combined.rotationOffset.z * 0.01745329f;

		float cx = std::cos(rx), sx = std::sin(rx);
		float cy = std::cos(ry), sy = std::sin(ry);
		float cz = std::cos(rz), sz = std::sin(rz);

		rot.entry[0].pt[0] =  cy * cz;  rot.entry[0].pt[1] = -cy * sz;  rot.entry[0].pt[2] =  sy;
		rot.entry[1].pt[0] =  cx * sz + sx * sy * cz;
		rot.entry[1].pt[1] =  cx * cz - sx * sy * sz;
		rot.entry[1].pt[2] = -sx * cy;
		rot.entry[2].pt[0] =  sx * sz - cx * sy * cz;
		rot.entry[2].pt[1] =  sx * cz + cx * sy * sz;
		rot.entry[2].pt[2] =  cx * cy;
	} else {
		rot.MakeIdentity();
	}

	// Pivot correction: rotate around the pivot bone's position, not the
	// inserted bone's origin (which sits at the parent's position).
	// The pivot bone (e.g. Spine2) has local.translate T set by animation.
	// To rotate around T: translate = T + offset - R*T
	//
	// When useBindPosePivot is enabled, use the translate captured at bone
	// insertion time (skeleton rest pose) instead of the live animated value.
	// This prevents weapons whose idle animations heavily offset the spine
	// from throwing the inertia pivot off.
	RE::NiPoint3 pivotT = { 0.0f, 0.0f, 0.0f };
	if (cachedTargetNode) {
		pivotT = (ws.useBindPosePivot && hasRefPosePivot)
			? refPosePivotTranslate
			: cachedTargetNode->local.translate;
	}

	RE::NiPoint3 rotatedPivot = MatMulPoint(rot, pivotT);

	RE::NiPoint3 posOffset = gs->enablePosition
		? RE::NiPoint3{ combined.positionOffset.x, combined.positionOffset.y, combined.positionOffset.z }
		: RE::NiPoint3{ 0.0f, 0.0f, 0.0f };

	node->local.translate.x = pivotT.x + posOffset.x - rotatedPivot.x;
	node->local.translate.y = pivotT.y + posOffset.y - rotatedPivot.y;
	node->local.translate.z = pivotT.z + posOffset.z - rotatedPivot.z;
	node->local.rotate = rot;
}

// ============================================================
// Main Update
// ============================================================
void Inertia::InertiaManager::Update(float delta, float realDelta)
{
	elapsedTime += delta;
	if (pivotWarmupTimer > 0.0f) pivotWarmupTimer -= delta;

	auto* player = RE::PlayerCharacter::GetSingleton();
	// Pip-Boy (and holotape-in-Pip-Boy): clear spring / impulse carryover every frame
	// while open, even if master toggle is off, so re-enabling does not inherit stale state.
	if (player && IsPipboyMenuOpen()) {
		ResetSpringPhysicsState();
	}

	auto* gs = Settings::GetSingleton();

	// MASTER SWITCH — disables ALL features (inertia + extras)
	if (!gs->enabled) return;

	if (!player) return;

	// One-frame Havok variable-value cache for this actor (cleared at scope exit).
	HavokVar::BeginFrame(player);
	const HavokVar::FrameGuard _havokVarFrameGuard{};

	// During save load or cell transitions the player's 3D model isn't loaded yet.
	// Without 3D there's no skeleton to manipulate and no valid actor states to read.
	if (!player->Get3D()) return;

	const bool weaponDrawn = player->GetWeaponMagicDrawn();

	// ============================================================
	// EXTRAS — run regardless of inertia toggle, FP state, or
	// requireWeaponDrawn so they remain functional when only inertia
	// is disabled (and so WBFOV correctly tracks holster transitions).
	// ============================================================

	// Weapon Based FOV (applies viewmodel FOV per equipped weapon).
	// Tracks transitions so toggling the feature off/on works correctly.
	{
		static bool s_wbfovWasEnabled = false;
		if (gs->wbfovEnabled) {
			WeaponFOV::Manager::GetSingleton()->Update(player, weaponDrawn);
			s_wbfovWasEnabled = true;
		} else if (s_wbfovWasEnabled) {
			// Just got disabled — restore vanilla FOV once.
			WeaponFOV::Manager::GetSingleton()->RestoreDefault();
			s_wbfovWasEnabled = false;
		}
	}

	// Ensure ChamberExclusion keyword is on the equipped weapon instance
	// (throttled to once per second — instance patching is idempotent).
	// Only meaningful when a weapon is drawn.
	if (weaponDrawn) {
		chamberExclusionTimer += delta;
		if (chamberExclusionTimer >= 1.0f) {
			chamberExclusionTimer = 0.0f;
			ChamberExclusion::Manager::GetSingleton()->ApplyKeywordToEquippedInstance(player);
		}
	}

	// ============================================================
	// Fire on Empty (EXTRAS — must NOT sit behind the inertia gates)
	// ------------------------------------------------------------
	// When the equipped weapon's magazine is empty (0 rounds loaded,
	// ignoring inventory reserve) and the fire input is down, force the
	// weapon to run its fire ACTION so the fire animation plays anyway.
	// Mod authors then replace that forced fire animation with a dry-fire
	// animation via Open Animation Replacer conditions.
	//
	// Placement matters: this used to live further down, behind
	// `inertiaEnabled` / `ws.enabled` / `requireWeaponDrawn` early-outs.
	// Any weapon whose inertia preset was disabled never reached the
	// check at all (verified: SCAR dry-fire presses produced zero log
	// output while 1911 presses logged — the SCAR's type preset was
	// gated out before the input read). Extras must be self-contained,
	// so this block reads the fire input and ammo count itself.
	//
	// Trigger model: fires while the input is HELD, latched once per
	// hold. Holding an automatic's trigger until the mag runs dry never
	// produces a fresh press, so an edge-only check can never catch it.
	// The latch resets on release or when ammo returns. Reload detection
	// uses gunState (kReloading) rather than the anim-event-driven
	// isCurrentlyReloading flag, because that flag is only updated by
	// the inertia-gated code below and can be stale here.
	{
		auto* foeSettings = Settings::GetSingleton();

		// ---- Draw / holster windows (must run before the trigger gate) ----
		// FOE uses fireOnEmptyMotionLock (not isCurrentlyEquipping) so a
		// dry-fire stop cannot arm Early Equip / disable the attack handler.
		// Real equip still sets isCurrentlyEquipping later for Early Equip.
		//
		// wasWeaponDrawn is still last frame here (member updates later).
		// 2.5s covers long rifle draws — 1.5s expired while InitiateStart
		// was still firing and dry-fire clicked over the draw (2026-07-21).
		constexpr float kFOEMotionLock = 2.5f;
		if (animEventSink.beginWeaponDrawThisFrame.exchange(false, std::memory_order_relaxed) ||
		    (weaponDrawn && !wasWeaponDrawn)) {
			isCurrentlyEquipping = true;
			equipAnimTimer = std::max(equipAnimTimer, kFOEMotionLock);
			fireOnEmptyMotionLock = std::max(fireOnEmptyMotionLock, kFOEMotionLock);
		}
		if (animEventSink.sheatheStartedThisFrame.exchange(false, std::memory_order_relaxed) ||
		    (!weaponDrawn && wasWeaponDrawn)) {
			isCurrentlyHolstering = true;
			holsterAnimTimer = std::max(holsterAnimTimer, kFOEMotionLock);
			fireOnEmptyMotionLock = std::max(fireOnEmptyMotionLock, kFOEMotionLock);
			// Abort an in-flight dry-fire so it can't finish over the sheathe.
			if (fireOnEmptyAnimActive) {
				// Disarm the wedged-graph safety net on a successful reset —
				// same stale-gunState misfire as the main stop path (see the
				// comment there); a spurious forced reset mid-sheathe would
				// visibly re-draw the weapon the player is putting away.
				if (StopEmptyFireAnimation(player, "holster started")) {
					fireOnEmptyVerifyTimer = 0.0f;
				}
				fireOnEmptyAnimActive = false;
				fireOnEmptyStopTimer = 0.0f;
				fireOnEmptyDurationQueryTimer = 0.0f;
				fireOnEmptyAnimElapsed = 0.0f;
				fireOnEmptyWasADS = false;
				fireOnEmptyAdsHeldAtTrigger = false;
				fireOnEmptyGuardAdsRelease = false;
				fireOnEmptySuppressGrace = 0.3f;
			}
		}
		if (fireOnEmptyMotionLock > 0.0f) {
			fireOnEmptyMotionLock -= delta;
			if (fireOnEmptyMotionLock < 0.0f) fireOnEmptyMotionLock = 0.0f;
		}
		if (isCurrentlyHolstering) {
			holsterAnimTimer -= delta;
			// Sheathe finished once the engine reports undrawn, or the
			// safety timer expires (covers missed undrawn edges).
			if (!weaponDrawn || holsterAnimTimer <= 0.0f) {
				isCurrentlyHolstering = false;
				holsterAnimTimer = 0.0f;
			}
		}

		// Fire input from the AttackInput vtable hook (ground truth from
		// the engine's own ButtonEvent dispatch). The old raw-byte peek
		// (AttackBlockHandler @ 0x72) proved unreliable in-game — see the
		// AttackInput namespace comment. Fall back to the byte peek only
		// if the hook failed to install.
		bool foeFireHeld = false;
		if (AttackInput::s_installed) {
			foeFireHeld = AttackInput::s_fireHeld;
		} else if (auto* pcCtrl = RE::PlayerControls::GetSingleton(); pcCtrl && pcCtrl->attackHandler) {
			auto* handlerBytes = reinterpret_cast<const std::uint8_t*>(pcCtrl->attackHandler);
			foeFireHeld = (handlerBytes[0x72] != 0);
		}

		const std::uint32_t foeMagAmmo = EquippedWeapon::GetMagazineAmmoCount(player);
		const auto gsNow = static_cast<std::uint32_t>(player->gunState);
		const bool fireRisingEdge = foeFireHeld && !prevFireInputHeld;

		// Diagnostic: log every fresh fire press with full gate state so a
		// silent no-trigger is attributable to a specific condition.
		if (foeSettings->fireOnEmptyEnabled && fireRisingEdge) {
			bool firstPerson = false;
			if (auto* cam = RE::PlayerCamera::GetSingleton(); cam && cam->currentState) {
				const auto camState = cam->currentState->id.get();
				firstPerson = (camState == RE::CameraStates::kFirstPerson ||
				               camState == RE::CameraStates::kIronSights);
			}
			logger::info("[FireOnEmpty] Fire pressed: mag={} gs={} phantom={} fp={} drawn={} equipping={} holstering={} motionLock={:.2f}",
				foeMagAmmo, gsNow, earlyAdsAutoFireWatching, firstPerson, weaponDrawn,
				isCurrentlyEquipping, isCurrentlyHolstering, fireOnEmptyMotionLock);
		}

		// Latch release: trigger released or ammo returned (e.g. reloaded).
		if (!foeFireHeld || foeMagAmmo > 0) {
			fireOnEmptyLatched = false;
		}

		// ---- dry-fire idle lifecycle ----
		// The fire idle does NOT reliably end on its own: with a vanilla
		// fire animation the clip loops inside the idle for as long as the
		// idle runs (verified in-game 2026-07-16 23:29: SCAR auto loop
		// emitted WeaponFire every ~130ms for the full 1.5s until the
		// safety net force-reset the graph — the visible "re-equip").
		// So the idle is ALWAYS stopped explicitly, once the weapon's own
		// fire interval elapses — or immediately if ammo returns (a
		// reload is about to animate over it).
		// A custom OAR dry-fire replacement clip that doesn't loop simply
		// ends before this stop and the stop becomes a harmless no-op.
		//
		// NOT an early-exit condition: !foeFireHeld ("trigger released").
		// Automatic weapons deliver PrimaryAttack as a train of discrete
		// press/release ButtonEvent pulses at the weapon's own cyclic
		// rate even while the physical button stays held (verified
		// in-game 2026-07-21: foeFireHeld toggled false ~180ms into
		// every ADS dry-fire, well under the >=0.35s fireOnEmptyStopTimer
		// floor, cutting the idle short on literally every cycle of a
		// held trigger). Reacting to that pulse as "released" tore the
		// idle down before the clip's floor duration — the exact
		// "getting cut short" symptom. The floor timer alone is the
		// gate; a genuine quick tap-release just means the idle keeps
		// playing to the floor anyway, same as the comment above always
		// intended ("hold the fire idle long enough to be visible/audible").
		//
		// Still an immediate interrupt (bypasses the floor): the player
		// releasing ADS mid-idle, or starting a reload. Both are the
		// player actively initiating a real state change — sitting
		// through the remaining floor first would fight their input
		// instead of just honoring it. Distinct from the fire-button
		// pulsing above: ADS hold (SecondaryAttack) isn't re-pulsed by
		// the weapon's fire-rate the way PrimaryAttack is, so s_adsHeld
		// going false here is a genuine release, not a cyclic artifact.
		// reloadStartThisFrame is peeked (load, not exchange) so the
		// later consumer at the top of Update() still sees it.
		if (fireOnEmptyAnimActive) {
			fireOnEmptyStopTimer -= delta;
			fireOnEmptyAnimElapsed += delta;

			// Deferred OAR duration query (see OARFireIdleQuery comment):
			// the fire clip usually isn't visible to OAR on the trigger
			// frame, so retry each frame for a short window. On a hit,
			// replace the provisional floor with the clip's real
			// remaining time. The freshness bound grows with elapsed time
			// so a clip that activated a couple frames after the trigger
			// still qualifies.
			if (fireOnEmptyDurationQueryTimer > 0.0f) {
				fireOnEmptyDurationQueryTimer -= delta;
				const bool lastAttempt = fireOnEmptyDurationQueryTimer <= 0.0f;
				const float d = OARFireIdleQuery::QueryFireClipDuration(
					fireOnEmptyAnimElapsed + 0.1f, lastAttempt);
				if (d > 0.02f) {
					fireOnEmptyDurationQueryTimer = 0.0f;
					fireOnEmptyStopTimer =
						std::max(std::clamp(d, 0.05f, 2.0f) - fireOnEmptyAnimElapsed, 0.05f);
					logger::info("[FireOnEmpty] Stop timer updated to {:.3f}s remaining "
						"(OAR duration {:.3f}s, {:.3f}s already elapsed)",
						fireOnEmptyStopTimer, d, fireOnEmptyAnimElapsed);
				} else if (lastAttempt) {
					fireOnEmptyDurationQueryTimer = 0.0f;
					logger::info("[FireOnEmpty] No OAR fire clip found within query window — keeping floor timer");
				}
			}

			const bool adsReleasedMidFlight =
				fireOnEmptyWasADS && AttackInput::s_installed && !AttackInput::s_adsHeld;
			// Mirror case: PRESSING ADS during a hip-fire dry-fire. The
			// special idle owns the graph, so the engine eats the sighted
			// transition and then boots the player back to idle when the
			// idle ends (reported in-game 2026-07-22). Treat it like the
			// release case: player-initiated state change → interrupt now,
			// then the tap below re-enters ADS from the clean base state.
			const bool adsPressedMidFlight =
				!fireOnEmptyWasADS && !fireOnEmptyAdsHeldAtTrigger &&
				AttackInput::s_installed && AttackInput::s_adsHeld;
			const bool reloadInterrupt =
				animEventSink.reloadStartThisFrame.load(std::memory_order_relaxed);
			const char* stopReason =
				adsReleasedMidFlight           ? "ADS released" :
				adsPressedMidFlight            ? "ADS pressed" :
				reloadInterrupt                ? "reload started" :
				(fireOnEmptyStopTimer <= 0.0f) ? "fire cycle complete" :
				(foeMagAmmo > 0)               ? "ammo returned" :
				                                 nullptr;
			if (stopReason) {
				// Hard stop, always — the soft-stop ("park in the special
				// idle while ADS is held") experiment is dead: the parked
				// idle owns the whole first-person graph, so locomotion /
				// sway / everything else froze until release (reported
				// in-game 2026-07-22 01:28), and no engine-side exit can
				// run from the parked state (all five stimuli refused;
				// see SoftStop post-mortem above StopEmptyFireAnimation).
				const bool stopResetRan = StopEmptyFireAnimation(player, stopReason);
				// A successful stop leaves the graph AT base state — the
				// exact state the wedged-graph safety net below would force.
				// Disarm it: gunState keeps reading "firing" for a while
				// after the stop (we suppress the attackState Exit
				// annotation, so the engine's bookkeeping never sees the
				// cycle end), and the net misread that stale value as a
				// wedge, firing a second visible re-draw ~0.8s after every
				// few dry fires (in-game log 2026-07-22 23:18). Only a stop
				// whose InitializeToBaseState was REFUSED still needs it.
				if (stopResetRan) {
					fireOnEmptyVerifyTimer = 0.0f;
				}
				// Hard reset forces a re-draw even fast-forwarded;
				// lock FOE (not Early Equip) until that window clears.
				fireOnEmptyMotionLock = std::max(fireOnEmptyMotionLock, 0.6f);
				// ADS still physically held → re-enter sighted instantly
				// via a synthetic release+press tap and fast-forward the
				// aim-enter blend (see AttackInput::SimulateTap for why a
				// tap, not a bare press). Restores the graph to the REAL
				// sighted state, so locomotion works and the eventual
				// real button release plays the engine's natural exit.
				// Covers both directions: an ADS dry-fire ending with ADS
				// still held, and a hip dry-fire interrupted by an ADS
				// press (the press the special idle swallowed).
				if ((fireOnEmptyWasADS || adsPressedMidFlight) &&
				    !adsReleasedMidFlight && !reloadInterrupt &&
				    AttackInput::s_installed && AttackInput::s_adsHeld) {
					if (AttackInput::SimulateTap("SecondaryAttack")) {
						// Fast-forward only when RE-entering (the player was
						// already sighted before the dry-fire, so the aim-enter
						// transition replaying would look like a glitch). On a
						// fresh hip->ADS press the natural aim-enter blend is
						// exactly what the player expects — let it play.
						if (fireOnEmptyWasADS) {
							g_suppressEquipSounds.store(true, std::memory_order_relaxed);
							player->UpdateAnimation(1000.0f);
							g_suppressEquipSounds.store(false, std::memory_order_relaxed);
						}
						logger::info("[FireOnEmpty] {} ADS via synthetic tap (gs={})",
							fireOnEmptyWasADS ? "Re-entered" : "Entered",
							static_cast<std::uint32_t>(player->gunState));
					}
				} else if (adsReleasedMidFlight && AttackInput::s_installed &&
				           !AttackInput::s_adsHeld && !reloadInterrupt) {
					// ADS released mid-dry-fire: the hard stop above reset the
					// graph to the HIP base state and fast-forwarded the
					// re-draw, so without help the screen snaps from the aimed
					// dry-fire pose straight to hip idle — the reported "exit
					// snap" (2026-07-22 18:10). Rebuild the exit the engine
					// never got to play: synthetically re-enter sighted and
					// fast-forward the enter blend (invisible — sighted is
					// what was on screen anyway), then dispatch a synthetic
					// RELEASE so the engine runs its natural aim-exit blend
					// from the sighted state. Event pairing stays balanced:
					// the real release already landed, and this release pairs
					// with our synthetic press inside the tap.
					if (AttackInput::SimulateTap("SecondaryAttack")) {
						g_suppressEquipSounds.store(true, std::memory_order_relaxed);
						player->UpdateAnimation(1000.0f);
						g_suppressEquipSounds.store(false, std::memory_order_relaxed);
						AttackInput::DispatchButton("SecondaryAttack", 0.0f, 0.5f);
						logger::info("[FireOnEmpty] Reconstructed natural ADS exit (gs={})",
							static_cast<std::uint32_t>(player->gunState));
					}
				}
				fireOnEmptyAnimActive = false;
				fireOnEmptyStopTimer  = 0.0f;
				fireOnEmptyDurationQueryTimer = 0.0f;
				fireOnEmptyAnimElapsed = 0.0f;
				fireOnEmptyWasADS = false;
				fireOnEmptyAdsHeldAtTrigger = false;
				fireOnEmptySuppressGrace = 0.6f;
			}
		}

		// ---- safety net: wedged-graph recovery ----
		// Armed each time we trigger the dry-fire idle. The idle path
		// should never touch gunState (it bypasses the attack state
		// machine entirely — the loop only ever happened with the old
		// attackStart injection), so this should never fire; if gunState
		// nonetheless reads as firing on an empty magazine when the timer
		// expires, reset the graph to base state (NAF's proven recovery)
		// rather than leave the player stuck. mag==0 guards the check:
		// with ammo present a firing gunState is legitimate engine
		// activity, not our leftover.
		if (fireOnEmptyVerifyTimer > 0.0f) {
			fireOnEmptyVerifyTimer -= delta;
			if (fireOnEmptyVerifyTimer <= 0.0f) {
				fireOnEmptyVerifyTimer = 0.0f;
				if (foeMagAmmo == 0 && GunStateLocal::IsFiringGunState(gsNow)) {
					ForceGraphBaseStateReset(player);
					// Hard reset re-draws — block FOE (not Early Equip) briefly.
					fireOnEmptyMotionLock = std::max(fireOnEmptyMotionLock, 2.5f);
				}
			}
		}

		if (foeSettings->fireOnEmptyEnabled &&
		    foeFireHeld &&
		    !fireOnEmptyLatched &&
		    !fireOnEmptyAnimActive &&
		    foeMagAmmo == 0 &&
		    gsNow != GunStateLocal::kReloading &&
		    gsNow != GunStateLocal::kThrowing &&
		    !earlyAdsAutoFireWatching)
		{
			// First-person / iron-sights only (matches the mod's other
			// viewmodel-focused features).
			bool firstPerson = false;
			if (auto* cam = RE::PlayerCamera::GetSingleton(); cam && cam->currentState) {
				const auto camState = cam->currentState->id.get();
				firstPerson = (camState == RE::CameraStates::kFirstPerson ||
				               camState == RE::CameraStates::kIronSights);
			}

			// Idle, ADS idle, or fire/attack (gunState 0 / 6 / 7 / 8).
			// Mid-draw / mid-holster are blocked by motionLock + flags.
			const bool inAllowedGunState =
				gsNow == GunStateLocal::kDrawn ||
				gsNow == GunStateLocal::kSighted ||
				gsNow == GunStateLocal::kFire ||
				gsNow == GunStateLocal::kFireSighted;

			const bool motionBlocked =
				fireOnEmptyMotionLock > 0.0f ||
				isCurrentlyEquipping ||
				isCurrentlyHolstering;

			if (firstPerson && weaponDrawn && inAllowedGunState && !motionBlocked)
			{
				// One attempt per trigger hold, whatever the outcome — also
				// prevents per-frame log spam while holding on an empty mag.
				fireOnEmptyLatched = true;

				std::string eid = GetEquippedWeaponEditorID(player);
				if (eid.empty()) {
					logger::info("[FireOnEmpty] Skipped: equipped weapon has no EditorID");
				} else {
					FireOnEmpty::FOEEntry entry;
					const bool found = FireOnEmpty::Manager::GetSingleton()->GetEntry(eid, entry);
					if (found && entry.enabled) {
						// Open both suppress windows BEFORE PlayIdle —
						// first WeaponFire/attackState land synchronously.
						FireAnnotationGuard::s_suppressWeaponFire.store(true, std::memory_order_relaxed);
						FireAnnotationGuard::s_suppressAttackState.store(true, std::memory_order_relaxed);
						FireAnnotationGuard::s_suppress.store(false, std::memory_order_relaxed);

						const bool ok = TriggerEmptyFireAction(player, entry.autoFire);
						logger::info("[FireOnEmpty] Empty-fire on '{}' (autoFire={}, gs={}, edge={}) -> triggered={}",
							eid, entry.autoFire, gsNow, fireRisingEdge, ok);
						if (ok) {
							// Provisional stop timer: the tuned floor (0.5s,
							// per 2026-07-21 direction). The real bound-clip
							// duration comes from OAR's Clips API, but the
							// fire clip is only sometimes visible on the
							// trigger frame (see OARFireIdleQuery comment),
							// so the query is DEFERRED: retried each frame
							// for the next ~0.3s by the active block above.
							// If it never lands, this floor stands.
							fireOnEmptyAnimActive = true;
							fireOnEmptyStopTimer  =
								std::clamp(EquippedWeapon::GetWeaponFireInterval(player), 0.5f, 1.0f);
							fireOnEmptyDurationQueryTimer = 0.3f;
							fireOnEmptyAnimElapsed = 0.0f;
							logger::info("[FireOnEmpty] Stop timer provisionally {:.3f}s (floor; OAR query deferred)",
								fireOnEmptyStopTimer);
							fireOnEmptyVerifyTimer = 1.5f;
							// Preserve ADS when started sighted (6) or
							// firing-from-ADS (8).
							fireOnEmptyWasADS =
								gsNow == GunStateLocal::kSighted ||
								gsNow == GunStateLocal::kFireSighted;
							fireOnEmptyAdsHeldAtTrigger =
								AttackInput::s_installed && AttackInput::s_adsHeld;
							PushEvent("Fire on Empty triggered");
						}
					} else {
						logger::info("[FireOnEmpty] Skipped: '{}' entryFound={} enabled={}",
							eid, found, found && entry.enabled);
					}
				}
			} else if (firstPerson && fireRisingEdge) {
				logger::info("[FireOnEmpty] Gated: drawn={} allowedGS={} motionLock={:.2f} equipping={} holstering={} gs={}",
					weaponDrawn, inAllowedGunState, fireOnEmptyMotionLock,
					isCurrentlyEquipping, isCurrentlyHolstering, gsNow);
			}
		}

		// ---- annotation suppression (see FireAnnotationGuard) ----
		// weaponFire: idle in flight OR short blend-out grace (empty mag).
		// attackState: ONLY while the dry-fire idle is in flight — keeping
		// it open after ADS soft-stop blocked SightedRelease (2026-07-21).
		if (fireOnEmptySuppressGrace > 0.0f) {
			fireOnEmptySuppressGrace -= delta;
			if (fireOnEmptySuppressGrace < 0.0f) fireOnEmptySuppressGrace = 0.0f;
		}
		const bool foeOn = foeSettings->fireOnEmptyEnabled && foeMagAmmo == 0;
		FireAnnotationGuard::s_suppressWeaponFire.store(
			foeOn && (fireOnEmptyAnimActive || fireOnEmptySuppressGrace > 0.0f),
			std::memory_order_relaxed);
		FireAnnotationGuard::s_suppressAttackState.store(
			foeOn && fireOnEmptyAnimActive,
			std::memory_order_relaxed);
		FireAnnotationGuard::s_suppress.store(false, std::memory_order_relaxed);

		prevFireInputHeld = foeFireHeld;
	}

	// ============================================================
	// Repeatable Gun Bash (EXTRAS — self-contained like Fire on Empty)
	// ------------------------------------------------------------
	// Vanilla ignores Melee presses while a bash animation is playing.
	// This block makes bashes combo:
	//   * a Melee press during an active bash is QUEUED (capped),
	//   * the bash's HitFrame anim event + a configurable delay opens
	//     the combo window,
	//   * when the window is open the next queued bash fires through
	//     the action layer (TriggerGunBashAction), starting the new
	//     bash before the old animation has fully wound down,
	//   * anything still queued when the bash ends naturally fires on
	//     the falling edge instead.
	// Below the AP ("stamina") threshold the combo is disabled and
	// presses fall back to vanilla behavior (ignored mid-bash).
	//
	// "Bash active" = ActorState::meleeAttackState != 0 (3-bit field,
	// non-zero during melee/bash per the workspace reference §Melee/
	// Bash) with a gun equipped — gun-typed weapons only, so melee
	// weapon swings are not combo-ed by this feature.
	{
		auto* rgb = Settings::GetSingleton();

		const bool gunEquipped = weaponDrawn && EquippedWeapon::IsGunEquipped(player);
		const bool bashActive  = gunEquipped && player->meleeAttackState != 0;
		const bool hitFrame    = animEventSink.hitFrameThisFrame.exchange(false, std::memory_order_relaxed);

		// Consume the press edge every frame — a stale edge from a menu
		// or a melee-weapon swing must never count as a queue request.
		const bool meleePressed = MeleeInput::s_meleePressedEdge;
		MeleeInput::s_meleePressedEdge = false;

		if (bashRetriggerCooldown > 0.0f) {
			bashRetriggerCooldown -= delta;
			if (bashRetriggerCooldown < 0.0f) bashRetriggerCooldown = 0.0f;
		}

		if (!rgb->bashComboEnabled || !MeleeInput::s_installed || !gunEquipped) {
			// Feature off / hook missing / no gun: drop all combo state.
			bashQueuedCount     = 0;
			bashComboWindowOpen = false;
			bashComboDelayTimer = 0.0f;
			wasBashActive       = bashActive;
		} else {
			// AP percentage gate ("stamina"). True = combo allowed.
			auto staminaOk = [&]() {
				if (!rgb->bashComboStaminaThresholdEnabled || !avifActionPoints) return true;
				const float cur  = player->GetActorValue(*avifActionPoints);
				const float base = player->GetBaseActorValue(*avifActionPoints);
				if (base <= 0.0f) return true;
				return (cur / base) * 100.0f >= rgb->bashComboStaminaThreshold;
			};

			if (bashActive && !wasBashActive) {
				// Fresh bash started (player-initiated or our follow-up):
				// its own HitFrame must arm the next window.
				bashComboWindowOpen = false;
				bashComboDelayTimer = 0.0f;
			}

			if (bashActive) {
				// -- queue presses landing during the bash --
				if (meleePressed) {
					if (!staminaOk()) {
						logger::info("[GunBash] Queue blocked — AP below {:.0f}%% threshold",
							rgb->bashComboStaminaThreshold);
					} else if (bashQueuedCount < rgb->bashComboMaxQueue) {
						++bashQueuedCount;
						logger::info("[GunBash] Queued follow-up bash ({}/{})",
							bashQueuedCount, rgb->bashComboMaxQueue);
						PushEvent("Gun bash queued");
					} else {
						logger::trace("[GunBash] Queue full ({})", bashQueuedCount);
					}
				}

				// -- HitFrame arms the combo delay --
				if (hitFrame && !bashComboWindowOpen && bashComboDelayTimer <= 0.0f) {
					bashComboDelayTimer = rgb->bashComboDelay;
					if (bashComboDelayTimer <= 0.0f) bashComboWindowOpen = true;
					logger::trace("[GunBash] HitFrame — combo window in {:.2f}s", rgb->bashComboDelay);
				}
				if (bashComboDelayTimer > 0.0f) {
					bashComboDelayTimer -= delta;
					if (bashComboDelayTimer <= 0.0f) {
						bashComboDelayTimer = 0.0f;
						bashComboWindowOpen = true;
						logger::trace("[GunBash] Combo window open");
					}
				}

				// -- consume the queue once the window is open --
				if (bashComboWindowOpen && bashQueuedCount > 0 && bashRetriggerCooldown <= 0.0f) {
					if (!staminaOk()) {
						// AP fell below the threshold mid-combo — drop the queue.
						logger::info("[GunBash] Combo cancelled — AP below threshold ({} queued dropped)",
							bashQueuedCount);
						bashQueuedCount = 0;
					} else if (TriggerGunBashAction(player)) {
						--bashQueuedCount;
						bashComboWindowOpen = false;  // new bash → wait for ITS HitFrame
						bashComboDelayTimer = 0.0f;
						bashRetriggerCooldown = 0.15f;
						PushEvent("Gun bash combo");
					} else {
						// Both trigger paths failed — drop the queue rather
						// than retrying every frame.
						logger::warn("[GunBash] Follow-up trigger failed — dropping queue ({})",
							bashQueuedCount);
						bashQueuedCount = 0;
					}
				}
			} else if (wasBashActive) {
				// Bash ended (falling edge).
				if (bashRetriggerCooldown > 0.0f) {
					// We injected a follow-up moments ago and its
					// meleeAttackState hasn't registered yet — this edge is
					// the OLD bash ending, not a combo break. Keep the queue
					// for the incoming bash.
				} else if (bashQueuedCount > 0 && staminaOk()) {
					// Queue leftover (e.g. HitFrame window never opened) —
					// fire the next bash back-to-back.
					if (TriggerGunBashAction(player)) {
						--bashQueuedCount;
						bashRetriggerCooldown = 0.15f;
						PushEvent("Gun bash combo (chained)");
					} else {
						bashQueuedCount = 0;
					}
				} else {
					bashQueuedCount = 0;
				}
				bashComboWindowOpen = false;
				bashComboDelayTimer = 0.0f;
			}

			wasBashActive = bashActive;
		}

		// Visual blend across the follow-up's graph reset (see BashBlend
		// at the top of the file). Capture and blend start happen inside
		// TriggerGunBashAction; here we only run the decay clock.
		// BashBlend::Apply at the end of Update re-derives the pose
		// delta fresh and writes the blend bone for this frame.
		BashBlend::Tick(delta);
	}

	// ============================================================
	// INERTIA + GAMEPLAY EXTRAS
	// ------------------------------------------------------------
	// `inertiaEnabled`, `requireWeaponDrawn`, and the per-weapon-type
	// `ws.enabled` flag used to be hard early-returns here, which silently
	// disabled every gameplay extra below them (Early ADS Return, Early
	// Equip, Early Fire Cancel, phantom-fire override, Super Sprint, Air
	// Walk Prevention) whenever inertia was off for ANY reason — e.g. a
	// weapon type with its inertia preset disabled also lost Early ADS.
	// They are now folded into a single `springsActive` flag that gates
	// ONLY the visual spring / impulse / viewmodel-offset work; the
	// gameplay extras run regardless.
	// ============================================================

	if (!hasLoggedGraphVars) {
		hasLoggedGraphVars = true;
		HavokVar::LogAllAttackVars(player);
	}

	// First-person only from here down. This is a real gate (not part of
	// springsActive): both the inertia springs and the gameplay extras are
	// designed around the first-person rig and FP input flow.
	auto* camera = RE::PlayerCamera::GetSingleton();
	if (!camera) return;
	bool inFP = false;
	if (camera->currentState) {
		auto stateID = camera->currentState->id;
		inFP = (stateID == RE::CameraStates::kFirstPerson || stateID == RE::CameraStates::kIronSights);
	}
	if (!inFP) {
		// Reset springs when leaving FP so no pop when returning
		if (isInFirstPerson) { Reset(); isInFirstPerson = false; }
		return;
	}
	isInFirstPerson = true;

	// Get current weapon settings (per-type or per-weapon preset). Fetched
	// unconditionally — extras read their per-type settings (earlyAds*,
	// earlyEquip*, earlyFireCancel*) from here even when ws.enabled is off.
	const WeaponInertiaSettings& ws = GetCurrentWeaponSettings(player);

	// Springs gate: master inertia toggle + optional weapon-drawn
	// requirement + per-weapon-type inertia enable. Gates ONLY the visual
	// inertia (impulse fires, spring feeds, viewmodel offset application).
	const bool springsActive = gs->inertiaEnabled &&
	                           (!gs->requireWeaponDrawn || weaponDrawn) &&
	                           ws.enabled;

	// Falling edge: drain all spring energy once so no stale offset stays
	// applied to the viewmodel and re-enabling starts clean (no pop).
	// Physics-only reset — gameplay-extra state must survive this.
	if (!springsActive && springsWereActive) {
		ResetSpringPhysicsState();
	}
	springsWereActive = springsActive;

	// Computed once per frame: no new impulses should fire while Pip-Boy is open.
	// Springs are still integrated and drained (reset before COMBINE); we simply
	// prevent any new velocity from being kicked in while the menu is up.
	const bool pipboyOpen = IsPipboyMenuOpen();

	// ---- EQUIP IMPULSE ----
	// Use the formID already cached by GetCurrentWeaponSettings (which
	// did a single atomic fetch) instead of re-querying equippedItems.
	std::uint32_t currentFormID = cachedWeaponSettingsFormID;

	// Ignore transient nullptr from equippedItems — if weapon is drawn
	// but equippedItems was empty, keep previous formID.
	if (currentFormID == 0 && weaponDrawn && cachedEquippedFormID != 0) {
		currentFormID = cachedEquippedFormID;
	}

	bool justEquipped = false;
	if (!wasWeaponDrawn && weaponDrawn) {
		justEquipped = true;
	} else if (currentFormID != cachedEquippedFormID && weaponDrawn && currentFormID != 0) {
		justEquipped = true;
	}

	cachedEquippedFormID = currentFormID;
	wasWeaponDrawn       = weaponDrawn;

	if (justEquipped) {
		cachedInsertedBone = nullptr;
		cachedTargetNode   = nullptr;
		hasRefPosePivot    = false;
		hasLoggedSkeleton  = false;
		pivotWarmupTimer   = 0.5f;
		isCurrentlyEquipping = true;
		equipAnimTimer       = 2.5f;
		fireOnEmptyMotionLock = std::max(fireOnEmptyMotionLock, 2.5f);
		earlyEquipAdsArmed   = false;
		earlyEquipFireArmed  = false;
		earlyEquipPending    = false;
		earlyEquipTimer      = 0.0f;

		ChamberExclusion::Manager::GetSingleton()->ApplyKeywordToEquippedInstance(player);
	}

	if (justEquipped && springsActive && ws.equipImpulseEnabled && !pipboyOpen) {
		equipImpulse.Fire(
			{ ws.equipImpulseZ, ws.equipImpulseY, ws.equipImpulseX },
			{ ws.equipRotImpulse, 0.0f, 0.0f },
			ws.equipBlendTime);
		PushEvent("Equip impulse");
	}

	// ---- Consume all reload anim events once (shared by reload impulse + early ADS return) ----
	bool reloadStarted  = animEventSink.reloadStartThisFrame.exchange(false, std::memory_order_relaxed);
	bool reloadEnd      = animEventSink.reloadEndThisFrame.exchange(false, std::memory_order_relaxed);
	bool reloadComplete = animEventSink.reloadCompleteThisFrame.exchange(false, std::memory_order_relaxed);
	bool initiateStart  = animEventSink.initiateStartThisFrame.exchange(false, std::memory_order_relaxed);

	if (reloadStarted)  logger::trace("[Update] consumed reloadStarted");
	if (reloadEnd)       logger::trace("[Update] consumed reloadEnd");
	if (reloadComplete)  logger::trace("[Update] consumed reloadComplete");
	if (initiateStart)   logger::trace("[Update] consumed initiateStart");

	// ---- Detect empty vs tactical reload (only lock when reload actually starts) ----
	if (reloadStarted) {
		std::uint32_t currentAmmo = 0;
		if (player->currentProcess && player->currentProcess->middleHigh) {
			RE::BSAutoLock lock{ player->currentProcess->middleHigh->equippedItemsLock };
			auto& items = player->currentProcess->middleHigh->equippedItems;
			if (items.size() > 0) {
				auto& eq = items[0];
				auto* wd = static_cast<RE::EquippedWeaponData*>(eq.data.get());
				if (eq.equipIndex.index == 0 && wd) {
					currentAmmo = wd->ammoCount;
				}
			}
		}
		lastReloadWasEmpty = (currentAmmo == 0);
		logger::info("[Reload] ammoCount={} -> {}",
			currentAmmo, lastReloadWasEmpty ? "EMPTY" : "TACTICAL");
	}

	// ---- Reload impulse (split: tactical vs empty) ----
	{
		// Select trigger event & settings based on empty vs tactical
		bool useEmpty = lastReloadWasEmpty;
		int  trigEvent = useEmpty ? ws.emptyReloadTriggerEvent : ws.reloadTriggerEvent;
		bool enabled   = useEmpty ? ws.emptyReloadImpulseEnabled : ws.reloadImpulseEnabled;

		bool triggered = false;
		switch (trigEvent) {
			case 1:  triggered = initiateStart;  break;
			case 2:  triggered = reloadComplete; break;
			default: triggered = reloadEnd;      break;
		}

		auto fireReload = [&](const char* label) {
			if (useEmpty) {
				emptyReloadImpulse.Fire(
					{ ws.emptyReloadImpulseZ, ws.emptyReloadImpulseY, ws.emptyReloadImpulseX },
					{ ws.emptyReloadRotImpulse, 0.0f, 0.0f },
					ws.emptyReloadImpulseBlendTime);
			} else {
				reloadImpulse.Fire(
					{ ws.reloadImpulseZ, ws.reloadImpulseY, ws.reloadImpulseX },
					{ ws.reloadRotImpulse, 0.0f, 0.0f },
					ws.reloadImpulseBlendTime);
			}
			logger::info("[Reload] Fired: {} (lastReloadWasEmpty={})", label, useEmpty);
			PushEvent(label);
		};

		float& delayTimer = useEmpty ? emptyReloadImpulseDelayTimer : reloadImpulseDelayTimer;
		float  delayVal   = useEmpty ? ws.emptyReloadImpulseDelay   : ws.reloadImpulseDelay;

		if (triggered && enabled && springsActive && !pipboyOpen) {
			if (delayVal <= 0.0f) {
				fireReload(useEmpty ? "Empty reload impulse" : "Tactical reload impulse");
			} else {
				delayTimer = delayVal;
			}
		}

		if (pipboyOpen) {
			delayTimer = 0.0f;  // cancel any pending delayed reload impulse
		} else if (delayTimer > 0.0f) {
			delayTimer -= delta;
			if (delayTimer <= 0.0f) {
				delayTimer = 0.0f;
				if (enabled && springsActive) {
					fireReload(useEmpty ? "Empty reload impulse (delayed)" : "Tactical reload impulse (delayed)");
				}
			}
		}
	}

	// ---- ADS detection + impulses ----
	bool isCurrentlyADS  = IsADS(camera);
	bool isCurrentlyScoped = IsScoped(camera);

	if (!wasADS && isCurrentlyADS) {
		// --- Spring dampening on ADS enter ---
		// Dampens both velocity (future motion) and current offset (existing displacement)
		// so both residual swinging and current arm position snap toward neutral.
		if (springsActive && ws.adsTransitionDampenEnabled) {
			float f = ws.adsTransitionDampenFactor;
			// Velocity -- controls future spring movement
			cameraSpring.positionVelocity.x *= f; cameraSpring.positionVelocity.y *= f; cameraSpring.positionVelocity.z *= f;
			cameraSpring.rotationVelocity.x *= f; cameraSpring.rotationVelocity.y *= f; cameraSpring.rotationVelocity.z *= f;
			movementSpring.positionVelocity.x *= f; movementSpring.positionVelocity.y *= f; movementSpring.positionVelocity.z *= f;
			movementSpring.rotationVelocity.x *= f; movementSpring.rotationVelocity.y *= f; movementSpring.rotationVelocity.z *= f;
			sprintSpring.positionVelocity.x *= f; sprintSpring.positionVelocity.y *= f; sprintSpring.positionVelocity.z *= f;
			jumpSpring.positionVelocity.x *= f; jumpSpring.positionVelocity.y *= f; jumpSpring.positionVelocity.z *= f;
			// Offset -- kills existing visual displacement so it doesn't carry over into ADS
			cameraSpring.positionOffset.x *= f; cameraSpring.positionOffset.y *= f; cameraSpring.positionOffset.z *= f;
			cameraSpring.rotationOffset.x *= f; cameraSpring.rotationOffset.y *= f; cameraSpring.rotationOffset.z *= f;
			movementSpring.positionOffset.x *= f; movementSpring.positionOffset.y *= f; movementSpring.positionOffset.z *= f;
			movementSpring.rotationOffset.x *= f; movementSpring.rotationOffset.y *= f; movementSpring.rotationOffset.z *= f;
			sprintSpring.positionOffset.x *= f; sprintSpring.positionOffset.y *= f; sprintSpring.positionOffset.z *= f;
			jumpSpring.positionOffset.x *= f; jumpSpring.positionOffset.y *= f; jumpSpring.positionOffset.z *= f;
		}
		// --- ADS enter impulse ---
		if (springsActive && ws.adsEnterImpulseEnabled && !pipboyOpen) {
			float scale = 1.0f;
			if (earlyAdsTriggered) {
				scale = ws.earlyAdsReturnImpulseScale;
				earlyAdsTriggered = false;
				logger::debug("[EarlyADS] Dampened ADS enter impulse to {:.0f}%", scale * 100.0f);
			}
			adsEnterImpulse.Fire(
				{ ws.adsEnterImpulseZ * scale, ws.adsEnterImpulseY * scale, ws.adsEnterImpulseX * scale },
				{ ws.adsEnterRotImpulse * scale, 0.0f, 0.0f },
				0.0f);
			PushEvent("ADS enter impulse");
		}
		// --- Start procedural enter transition ---
		if (springsActive && ws.adsEnterTransition.enabled) {
			auto* idata = GetEquippedWeaponInstanceData(player);
			float dur = GetSightedTransitionSeconds(idata);
			if (dur > 0.0f) {
				adsTransitionActive   = true;
				adsTransitionIsEnter  = true;
				adsTransitionDuration = dur;
				adsTransitionTimer    = 0.0f;
				adsTransitionProgress = 0.0f;
			}
		}
	}
	if (wasADS && !isCurrentlyADS) {
		// --- Spring dampening on ADS exit ---
		if (springsActive && ws.adsTransitionDampenEnabled) {
			float f = ws.adsTransitionDampenFactor;
			cameraSpring.positionVelocity.x *= f; cameraSpring.positionVelocity.y *= f; cameraSpring.positionVelocity.z *= f;
			cameraSpring.rotationVelocity.x *= f; cameraSpring.rotationVelocity.y *= f; cameraSpring.rotationVelocity.z *= f;
			movementSpring.positionVelocity.x *= f; movementSpring.positionVelocity.y *= f; movementSpring.positionVelocity.z *= f;
			movementSpring.rotationVelocity.x *= f; movementSpring.rotationVelocity.y *= f; movementSpring.rotationVelocity.z *= f;
			sprintSpring.positionVelocity.x *= f; sprintSpring.positionVelocity.y *= f; sprintSpring.positionVelocity.z *= f;
			jumpSpring.positionVelocity.x *= f; jumpSpring.positionVelocity.y *= f; jumpSpring.positionVelocity.z *= f;
			cameraSpring.positionOffset.x *= f; cameraSpring.positionOffset.y *= f; cameraSpring.positionOffset.z *= f;
			cameraSpring.rotationOffset.x *= f; cameraSpring.rotationOffset.y *= f; cameraSpring.rotationOffset.z *= f;
			movementSpring.positionOffset.x *= f; movementSpring.positionOffset.y *= f; movementSpring.positionOffset.z *= f;
			movementSpring.rotationOffset.x *= f; movementSpring.rotationOffset.y *= f; movementSpring.rotationOffset.z *= f;
			sprintSpring.positionOffset.x *= f; sprintSpring.positionOffset.y *= f; sprintSpring.positionOffset.z *= f;
			jumpSpring.positionOffset.x *= f; jumpSpring.positionOffset.y *= f; jumpSpring.positionOffset.z *= f;
		}
		// --- ADS exit impulse ---
		if (springsActive && ws.adsExitImpulseEnabled && !pipboyOpen) {
			adsExitImpulse.Fire(
				{ ws.adsExitImpulseZ, ws.adsExitImpulseY, ws.adsExitImpulseX },
				{ ws.adsExitRotImpulse, 0.0f, 0.0f },
				0.0f);
			PushEvent("ADS exit impulse");
		}
		// --- Start procedural exit transition ---
		if (springsActive && ws.adsExitTransition.enabled) {
			auto* idata = GetEquippedWeaponInstanceData(player);
			float dur = GetSightedTransitionSeconds(idata);
			if (dur > 0.0f) {
				adsTransitionActive   = true;
				adsTransitionIsEnter  = false;
				adsTransitionDuration = dur;
				adsTransitionTimer    = 0.0f;
				adsTransitionProgress = 0.0f;
			}
		}
		// Also cancel any in-progress enter transition
		if (adsTransitionActive && adsTransitionIsEnter)
			adsTransitionActive = false;
	}
	wasADS    = isCurrentlyADS;
	wasScoped = isCurrentlyScoped;

	// ---- EARLY ADS RETURN (Extras feature) ----
	// Arms when the player is holding ADS input during a reload (whether they
	// were ADS before it started or pressed ADS mid-reload). Ends the reload
	// early on the configured trigger event + delay, letting the game's own
	// input system re-enter ADS naturally.

	if (uncullBoneReplayFrames > 0) {
		DispatchWeaponUncullRepair(player);
		--uncullBoneReplayFrames;
	}

	// Consume the sightedExit flag (fires same frame as reloadStateEnter)
	animEventSink.sightedExitThisFrame.exchange(false, std::memory_order_relaxed);

	// Track reload state via anim events
	if (reloadStarted) {
		isCurrentlyReloading = true;
		reloadElapsedTime = 0.0f;
	}
	if (reloadEnd) {
		lastMeasuredReloadDuration = reloadElapsedTime;
		isCurrentlyReloading = false;
		recentlyReloadedTimer = 2.0f;
	}
	if (isCurrentlyReloading)
		reloadElapsedTime += delta;
	if (recentlyReloadedTimer > 0.0f) recentlyReloadedTimer -= delta;

	// Read ADS / Fire input state.
	// Fire comes from the AttackInput vtable hook (engine-dispatched
	// "PrimaryAttack" ButtonEvents — ground truth). The old raw-byte peek
	// of AttackBlockHandler @ 0x72 proved unreliable in-game (whole
	// sessions of live fire never read non-zero); it remains only as a
	// fallback if the hook failed to install.
	// ADS keeps the generic HeldStateHandler::heldStateActive flag for
	// compatibility with the existing EarlyADS arm behavior (it's a
	// "either attack button held" flag, which EarlyADS was tuned around).
	bool adsInputHeld  = false;
	bool fireInputHeld = false;
	auto* pc = RE::PlayerControls::GetSingleton();
	RE::HeldStateHandler* atkHandler = nullptr;
	if (pc && pc->attackHandler) {
		atkHandler = reinterpret_cast<RE::HeldStateHandler*>(pc->attackHandler);
		// Generic flag (kept for compatibility with the existing EarlyADS arm).
		adsInputHeld = atkHandler->heldStateActive;
		if (AttackInput::s_installed) {
			fireInputHeld = AttackInput::s_fireHeld;
		} else {
			auto* base = reinterpret_cast<const std::uint8_t*>(pc->attackHandler);
			fireInputHeld = (base[0x72] != 0);
		}
	}

	// Force-idle: keep the ADS/attack handler suppressed for a few frames
	// after Early ADS so the game transitions through idle (gunState 0)
	// before naturally re-entering ADS.  This gives the attack state machine
	// the clean idle reset it needs for automatic fire.
	if (earlyAdsForceIdleFrames > 0) {
		--earlyAdsForceIdleFrames;
		if (atkHandler) {
			atkHandler->heldStateActive = false;
			atkHandler->inputEventHandlingEnabled = false;
		}
		HavokVar::SetBool(player, HavokVar::kIsReloading, false);
		auto idleGS = static_cast<std::uint32_t>(player->gunState);
		logger::info("[EarlyADS-Idle] frame={} gs={} (forcing idle, isReloading→false)",
			earlyAdsForceIdleCountdown - earlyAdsForceIdleFrames, idleGS);
		if (earlyAdsForceIdleFrames == 0 && atkHandler) {
			atkHandler->inputEventHandlingEnabled = true;
			logger::info("[EarlyADS-Idle] Released — handler restored");

			if (ws.earlyAdsAutoFireEnabled && IsWeaponAutomatic(player)) {
				earlyAdsAutoFireWatching     = true;
				earlyAdsAutoFireTimer        = 5.0f;   // safety cap on phantom-override duration
				earlyAdsAutoFireAttempts     = 0;
				earlyAdsAutoFireGraceTimer   = 0.0f;   // ready to fire immediately on first anim event
				earlyAdsAutoFirePhantomGap   = 0.0f;
				earlyAdsAutoFireSeenGS8      = false;
				logger::info("[EarlyADS-AutoFire] Phantom override armed (auto weapon) — will drive QueueWeaponFire from anim events");
			}
		}
	}

	// Track per-frame ammo count for engine-discharge detection.  Compared
	// against the same value on the next frame so we can tell whether a
	// shot consumed ammo (real discharge) since we last looked.
	const std::uint32_t curMagAmmo  = EquippedWeapon::GetMagazineAmmoCount(player);
	const bool          ammoDecreased = ammoCountInitialized && (curMagAmmo < lastEquippedAmmoCount);

	// Auto-arm phantom override on detection (handles non-ADS / hipfire
	// early-fire too).  REQUIRED gates (all must hold to auto-arm):
	//   - recentlyReloadedTimer > 0 (within 2s window after reload end)
	//   - weaponFire anim event fired this frame
	//   - gunState ∈ {kFire (7), kFireSighted (8)} — player is actually in
	//     a firing state.  Tightened from `>= 4` because that incorrectly
	//     included kReloading (4), kThrowing (5), and kSighted (6) — none
	//     of which represent real firing per CommonLibF4's GUN_STATE enum.
	//   - attack SM is idle (atkState==0 && !attacking) — only auto-arm
	//     when the engine pipeline is NOT already running.  We still arm
	//     it via the EarlyADS-Idle path even if engine claims attacking,
	//     and rely on ammo-decrement detection below to avoid duplicates.
	if (!earlyAdsAutoFireWatching && ws.earlyAdsAutoFireEnabled && recentlyReloadedTimer > 0.0f) {
		auto probeGS = static_cast<std::uint32_t>(player->gunState);
		bool gotFireProbe = animEventSink.firedThisFrame.load(std::memory_order_relaxed);
		const bool probeIsFiringState = GunStateLocal::IsFiringGunState(probeGS);
		if (gotFireProbe && probeIsFiringState) {
			bool probeAttacking = false;
			std::int32_t probeAtkState = 0;
			HavokVar::GetBool(player, HavokVar::kIsAttacking, probeAttacking);
			HavokVar::GetInt(player, HavokVar::kIAttackState, probeAtkState);
			if (probeAtkState == 0 && !probeAttacking) {
				earlyAdsAutoFireWatching   = true;
				earlyAdsAutoFireTimer      = 5.0f;
				earlyAdsAutoFireAttempts   = 0;
				earlyAdsAutoFireGraceTimer = 0.0f;
				earlyAdsAutoFirePhantomGap = 0.0f;
				logger::info("[Phantom-Fire] Auto-armed (gs={}, atkState=0, attacking=false, mag={}) — recently reloaded, will drive QueueWeaponFire from anim events",
					probeGS, curMagAmmo);
				PushEvent("Phantom override: auto-armed");
			}
		}
	}

	// Phantom-fire override.  Behaviour change vs the previous version:
	//
	//   * The "exit on state machine engaged" condition has been REMOVED.
	//     After Early ADS / Early Fire Cancel, the engine's attack pipeline
	//     reports `attacking=true` and emits weaponFire anim events at the
	//     correct cadence — but it silently fails to actually discharge
	//     ammo (verified in logs: 10+ weaponFire events, zero ammoCount
	//     decrement).  Exiting on state-machine-engaged left the player in
	//     animation-only-fire purgatory.  We now stay armed and use
	//     ammo-decrement detection to tell whether the engine is firing
	//     for real, instead of trusting its `attacking` flag.
	//
	//   * Forced shots are paced by the WEAPON'S OWN fire interval
	//     (RangedData::fireSeconds / InstanceData::speed).  Previously we
	//     used a flat 30ms grace, which is up to 3× faster than the
	//     auto-fire WAV's loop_end marker can be reached, leaving the
	//     loop sound permanently re-entering its spin-up and never
	//     getting a chance to play its spin-down (the persistent
	//     auto-fire loop sound bug the user reported).
	//
	//   * If the engine already discharged this frame (ammoDecreased),
	//     we DON'T queue a duplicate shot — but we DO reset our pacing
	//     timer to the weapon's interval so we'd resume firing if the
	//     engine drops out again mid-burst.  This avoids double-fire
	//     when the engine is functioning normally.
	if (earlyAdsAutoFireWatching) {
		earlyAdsAutoFireTimer -= delta;
		earlyAdsAutoFireGraceTimer -= delta;
		earlyAdsAutoFirePhantomGap += delta;

		auto afGS = static_cast<std::uint32_t>(player->gunState);
		bool gotFireEvent = animEventSink.firedThisFrame.load(std::memory_order_relaxed);
		const bool afIsFiringState = GunStateLocal::IsFiringGunState(afGS);

		bool afReloading = false, afAttacking = false;
		std::int32_t afAtkState = 0;
		HavokVar::GetBool(player, HavokVar::kIsReloading, afReloading);
		HavokVar::GetBool(player, HavokVar::kIsAttacking, afAttacking);
		HavokVar::GetInt(player, HavokVar::kIAttackState, afAtkState);

		if (afReloading) {
			HavokVar::SetBool(player, HavokVar::kIsReloading, false);
		}

		// Engine fired this frame on its own — credit it as a phantom
		// shot (so safety counters/log line up) and skip our forced shot.
		if (ammoDecreased) {
			++earlyAdsAutoFireAttempts;
			earlyAdsAutoFirePhantomGap = 0.0f;
			// Reset pacing to weapon's natural interval — we'll only
			// fire ourselves if the engine misses the next one.
			earlyAdsAutoFireGraceTimer = EquippedWeapon::GetWeaponFireInterval(player);
			logger::trace("[Phantom-Fire] Engine fired (mag {}→{}, shots={}) — skip duplicate, repace {:.3f}s",
				lastEquippedAmmoCount, curMagAmmo, earlyAdsAutoFireAttempts, earlyAdsAutoFireGraceTimer);
		}

		if (!afIsFiringState && earlyAdsAutoFirePhantomGap > 0.05f) {
			// Player released trigger — gunState dropped out of the
			// firing range (kFire/kFireSighted).  Exit immediately and
			// best-effort silence the loop sound.  Fade duration is
			// controlled by Settings::autoFireSoundFadeMs (UI slider).
			RE::BSFixedString evtStop("attackStop");
			RE::BSFixedString evtAttackEnd("AttackEnd");
			RE::BSFixedString evtSoundStop("SoundStop");
			player->NotifyAnimationGraphImpl(evtStop);
			player->NotifyAnimationGraphImpl(evtAttackEnd);
			player->NotifyAnimationGraphImpl(evtSoundStop);
			HavokVar::SetBool(player, HavokVar::kIsAttacking, false);
			int faded = 0;
			const std::uint16_t fadeMs = static_cast<std::uint16_t>(
				std::clamp(gs->autoFireSoundFadeMs, 0, 5000));
			if (gs->autoFireSoundFadeEnabled)
				faded = LocalSound::FadeOutAllPlayerWeaponSounds(player, fadeMs);
			logger::info("[Phantom-Fire] gunState left firing range (gs={}, shots={}, fadeMs={}, fadedHandles={}) — exit phantom override",
				afGS, earlyAdsAutoFireAttempts, fadeMs, faded);
			earlyAdsAutoFireWatching = false;
			PushEvent("AutoFire: trigger released");
		} else if (earlyAdsAutoFireTimer <= 0.0f) {
			// Safety cap (5s) — force stop phantom loop and exit.
			RE::BSFixedString evtStop("attackStop");
			RE::BSFixedString evtAttackEnd("AttackEnd");
			RE::BSFixedString evtSoundStop("SoundStop");
			player->NotifyAnimationGraphImpl(evtStop);
			player->NotifyAnimationGraphImpl(evtAttackEnd);
			player->NotifyAnimationGraphImpl(evtSoundStop);
			HavokVar::SetBool(player, HavokVar::kIsAttacking, false);
			int faded = 0;
			const std::uint16_t fadeMs = static_cast<std::uint16_t>(
				std::clamp(gs->autoFireSoundFadeMs, 0, 5000));
			if (gs->autoFireSoundFadeEnabled)
				faded = LocalSound::FadeOutAllPlayerWeaponSounds(player, fadeMs);
			logger::warn("[Phantom-Fire] Safety timeout (5s) — force attackStop+AttackEnd+SoundStop+FadeOut, exit (shots={}, fadeMs={}, fadedHandles={})",
				earlyAdsAutoFireAttempts, fadeMs, faded);
			earlyAdsAutoFireWatching = false;
		} else if (gotFireEvent && afIsFiringState && !ammoDecreased) {
			// Real anim fire event in a real firing state, and the
			// engine did NOT just discharge a shot — convert into a
			// real discharge, paced by the weapon's own fire interval.
			earlyAdsAutoFirePhantomGap = 0.0f;
			if (earlyAdsAutoFireGraceTimer <= 0.0f) {
				auto* base = GetEquippedWeaponBase(player);
				auto* weap = base ? base->As<RE::TESObjectWEAP>() : nullptr;
				auto* ammo = player->GetCurrentAmmo(RE::BGSEquipIndex{ 0 });
				if (weap) {
					auto* tqi = TaskQueueInterfaceLocal::GetSingleton();
					if (tqi) {
						const std::uint32_t magBefore = curMagAmmo;
						tqi->QueueWeaponFire(weap, player, RE::BGSEquipIndex{ 0 }, ammo);
						++earlyAdsAutoFireAttempts;
						const float interval = EquippedWeapon::GetWeaponFireInterval(player);
						logger::info("[Phantom-Fire] Forced shot {} via QueueWeaponFire (gs={}, magBefore={}, pace={:.3f}s, ammo={})",
							earlyAdsAutoFireAttempts, afGS, magBefore, interval,
							ammo ? ammo->GetFormEditorID() : "null");
						PushEvent("AutoFire: forced shot");
						// Pace next forced shot at the weapon's natural
						// interval — slow enough that the auto-fire WAV
						// loop can reach its end-marker before we'd
						// retrigger it (fixes "loop never stops" bug).
						earlyAdsAutoFireGraceTimer = interval;
					}
				}
			}
		} else if (earlyAdsAutoFirePhantomGap > 0.15f) {
			// No weaponFire anim event for 150ms — player let go of trigger.
			// Force the phantom loop to end so audio/anim stop cleanly.
			// We send three events back-to-back:
			//   attackStop  — clears the attack state machine
			//   AttackEnd   — known graph event (rootbehavior.xml line 6165),
			//                 some sound annotations on this end fire-loop
			//                 sounds via the SoundStop hook
			//   SoundStop   — generic graph sound-stop event (line 6016) —
			//                 best-effort stop for any active loop sounds
			// Plus FadeOutAndRelease on the equipped weapon's attackSound
			// (offset 0x78 on EquippedWeaponData) — this is the actual loop
			// sound the engine started directly via BSAudioManager when
			// QueueWeaponFire fired the weapon, and it doesn't get cleaned
			// up by attackStop alone because the proper attack state machine
			// never engaged for our forced shots.
			RE::BSFixedString evtStop("attackStop");
			RE::BSFixedString evtAttackEnd("AttackEnd");
			RE::BSFixedString evtSoundStop("SoundStop");
			bool r1 = player->NotifyAnimationGraphImpl(evtStop);
			bool r2 = player->NotifyAnimationGraphImpl(evtAttackEnd);
			bool r3 = player->NotifyAnimationGraphImpl(evtSoundStop);
			HavokVar::SetBool(player, HavokVar::kIsAttacking, false);
			int faded = 0;
			const std::uint16_t fadeMs = static_cast<std::uint16_t>(
				std::clamp(gs->autoFireSoundFadeMs, 0, 5000));
			if (gs->autoFireSoundFadeEnabled)
				faded = LocalSound::FadeOutAllPlayerWeaponSounds(player, fadeMs);
			logger::info("[EarlyADS-AutoFire] Trigger released (gap={:.3f}s, shots={}) — attackStop={}, AttackEnd={}, SoundStop={}, fadeMs={}, fadedHandles={}, exit phantom override",
				earlyAdsAutoFirePhantomGap, earlyAdsAutoFireAttempts, r1, r2, r3, fadeMs, faded);
			earlyAdsAutoFireWatching = false;
			PushEvent("AutoFire: trigger released");
		}
	}

	// ============================================================
	// EARLY EQUIP — ADS or hipfire during equip animations.
	// Uses InitiateStart as the trigger event, shares delay/blend
	// settings with Early ADS Return.
	// ============================================================
	if (isCurrentlyEquipping) {
		equipAnimTimer -= delta;
		if (equipAnimTimer <= 0.0f) {
			isCurrentlyEquipping = false;
			earlyEquipAdsArmed   = false;
			earlyEquipFireArmed  = false;
			earlyEquipPending    = false;
			earlyEquipTimer      = 0.0f;
			logger::debug("[EarlyEquip] Equip state expired (timeout)");
		}
	}

	// Arm early equip ADS
	if (isCurrentlyEquipping && ws.earlyEquipAdsEnabled
	    && !earlyEquipAdsArmed && !earlyEquipFireArmed && adsInputHeld)
	{
		earlyEquipAdsArmed  = true;
		earlyEquipPending   = false;
		earlyEquipTimer     = 0.0f;
		logger::info("[EarlyEquip] ADS armed — ADS input held during equip (gunState={})",
			static_cast<std::uint32_t>(player->gunState));
	}

	// Arm early equip fire (only if ADS not already armed)
	if (isCurrentlyEquipping && ws.earlyEquipFireEnabled
	    && !earlyEquipFireArmed && !earlyEquipAdsArmed && fireInputHeld && !adsInputHeld)
	{
		earlyEquipFireArmed = true;
		earlyEquipPending   = false;
		earlyEquipTimer     = 0.0f;
		logger::info("[EarlyEquip] Fire armed — fire input held during equip (gunState={})",
			static_cast<std::uint32_t>(player->gunState));
	}

	// Disarm if equip ended naturally
	if ((earlyEquipAdsArmed || earlyEquipFireArmed) && !isCurrentlyEquipping) {
		logger::debug("[EarlyEquip] Disarmed — equip ended naturally");
		earlyEquipAdsArmed  = false;
		earlyEquipFireArmed = false;
		earlyEquipPending   = false;
		earlyEquipTimer     = 0.0f;
	}

	// Trigger on InitiateStart during equip
	if ((earlyEquipAdsArmed || earlyEquipFireArmed) && initiateStart && !earlyEquipPending) {
		earlyEquipPending = true;
		earlyEquipTimer   = ws.earlyAdsReturnDelay;
		logger::info("[EarlyEquip] InitiateStart trigger hit, delay={:.2f}s (ads={}, fire={})",
			ws.earlyAdsReturnDelay, earlyEquipAdsArmed, earlyEquipFireArmed);
	}

	if (earlyEquipPending) {
		earlyEquipTimer -= delta;
		if (earlyEquipTimer <= 0.0f) {
			earlyEquipPending = false;
			earlyEquipTimer   = 0.0f;

			auto preGS = static_cast<std::uint32_t>(player->gunState);
			bool isAdsEquip = earlyEquipAdsArmed;

			// Briefly suppress the attack handler so the animation graph
			// transitions cleanly to idle (gunState 0) before ADS or fire
			// input is allowed to drive the next state.
			if (atkHandler) {
				atkHandler->heldStateActive = false;
				atkHandler->inputEventHandlingEnabled = false;
			}

			// Un-hide weapon bones that the equip animation may have culled.
			DispatchWeaponUncullRepair(player);
			uncullBoneReplayFrames = 3;

			logger::info("[EarlyEquip] Triggered — gunState={} (handler blocked, ads={}, fire={})",
				preGS, isAdsEquip, !isAdsEquip);

			isCurrentlyEquipping = false;
			equipAnimTimer       = 0.0f;
			earlyEquipAdsArmed   = false;
			earlyEquipFireArmed  = false;

			if (isAdsEquip) {
				earlyAdsTriggered      = true;
				earlyAdsForceIdleFrames    = 3;
				earlyAdsForceIdleCountdown = 3;
				earlyAdsPostDiagFrames     = 10;
				PushEvent("Early Equip ADS");
			} else {
				earlyAdsForceIdleFrames    = 3;
				earlyAdsForceIdleCountdown = 3;
				earlyAdsPostDiagFrames     = 10;
				PushEvent("Early Equip Fire");
			}
		}
	}

	// Arm early ADS if we're mid-reload and ADS input is held
	if (isCurrentlyReloading && ws.earlyAdsReturnEnabled && !earlyAdsArmed && adsInputHeld) {
		earlyAdsArmed         = true;
		earlyAdsReturnPending = false;
		earlyAdsReturnTimer   = 0.0f;
		logger::debug("[EarlyADS] Armed — ADS input held during reload (gunState={})",
			static_cast<std::uint32_t>(player->gunState));
	}

	// Disarm if reload ended naturally before our timer fired.
	if (earlyAdsArmed && !isCurrentlyReloading) {
		logger::debug("[EarlyADS] Disarmed — reload ended naturally (gunState={})",
			static_cast<std::uint32_t>(player->gunState));
		earlyAdsArmed         = false;
		earlyAdsReturnPending = false;
		earlyAdsReturnTimer   = 0.0f;
	}

	if (earlyAdsArmed && ws.earlyAdsReturnEnabled) {
		// Check trigger event
		bool earlyAdsTrigger = false;
		switch (ws.earlyAdsReturnTrigger) {
			case 1:  earlyAdsTrigger = reloadEnd;      break;
			case 2:  earlyAdsTrigger = initiateStart;   break;
			default: earlyAdsTrigger = reloadComplete;  break;
		}
		if (earlyAdsTrigger && !earlyAdsReturnPending) {
			earlyAdsReturnPending = true;
			earlyAdsReturnTimer   = ws.earlyAdsReturnDelay;
			logger::info("[EarlyADS] Trigger event hit (type={}), delay={:.2f}s",
				ws.earlyAdsReturnTrigger, ws.earlyAdsReturnDelay);
		}
		if (earlyAdsReturnPending) {
			earlyAdsReturnTimer -= delta;
			if (earlyAdsReturnTimer <= 0.0f) {
				earlyAdsReturnPending = false;
				earlyAdsReturnTimer   = 0.0f;

				auto preGS = static_cast<std::uint32_t>(player->gunState);

				if (preGS != 4) {
					// Reload already ended naturally — don't send a redundant event
					// that could desync the attack state machine.
					logger::info("[EarlyADS] Skipped — gunState={} (reload already ended naturally)", preGS);
					earlyAdsArmed         = false;
					isCurrentlyReloading  = false;
					earlyAdsTriggered     = true;
					PushEvent("Early ADS (natural)");
				} else {
					// Still reloading — temporarily block the ADS/attack handler
					// so ReloadEnd transitions to idle (gunState 0) instead of
					// directly to sighted (gunState 6).  The natural sequence
					// is 4→0→6; going 4→6 directly breaks automatic fire
					// because the attack state machine never sees idle.
					HavokVar::LogAllAttackVars(player);

					if (atkHandler) {
						atkHandler->heldStateActive = false;
						atkHandler->inputEventHandlingEnabled = false;
					}

				// Send all UncullBone events + ReloadEnd as one ordered batch:
				//   Phase 1 (graph): UncullBone × 13 → ReloadEnd
				//   Phase 2 (sinks): UncullBone × 13 → ReloadEnd
				// UncullBone first so the reload sub-graph accepts them before
				// the state-transition event fires.
				ClearEngineWeaponCullFlags(player);
				RE::BSFixedString evtEnd("ReloadEnd");
				bool r1 = SendUncullBatch(player, &evtEnd);
				auto postGS = static_cast<std::uint32_t>(player->gunState);

				HavokVar::SetBool(player, HavokVar::kIsReloading, false);
				HavokVar::SetBool(player, HavokVar::kIsAttacking, false);

				DispatchWeaponUncullRepair(player);
				uncullBoneReplayFrames = 3;

				logger::info("[EarlyADS] Timer fired — ReloadEnd={}, gunState {} -> {} (handler blocked, forced isReloading=false)",
						r1, preGS, postGS);

					HavokVar::LogAllAttackVars(player);

					earlyAdsArmed         = false;
					isCurrentlyReloading  = false;
					earlyAdsTriggered     = true;
					earlyAdsPostDiagFrames = 10;
					earlyAdsForceIdleFrames = 3;
					earlyAdsForceIdleCountdown = 3;
					PushEvent("Early ADS (ReloadEnd→idle)");
				}
			}
		}
	}

	// ============================================================
	// EARLY FIRE CANCEL — mirrors the EarlyADS flow but driven by
	// the FIRE input held during a reload (no ADS involved).
	// Reload-cancel-into-fire on auto weapons hits the same
	// "fires anim plays but nothing discharges" bug that EarlyADS hits.
	// Same fix: ReloadEnd → force isReloading=false → force idle frames →
	// arm phantom override.
	//
	// IMPORTANT difference from EarlyADS: we trigger IMMEDIATELY on
	// (reloading + fire-held) rather than waiting for a trigger event,
	// because canceling a reload by firing usually skips the natural
	// reloadComplete event entirely (the engine cuts the reload anim short).
	// A tiny built-in 50 ms hold-debounce filters out spurious single-frame
	// fire taps right at reload boundaries.
	//
	// Suppressed if the EarlyADS path is already active so the two flows
	// don't race each other on the same reload.
	// ============================================================
	if (isCurrentlyReloading && ws.earlyFireCancelEnabled
	    && !earlyFireCancelArmed && !earlyAdsArmed
	    && fireInputHeld)
	{
		earlyFireCancelArmed   = true;
		earlyFireCancelPending = true;
		earlyFireCancelTimer   = 0.05f;  // 50 ms hold-debounce
		logger::info("[EarlyFireCancel] Armed — fire input held during reload (gunState={})",
			static_cast<std::uint32_t>(player->gunState));
	}

	// Disarm if reload ended naturally before our debounce expired,
	// or if the player released fire before the debounce expired.
	if (earlyFireCancelArmed && (!isCurrentlyReloading || !fireInputHeld)) {
		if (earlyFireCancelPending) {
			logger::debug("[EarlyFireCancel] Disarmed — {} (gunState={})",
				!isCurrentlyReloading ? "reload ended naturally" : "fire released before debounce",
				static_cast<std::uint32_t>(player->gunState));
		}
		earlyFireCancelArmed   = false;
		earlyFireCancelPending = false;
		earlyFireCancelTimer   = 0.0f;
	}

	if (earlyFireCancelArmed && earlyFireCancelPending) {
		earlyFireCancelTimer -= delta;
		if (earlyFireCancelTimer <= 0.0f) {
			earlyFireCancelPending = false;
			earlyFireCancelTimer   = 0.0f;

			auto preGS = static_cast<std::uint32_t>(player->gunState);

			if (preGS != 4) {
				// Reload already ended naturally — skip force-end (would desync SM).
				// Still arm the phantom override so the impending fire animation
				// converts into real shots.
				logger::info("[EarlyFireCancel] Skipped force-end — gunState={} (reload already ended)", preGS);
				earlyFireCancelArmed = false;
				isCurrentlyReloading = false;
				if (ws.earlyAdsAutoFireEnabled && IsWeaponAutomatic(player)) {
					earlyAdsAutoFireWatching   = true;
					earlyAdsAutoFireTimer      = 5.0f;
					earlyAdsAutoFireAttempts   = 0;
					earlyAdsAutoFireGraceTimer = 0.0f;
					earlyAdsAutoFirePhantomGap = 0.0f;
					earlyAdsAutoFireSeenGS8    = false;
					logger::info("[EarlyFireCancel] Phantom override armed (auto weapon, natural reload end)");
				}
				PushEvent("Early Fire Cancel (natural)");
			} else {
				// Still reloading — force the reload to end and run the same
				// idle-transition recovery as EarlyADS so the attack state
				// machine sees a clean idle (gunState 4→0→4-or-firing) before
				// the player's held fire input is allowed to drive it again.
				HavokVar::LogAllAttackVars(player);

				// Briefly suppress the attack handler so ReloadEnd transitions
				// to idle (gs=0) instead of bouncing right back to firing/reload.
				if (atkHandler) {
					atkHandler->heldStateActive = false;
					atkHandler->inputEventHandlingEnabled = false;
				}

			// Send all UncullBone events + ReloadEnd as one ordered batch:
			//   Phase 1 (graph): UncullBone × 13 → ReloadEnd
			//   Phase 2 (sinks): UncullBone × 13 → ReloadEnd
			ClearEngineWeaponCullFlags(player);
			RE::BSFixedString evtEnd("ReloadEnd");
			bool r1 = SendUncullBatch(player, &evtEnd);
			auto postGS = static_cast<std::uint32_t>(player->gunState);

			HavokVar::SetBool(player, HavokVar::kIsReloading, false);
			HavokVar::SetBool(player, HavokVar::kIsAttacking, false);

			DispatchWeaponUncullRepair(player);
			uncullBoneReplayFrames = 3;

			logger::info("[EarlyFireCancel] Triggered — ReloadEnd={}, gunState {} -> {} (handler blocked, isReloading=false)",
					r1, preGS, postGS);

				HavokVar::LogAllAttackVars(player);

				earlyFireCancelArmed       = false;
				isCurrentlyReloading       = false;
				earlyAdsPostDiagFrames     = 10;
				earlyAdsForceIdleFrames    = 3;
				earlyAdsForceIdleCountdown = 3;
				// Note: the existing earlyAdsForceIdleFrames countdown block
				// (above) re-enables the handler and arms the phantom override
				// once the idle frames complete — that path is shared so we
				// don't need to duplicate the arming here.
				PushEvent("Early Fire Cancel (ReloadEnd→idle)");
			}
		}
	}

	// Post-EarlyADS diagnostic: log attack variables for a few frames
	if (earlyAdsPostDiagFrames > 0) {
		--earlyAdsPostDiagFrames;
		auto diagGS = static_cast<std::uint32_t>(player->gunState);
		std::int32_t atkState = -1;
		bool diagFiring = false, diagReloading = false, diagAttacking = false, diagAtkReady = false, diagAtkNotReady = false;
		HavokVar::GetInt(player, HavokVar::kIAttackState, atkState);
		HavokVar::GetBool(player, HavokVar::kIsFiring, diagFiring);
		HavokVar::GetBool(player, HavokVar::kIsReloading, diagReloading);
		HavokVar::GetBool(player, HavokVar::kIsAttacking, diagAttacking);
		HavokVar::GetBool(player, HavokVar::kIsAttackReady, diagAtkReady);
		HavokVar::GetBool(player, HavokVar::kIsAttackNotReady, diagAtkNotReady);
		logger::trace("[EarlyADS-Diag] frame={} gs={} iAtkState={} firing={} reloading={} attacking={} atkReady={} atkNotReady={}",
			10 - earlyAdsPostDiagFrames, diagGS, atkState, diagFiring, diagReloading, diagAttacking, diagAtkReady, diagAtkNotReady);
	}

	// ---- FIRE RECOVERY impulse (with sustained fire accumulation) ----
	// Try to register anim event sink if not yet registered (player graph may not exist at game load)
	if (!animEventSink.registered) RegisterAnimEventSink();

	// Consume the atomic flag set by the animation event sink.
	// Only accept weaponFire if the player is actually in a firing gunState
	// (kFire = 7 or kFireSighted = 8) to filter out spurious fire events
	// from weapon idle/equip/transition animations.  The sink already drops
	// these at source, so this is defense-in-depth in case future code
	// paths set firedThisFrame without going through the sink.
	bool rawFireEvent = animEventSink.firedThisFrame.exchange(false, std::memory_order_relaxed);
	const auto _rawGS = static_cast<std::uint32_t>(player->gunState);
	bool isCurrentlyFiring = rawFireEvent && GunStateLocal::IsFiringGunState(_rawGS);
	if (rawFireEvent && !isCurrentlyFiring) {
		logger::trace("[Fire] Ignored spurious weaponFire (gunState={})", _rawGS);
	}
	fireRecoveryCooldownTimer = std::max(0.0f, fireRecoveryCooldownTimer - delta);
	recentlyFiredTimer = std::max(0.0f, recentlyFiredTimer - delta);

	if (isCurrentlyFiring) {
		sustainedFireTime += delta;
		recentlyFiredTimer = 0.3f;
		recentlyFiredADS = isCurrentlyADS;
	}

	adsFireRecoveryCooldownTimer = std::max(0.0f, adsFireRecoveryCooldownTimer - delta);

	if (wasFiring && !isCurrentlyFiring) {
		float sustainedMult = 1.0f + std::min(sustainedFireTime * ws.sustainedFireBuildRate,
		                                       ws.sustainedFireMax - 1.0f);
		bool wasADSFire = recentlyFiredADS;  // set when last fire event was detected while ADS

		if (wasADSFire && springsActive && ws.adsFireRecoveryImpulseEnabled && adsFireRecoveryCooldownTimer <= 0.0f && !pipboyOpen) {
			adsFireRecoveryImpulse.Fire(
				{ ws.adsFireRecoveryImpulseZ * sustainedMult, ws.adsFireRecoveryImpulseY * sustainedMult, ws.adsFireRecoveryImpulseX * sustainedMult },
				{ ws.adsFireRecoveryRotImpulse * sustainedMult, 0.0f, 0.0f },
				0.0f);
			adsFireRecoveryCooldownTimer = ws.adsFireRecoveryCooldown;
			if (sustainedMult > 1.5f)
				PushEvent("ADS fire recovery (sustained)");
			else
				PushEvent("ADS fire recovery");
		} else if (!wasADSFire && springsActive && ws.fireRecoveryImpulseEnabled && fireRecoveryCooldownTimer <= 0.0f && !pipboyOpen) {
			fireRecoveryImpulse.Fire(
				{ ws.fireRecoveryImpulseZ * sustainedMult, ws.fireRecoveryImpulseY * sustainedMult, ws.fireRecoveryImpulseX * sustainedMult },
				{ ws.fireRecoveryRotImpulse * sustainedMult, 0.0f, 0.0f },
				0.0f);
			fireRecoveryCooldownTimer = ws.fireRecoveryCooldown;
			if (sustainedMult > 1.5f)
				PushEvent("Fire recovery (sustained)");
			else
				PushEvent("Fire recovery");
		}
		sustainedFireTime = 0.0f;
	}
	if (!isCurrentlyFiring) {
		sustainedFireTime = std::max(0.0f, sustainedFireTime - ws.sustainedFireDecay * delta);
	}
	wasFiring = isCurrentlyFiring;

	// ---- ACTION BLEND FACTOR ----
	{
		bool inMelee = IsInMeleeAction(player);
		if (inMelee) {
			if (!wasInMelee) meleeElapsedTime = 0.0f;
			meleeElapsedTime += delta;
		} else if (wasInMelee) {
			lastMeasuredMeleeDuration = meleeElapsedTime;
		}
		wasInMelee = inMelee;

		float leadTime = gs->actionBlendBackLeadTime;

		bool reloadBlendingBack = false;
		if (isCurrentlyReloading && lastMeasuredReloadDuration > leadTime) {
			float remaining = lastMeasuredReloadDuration - reloadElapsedTime;
			if (remaining <= leadTime)
				reloadBlendingBack = true;
		}

		bool meleeBlendingBack = false;
		if (inMelee && lastMeasuredMeleeDuration > leadTime) {
			float remaining = lastMeasuredMeleeDuration - meleeElapsedTime;
			if (remaining <= leadTime)
				meleeBlendingBack = true;
		}

		float targetBlend = 1.0f;
		if (gs->blendDuringFiring && isCurrentlyFiring)
			targetBlend = std::min(targetBlend, gs->blendFiringMinIntensity);
		if (gs->blendDuringReload && isCurrentlyReloading && !reloadBlendingBack)
			targetBlend = std::min(targetBlend, gs->blendReloadMinIntensity);
		if (gs->blendDuringMelee && inMelee && !meleeBlendingBack)
			targetBlend = std::min(targetBlend, gs->blendMeleeMinIntensity);

		float blendSpeed = gs->actionBlendSpeed;
		if (targetBlend < actionBlendFactor)
			actionBlendFactor = std::max(targetBlend, actionBlendFactor - blendSpeed * delta);
		else
			actionBlendFactor = std::min(targetBlend, actionBlendFactor + blendSpeed * delta);
	}

	// ---- WEIGHT MULT ----
	float intensityMult = gs->globalIntensity * actionBlendFactor * cachedWeightMult;

	// ---- ADS inertia mult ----
	if (isCurrentlyADS) {
		if (isCurrentlyScoped && ws.adsScopeInertiaEnabled)
			intensityMult *= ws.adsScopeInertiaMult;
		else if (!isCurrentlyScoped && ws.adsInertiaEnabled)
			intensityMult *= ws.adsInertiaMult;
	}

	// ---- SETTLING / CAMERA SPRING / MOVEMENT SPRING / WALK OFFSETS ----
	// Pure inertia visuals — skipped entirely when springs are gated off.
	// Camera-velocity state (lastCameraYaw etc.) is reset on the falling
	// edge above and re-initializes on the first active frame, so skipping
	// here cannot produce a velocity spike on re-enable.
	if (springsActive) {
	// ---- SETTLING ----
	// Camera velocity must use wall-clock realDelta, NOT game-time delta.
	// Mouse input is not affected by sgtm, so the angular change between
	// frames is the same regardless of time multiplier. Dividing by the
	// scaled gameDelta would inflate velocity proportionally to 1/sgtm,
	// causing overshooting and hitchy springs at low time multipliers.
	auto camVel   = CalculateCameraVelocity(realDelta);
	auto localMove = CalculateLocalMovement(player);
	float camSpeed = std::abs(camVel.x) + std::abs(camVel.z);
	float moveSpeed = std::abs(localMove.x) + std::abs(localMove.y);

	if (camSpeed > 0.001f || moveSpeed > 0.001f) {
		timeSinceMovement = 0.0f;
		settlingFactor    = std::max(0.0f, settlingFactor - gs->settleSpeed * delta * 3.0f);
	} else {
		timeSinceMovement += delta;
		if (timeSinceMovement > gs->settleDelay) {
			settlingFactor = std::min(1.0f, settlingFactor + gs->settleSpeed * delta * 0.5f);
		}
	}

	// ---- CAMERA SPRING ----
	UpdateCameraSpring(cameraSpring, ws, camVel, delta, intensityMult);

	// ---- MOVEMENT SPRING ----
	UpdateMovementSpring(movementSpring, ws, localMove, delta, intensityMult);

	// ---- WALK DIRECTION OFFSETS ----
	// Compute per-direction target weights from input vector.
	// localMove.x: positive=right, negative=left
	// localMove.y: positive=forward, negative=backward
	// Weights are the projection onto each cardinal axis, so diagonals
	// naturally blend two directions (e.g. forward-left = 0.707 fwd + 0.707 left).
	{
		float tgtFwd   = std::max(0.0f,  localMove.y);
		float tgtBack  = std::max(0.0f, -localMove.y);
		float tgtRight = std::max(0.0f,  localMove.x);
		float tgtLeft  = std::max(0.0f, -localMove.x);

		// Choose blend speed: rapid blend-out when ADS, normal otherwise
		float blendIn  = ws.walkOffsetBlendInSpeed;
		float blendOut = isCurrentlyADS ? ws.walkOffsetAdsBlendOutSpeed : ws.walkOffsetBlendOutSpeed;
		// When ADS, also force all targets to 0 so offsets blend out
		if (isCurrentlyADS || !ws.walkOffsetsEnabled) {
			tgtFwd = tgtBack = tgtRight = tgtLeft = 0.0f;
		}

		auto moveToward = [&](float& cur, float tgt) {
			float speed = (tgt > cur) ? blendIn : blendOut;
			if (cur < tgt)       cur = std::min(cur + speed * delta, tgt);
			else if (cur > tgt)  cur = std::max(cur - speed * delta, tgt);
		};
		moveToward(walkWeightFwd,   tgtFwd);
		moveToward(walkWeightBack,  tgtBack);
		moveToward(walkWeightLeft,  tgtLeft);
		moveToward(walkWeightRight, tgtRight);
	}
	}  // end if (springsActive) — settling/camera/movement/walk-offset visuals

	// ---- SPRINT IMPULSE ----
	// moveMode bit 0x100 = sprinting (UneducatedShooter pattern)
	bool currentlySpriniting = (player->moveMode & 0x100) != 0;

	// ---- SUPER SPRINT (input-hook approach) ----
	// The SprintHandler::HandleEvent(ButtonEvent*) vtable hook (installed in
	// InitSuperSprint) eats sprint JustPressed events while s_eatEnabled is
	// true. Because the engine never receives the second tap, its internal
	// sprint toggle stays ON, moveMode keeps 0x100, and the animation never
	// cancels.  We just manage the window + activation here.
	{
		auto* sgs = Settings::GetSingleton();
		const bool ssEnabled = sgs->superSprintEnabled && superSprintKeyword
			&& avifSpeedMult && SuperSprintInput::s_installed;

		if (ssEnabled) {
			// -----------------------------------------------------------
			// 1. ACTIVATION WINDOW — open on first sprint tap, enable eating
			// -----------------------------------------------------------
			if (!superSprintActive) {
				// Sprint just started (first tap) — open the activation window
				// and enable the input hook to eat the next sprint press
				if (currentlySpriniting && !wasSprinting) {
					superSprintWindowActive = true;
					superSprintWindowStart  = elapsedTime;
					SuperSprintInput::s_eatEnabled = true;
				}

				// Expire the window if too much time has passed
				if (superSprintWindowActive &&
					(elapsedTime - superSprintWindowStart) > sgs->superSprintDoubleTapWindow) {
					superSprintWindowActive = false;
					SuperSprintInput::s_eatEnabled = false;
				}

				// The input hook ate a sprint press during the window → ACTIVATE
				if (superSprintWindowActive && SuperSprintInput::s_eatTriggered) {
					SuperSprintInput::s_eatTriggered = false;
					SuperSprintInput::s_eatEnabled   = false;
					superSprintWindowActive = false;

					// Block activation if AP is below the stamina threshold
					bool allowActivation = true;
					if (sgs->superSprintStaminaThresholdEnabled && avifActionPoints) {
						float curAP  = player->GetActorValue(*avifActionPoints);
						float baseAP = player->GetBaseActorValue(*avifActionPoints);
						if (baseAP > 0.0f) {
							float pct = (curAP / baseAP) * 100.0f;
							if (pct < sgs->superSprintStaminaThreshold) {
								allowActivation = false;
								logger::trace("[SuperSprint] Activation blocked — AP {:.1f}%% < threshold {:.0f}%%",
									pct, sgs->superSprintStaminaThreshold);
							}
						}
					}

					if (allowActivation) {
						superSprintActive = true;

						// Boost SpeedMult actor value (movement speed)
						float baseSpeed = player->GetActorValue(*avifSpeedMult);
						superSprintSpeedBoost = baseSpeed * (sgs->superSprintSpeedMult - 1.0f);
						player->ModActorValue(static_cast<RE::ACTOR_VALUE_MODIFIER>(1),
							*avifSpeedMult, superSprintSpeedBoost);

						// Boost AnimationMult actor value (animation playback speed).
						// AnimationMult base is 100 (= 1.0x); adding 25 → 125 → 1.25x.
						if (avifAnimMult && sgs->superSprintAnimSpeedMult > 1.001f) {
							float baseAnim = player->GetActorValue(*avifAnimMult);
							superSprintAnimBoost = baseAnim * (sgs->superSprintAnimSpeedMult - 1.0f);
							player->ModActorValue(static_cast<RE::ACTOR_VALUE_MODIFIER>(1),
								*avifAnimMult, superSprintAnimBoost);
						}

						// Seed AP tracking for multiplicative extra drain
						if (avifActionPoints) {
							superSprintPrevAP = player->GetActorValue(*avifActionPoints);
						}

						// Apply OAR keyword to player's NPC base form
						auto* npc = player->GetNPC();
						if (npc) {
							SuperSprintHelpers::AddKeyword(
								static_cast<RE::BGSKeywordForm*>(npc), superSprintKeyword);
						}

						PushEvent("Super Sprint activated");
						logger::info("[SuperSprint] Activated — speedBoost={:.1f}, animBoost={:.1f}, window={:.3f}s",
							superSprintSpeedBoost, superSprintAnimBoost,
							elapsedTime - superSprintWindowStart);
					}
				}
			}

			// -----------------------------------------------------------
			// 2. DEACTIVATION — sprint stopped naturally (3rd tap, AP out,
			//    player stopped moving, etc.)
			// -----------------------------------------------------------
			if (superSprintActive && !currentlySpriniting && wasSprinting) {
				superSprintActive = false;

				if (std::abs(superSprintSpeedBoost) > 0.001f) {
					player->ModActorValue(static_cast<RE::ACTOR_VALUE_MODIFIER>(1),
						*avifSpeedMult, -superSprintSpeedBoost);
					superSprintSpeedBoost = 0.0f;
				}

				// Remove animation speed boost
				if (avifAnimMult && std::abs(superSprintAnimBoost) > 0.001f) {
					player->ModActorValue(static_cast<RE::ACTOR_VALUE_MODIFIER>(1),
						*avifAnimMult, -superSprintAnimBoost);
					superSprintAnimBoost = 0.0f;
				}

				auto* npc = player->GetNPC();
				if (npc) {
					SuperSprintHelpers::RemoveKeyword(
						static_cast<RE::BGSKeywordForm*>(npc), superSprintKeyword);
				}

				superSprintPrevAP = -1.0f;
				SuperSprintInput::s_eatEnabled = false;
				PushEvent("Super Sprint deactivated");
				logger::info("[SuperSprint] Deactivated");
			}

			// -----------------------------------------------------------
			// 3. PER-FRAME EFFECTS (AP drain, animation speed)
			// -----------------------------------------------------------
			if (superSprintActive && currentlySpriniting) {
				// Extra AP drain (multiplicative with engine's sprint drain)
				if (avifActionPoints && sgs->superSprintAPCostMult > 1.001f) {
					float currentAP = player->GetActorValue(*avifActionPoints);
					if (superSprintPrevAP >= 0.0f) {
						float engineDrain = superSprintPrevAP - currentAP;
						if (engineDrain > 0.0f) {
							float extraDrain = engineDrain * (sgs->superSprintAPCostMult - 1.0f);
							player->ModActorValue(static_cast<RE::ACTOR_VALUE_MODIFIER>(2),
								*avifActionPoints, -extraDrain);
							currentAP -= extraDrain;
						}
					}
					superSprintPrevAP = currentAP;
				}

				// Disengage super sprint if AP% drops below the stamina threshold
				if (sgs->superSprintStaminaThresholdEnabled && avifActionPoints) {
					float curAP  = player->GetActorValue(*avifActionPoints);
					float baseAP = player->GetBaseActorValue(*avifActionPoints);
					if (baseAP > 0.0f && (curAP / baseAP) * 100.0f < sgs->superSprintStaminaThreshold) {
						// Force deactivation — remove boosts and keyword
						superSprintActive = false;

						if (std::abs(superSprintSpeedBoost) > 0.001f) {
							player->ModActorValue(static_cast<RE::ACTOR_VALUE_MODIFIER>(1),
								*avifSpeedMult, -superSprintSpeedBoost);
							superSprintSpeedBoost = 0.0f;
						}
						if (avifAnimMult && std::abs(superSprintAnimBoost) > 0.001f) {
							player->ModActorValue(static_cast<RE::ACTOR_VALUE_MODIFIER>(1),
								*avifAnimMult, -superSprintAnimBoost);
							superSprintAnimBoost = 0.0f;
						}
						auto* npc = player->GetNPC();
						if (npc) {
							SuperSprintHelpers::RemoveKeyword(
								static_cast<RE::BGSKeywordForm*>(npc), superSprintKeyword);
						}
						superSprintPrevAP = -1.0f;
						SuperSprintInput::s_eatEnabled = false;
						PushEvent("Super Sprint cancelled (low stamina)");
						logger::info("[SuperSprint] Disengaged — AP {:.1f}%% below threshold {:.0f}%%",
							(curAP / baseAP) * 100.0f, sgs->superSprintStaminaThreshold);
					}
				}
			}
		} else if (superSprintActive) {
			// Feature was disabled while super sprint was active — clean up
			superSprintActive = false;
			if (std::abs(superSprintSpeedBoost) > 0.001f && avifSpeedMult) {
				player->ModActorValue(static_cast<RE::ACTOR_VALUE_MODIFIER>(1),
					*avifSpeedMult, -superSprintSpeedBoost);
				superSprintSpeedBoost = 0.0f;
			}
			if (avifAnimMult && std::abs(superSprintAnimBoost) > 0.001f) {
				player->ModActorValue(static_cast<RE::ACTOR_VALUE_MODIFIER>(1),
					*avifAnimMult, -superSprintAnimBoost);
				superSprintAnimBoost = 0.0f;
			}
			auto* npc = player->GetNPC();
			if (npc && superSprintKeyword) {
				SuperSprintHelpers::RemoveKeyword(
					static_cast<RE::BGSKeywordForm*>(npc), superSprintKeyword);
			}
			superSprintPrevAP = -1.0f;
			SuperSprintInput::s_eatEnabled = false;
			logger::info("[SuperSprint] Force-deactivated (feature disabled)");
		}
	}

	isSprinting = currentlySpriniting;

	const bool sprintJustStarted = currentlySpriniting && !wasSprinting;
	const bool sprintJustStopped = !currentlySpriniting && wasSprinting;

	if (sprintJustStarted && springsActive && ws.sprintInertiaEnabled && ws.sprintStartEnabled && !pipboyOpen) {
		sprintSpring.positionVelocity.x += ws.sprintImpulseZ * intensityMult;
		sprintSpring.positionVelocity.y += ws.sprintImpulseY * intensityMult;
		sprintSpring.positionVelocity.z += ws.sprintImpulseX * intensityMult;
		sprintSpring.rotationVelocity.x += ws.sprintRotImpulse * intensityMult;
		PushEvent("Sprint start impulse");
	}
	if (sprintJustStopped && springsActive && ws.sprintInertiaEnabled && ws.sprintStopEnabled && !pipboyOpen) {
		sprintSpring.positionVelocity.x += ws.sprintStopImpulseZ * intensityMult;
		sprintSpring.positionVelocity.y += ws.sprintStopImpulseY * intensityMult;
		sprintSpring.positionVelocity.z += ws.sprintStopImpulseX * intensityMult;
		sprintSpring.rotationVelocity.x += ws.sprintStopRotImpulse * intensityMult;
		PushEvent("Sprint stop impulse");
	}
	wasSprinting = currentlySpriniting;
	sprintSpring.positionOffset = SpringStep3(sprintSpring.positionOffset, sprintSpring.positionVelocity,
		{ 0, 0, 0 }, ws.sprintStiffness, ws.sprintDamping, 1.0f, delta);
	sprintSpring.rotationOffset = SpringStep3(sprintSpring.rotationOffset, sprintSpring.rotationVelocity,
		{ 0, 0, 0 }, ws.sprintStiffness, ws.sprintDamping, 1.0f, delta);

	// ---- JUMP/LAND IMPULSE ----
	// kJumping  = player actively jumped (pressed jump button)
	// kInAir    = player became airborne passively (walked/fell off a ledge)
	// We fire different impulses for each case so they can be tuned independently.
	bool currentlyInAir  = false;
	bool currentlyJumping = false;
	if (player->currentProcess && player->currentProcess->middleHigh &&
	    player->currentProcess->middleHigh->charController) {
		using HkStateType = RE::hknpCharacterState::hknpCharacterStateType;
		auto hkState = player->currentProcess->middleHigh->charController->context.currentState.get();
		currentlyJumping = (hkState == HkStateType::kJumping);
		currentlyInAir   = currentlyJumping || (hkState == HkStateType::kInAir);
	} else {
		currentlyInAir = (player->flyState != 0);
	}

	// Prevent walking/running animations while airborne by zeroing the Speed
	// behavior variable. The engine sometimes keeps the walk/run blend active
	// during jumps/falls, making the legs visibly cycle in mid-air.
	if (gs->disableAirWalk && currentlyInAir) {
		HavokVar::SetFloat(player, HavokVar::kSpeed, 0.0f);
	}

	// Confirmed air state: ignore brief physics flickers (step-up mods, ground snap
	// corrections) that last only 1-3 frames. Real jumps stay airborne for 0.12s+;
	// flickers are typically < 0.05s.
	static constexpr float kMinAirTimeForJumpSpring = 0.12f;
	bool prevConfirmedInAir = confirmedInAir;
	if (currentlyInAir && airTime >= kMinAirTimeForJumpSpring) {
		confirmedInAir = true;
	} else if (!currentlyInAir) {
		confirmedInAir = false;
	}
	bool confirmedLanding = !confirmedInAir && prevConfirmedInAir;

	if (!wasInAir && currentlyInAir) {
		airTime = 0.0f;
		if (currentlyJumping) {
			pendingFallImpulse = false;
			pendingFallTimer   = 0.0f;
			didJump = true;
			if (springsActive && !pipboyOpen) {
				jumpSpring.positionVelocity.x += ws.jumpImpulseZ * intensityMult;
				jumpSpring.positionVelocity.y += ws.jumpImpulseY * intensityMult;
				jumpSpring.positionVelocity.z += ws.jumpImpulseX * intensityMult;
				jumpSpring.rotationVelocity.x += ws.jumpRotPitch * intensityMult;
				jumpSpring.rotationVelocity.y += ws.jumpRotYaw   * intensityMult;
				jumpSpring.rotationVelocity.z += ws.jumpRotRoll  * intensityMult;
				currentJumpStiffness = ws.jumpStiffness;
				currentJumpDamping   = ws.jumpDamping;
				PushEvent("Jump impulse");
			}
		} else {
			didJump = false;
			pendingFallImpulse = true;
			pendingFallTimer   = 0.0f;
		}
	}

	// Advance the pending fall timer; fire once the player has been falling long enough.
	// This prevents false triggers from ledge-edge collision jitter.
	static constexpr float kFallImpulseDelay = 0.25f;
	if (pendingFallImpulse) {
		if (!currentlyInAir) {
			pendingFallImpulse = false;
			pendingFallTimer   = 0.0f;
		} else {
			pendingFallTimer += delta;
			if (pendingFallTimer >= kFallImpulseDelay) {
				pendingFallImpulse = false;
				pendingFallTimer   = 0.0f;
				if (springsActive && !pipboyOpen) {
					jumpSpring.positionVelocity.x += ws.fallImpulseZ * intensityMult;
					jumpSpring.positionVelocity.y += ws.fallImpulseY * intensityMult;
					jumpSpring.positionVelocity.z += ws.fallImpulseX * intensityMult;
					jumpSpring.rotationVelocity.x += ws.fallRotPitch * intensityMult;
					jumpSpring.rotationVelocity.y += ws.fallRotYaw   * intensityMult;
					jumpSpring.rotationVelocity.z += ws.fallRotRoll  * intensityMult;
					currentJumpStiffness = ws.fallStiffness;
					currentJumpDamping   = ws.fallDamping;
					PushEvent("Fall-from-ledge impulse");
				}
			}
		}
	}

	// Landing impulse uses confirmedLanding to ignore brief collision flickers
	if (confirmedLanding && springsActive && airTime > 0.05f && !pipboyOpen) {
		float landScale = std::min(airTime * ws.airTimeImpulseScale, 2.0f);
		jumpSpring.positionVelocity.x += ws.landImpulseZ * intensityMult * landScale;
		jumpSpring.positionVelocity.y += ws.landImpulseY * intensityMult * landScale;
		jumpSpring.positionVelocity.z += ws.landImpulseX * intensityMult * landScale;
		jumpSpring.rotationVelocity.x += ws.landRotPitch * intensityMult * landScale;
		jumpSpring.rotationVelocity.y += ws.landRotYaw   * intensityMult * landScale;
		jumpSpring.rotationVelocity.z += ws.landRotRoll  * intensityMult * landScale;
		currentJumpStiffness = ws.landStiffness;
		currentJumpDamping   = ws.landDamping;
		PushEvent("Landing impulse");
	}
	wasInAir = currentlyInAir;
	if (currentlyInAir) airTime += delta;
	else airTime = 0.0f;

	jumpSpring.positionOffset = SpringStep3(jumpSpring.positionOffset, jumpSpring.positionVelocity,
		{ 0, 0, 0 }, currentJumpStiffness, currentJumpDamping, 1.0f, delta);
	jumpSpring.rotationOffset = SpringStep3(jumpSpring.rotationOffset, jumpSpring.rotationVelocity,
		{ 0, 0, 0 }, currentJumpStiffness, currentJumpDamping, 1.0f, delta);

	// ---- LEAN INERTIA (UneducatedShooter) ----
	// Derive lean state from the CameraInserted1st bone that UneducatedShooter
	// inserts above the Camera node.  UneducatedShooter calls
	//   GetRotationMatrix33(rotZ * toRad, 0, 0)
	// where the first parameter is "pitch" — a rotation around the Y axis.
	// The resulting matrix (m_pitch) is:
	//   entry[0] = [ cos(θ), 0, sin(θ) ]
	//   entry[1] = [ 0,      1, 0      ]
	//   entry[2] = [-sin(θ), 0, cos(θ) ]
	// So entry[0].v.z == sin(θ) encodes the lean angle.
	// Pure inertia visual — gated. Lean smoothing state is reset with the
	// other spring state on the falling edge.
	if (springsActive) {
		float rawLeanWeight = 0.0f;
		auto* fpRoot = player->Get3D(true);
		if (fpRoot) {
			auto* camInserted = fpRoot->GetObjectByName("CameraInserted1st");
			if (camInserted) {
				const float sinTheta = camInserted->local.rotate.entry[0].v.z;
				rawLeanWeight = std::clamp(sinTheta, -1.0f, 1.0f);
			}
		}

		// Smooth the weight toward the raw value (frame-rate-independent
		// exponential decay: alpha = 1 - e^(-speed * dt)).
		const float leanBlend = 1.0f - std::expf(-ws.leanOffsetBlendSpeed * delta);
		currentLeanWeight = currentLeanWeight + (rawLeanWeight - currentLeanWeight) * leanBlend;

		// Additive offset — blend lateral/vertical/forward additive position
		const bool leanOffsetActive = ws.leanOffsetEnabled && !(ws.leanOffsetDisableInADS && isCurrentlyADS);
		if (leanOffsetActive) {
			const float tx = ws.leanOffsetX * currentLeanWeight * intensityMult;
			const float ty = ws.leanOffsetY * currentLeanWeight * intensityMult;
			const float tz = ws.leanOffsetZ * currentLeanWeight * intensityMult;
			leanAdditiveOffset.x += (tx - leanAdditiveOffset.x) * leanBlend;
			leanAdditiveOffset.y += (ty - leanAdditiveOffset.y) * leanBlend;
			leanAdditiveOffset.z += (tz - leanAdditiveOffset.z) * leanBlend;
		} else {
			leanAdditiveOffset.x += (0.0f - leanAdditiveOffset.x) * leanBlend;
			leanAdditiveOffset.y += (0.0f - leanAdditiveOffset.y) * leanBlend;
			leanAdditiveOffset.z += (0.0f - leanAdditiveOffset.z) * leanBlend;
		}

		// Spring impulse on lean direction transitions
		const bool leanImpulseActive = ws.leanImpulseEnabled && !(ws.leanImpulseDisableInADS && isCurrentlyADS);
		if (leanImpulseActive) {
			const float curDir  = (currentLeanWeight > 0.1f) ? 1.0f : (currentLeanWeight < -0.1f) ? -1.0f : 0.0f;
			const float prevDir = prevLeanDir;
			if (curDir != prevDir && (curDir != 0.0f || prevDir != 0.0f) && !pipboyOpen) {
				// Direction changed (or started/stopped leaning) — fire impulse
				const float dir = (curDir != 0.0f) ? curDir : -prevDir; // direction of the change
				RE::NiPoint3 posImp{
					ws.leanImpulseX * dir * intensityMult,
					ws.leanImpulseY * dir * intensityMult,
					ws.leanImpulseZ * dir * intensityMult
				};
				RE::NiPoint3 rotImp{
					ws.leanRotImpulsePitch * dir * intensityMult,
					ws.leanRotImpulseYaw   * dir * intensityMult,
					ws.leanRotImpulseRoll  * dir * intensityMult
				};
				leanImpulse.Fire(posImp, rotImp, 0.0f);
				PushEvent("Lean impulse");
			}
			prevLeanDir = curDir;
		}
		UpdateImpulseSpring(leanImpulse, ws.leanStiffness, ws.leanDamping, delta);
	}

	// ---- SNEAK IMPULSE ----
	// Use animation events (sneakStateEnter/Exit) for reliable sneak detection.
	// moveMode bits and ActorState::stance are unreliable in this engine build.
	{
		const bool sneakStartEvt = animEventSink.sneakStartedThisFrame.exchange(false, std::memory_order_relaxed);
		const bool sneakStopEvt  = animEventSink.sneakStoppedThisFrame.exchange(false, std::memory_order_relaxed);
		const bool sneakingBefore = wasSneaking;
		if (sneakStartEvt)      wasSneaking = true;
		else if (sneakStopEvt)  wasSneaking = false;

		if (springsActive && ws.sneakImpulseEnabled && wasSneaking != sneakingBefore && !pipboyOpen) {
			if (wasSneaking) {
				RE::NiPoint3 posImp{ ws.sneakEnterImpulseX * intensityMult, ws.sneakEnterImpulseY * intensityMult, ws.sneakEnterImpulseZ * intensityMult };
				RE::NiPoint3 rotImp{ ws.sneakEnterRotImpulse * intensityMult, 0.0f, 0.0f };
				sneakImpulse.Fire(posImp, rotImp, 0.0f);
				PushEvent("Sneak enter impulse");
			} else {
				RE::NiPoint3 posImp{ ws.sneakExitImpulseX * intensityMult, ws.sneakExitImpulseY * intensityMult, ws.sneakExitImpulseZ * intensityMult };
				RE::NiPoint3 rotImp{ ws.sneakExitRotImpulse * intensityMult, 0.0f, 0.0f };
				sneakImpulse.Fire(posImp, rotImp, 0.0f);
				PushEvent("Sneak exit impulse");
			}
		}
		UpdateImpulseSpring(sneakImpulse, ws.sneakStiffness, ws.sneakDamping, delta);
	}

	// ---- IMPULSE SPRINGS ----
	UpdateImpulseSpring(equipImpulse,           ws.equipStiffness,            ws.equipDamping, delta);
	UpdateImpulseSpring(adsEnterImpulse,        ws.adsEnterStiffness,         ws.adsEnterDamping, delta);
	UpdateImpulseSpring(adsExitImpulse,         ws.adsExitStiffness,          ws.adsExitDamping, delta);
	UpdateImpulseSpring(fireRecoveryImpulse,    ws.fireRecoveryStiffness,     ws.fireRecoveryDamping, delta);
	UpdateImpulseSpring(adsFireRecoveryImpulse, ws.adsFireRecoveryStiffness,  ws.adsFireRecoveryDamping, delta);
	UpdateImpulseSpring(reloadImpulse,          ws.reloadStiffness,           ws.reloadDamping, delta);
	UpdateImpulseSpring(emptyReloadImpulse,     ws.emptyReloadStiffness,      ws.emptyReloadDamping, delta);

	// Pip-Boy uses the same first-person rig / weapon chain as viewmodel offsets.
	// Springs are integrated above every frame; a start-of-Update reset was not
	// enough — COMBINE would still pick up impulses + camera/movement. Clear here
	// so we never push deferred or immediate offsets while the menu is up.
	if (IsPipboyMenuOpen()) {
		ResetSpringPhysicsState();
	}

	// ---- PROCEDURAL ADS TRANSITION + COMBINE + APPLY ----
	// The whole tail (transition envelope, spring summation, viewmodel
	// offset application, deferred-offset handoff, debug telemetry) is
	// pure inertia output — skipped entirely when springs are gated off.
	if (springsActive) {
	SpringState adsTransitionOffset{};  // starts zeroed
	if (adsTransitionActive) {
		adsTransitionTimer += delta;
		adsTransitionProgress = std::clamp(adsTransitionTimer / adsTransitionDuration, 0.0f, 1.0f);

		const ADSTransitionSettings& tcfg = adsTransitionIsEnter ? ws.adsEnterTransition : ws.adsExitTransition;
		float env = EvaluateTransitionEnvelope(tcfg, adsTransitionProgress);

		adsTransitionOffset.positionOffset.x = tcfg.peakOffsetX * env * intensityMult;
		adsTransitionOffset.positionOffset.y = tcfg.peakOffsetY * env * intensityMult;
		adsTransitionOffset.positionOffset.z = tcfg.peakOffsetZ * env * intensityMult;
		adsTransitionOffset.rotationOffset.x = tcfg.peakRotPitch * env * intensityMult;
		adsTransitionOffset.rotationOffset.y = tcfg.peakRotYaw   * env * intensityMult;
		adsTransitionOffset.rotationOffset.z = tcfg.peakRotRoll  * env * intensityMult;

		if (adsTransitionProgress >= 1.0f)
			adsTransitionActive = false;
	}

	// ---- COMBINE ----
	SpringState combined;
	auto addSpring = [](SpringState& out, const SpringState& a) {
		out.positionOffset.x += a.positionOffset.x;
		out.positionOffset.y += a.positionOffset.y;
		out.positionOffset.z += a.positionOffset.z;
		out.rotationOffset.x += a.rotationOffset.x;
		out.rotationOffset.y += a.rotationOffset.y;
		out.rotationOffset.z += a.rotationOffset.z;
	};
	addSpring(combined, cameraSpring);

	// Scale movement spring output by actionBlendFactor so existing
	// oscillation energy is also suppressed during reload/fire/melee,
	// not just the driving force.
	{
		SpringState scaledMovement = movementSpring;
		scaledMovement.positionOffset.x *= actionBlendFactor;
		scaledMovement.positionOffset.y *= actionBlendFactor;
		scaledMovement.positionOffset.z *= actionBlendFactor;
		scaledMovement.rotationOffset.x *= actionBlendFactor;
		scaledMovement.rotationOffset.y *= actionBlendFactor;
		scaledMovement.rotationOffset.z *= actionBlendFactor;
		addSpring(combined, scaledMovement);
	}

	addSpring(combined, sprintSpring);
	addSpring(combined, jumpSpring);
	addSpring(combined, equipImpulse.state);
	addSpring(combined, adsEnterImpulse.state);
	addSpring(combined, adsExitImpulse.state);
	addSpring(combined, fireRecoveryImpulse.state);
	addSpring(combined, adsFireRecoveryImpulse.state);
	addSpring(combined, reloadImpulse.state);
	addSpring(combined, emptyReloadImpulse.state);
	addSpring(combined, leanImpulse.state);
	addSpring(combined, sneakImpulse.state);
	addSpring(combined, adsTransitionOffset);

	// Lean additive offset (positional, not spring — tracks lean weight directly)
	if (ws.leanOffsetEnabled) {
		combined.positionOffset.x += leanAdditiveOffset.x;
		combined.positionOffset.y += leanAdditiveOffset.y;
		combined.positionOffset.z += leanAdditiveOffset.z;
	}

	// Walk direction offsets — weighted blend of 4 directional poses
	{
		auto blendDir = [](const WalkDirectionOffset& d, float w, RE::NiPoint3& pos, RE::NiPoint3& rot) {
			if (w <= 0.0f) return;
			pos.x += d.posX * w;
			pos.y += d.posY * w;
			pos.z += d.posZ * w;
			rot.x += d.rotPitch * w;
			rot.y += d.rotYaw * w;
			rot.z += d.rotRoll * w;
		};
		float walkABF = actionBlendFactor;
		blendDir(ws.walkForward,  walkWeightFwd   * walkABF, combined.positionOffset, combined.rotationOffset);
		blendDir(ws.walkBackward, walkWeightBack  * walkABF, combined.positionOffset, combined.rotationOffset);
		blendDir(ws.walkLeft,     walkWeightLeft  * walkABF, combined.positionOffset, combined.rotationOffset);
		blendDir(ws.walkRight,    walkWeightRight * walkABF, combined.positionOffset, combined.rotationOffset);
	}

	// Sanitize combined â€” if any axis went NaN, zero it
	auto sanitize = [](RE::NiPoint3& v) {
		if (!std::isfinite(v.x)) v.x = 0;
		if (!std::isfinite(v.y)) v.y = 0;
		if (!std::isfinite(v.z)) v.z = 0;
	};
	sanitize(combined.positionOffset);
	sanitize(combined.rotationOffset);

	// Store for deferred application (frame-gen safe)
	deferredOffsets.hasOffsets = true;
	deferredOffsets.isADS     = isCurrentlyADS;
	deferredOffsets.combined  = combined;
	deferredOffsets.settings  = ws;

	// Also apply immediately if not using second hook
	auto* fpNode = static_cast<RE::NiNode*>(player->Get3D(true));
	RE::NiNode* pivot = nullptr;
	if (fpNode) {
		pivot = FindTargetNode(fpNode, ws, isCurrentlyADS);
		if (pivot) {
			ApplyOffset(pivot, combined, ws);
		}
	}

	// Diagnostic logging â€” always runs once per second so we can trace issues
	debugFrameCounter++;
	bool doBaselineLog = (debugFrameCounter >= 30);
	if (doBaselineLog) {
		debugFrameCounter = 0;
		logger::trace("[FPGunplayOverhaul] dt={:.4f} "
			"camVel p={:.3f} y={:.3f} | "
			"PITCH spr: off={:.4f} vel={:.4f} | "
			"YAW pos: off={:.4f} vel={:.4f} | "
			"ROLL spr: off={:.4f} vel={:.4f} | "
			"comb pos=({:.4f},{:.4f},{:.4f}) rot=({:.4f},{:.4f},{:.4f}) | "
			"int={:.2f} aBlend={:.2f} settle={:.3f} stiff={:.0f} damp={:.1f} "
			"pMul={:.2f} cpMul={:.2f} inv={} fID=0x{:08X} piv={}",
			delta,
			smoothedCameraVelocity.x, smoothedCameraVelocity.z,
			cameraSpring.rotationOffset.z, cameraSpring.rotationVelocity.z,
			cameraSpring.positionOffset.z, cameraSpring.positionVelocity.z,
			cameraSpring.rotationOffset.y, cameraSpring.rotationVelocity.y,
			combined.positionOffset.x, combined.positionOffset.y, combined.positionOffset.z,
			combined.rotationOffset.x, combined.rotationOffset.y, combined.rotationOffset.z,
			intensityMult, actionBlendFactor, settlingFactor,
			ws.stiffness, ws.damping,
			ws.pitchMultiplier, ws.cameraPitchMult, ws.invertCameraPitch ? 1 : 0,
			cachedWeaponSettingsFormID,
			pivot ? "OK" : "NULL");
	}

	if (!pivot && fpNode) {
		static int nullPivotLogThrottle = 0;
		if (++nullPivotLogThrottle % 60 == 1) {
			logger::warn("[FPGunplayOverhaul] FindTargetNode returned nullptr (isADS={}, pivotWarmup={:.2f})",
				isCurrentlyADS, pivotWarmupTimer);
		}
	}
	}  // end if (springsActive) — transition/combine/apply/telemetry

	// Repeatable Gun Bash — solve and write the transition-blend bone
	// for this frame (see BashBlend at the top of the file). Runs its
	// own dedicated bone above RArm_Hand, deliberately OUTSIDE the
	// springsActive gate: masking the follow-up's reset snap should not
	// require inertia to be enabled.
	BashBlend::Apply(player);

	// Snapshot magazine ammo count for next-frame comparison.  Read here
	// (end of Update) so phantom-fire's `ammoDecreased` check at the top
	// of next Update sees a true frame-to-frame delta — including any
	// shots that QueueWeaponFire processed during this frame.
	lastEquippedAmmoCount = EquippedWeapon::GetMagazineAmmoCount(player);
	ammoCountInitialized  = true;
}

// ============================================================
// First-person node update (deferred, frame-gen safe)
// ============================================================
void Inertia::InertiaManager::OnFirstPersonUpdate(RE::NiAVObject* firstPersonObject)
{
	if (IsPipboyMenuOpen()) {
		deferredOffsets.hasOffsets = false;
		return;
	}
	if (!deferredOffsets.hasOffsets) return;
	if (!firstPersonObject) return;
	auto* node = static_cast<RE::NiNode*>(firstPersonObject);

	auto* pivot = FindTargetNode(node, deferredOffsets.settings, deferredOffsets.isADS);
	if (pivot) {
		ApplyOffset(pivot, deferredOffsets.combined, deferredOffsets.settings);
	}
	deferredOffsets.hasOffsets = false;
}

// ============================================================
// Reset / lifecycle
// ============================================================
void Inertia::InertiaManager::ResetSpringPhysicsState()
{
	cameraSpring.Reset();
	movementSpring.Reset();
	walkWeightFwd = walkWeightBack = walkWeightLeft = walkWeightRight = 0.0f;
	sprintSpring.Reset();
	jumpSpring.Reset();
	equipImpulse.Reset();
	adsEnterImpulse.Reset();
	adsExitImpulse.Reset();
	fireRecoveryImpulse.Reset();
	adsFireRecoveryImpulse.Reset();
	reloadImpulse.Reset();
	emptyReloadImpulse.Reset();
	leanImpulse.Reset();
	sneakImpulse.Reset();
	currentLeanWeight = 0.0f;
	prevLeanDir       = 0.0f;
	leanAdditiveOffset = { 0.0f, 0.0f, 0.0f };
	wasSneaking = false;
	smoothedLocalMovement = { 0.0f, 0.0f, 0.0f };
	deferredOffsets.hasOffsets = false;
	settlingFactor = 0.0f;
	timeSinceMovement = 0.0f;
	actionBlendFactor = 1.0f;
	adsTransitionActive    = false;
	adsTransitionTimer     = 0.0f;
	adsTransitionProgress  = 0.0f;
	adsTransitionDuration  = 0.0f;
	initialized            = false;
	lastCameraYaw          = 0.0f;
	lastCameraPitch        = 0.0f;
	smoothedCameraVelocity = { 0.0f, 0.0f, 0.0f };
}

void Inertia::InertiaManager::Reset()
{
	ResetSpringPhysicsState();
	reloadElapsedTime = 0.0f;
	lastMeasuredReloadDuration = 0.0f;
	meleeElapsedTime = 0.0f;
	lastMeasuredMeleeDuration = 0.0f;
	wasInMelee = false;
	airTime = 0.0f;
	wasADS = false;
	wasFiring = false;
	fireRecoveryCooldownTimer = 0.0f;
	adsFireRecoveryCooldownTimer = 0.0f;
	reloadImpulseDelayTimer = 0.0f;
	emptyReloadImpulseDelayTimer = 0.0f;
	lastReloadWasEmpty = false;
	lastKnownAmmoCount = 0;
	isCurrentlyReloading  = false;
	isCurrentlyEquipping  = false;
	equipAnimTimer        = 0.0f;
	isCurrentlyHolstering = false;
	holsterAnimTimer      = 0.0f;
	earlyEquipAdsArmed    = false;
	earlyEquipFireArmed   = false;
	earlyEquipPending     = false;
	earlyEquipTimer       = 0.0f;
	earlyAdsArmed         = false;
	earlyAdsReturnPending = false;
	earlyAdsReturnTimer   = 0.0f;
	earlyAdsTriggered     = false;
	earlyAdsPostDiagFrames = 0;
	earlyAdsForceIdleFrames = 0;
	earlyAdsForceIdleCountdown = 0;
	earlyAdsAutoFireWatching = false;
	earlyAdsAutoFireTimer = 0.0f;
	earlyAdsAutoFireAttempts = 0;
	earlyAdsAutoFireGraceTimer = 0.0f;
	earlyAdsAutoFirePhantomGap = 0.0f;
	earlyAdsAutoFireSeenGS8 = false;
	recentlyReloadedTimer = 0.0f;

	lastEquippedAmmoCount = 0;
	ammoCountInitialized  = false;

	earlyFireCancelArmed   = false;
	earlyFireCancelPending = false;
	earlyFireCancelTimer   = 0.0f;

	// Fire on Empty: if a dry-fire idle is still playing, stop it so it
	// can't blend through camera switches / game loads.
	if (fireOnEmptyAnimActive) {
		if (auto* p = RE::PlayerCharacter::GetSingleton()) {
			StopEmptyFireAnimation(p, "reset");
		}
	}
	fireOnEmptyAnimActive  = false;
	fireOnEmptyStopTimer   = 0.0f;
	fireOnEmptyVerifyTimer = 0.0f;
	fireOnEmptyLatched     = false;
	prevFireInputHeld      = false;
	fireOnEmptyWasADS = false;
	fireOnEmptyAdsHeldAtTrigger = false;
	fireOnEmptyGuardAdsRelease = false;
	fireOnEmptyMotionLock = 0.0f;
	fireOnEmptyDurationQueryTimer = 0.0f;
	fireOnEmptyAnimElapsed = 0.0f;
	fireOnEmptySuppressGrace = 0.0f;

	// Repeatable Gun Bash — drop the combo queue across camera switches /
	// game loads (a queued bash firing into a fresh scene would be wrong).
	wasBashActive        = false;
	bashQueuedCount      = 0;
	bashComboDelayTimer  = 0.0f;
	bashComboWindowOpen  = false;
	bashRetriggerCooldown = 0.0f;
	MeleeInput::s_meleePressedEdge = false;
	// A half-finished pose blend must not carry into a fresh scene.
	BashBlend::ResetState();

	FireAnnotationGuard::s_suppress.store(false, std::memory_order_relaxed);
	FireAnnotationGuard::s_suppressWeaponFire.store(false, std::memory_order_relaxed);
	FireAnnotationGuard::s_suppressAttackState.store(false, std::memory_order_relaxed);

	// Defensive: Early Equip / Early ADS can leave the attack handler
	// disabled if Reset() runs mid force-idle. Restore so ADS/holster work.
	if (auto* pcCtrl = RE::PlayerControls::GetSingleton(); pcCtrl && pcCtrl->attackHandler) {
		auto* atk = reinterpret_cast<RE::HeldStateHandler*>(pcCtrl->attackHandler);
		atk->inputEventHandlingEnabled = true;
	}

	pendingFallImpulse = false;
	pendingFallTimer   = 0.0f;
	confirmedInAir     = false;
	didJump            = false;
	cachedTargetNode = nullptr;
	cachedInsertedBone = nullptr;
	hasRefPosePivot = false;
	refPosePivotTranslate = { 0.0f, 0.0f, 0.0f };
	cachedWeaponSettingsValid = false;

	// Clean up super sprint state (remove speed/anim boosts and keyword if active)
	if (superSprintActive) {
		auto* player = RE::PlayerCharacter::GetSingleton();
		if (player && avifSpeedMult && std::abs(superSprintSpeedBoost) > 0.001f) {
			player->ModActorValue(static_cast<RE::ACTOR_VALUE_MODIFIER>(1), *avifSpeedMult, -superSprintSpeedBoost);
		}
		if (player && avifAnimMult && std::abs(superSprintAnimBoost) > 0.001f) {
			player->ModActorValue(static_cast<RE::ACTOR_VALUE_MODIFIER>(1), *avifAnimMult, -superSprintAnimBoost);
		}
		if (player && superSprintKeyword) {
			auto* npc = player->GetNPC();
			if (npc) {
				SuperSprintHelpers::RemoveKeyword(static_cast<RE::BGSKeywordForm*>(npc), superSprintKeyword);
			}
		}
	}
	superSprintActive      = false;
	superSprintWindowActive = false;
	superSprintWindowStart  = 0.0f;
	superSprintSpeedBoost  = 0.0f;
	superSprintAnimBoost   = 0.0f;
	superSprintPrevAP      = -1.0f;
	SuperSprintInput::s_eatEnabled  = false;
	SuperSprintInput::s_eatTriggered = false;
}

void Inertia::InertiaManager::OnGameLoaded()
{
	Reset();
	InertiaPresets::GetSingleton()->ResolveKeywordPointers();
	hasLoggedSkeleton = false;
	pivotWarmupTimer = 0.5f;
	RegisterAnimEventSink();
}

// ============================================================
// Super Sprint — runtime keyword and cached AVIF init
// ============================================================
void Inertia::InertiaManager::InitSuperSprint()
{
	// Cache frequently-used ActorValueInfo pointers from the AV singleton
	auto* avSingleton = RE::ActorValue::GetSingleton();
	if (avSingleton) {
		avifSpeedMult    = avSingleton->speedMult;
		avifAnimMult     = avSingleton->animationMult;
		avifActionPoints = avSingleton->actionPoints;
	}
	if (!avifSpeedMult)    logger::warn("[SuperSprint] Could not resolve SpeedMult AVIF");
	if (!avifAnimMult)     logger::warn("[SuperSprint] Could not resolve AnimationMult AVIF");
	if (!avifActionPoints) logger::warn("[SuperSprint] Could not resolve ActionPoints AVIF");

	// Create a runtime BGSKeyword named "AnimSuperSprintKeyword" for OAR condition matching.
	// If one already exists (e.g., from an ESP), reuse it.
	superSprintKeyword = RE::TESForm::GetFormByEditorID<RE::BGSKeyword>("AnimSuperSprintKeyword");
	if (!superSprintKeyword) {
		auto* factory = RE::ConcreteFormFactory<RE::BGSKeyword, RE::ENUM_FORM_ID::kKYWD>::GetFormFactory();
		if (factory) {
			superSprintKeyword = factory->Create();
			if (superSprintKeyword) {
				superSprintKeyword->SetFormEditorID("AnimSuperSprintKeyword");
				logger::info("[SuperSprint] Created runtime keyword 'AnimSuperSprintKeyword' (FormID 0x{:08X})",
					superSprintKeyword->GetFormID());
			}
		}
		if (!superSprintKeyword) {
			logger::error("[SuperSprint] Failed to create runtime keyword — OAR keyword condition will not work");
		}
	} else {
		logger::info("[SuperSprint] Found existing keyword 'AnimSuperSprintKeyword' (FormID 0x{:08X})",
			superSprintKeyword->GetFormID());
	}

	superSprintActive      = false;
	superSprintWindowActive = false;
	superSprintWindowStart  = 0.0f;
	superSprintSpeedBoost  = 0.0f;
	superSprintAnimBoost   = 0.0f;
	superSprintPrevAP      = -1.0f;

	// Install the SprintHandler vtable hook to eat sprint key presses
	// during the activation window (prevents the engine from toggling
	// sprint off on the second tap).
	if (!SuperSprintInput::s_installed) {
		SuperSprintInput::Install();
	}
	SuperSprintInput::s_eatEnabled  = false;
	SuperSprintInput::s_eatTriggered = false;

	// Install the AttackBlockHandler vtable hook for ground-truth fire/ADS
	// button state (used by Fire on Empty and Early Fire Cancel).
	if (!AttackInput::s_installed) {
		AttackInput::Install();
	}

	// Install the MeleeThrowHandler vtable hook for gun-bash presses
	// (used by Repeatable Gun Bash to queue follow-up bashes).
	if (!MeleeInput::s_installed) {
		MeleeInput::Install();
	}

	// Install the player graph-event hook that lets Fire on Empty swallow
	// weaponFire annotations (prevents real discharge during a dry-fire
	// on a vanilla fire clip).
	if (!FireAnnotationGuard::s_installed) {
		FireAnnotationGuard::Install();
	}

	logger::info("[SuperSprint] Initialized — SpeedMult={}, AnimMult={}, AP={}, Keyword={}, InputHook={}",
		avifSpeedMult != nullptr, avifAnimMult != nullptr,
		avifActionPoints != nullptr, superSprintKeyword != nullptr,
		SuperSprintInput::s_installed);
}

// ============================================================
// Hooks
// ============================================================
namespace
{
	using RunActorUpdatesFn = void(*)(void*, float, bool);
	REL::Relocation<std::uintptr_t> ptr_RunActorUpdates{ REL::ID(556439), 0x17 };
	RunActorUpdatesFn RunActorUpdatesOrig{ nullptr };

	void HookedActorUpdate(void* list, float dt, bool instant)
	{
		if (RunActorUpdatesOrig) RunActorUpdatesOrig(list, dt, instant);

		// The dt parameter at this hook site is always 0.
		// UneducatedShooter also ignores it and computes its own delta
		// from the engine time global. We use a high-resolution clock
		// and then scale by the global time multiplier (set by `sgtm`)
		// so our spring physics and procedural animations stay in sync
		// with the game world when slow-motion is active.
		static auto lastClock = std::chrono::high_resolution_clock::now();
		auto now = std::chrono::high_resolution_clock::now();
		float realDelta = std::chrono::duration<float>(now - lastClock).count();
		lastClock = now;

		realDelta = std::clamp(realDelta, 0.0001f, 0.1f);

		const float gameDelta = realDelta * GetGlobalTimeMult();

		Inertia::InertiaManager::GetSingleton()->Update(gameDelta, realDelta);
	}
}

void Inertia::Install()
{
	auto& trampoline = F4SE::GetTrampoline();
	RunActorUpdatesOrig = reinterpret_cast<RunActorUpdatesFn>(
		trampoline.write_call<5>(ptr_RunActorUpdates.address(), &HookedActorUpdate));

	logger::info("[FPGunplayOverhaul] Hooks installed: RunActorUpdates @ REL::ID(556439)+0x17");

	// Validate global time multiplier access (sgtm). At startup it should
	// be 1.0; if we read something wildly different, the REL::ID is wrong.
	float initSgtm = GetGlobalTimeMult();
	logger::info("[FPGunplayOverhaul] Global time multiplier (sgtm) at init: {:.4f}", initSgtm);
}
