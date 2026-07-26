#include "ContextualLean.h"
#include "Settings.h"
#include "SyntheticInput.h"

#include <nlohmann/json.hpp>

#include <Windows.h>
#include <cmath>
#include <cstdlib>
#include <filesystem>
#include <fstream>

// ============================================================
// bhkPickData ray helper
// ------------------------------------------------------------
// Multi-runtime CommonLib supplies bhkPickData and TESObjectCELL::Pick
// wrappers with OG/NG/AE relocations. The ray query's typed filter-data
// member is shared by all supported runtimes.
// ============================================================
namespace
{
	// Set the low collision-layer bits through CommonLib's declared Havok
	// layout. This is castQuery +0x0C; an older shim used +0x0A, which
	// straddled hknpMaterialId padding and the real filter word.
	void SetCollisionLayer(RE::bhkPickData& a_pick, std::uint32_t a_layer)
	{
		a_pick.castQuery.m_filterData.m_collisionFilterInfo = a_layer;
	}

	// kLOS = 41 — the "what blocks line of sight" layer. Semantically the
	// right query for "is there cover in front of me worth peeking around",
	// and it does not register bipeds, so passing NPCs don't trigger leans.
	constexpr std::uint32_t kColLayerLOS = 41;

	// Cast a ray from a_from to a_to against the player's parent cell.
	// Returns true and writes the hit fraction (0..1 along the segment)
	// when something LOS-blocking was hit. Per the reference contract,
	// a pick counts as a hit when HasHit() OR the returned NiAVObject
	// is non-null.
	bool CastLOSRay(RE::TESObjectCELL* a_cell, const RE::NiPoint3& a_from,
		const RE::NiPoint3& a_to, float& a_fractionOut)
	{
		if (!a_cell) return false;
		RE::bhkPickData pick{};
		SetCollisionLayer(pick, kColLayerLOS);
		pick.SetStartEnd(a_from, a_to);
		RE::NiAVObject* hitObj = a_cell->Pick(pick);
		if (pick.HasHit() || hitObj) {
			a_fractionOut = pick.GetHitFraction();
			return true;
		}
		return false;
	}
}

// ============================================================
// Synthetic input injection
// ------------------------------------------------------------
// UneducatedShooter hooks PerformInputProcessing on the
// BSInputEventReceiver embedded in PlayerCamera (base at +0x38) and
// matches raw button ids against its MCM lean keybinds. We build
// ButtonEvents that look exactly like the user pressing their own
// lean keys and dispatch them through that receiver's virtual call,
// which lands in US's hook (then forwards to the engine, same as a
// real press would).
//
// US folds all devices into one id space (see its
// InputEventReceiverOverride): keyboard scancodes 0-255 as-is,
// mouse buttons +256, mouse wheel 264/265, gamepad buttons mapped
// 266-281 from their XINPUT masks. Keybinds.json stores ids in that
// space, so we decompose them back into (device, raw idCode) here.
// ============================================================
namespace
{
	struct DecomposedKey
	{
		RE::INPUT_DEVICE device{ RE::INPUT_DEVICE::kKeyboard };
		std::uint32_t idCode{ 0xFFFF };
		bool valid{ false };
	};

	// Reverse of US's GamepadMaskToKeycode: unified id 266+ back to the
	// XINPUT button mask carried by real gamepad ButtonEvents.
	std::uint32_t GamepadOffsetToMask(std::uint32_t a_offset)
	{
		static constexpr std::uint32_t kMasks[16] = {
			0x0001,  // 266 DPAD_UP
			0x0002,  // 267 DPAD_DOWN
			0x0004,  // 268 DPAD_LEFT
			0x0008,  // 269 DPAD_RIGHT
			0x0010,  // 270 START
			0x0020,  // 271 BACK
			0x0040,  // 272 LEFT_THUMB
			0x0080,  // 273 RIGHT_THUMB
			0x0100,  // 274 LEFT_SHOULDER
			0x0200,  // 275 RIGHT_SHOULDER
			0x1000,  // 276 A
			0x2000,  // 277 B
			0x4000,  // 278 X
			0x8000,  // 279 Y
			0x0009,  // 280 LT (US's convention)
			0x000A,  // 281 RT
		};
		return (a_offset < 16) ? kMasks[a_offset] : 0;
	}

	DecomposedKey DecomposeUnifiedKey(std::uint32_t a_unified)
	{
		DecomposedKey out{};
		if (a_unified == 0xFFFF) {
			// US's "unbound" sentinel. A keyboard event with idCode 0xFFFF
			// still MATCHES US's comparison (id == leanLeft when leanLeft
			// is also 0xFFFF) — but with both keys unbound the two
			// directions become indistinguishable, so AreKeysUsable()
			// refuses that configuration before we ever get here.
			out.device = RE::INPUT_DEVICE::kKeyboard;
			out.idCode = 0xFFFF;
			out.valid = true;
		} else if (a_unified < 256) {
			out.device = RE::INPUT_DEVICE::kKeyboard;
			out.idCode = a_unified;
			out.valid = true;
		} else if (a_unified < 266) {
			// Mouse buttons 256-263, wheel 264-265 — US adds 256 to the raw id.
			out.device = RE::INPUT_DEVICE::kMouse;
			out.idCode = a_unified - 256;
			out.valid = true;
		} else if (a_unified < 282) {
			out.device = RE::INPUT_DEVICE::kGamepad;
			out.idCode = GamepadOffsetToMask(a_unified - 266);
			out.valid = out.idCode != 0;
		}
		return out;
	}

	// Fill one synthetic ButtonEvent. value 1 + heldDownSecs 0 reads as a
	// fresh press to US (it requires heldDownSecs == 0); value 0 is a
	// release. The user-event string is left empty on purpose: US matches
	// by idCode only, and an empty name means the engine's own handlers
	// (which match by name) ignore the forwarded event.
	void FillButtonEvent(RE::ButtonEvent& a_evt, const DecomposedKey& a_key,
		float a_value, float a_heldSecs)
	{
		SyntheticInput::InitializeButtonEvent(a_evt);
		a_evt.device       = a_key.device;
		a_evt.deviceID     = 0;
		a_evt.eventType    = RE::INPUT_EVENT_TYPE::kButton;
		a_evt.next         = nullptr;
		a_evt.timeCode     = 0;
		a_evt.handled      = RE::InputEvent::HANDLED_RESULT::kUnhandled;
		a_evt.strUserEvent = RE::BSFixedString("");
		a_evt.idCode       = a_key.idCode;
		a_evt.disabled     = false;
		a_evt.value        = a_value;
		a_evt.heldDownSecs = a_heldSecs;
	}

	// Dispatch a chain of events through PlayerCamera's input receiver in
	// one PerformInputProcessing call (US iterates the ->next chain, so
	// multi-step sequences apply atomically between frames — intermediate
	// lean states never get a frame of blend time and are invisible).
	bool DispatchThroughCameraReceiver(RE::ButtonEvent* a_events, std::size_t a_count)
	{
		auto* pcam = RE::PlayerCamera::GetSingleton();
		if (!pcam || a_count == 0) return false;
		for (std::size_t i = 0; i + 1 < a_count; ++i) {
			a_events[i].next = &a_events[i + 1];
		}
		a_events[a_count - 1].next = nullptr;
		// Virtual call — lands in UneducatedShooter's hooked slot when US
		// is installed, then forwards to the engine's own handler.
		RE::BSInputEventReceiver* recv = pcam;
		recv->PerformInputProcessing(a_events);
		return true;
	}
}

namespace ContextualLean
{
	// ============================================================
	// US config loading
	// ============================================================

	namespace
	{
		constexpr const char* kUSConfigINI   = "Data\\MCM\\Config\\UneducatedShooter\\settings.ini";
		constexpr const char* kUSSettingsINI = "Data\\MCM\\Settings\\UneducatedShooter.ini";
		constexpr const char* kUSKeybinds    = "Data\\MCM\\Settings\\Keybinds.json";

		std::int64_t FileWriteTime(const char* a_path)
		{
			std::error_code ec;
			auto t = std::filesystem::last_write_time(a_path, ec);
			return ec ? 0 : t.time_since_epoch().count();
		}
	}

	void Manager::LoadUSConfig()
	{
		// Mirror US's own LoadConfigs() file resolution exactly: when the
		// MCM Config default file exists, US reads the MCM Settings
		// override file instead (and keeps its GetValue defaults for any
		// missing key). When the override file can't be loaded, US keeps
		// its global initializers — we reproduce those too so our view of
		// "toggle mode" etc. matches what US is actually running with.
		const char* path = kUSConfigINI;
		if (std::filesystem::exists(kUSConfigINI)) {
			path = kUSSettingsINI;
		}

		CSimpleIniA ini(true, false, false);
		const SI_Error rc = ini.LoadFile(path);
		if (rc >= 0) {
			// Same keys + same defaults as US's LoadConfigs().
			usDisableLean  = std::atoi(ini.GetValue("Leaning", "bleanDisable", "0")) > 0;
			usToggleLean   = std::atoi(ini.GetValue("Leaning", "bToggleLean", "1")) > 0;
			usADSOnly      = std::atoi(ini.GetValue("Leaning", "bADSOnly", "0")) > 0;
			usLeanTimeCost = static_cast<float>(std::atof(ini.GetValue("Leaning", "fleanTimeCost", "0.2")));
			usLeanMax      = static_cast<float>(std::atof(ini.GetValue("Leaning", "fleanMax", "15.0")));
		} else {
			// US's global initializer values (used when its INI load fails).
			usDisableLean  = false;
			usToggleLean   = false;
			usADSOnly      = false;
			usLeanTimeCost = 1.0f;
			usLeanMax      = 15.0f;
		}
		if (usLeanTimeCost <= 0.01f) usLeanTimeCost = 0.01f;

		// Lean keybinds — same file and ids US reads.
		usLeanLeftKey  = 0xFFFF;
		usLeanRightKey = 0xFFFF;
		if (std::filesystem::exists(kUSKeybinds)) {
			try {
				std::ifstream reader(kUSKeybinds);
				nlohmann::json keyBinds;
				reader >> keyBinds;
				if (keyBinds.contains("keybinds")) {
					for (auto& key : keyBinds["keybinds"]) {
						if (key.value("modName", std::string{}) != "UneducatedShooter") continue;
						const std::string id = key.value("id", std::string{});
						if (id == "keyLeanLeft") {
							usLeanLeftKey = static_cast<std::uint32_t>(key.value("keycode", 0xFFFF));
						} else if (id == "keyLeanRight") {
							usLeanRightKey = static_cast<std::uint32_t>(key.value("keycode", 0xFFFF));
						}
					}
				}
			} catch (...) {
				logger::warn("[ContextualLean] Failed to parse {}", kUSKeybinds);
			}
		}

		settingsFileTime = FileWriteTime(path);
		keybindsFileTime = FileWriteTime(kUSKeybinds);
		usConfigLoaded = true;

		logger::info("[ContextualLean] US config: disableLean={} toggleLean={} adsOnly={} "
			"timeCost={:.2f} leanMax={:.1f} leanLeft={} leanRight={} (from {})",
			usDisableLean, usToggleLean, usADSOnly, usLeanTimeCost, usLeanMax,
			usLeanLeftKey, usLeanRightKey, path);
	}

	void Manager::MaybeReloadUSConfig(float a_delta)
	{
		configRecheckTimer += a_delta;
		if (configRecheckTimer < 2.0f) return;
		configRecheckTimer = 0.0f;

		const char* path = std::filesystem::exists(kUSConfigINI) ? kUSSettingsINI : kUSConfigINI;
		if (FileWriteTime(path) != settingsFileTime ||
			FileWriteTime(kUSKeybinds) != keybindsFileTime) {
			logger::info("[ContextualLean] US config files changed — reloading");
			LoadUSConfig();
		}
	}

	bool Manager::AreKeysUsable() const
	{
		// With both keys unbound (or bound to the same key), US cannot
		// distinguish left from right, so directional control is
		// impossible — require two distinct bindings.
		return usLeanLeftKey != usLeanRightKey &&
		       DecomposeUnifiedKey(usLeanLeftKey).valid &&
		       DecomposeUnifiedKey(usLeanRightKey).valid;
	}

	// ============================================================
	// Init / lifecycle
	// ============================================================

	void Manager::Init()
	{
		usInstalled = ::GetModuleHandleW(L"UneducatedShooter.dll") != nullptr;
		if (!usInstalled) {
			logger::info("[ContextualLean] UneducatedShooter.dll not loaded — feature disabled");
			return;
		}
		logger::info("[ContextualLean] UneducatedShooter.dll detected");
		LoadUSConfig();
	}

	void Manager::OnGameLoaded()
	{
		if (usInstalled) {
			LoadUSConfig();
		}
		// Any lean we were driving died with the old game state.
		ourLeanDir = 0;
		desiredHoldTimer = 0.0f;
		lastDesired = 0;
		patternFailTimer = 0.0f;
		minHoldTimer = 0.0f;
		cooldownTimer = 0.0f;
		overrideCheckTimer = 0.0f;
		backoffTimer = 0.0f;
		sinceLastEngage = 999.0f;
		quickEngageCount = 0;
		lastReleaseWasPatternFail = false;
	}

	void Manager::NotifyInputDevice(RE::INPUT_DEVICE a_device)
	{
		lastInputWasGamepad = (a_device == RE::INPUT_DEVICE::kGamepad);
	}

	// ============================================================
	// Synthetic lean control
	// ------------------------------------------------------------
	// The sequences below are STATELESS: they force US into the target
	// lean state no matter what its internal leanState currently is, so
	// no fragile mirroring of US internals is needed.
	//
	// Hold mode (bToggleLean=0):
	//   - a press unconditionally sets the direction; a release only
	//     clears when it matches the active direction. So:
	//     force dir  = press(dirKey)
	//     force none = release(leftKey) + release(rightKey)
	//
	// Toggle mode (bToggleLean=1), per US's ProcessButtonEvent table
	// (press target while in target -> off, otherwise -> target):
	//     press(opposite) then press(target) ends in `target` from any
	//     of the three states; appending one more press(target) then
	//     ends in `none` from any state.
	// ============================================================

	bool Manager::InjectForceDirection(int a_dir)
	{
		const DecomposedKey target = DecomposeUnifiedKey(a_dir > 0 ? usLeanLeftKey : usLeanRightKey);
		const DecomposedKey opposite = DecomposeUnifiedKey(a_dir > 0 ? usLeanRightKey : usLeanLeftKey);
		if (!target.valid || !opposite.valid) return false;

		RE::ButtonEvent evts[2]{};
		if (usToggleLean) {
			FillButtonEvent(evts[0], opposite, 1.0f, 0.0f);
			FillButtonEvent(evts[1], target, 1.0f, 0.0f);
			return DispatchThroughCameraReceiver(evts, 2);
		}
		FillButtonEvent(evts[0], target, 1.0f, 0.0f);
		return DispatchThroughCameraReceiver(evts, 1);
	}

	bool Manager::InjectForceNone()
	{
		const DecomposedKey left = DecomposeUnifiedKey(usLeanLeftKey);
		const DecomposedKey right = DecomposeUnifiedKey(usLeanRightKey);
		if (!left.valid || !right.valid) return false;

		RE::ButtonEvent evts[3]{};
		if (usToggleLean) {
			// From any state: press(right) -> {0,-1,-1}, press(left) -> 1,
			// press(left) -> 0.
			FillButtonEvent(evts[0], right, 1.0f, 0.0f);
			FillButtonEvent(evts[1], left, 1.0f, 0.0f);
			FillButtonEvent(evts[2], left, 1.0f, 0.0f);
			return DispatchThroughCameraReceiver(evts, 3);
		}
		// Hold mode: releases only clear on a matching direction, so
		// releasing both keys is a safe universal "stop".
		FillButtonEvent(evts[0], left, 0.0f, 0.25f);
		FillButtonEvent(evts[1], right, 0.0f, 0.25f);
		return DispatchThroughCameraReceiver(evts, 2);
	}

	void Manager::Disengage(bool a_inject, bool a_patternFail)
	{
		if (ourLeanDir != 0 && a_inject) {
			InjectForceNone();
		}
		ourLeanDir = 0;
		patternFailTimer = 0.0f;
		minHoldTimer = 0.0f;
		overrideCheckTimer = 0.0f;
		lastReleaseWasPatternFail = a_patternFail;
		// Cover-gone releases need spacing before the next attempt (the
		// geometry that just failed is probably still there). Releases the
		// player caused deliberately only need enough cooldown to let the
		// un-lean blend begin, so quick re-triggers stay responsive.
		cooldownTimer = a_patternFail
			? std::max(Settings::GetSingleton()->contextualLeanDisengageDelay, 0.4f)
			: 0.1f;
	}

	// ============================================================
	// Detection
	// ------------------------------------------------------------
	// Shared ray helpers. All rays run forward from a base origin with a
	// lateral offset along the screen-right vector.
	// ============================================================

	namespace
	{
		struct RayFrame
		{
			RE::TESObjectCELL* cell{ nullptr };
			RE::NiPoint3 origin{};
			RE::NiPoint3 fwd{};
			RE::NiPoint3 right{};
			bool valid{ false };
		};

		// Build the ray frame from the player's aim. The aim direction
		// comes from the actor's data angles, which UneducatedShooter's
		// lean does NOT modify, so `fwd` is lean-independent.
		RayFrame BuildRayFrame(RE::PlayerCharacter* a_player)
		{
			RayFrame f{};
			f.cell = a_player->parentCell;
			if (!f.cell) return f;

			a_player->GetEyeVector(f.origin, f.fwd, true);
			const float fwdLen = std::sqrt(f.fwd.x * f.fwd.x + f.fwd.y * f.fwd.y + f.fwd.z * f.fwd.z);
			if (fwdLen < 0.001f) return f;
			f.fwd.x /= fwdLen; f.fwd.y /= fwdLen; f.fwd.z /= fwdLen;

			// Screen-right = forward x world-up (Z-up world; at yaw 0 the
			// actor faces +Y and +X is to the right).
			f.right = { f.fwd.y, -f.fwd.x, 0.0f };
			const float rightLen = std::sqrt(f.right.x * f.right.x + f.right.y * f.right.y);
			if (rightLen < 0.01f) return f;  // aiming almost straight up/down
			f.right.x /= rightLen; f.right.y /= rightLen;

			f.valid = true;
			return f;
		}

		// Forward ray at a lateral offset. Returns hit distance in
		// a_distOut (rayLen when nothing was hit).
		bool CastOffsetRay(const RayFrame& a_frame, float a_lateral, float a_rayLen, float& a_distOut)
		{
			RE::NiPoint3 from{
				a_frame.origin.x + a_frame.right.x * a_lateral,
				a_frame.origin.y + a_frame.right.y * a_lateral,
				a_frame.origin.z
			};
			RE::NiPoint3 to{
				from.x + a_frame.fwd.x * a_rayLen,
				from.y + a_frame.fwd.y * a_rayLen,
				from.z + a_frame.fwd.z * a_rayLen
			};
			float fraction = 1.0f;
			if (CastLOSRay(a_frame.cell, from, to, fraction)) {
				a_distOut = fraction * a_rayLen;
				return true;
			}
			a_distOut = a_rayLen;
			return false;
		}
	}

	float Manager::ReadLeanMagnitude(RE::PlayerCharacter* a_player) const
	{
		float sinTheta = 0.0f;
		if (auto* fpRoot = a_player->Get3D(true)) {
			if (auto* camInserted = fpRoot->GetObjectByName("CameraInserted1st")) {
			sinTheta = std::clamp(camInserted->local.rotate.entry[0].z, -1.0f, 1.0f);
			}
		}
		const float sinMax = std::sin(std::max(usLeanMax * 0.017453292f, 0.001f));
		return std::clamp(std::fabs(sinTheta) / sinMax, 0.0f, 1.0f);
	}

	// ENGAGE evaluation — deliberately strict ("tight window" design):
	// the player should never be leaned unless the geometry unambiguously
	// reads as "cover edge with a clearly open side". Ambiguous meshes
	// (e.g. a column with more wall behind it) produce NO lean rather
	// than a guess.
	//
	// Conditions for leaning toward side Y around cover on side X:
	//   1. The X-side ray hits within the engage distance.
	//   2. The Y-side ray hits NOTHING within the whole probe length
	//      (= disengage distance) — full clearance, not merely "farther".
	//   3. The center ray is also blocked within engage distance, OR the
	//      X-side hit is very close (half the engage distance) — i.e. the
	//      player is actually up against the cover, not just walking
	//      down a corridor with a wall on one side.
	// Plus a thin-obstacle special case (center blocked, both wide rays
	// fully clear) resolved by narrow probes.
	int Manager::EvaluateEngageDirection(RE::PlayerCharacter* a_player)
	{
		auto* gs = Settings::GetSingleton();
		const RayFrame frame = BuildRayFrame(a_player);
		if (!frame.valid) return 0;

		const float engageDist = std::max(gs->contextualLeanEngageDistance, 16.0f);
		const float probeLen = std::max(gs->contextualLeanDisengageDistance, engageDist);
		const float sideOff = std::clamp(gs->contextualLeanSideOffset, 4.0f, 128.0f);

		float distC = probeLen, distL = probeLen, distR = probeLen;
		const bool hitC = CastOffsetRay(frame, 0.0f, probeLen, distC);
		const bool hitL = CastOffsetRay(frame, -sideOff, probeLen, distL);
		const bool hitR = CastOffsetRay(frame, +sideOff, probeLen, distR);

		const bool blockedC = hitC && distC <= engageDist;
		const bool blockedL = hitL && distL <= engageDist;
		const bool blockedR = hitR && distR <= engageDist;

		// Openness is a CONTRAST test, not a full-clearance test: the open
		// side must see meaningfully deeper than the cover (so a column
		// with a wall just behind it stays ambiguous -> no lean), but
		// distant back walls are allowed (requiring the whole probe to be
		// empty made indoor corners engage inconsistently, since incidental
		// geometry down the open side kept failing the pattern).
		constexpr float kOpenContrast = 60.0f;  // open side must see this much past the cover
		auto openVs = [&](bool a_hit, float a_dist, float a_coverDist) {
			if (!a_hit) return true;
			const float required = std::min(probeLen - 1.0f, a_coverDist + kOpenContrast);
			return a_dist > engageDist && a_dist >= required;
		};

		// The CENTER ray is the player's actual aim: leaning is only
		// justified when they are LOOKING AT the cover they'd peek around.
		// Side-ray-only contact (a wall at the screen edge while aiming at
		// open space) must never lean — that reads as random to the player.
		if (!blockedC) {
			return 0;
		}

		// Cover under the crosshair + clearly deeper opening on the right
		// -> lean right (US state -1).
		if (blockedL && openVs(hitR, distR, distL)) {
			return -1;
		}
		// Mirror image -> lean left (US state 1).
		if (blockedR && openVs(hitL, distL, distR)) {
			return 1;
		}

		// blockedC is guaranteed here.
		if (openVs(hitL, distL, distC) && openVs(hitR, distR, distC)) {
			// Thin obstacle (pole, pillar, doorframe edge): both wide rays
			// pass around it. Narrow probes at 40% offset decide which side
			// of it the muzzle is on; if both (or neither) are blocked the
			// obstacle is too centered to usefully lean around -> no lean.
			float dnl = probeLen, dnr = probeLen;
			const bool hnl = CastOffsetRay(frame, -sideOff * 0.4f, probeLen, dnl);
			const bool hnr = CastOffsetRay(frame, +sideOff * 0.4f, probeLen, dnr);
			const bool nBlockedL = hnl && dnl <= engageDist;
			const bool nBlockedR = hnr && dnr <= engageDist;
			if (nBlockedL && openVs(hnr, dnr, dnl)) return -1;
			if (nBlockedR && openVs(hnl, dnl, dnr)) return 1;
		}
		return 0;
	}

	// RELEASE evaluation — runs while leaning, and deliberately does NOT
	// use the live camera: the lean itself displaces the camera sideways,
	// so rays from the leaned viewpoint would see past the corner and
	// instantly report "cover gone" (that feedback loop was the cause of
	// the engage/release flicker in the first build). Instead, rays run
	// from the engage-time anchor translated by the player's movement
	// since engage (data.location delta — the lean's own char-proxy shift
	// contributes only a few units, well inside the thresholds).
	//
	// The lean stays valid while the cover evidence is still present:
	// the blocked side's ray OR the center ray hits within the disengage
	// distance. Direction is LATCHED — this never re-picks a side.
	bool Manager::AnchoredPatternStillValid(RE::PlayerCharacter* a_player)
	{
		auto* gs = Settings::GetSingleton();
		RayFrame frame = BuildRayFrame(a_player);
		if (!frame.valid) return false;

		// Re-anchor the origin: engage-time eye position plus how far the
		// player has actually moved since engage.
		frame.origin = {
			anchorEyePos.x + (a_player->data.location.x - anchorPlayerPos.x),
			anchorEyePos.y + (a_player->data.location.y - anchorPlayerPos.y),
			anchorEyePos.z + (a_player->data.location.z - anchorPlayerPos.z)
		};

		const float engageDist = std::max(gs->contextualLeanEngageDistance, 16.0f);
		const float probeLen = std::max(gs->contextualLeanDisengageDistance, engageDist);
		const float sideOff = std::clamp(gs->contextualLeanSideOffset, 4.0f, 128.0f);
		// The blocked (cover) side is opposite the lean direction:
		// leaning right (dir -1) means cover was on the left.
		const float coverLateral = (ourLeanDir < 0) ? -sideOff : +sideOff;

		// The rays are exactly probeLen long, so any hit is within the
		// disengage distance by construction.
		float dist = probeLen;
		const bool hitC = CastOffsetRay(frame, 0.0f, probeLen, dist);
		const bool hitB = CastOffsetRay(frame, coverLateral, probeLen, dist);
		// Narrow probe on the cover side — keeps thin-obstacle leans
		// (engaged via the narrow probes) alive too.
		const bool hitN = CastOffsetRay(frame, coverLateral * 0.4f, probeLen, dist);

		return hitC || hitB || hitN;
	}

	// ============================================================
	// Per-frame update
	// ============================================================

	void Manager::Update(RE::PlayerCharacter* a_player, float a_delta)
	{
		auto* gs = Settings::GetSingleton();

		if (cooldownTimer > 0.0f) cooldownTimer -= a_delta;
		if (backoffTimer > 0.0f) backoffTimer -= a_delta;
		// Engage-to-engage wall clock for the rapid-cycle guard (must
		// advance regardless of gating so intervals aren't understated).
		if (sinceLastEngage < 999.0f) sinceLastEngage += a_delta;

		// Hard prerequisites: feature on, US present with leaning enabled
		// and two usable keybinds.
		const bool featureAvailable =
			gs->contextualLeanEnabled &&
			usInstalled && usConfigLoaded &&
			!usDisableLean && AreKeysUsable();

		if (!featureAvailable) {
			// Feature turned off (or US lean unusable) mid-lean: release
			// once. Injection outside ADS is only processed by US when
			// !bADSOnly, but in the bADSOnly case US clears the lean on
			// its own every non-ADS frame, so either way it ends.
			if (ourLeanDir != 0) Disengage(true);
			return;
		}

		MaybeReloadUSConfig(a_delta);

		// Input device gate (KB+M / gamepad enable options).
		const bool deviceAllowed = lastInputWasGamepad
			? gs->contextualLeanGamepad
			: gs->contextualLeanKBM;

		// Menus: US ignores injected (and real) lean input while
		// UI->menuMode is set, so we can neither engage nor release —
		// freeze the state machine until the menu closes.
		auto* ui = RE::UI::GetSingleton();
		if (!ui || ui->menuMode != 0) return;
		static const RE::BSFixedString kVATSMenu{ "VATSMenu" };
		static const RE::BSFixedString kPipboyMenu{ "PipboyMenu" };
		if (ui->GetMenuOpen(kVATSMenu) || ui->GetMenuOpen(kPipboyMenu)) return;

		// Gameplay gates. Detection (ray pattern + stability timer) runs in
		// first person even BEFORE aiming, so that a pattern that is
		// already stable engages on the very first sighted frame — the
		// lean blend then runs concurrently with the ADS blend and the
		// whole transition reads as one motion. Actual engagement (input
		// injection) still requires ADS (gunState kSighted=6 /
		// kFireSighted=8 — same check US's IsInADS uses).
		bool inFP = false;
		if (auto* pcam = RE::PlayerCamera::GetSingleton(); pcam && pcam->currentState) {
			const auto stateID = pcam->currentState->id;
			inFP = (stateID == RE::CameraStates::kFirstPerson ||
			        stateID == RE::CameraStates::kIronSights);
		}
		// Mask to 4 bits: Dear-Modding's signed GUN_STATE bitfield
		// sign-extends kFireSighted (8) to 0xFFFFFFF8 without the mask.
		const auto gunState = static_cast<std::uint32_t>(a_player->gunState) & 0xFu;
		const bool inADS = (gunState == 6 || gunState == 8);
		const bool detectOK = inFP && deviceAllowed && !a_player->IsDead(true);

		if (!detectOK) {
			if (ourLeanDir != 0) {
				// First person / device / alive lost mid-lean. With
				// bADSOnly, US force-clears its lean state every non-ADS
				// frame already — injecting would do nothing (its input
				// gate also drops the events). Otherwise send the release
				// so the lean ends naturally.
				Disengage(!usADSOnly);
			}
			desiredHoldTimer = 0.0f;
			lastDesired = 0;
			return;
		}

		if (ourLeanDir != 0 && !inADS) {
			// ADS released mid-lean: user-intent release (short cooldown,
			// doesn't count toward the rapid-cycle guard — re-ADSing at
			// the same corner should re-lean promptly).
			Disengage(!usADSOnly);
			desiredHoldTimer = 0.0f;
			lastDesired = 0;
			return;
		}

		if (backoffTimer > 0.0f) return;

		// ============================================================
		// IDLE — evaluate the strict engage pattern.
		// ============================================================
		if (ourLeanDir == 0) {
			// Don't evaluate while a previous lean is still blending out:
			// the residual camera displacement would skew the rays (this
			// also naturally spaces out any engage/release cycling).
			if (cooldownTimer > 0.0f || ReadLeanMagnitude(a_player) > 0.15f) {
				desiredHoldTimer = 0.0f;
				lastDesired = 0;
				return;
			}

			const int desired = EvaluateEngageDirection(a_player);
			if (desired == lastDesired) {
				desiredHoldTimer += a_delta;
			} else {
				desiredHoldTimer = 0.0f;
				lastDesired = desired;
			}

			// Engagement itself requires ADS; before that we only keep the
			// stability timer warm so ADS entry can engage instantly.
			if (!inADS || desired == 0 || desiredHoldTimer < gs->contextualLeanEngageDelay) {
				return;
			}

			// Rapid-cycle guard: only engage -> cover-gone-release ->
			// engage loops count (geometry that defeats the hysteresis).
			// Deliberate re-triggers (ADS exit, moving/turning away)
			// reset the counter instead.
			if (lastReleaseWasPatternFail && sinceLastEngage < 2.0f) {
				if (++quickEngageCount >= 2) {
					logger::info("[ContextualLean] Rapid engage cycling detected — backing off");
					backoffTimer = 3.0f;
					quickEngageCount = 0;
					return;
				}
			} else {
				quickEngageCount = 0;
			}

			if (InjectForceDirection(desired)) {
				ourLeanDir = desired;
				minHoldTimer = 0.0f;
				patternFailTimer = 0.0f;
				sinceLastEngage = 0.0f;
				// Latch the anchor: release decisions compare against
				// where the player stood and aimed at engage time.
				RE::NiPoint3 fwdUnused;
				a_player->GetEyeVector(anchorEyePos, fwdUnused, true);
				anchorPlayerPos = a_player->data.location;
				anchorYaw = a_player->data.angle.z;
				// Verify the lean visibly appears (external mods / manual
				// key presses could cancel it) once US's blend has had
				// time to develop.
				overrideCheckTimer = usLeanTimeCost + 0.4f;
				if (gs->debugLogging) {
					logger::info("[ContextualLean] Engage {} (pattern stable {:.2f}s)",
						desired > 0 ? "LEFT" : "RIGHT", desiredHoldTimer);
				}
			}
			return;
		}

		// ============================================================
		// LEANING — direction is latched; only release conditions run.
		// ============================================================
		minHoldTimer += a_delta;

		// External-cancel detection: some time after engaging, the live
		// lean magnitude should be clearly non-zero. If not, the user (or
		// US itself) cancelled — send our release anyway so US's hold-mode
		// state can't be left dangling, then back off instead of fighting.
		// (Magnitude only: the inserted bone's sign convention is
		// unverified, and a wrong sign here caused false cancels before.)
		if (overrideCheckTimer > 0.0f) {
			overrideCheckTimer -= a_delta;
			if (overrideCheckTimer <= 0.0f && ReadLeanMagnitude(a_player) < 0.2f) {
				logger::info("[ContextualLean] Lean did not take effect "
					"(externally cancelled?) — backing off");
				Disengage(true);
				backoffTimer = 2.0f;
				return;
			}
		}

		// Movement release: contextual leans are position-bound — stepping
		// away from the engage spot invalidates the lean immediately (the
		// same rule shipped contextual-lean systems use). data.location
		// includes a few units of lean-induced proxy shift, which the
		// tolerance comfortably absorbs.
		const float dx = a_player->data.location.x - anchorPlayerPos.x;
		const float dy = a_player->data.location.y - anchorPlayerPos.y;
		if (std::sqrt(dx * dx + dy * dy) > gs->contextualLeanMoveTolerance) {
			if (gs->debugLogging) {
				logger::info("[ContextualLean] Release (moved from engage spot)");
			}
			Disengage(true);
			return;
		}

		// Turn release: aiming well away from the engage direction means
		// the player is no longer peeking that corner.
		float yawDelta = a_player->data.angle.z - anchorYaw;
		constexpr float kPi = 3.14159265f;
		while (yawDelta > kPi) yawDelta -= 2.0f * kPi;
		while (yawDelta < -kPi) yawDelta += 2.0f * kPi;
		if (std::fabs(yawDelta) > gs->contextualLeanYawTolerance * 0.017453292f) {
			if (gs->debugLogging) {
				logger::info("[ContextualLean] Release (turned away from engage direction)");
			}
			Disengage(true);
			return;
		}

		// Cover-gone release: anchored rays no longer see the obstacle.
		// Runs against the ANCHORED origin, so the lean's own camera
		// displacement cannot feed back into this decision.
		if (AnchoredPatternStillValid(a_player)) {
			patternFailTimer = 0.0f;
			return;
		}
		patternFailTimer += a_delta;
		if (minHoldTimer >= gs->contextualLeanMinHold &&
			patternFailTimer >= gs->contextualLeanDisengageDelay) {
			if (gs->debugLogging) {
				logger::info("[ContextualLean] Release (cover gone for {:.2f}s)", patternFailTimer);
			}
			Disengage(true, true);
		}
	}
}
