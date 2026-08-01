#include "CrouchSlide.h"

#include "Settings.h"
#include "SyntheticInput.h"

#include <cmath>
#include <cstring>

// ============================================================
// Crouch Slide
// ------------------------------------------------------------
// Commit-to-a-slide movement extra. Pressing crouch (or a bound hotkey)
// while sprinting launches a fast, eased ground slide driven directly
// through the Havok character controller. The player ends crouched, gets a
// runtime OAR keyword for the slide window, and can shoot the whole time.
//
// Everything here runs on the game thread from InertiaManager::Update
// (the PlayerCharacter::UpdateAnimation vfunc hook) except the SneakHandler
// input hook, which only sets an atomic flag consumed by Update.
//
// Multi-runtime: no raw offsets are introduced. The only hook is a vtable
// patch on SneakHandler slot 8 (identical to the proven SuperSprintInput
// SprintHandler patch), and all engine access goes through CommonLibF4
// virtuals / VariantID-backed helpers, so OG / NG / AE share one path.
// ============================================================

namespace
{
	// Havok world scale: 1 meter = 69.99125 game units (TES5Edit HK2GU).
	// Character-controller velocity is in Havok m/s, so game-unit speeds are
	// scaled by the inverse before being written to SetLinearVelocityImpl.
	constexpr float kGameToHavok = 1.0f / 69.99125f;

	// moveMode sprint bit (UneducatedShooter / Inertia convention).
	constexpr std::uint32_t kSprintBit = 0x100;

	// Slide velocity envelope — "rough and tumble". A punchy, near-instant
	// launch to peak speed, then a linear friction-style decay to zero. This
	// replaces the old symmetric smoothstep (short ease-in, flat top, long
	// smooth ease-out), which felt too floaty / gliding. The tiny ease-in
	// avoids a literal one-frame velocity teleport while still slamming to
	// peak almost immediately; the straight ramp-down reads as momentum
	// bleeding off to ground friction rather than easing gently to a stop.
	// Because the area under this shape is 0.5, the derived peak works out to
	// 2 * distance / duration — a noticeably faster burst than the old ~1.48x.
	constexpr float kEaseInFrac = 0.06f;  // ~1-2 frames of launch accel

	// Minimum gap between synthetic crouch toggle re-issues (pending + slide).
	// The toggle needs a few frames to register before we may conclude it was
	// swallowed; re-issuing sooner risks a double toggle that stands the
	// player back up.
	constexpr float kForceCrouchRetryInterval = 0.12f;  // ~7 frames @ 60fps

	// Friction curvature of the decay. 1.0 = linear (constant deceleration).
	// >1.0 makes the decay convex: the slide sheds speed faster right after the
	// launch and trails off harder near the end, which reads as ground friction
	// grabbing the player rather than a smooth glide. Kept moderate so the
	// derived peak speed only rises a little over the old linear shape.
	constexpr float kFrictionPower = 1.3f;

	// smoothstep on [0,1]
	float SmoothStep01(float a_x)
	{
		a_x = std::clamp(a_x, 0.0f, 1.0f);
		return a_x * a_x * (3.0f - 2.0f * a_x);
	}

	// Normalized speed envelope over the slide, p in [0,1]. Snaps up to 1.0
	// over the first kEaseInFrac (the launch), then decays as (1 - t)^power to
	// 0.0 at p = 1 (the friction bleed-off), where t is the fraction of the
	// post-launch window.
	float SlideEnvelope(float a_p)
	{
		if (a_p <= 0.0f || a_p >= 1.0f) return 0.0f;
		if (a_p < kEaseInFrac) return SmoothStep01(a_p / kEaseInFrac);
		const float t = (a_p - kEaseInFrac) / (1.0f - kEaseInFrac);
		return std::pow(1.0f - t, kFrictionPower);
	}

	// Definite integral of SlideEnvelope over [0,1]: the launch ramp averages
	// 0.5 over its width (0.5 * kEaseInFrac) plus the convex decay tail, whose
	// integral of (1 - t)^power over the remaining (1 - kEaseInFrac) width is
	// (1 - kEaseInFrac) / (power + 1). Peak speed = distance / (duration *
	// integral).
	constexpr float kEnvelopeIntegral =
		0.5f * kEaseInFrac + (1.0f - kEaseInFrac) / (kFrictionPower + 1.0f);

	// Wrap an angle to [-pi, pi].
	float WrapPi(float a_a)
	{
		constexpr float kPi = 3.14159265358979323846f;
		constexpr float kTwoPi = 2.0f * kPi;
		a_a = std::fmod(a_a + kPi, kTwoPi);
		if (a_a < 0.0f) a_a += kTwoPi;
		return a_a - kPi;
	}

	// Append/remove a keyword on a runtime BGSKeywordForm (the player NPC).
	// Same allocate-copy-swap approach as SuperSprintHelpers so the two
	// features behave identically for OAR.
	void AddKeywordTo(RE::BGSKeywordForm* a_form, RE::BGSKeyword* a_kw)
	{
		if (!a_form || !a_kw) return;
		for (std::uint32_t i = 0; i < a_form->numKeywords; ++i) {
			if (a_form->keywords[i] == a_kw) return;
		}
		const auto newCount = a_form->numKeywords + 1;
		auto** newArr = new RE::BGSKeyword*[newCount];
		for (std::uint32_t i = 0; i < a_form->numKeywords; ++i) {
			newArr[i] = a_form->keywords[i];
		}
		newArr[a_form->numKeywords] = a_kw;
		a_form->keywords = newArr;
		a_form->numKeywords = newCount;
	}

	void RemoveKeywordFrom(RE::BGSKeywordForm* a_form, RE::BGSKeyword* a_kw)
	{
		if (!a_form || !a_kw) return;
		std::uint32_t idx = UINT32_MAX;
		for (std::uint32_t i = 0; i < a_form->numKeywords; ++i) {
			if (a_form->keywords[i] == a_kw) { idx = i; break; }
		}
		if (idx == UINT32_MAX) return;
		for (std::uint32_t i = idx; i < a_form->numKeywords - 1; ++i) {
			a_form->keywords[i] = a_form->keywords[i + 1];
		}
		a_form->numKeywords--;
	}

	// Reach the player's Havok character controller (same access path used by
	// the jump/land detection in Inertia.cpp).
	RE::bhkCharacterController* GetCharController(RE::PlayerCharacter* a_player)
	{
		if (a_player && a_player->currentProcess && a_player->currentProcess->middleHigh) {
			return a_player->currentProcess->middleHigh->charController.get();
		}
		return nullptr;
	}

	// The live slide sound. A file-static because there is a single manager
	// and only one slide can play at a time; keeps CrouchSlide.h free of the
	// engine audio includes.
	RE::BSSoundHandle g_slideSound{};
	bool g_slideSoundValid = false;
}

// ============================================================
// SneakHandler input hook — detect "crouch pressed while sprinting"
// ------------------------------------------------------------
// We do NOT eat the press: letting the engine process the real Sneak event
// is exactly what leaves the player crouched at the end of the slide. The
// hook only records that the crouch key was pressed while the sprint bit was
// set, which Update consumes to start a slide. Same vtable-slot-8 patch as
// SuperSprintInput (BSInputEventUser::HandleEvent(ButtonEvent*)).
// ============================================================
namespace SneakSlideInput
{
	using FnHandleButton = void (*)(void*, const RE::ButtonEvent*);

	static FnHandleButton s_originalHandleButton = nullptr;
	static bool           s_installed = false;

	// Set by the hook on a sprint+crouch press; consumed by Update.
	inline std::atomic<bool> s_crouchPressedWhileSprint{ false };

	static void HookedHandleButton(void* a_self, const RE::ButtonEvent* a_event)
	{
		if (a_event && a_event->QJustPressed()) {
			// Only the Sneak user event, only while the feature + crouch-key
			// trigger are enabled and the player is sprinting this instant.
			const auto& userEvent = a_event->QUserEvent();
			if (userEvent.c_str() && std::strcmp(userEvent.c_str(), "Sneak") == 0) {
				auto* sgs = Settings::GetSingleton();
				auto* pc = RE::PlayerCharacter::GetSingleton();
				if (sgs && pc && sgs->crouchSlideEnabled && sgs->crouchSlideUseCrouchKey &&
					(pc->moveMode & kSprintBit) != 0) {
					s_crouchPressedWhileSprint.store(true);
				}
			}
		}
		// Always forward to the engine so crouch toggles normally.
		if (s_originalHandleButton) {
			s_originalHandleButton(a_self, a_event);
		}
	}

	static bool Install()
	{
		auto* pc = RE::PlayerControls::GetSingleton();
		if (!pc || !pc->sneakHandler) {
			logger::error("[CrouchSlide] PlayerControls or SneakHandler is null - crouch-key trigger disabled");
			return false;
		}

		// BSInputEventUser single-inheritance: vtable pointer at object+0.
		uintptr_t vtable = *reinterpret_cast<uintptr_t*>(pc->sneakHandler);
		constexpr uintptr_t kSlotOffset = 8 * sizeof(void*);  // HandleEvent(ButtonEvent*) = slot 8
		uintptr_t addr = vtable + kSlotOffset;

		std::memcpy(&s_originalHandleButton, reinterpret_cast<void*>(addr), sizeof(void*));

		uintptr_t hookAddr = reinterpret_cast<uintptr_t>(&HookedHandleButton);
		DWORD oldProtect = 0;
		if (!VirtualProtect(reinterpret_cast<void*>(addr), sizeof(void*), PAGE_EXECUTE_READWRITE, &oldProtect)) {
			logger::error("[CrouchSlide] VirtualProtect failed - crouch-key trigger disabled");
			return false;
		}
		std::memcpy(reinterpret_cast<void*>(addr), &hookAddr, sizeof(void*));
		VirtualProtect(reinterpret_cast<void*>(addr), sizeof(void*), oldProtect, &oldProtect);

		s_installed = true;
		logger::info("[CrouchSlide] Hooked SneakHandler::HandleEvent(ButtonEvent*) - vtable=0x{:X}, slot=8, original=0x{:X}",
			vtable, reinterpret_cast<uintptr_t>(s_originalHandleButton));
		return true;
	}

	// Dispatch a synthetic Sneak press+release straight through the engine's
	// (unhooked) original handler. This is the only mechanism confirmed to
	// actually crouch the player: it runs the full handler sequence (break
	// sprint, then enter sneak, driving the animation graph). We tried
	// Actor::SetSneaking(bool) instead and it only writes the sneak state
	// bool WITHOUT playing the crouch - worse, IsSneaking() reads that same
	// bool, so setting it blinded all of our own "are we crouched yet"
	// checks. Never write the bool directly.
	static void SendSyntheticSneak()
	{
		auto* pc = RE::PlayerControls::GetSingleton();
		if (!pc || !pc->sneakHandler || !s_originalHandleButton) return;

		auto fill = [](RE::ButtonEvent& a_evt, float a_value, float a_held) {
			SyntheticInput::InitializeButtonEvent(a_evt);
			a_evt.device = RE::INPUT_DEVICE::kKeyboard;
			a_evt.deviceID = 0;
			a_evt.eventType = RE::INPUT_EVENT_TYPE::kButton;
			a_evt.next = nullptr;
			a_evt.timeCode = 0;
			a_evt.handled = RE::InputEvent::HANDLED_RESULT::kUnhandled;
			a_evt.strUserEvent = RE::BSFixedString("Sneak");
			a_evt.idCode = 0;
			a_evt.disabled = false;
			a_evt.value = a_value;
			a_evt.heldDownSecs = a_held;
		};

		RE::ButtonEvent press;
		fill(press, 1.0f, 0.0f);       // QJustPressed()
		s_originalHandleButton(pc->sneakHandler, &press);

		RE::ButtonEvent release;
		fill(release, 0.0f, 0.01f);    // QReleased()
		s_originalHandleButton(pc->sneakHandler, &release);
	}
}

// ============================================================
// Manager
// ============================================================
void CrouchSlide::Manager::Init()
{
	// Cache the actor-value pointers used for AP cost and the post-slide
	// crouch-walk speed ramp.
	if (auto* avSingleton = RE::ActorValue::GetSingleton()) {
		m_avSpeedMult = avSingleton->speedMult;
		m_avActionPoints = avSingleton->actionPoints;
	}
	if (!m_avSpeedMult)    logger::warn("[CrouchSlide] Could not resolve SpeedMult AVIF");
	if (!m_avActionPoints) logger::warn("[CrouchSlide] Could not resolve ActionPoints AVIF");

	// Create (or reuse) the runtime keyword the user requested for OAR. Exact
	// editor ID "AnimsCrouchSlideKeyword" per the design spec.
	m_keyword = RE::TESForm::GetFormByEditorID<RE::BGSKeyword>("AnimsCrouchSlideKeyword");
	if (!m_keyword) {
		if (auto* factory = RE::ConcreteFormFactory<RE::BGSKeyword, RE::ENUM_FORM_ID::kKYWD>::GetFormFactory()) {
			m_keyword = factory->Create();
			if (m_keyword) {
				m_keyword->SetFormEditorID("AnimsCrouchSlideKeyword");
				logger::info("[CrouchSlide] Created runtime keyword 'AnimsCrouchSlideKeyword' (FormID 0x{:08X})",
					m_keyword->GetFormID());
			}
		}
		if (!m_keyword) {
			logger::error("[CrouchSlide] Failed to create runtime keyword - OAR keyword condition will not work");
		}
	} else {
		logger::info("[CrouchSlide] Found existing keyword 'AnimsCrouchSlideKeyword' (FormID 0x{:08X})",
			m_keyword->GetFormID());
	}

	if (!SneakSlideInput::s_installed) {
		SneakSlideInput::Install();
	}

	m_state = State::kIdle;
	SneakSlideInput::s_crouchPressedWhileSprint.store(false);
	g_hotkeyPressed.store(false);

	logger::info("[CrouchSlide] Initialized - SpeedMult={}, AP={}, Keyword={}, InputHook={}",
		m_avSpeedMult != nullptr, m_avActionPoints != nullptr,
		m_keyword != nullptr, SneakSlideInput::s_installed);
}

void CrouchSlide::Manager::Update(const FrameState& a_fs)
{
	auto* player = a_fs.player;
	auto* sgs = Settings::GetSingleton();
	if (!player || !sgs) return;

	// Always drain the input flags so a press made while disabled cannot
	// linger and surprise-trigger a slide after the feature is re-enabled.
	const bool crouchPressed = SneakSlideInput::s_crouchPressedWhileSprint.exchange(false);
	const bool hotkeyPressed = g_hotkeyPressed.exchange(false);

	// Track sprint history internally so the caller does not have to preserve
	// its own previous-frame sprint state for us.
	const bool prevSprinting = m_prevSprinting;
	m_prevSprinting = a_fs.sprinting;

	// Feature disabled: abort any active slide and stop here.
	if (!sgs->crouchSlideEnabled) {
		if (m_state != State::kIdle) {
			CancelSlide(player, "feature disabled");
		}
		return;
	}

	// --------------------------------------------------------
	// PENDING CROUCH — armed, waiting for the crouch pose before we drive
	// velocity. This is what makes it read as a slide instead of a lunge:
	// pushing the player forward while still standing / mid-transition feels
	// like a launch, so we hold until the crouch anim state is latched (or a
	// short safety timeout elapses, in case the sneak event is missed).
	// --------------------------------------------------------
	if (m_state == State::kPendingCrouch) {
		// Broke the setup before it could start: bail out cleanly.
		if (a_fs.inAir) {
			CancelSlide(player, "airborne before crouch");
			return;
		}

		m_crouchWaitTime += a_fs.delta;

		// Re-issue the crouch toggle until it takes. On a jump landing the
		// recovery state can swallow the press, so we retry on a throttle
		// (long enough for a press to register before we conclude it was
		// eaten) and stop the instant IsSneaking() reports true — re-issuing
		// after it engaged would toggle the player back OUT of the crouch.
		if (m_needsForceCrouch && !player->IsSneaking()) {
			m_forceCrouchRetryTimer += a_fs.delta;
			if (m_forceCrouchRetryTimer >= kForceCrouchRetryInterval) {
				ForceCrouch(player);
				m_forceCrouchRetryTimer = 0.0f;
				logger::info("[CrouchSlide] Re-issued crouch toggle while pending (t={:.2f}s)", m_crouchWaitTime);
			}
		} else {
			m_forceCrouchRetryTimer = 0.0f;
		}

		// Begin once the crouch POSE is latched (anti-lunge: driving velocity on
		// the input-flag edge, before the pose settles, reads as a launch). This
		// is the fast, normal path.
		if (a_fs.sneaking) {
			BeginSlideMotion(player);
			return;
		}

		// Fallback: the toggle registered (IsSneaking() true) but the pose anim
		// event has not latched (missed / renamed event). Rather than stall the
		// whole timeout we begin after a short settle; the crouch is already
		// engaged and blending by then, so no standing-lunge.
		constexpr float kCrouchSettleTime  = 0.15f;  // brief settle if the pose event is missed
		constexpr float kCrouchWaitTimeout = 0.45f;  // hard cap; crouch clearly failed
		if (player->IsSneaking() && m_crouchWaitTime >= kCrouchSettleTime) {
			BeginSlideMotion(player);
			return;
		}

		if (m_crouchWaitTime >= kCrouchWaitTimeout) {
			// Never got crouched (e.g. crouch blocked here). Do NOT slide
			// standing, which is exactly the launch/standing-slide bug.
			CancelSlide(player, "crouch never engaged");
		}
		return;
	}

	// --------------------------------------------------------
	// SLIDING — drive velocity, advance i-frames, watch for the end.
	// --------------------------------------------------------
	if (m_state == State::kSliding) {
		// Abort on jump / becoming airborne — the player broke the slide.
		if (a_fs.inAir) {
			CancelSlide(player, "airborne");
			return;
		}

		// Keep the player pinned in the crouch for the whole slide. Landing
		// recovery can stand the player back up partway through — this
		// mid-slide clearing is the "jump slide won't stay crouched" bug (the
		// original code only enforced the crouch at slide START). Primary
		// defense is the forceSneak pin set in BeginSlideMotion; this throttled
		// toggle re-issue is the fallback in case that bit turns out to be
		// inert for the player. Only fires when sneak reads off, never faster
		// than a press can register.
		if (m_needsForceCrouch && !player->IsSneaking()) {
			m_forceCrouchRetryTimer += a_fs.delta;
			if (m_forceCrouchRetryTimer >= kForceCrouchRetryInterval) {
				ForceCrouch(player);
				m_forceCrouchRetryTimer = 0.0f;
				logger::info("[CrouchSlide] Re-issued crouch toggle mid-slide (t={:.2f}s)", m_elapsed);
			}
		} else {
			m_forceCrouchRetryTimer = 0.0f;
		}

		m_elapsed += a_fs.delta;

		// Retry the gun-down visual until it takes. The graph refuses the
		// action while it is mid-transition at slide start; keep trying for
		// the first ~0.5s, then give up (firing/other actions will override
		// gun-down anyway, so a late success is pointless).
		if (!m_gunDownApplied && m_gunDownAction && m_gunDownRetryTime < 0.5f) {
			m_gunDownRetryTime += a_fs.delta;
			m_gunDownApplied = player->PerformAction(m_gunDownAction, nullptr);
			if (m_gunDownApplied) {
				logger::info("[CrouchSlide] Gun-down applied on retry at t={:.2f}s", m_elapsed);
			}
		}

		// Advance / expire i-frames.
		if (m_iframesActive) {
			m_iframesRemaining -= a_fs.delta;
			if (m_iframesRemaining <= 0.0f) {
				SetIFrames(player, false);
			}
		}

		// Steering: track the camera yaw, clamped to +/- the configured max
		// deviation from the heading captured at slide start. The camera
		// itself is never touched, only how much it can bend the slide.
		const float yaw = player->data.angle.z;
		float dyaw = WrapPi(yaw - m_startYaw);
		dyaw = std::clamp(dyaw, -m_maxSteer, m_maxSteer);
		const float heading = m_initialHeading + dyaw;
		m_dirX = std::sin(heading);
		m_dirY = std::cos(heading);

		// Eased speed for this frame, in GAME units/sec.
		const float p = (m_duration > 0.0001f) ? (m_elapsed / m_duration) : 1.0f;
		const float speed = m_peakSpeed * SlideEnvelope(p);

		// Throttle the diagnostic log to ~4 Hz so a slide produces a handful
		// of lines, not one per frame.
		m_diagTimer += a_fs.delta;
		const bool doLog = (m_diagTimer >= 0.25f);
		if (doLog) m_diagTimer = 0.0f;

		auto* cc = GetCharController(player);
		if (cc) {
			// Read last frame's post-move velocity first (read-back diagnostic:
			// shows whether the previous frame's write actually moved us).
			RE::hkVector4f cur;
			cc->GetLinearVelocityImpl(cur);

			// On-ground actor velocity, per the shipped ActorVelocityFramework
			// (BethesdaGhidra-verified FO4 mod): the levers are the base
			// controller's velocityTime + outVelocity + velocityMod, and they
			// are in GAME units, NOT Havok units. Our first attempt wrote Havok
			// units (~70x too small) through SetLinearVelocityImpl, which the
			// on-ground locomotion pass also overwrites — hence the player only
			// crouched. Write game-unit velocity into the fields the character
			// move integrates. Horizontal only; leave Z at 0 so the controller
			// keeps the player grounded (no leap), unlike the framework's
			// launch path which also forces kInAir + a Z nudge.
			const float vx = m_dirX * speed;
			const float vy = m_dirY * speed;
			cc->velocityTime = a_fs.delta;
			cc->outVelocity  = RE::hkVector4f(vx, vy, 0.0f, 0.0f);
			cc->velocityMod  = cc->outVelocity;

			if (doLog) {
				logger::info("[CrouchSlide] drive cc=ok p={:.2f} speed(u/s)={:.0f} set=({:.0f},{:.0f}) prevVelHk=({:.2f},{:.2f},{:.2f}) cached=({:.2f},{:.2f},{:.2f})",
					p, speed, vx, vy, cur.x, cur.y, cur.z,
					cc->cachedLinearVelocity.x, cc->cachedLinearVelocity.y, cc->cachedLinearVelocity.z);
			}
		} else if (doLog) {
			logger::warn("[CrouchSlide] drive cc=NULL - cannot move player (charController unreachable)");
		}

		if (m_elapsed >= m_duration) {
			EndSlide(player);
		}
		return;
	}

	// --------------------------------------------------------
	// RAMP-UP — ease the temporary SpeedMult reduction back to zero so the
	// player accelerates smoothly to crouch-walk speed. Skipped if they
	// stood up (no longer sneaking).
	// --------------------------------------------------------
	if (m_state == State::kRampUp) {
		if (!player->IsSneaking() || m_rampDuration <= 0.0001f) {
			// Remove any remaining reduction and finish.
			if (m_avSpeedMult && std::abs(m_rampSpeedDelta) > 0.001f) {
				player->ModActorValue(static_cast<RE::ACTOR_VALUE_MODIFIER>(1), *m_avSpeedMult, -m_rampSpeedDelta);
			}
			m_rampSpeedDelta = 0.0f;
			m_state = State::kIdle;
			return;
		}

		m_rampElapsed += a_fs.delta;
		const float p = std::clamp(m_rampElapsed / m_rampDuration, 0.0f, 1.0f);
		// Linearly decay the reduction from its start value to 0.
		const float desired = m_rampInitialReduction * (1.0f - p);
		if (m_avSpeedMult) {
			const float adjust = desired - m_rampSpeedDelta;
			if (std::abs(adjust) > 0.0001f) {
				player->ModActorValue(static_cast<RE::ACTOR_VALUE_MODIFIER>(1), *m_avSpeedMult, adjust);
			}
		}
		m_rampSpeedDelta = desired;
		if (p >= 1.0f) {
			m_rampSpeedDelta = 0.0f;
			m_state = State::kIdle;
		}
		return;
	}

	// --------------------------------------------------------
	// IDLE — evaluate triggers.
	// --------------------------------------------------------
	// A slide can only start in first person, on the ground, out of power
	// armor, and with enough AP.
	if (!a_fs.firstPerson || a_fs.inAir) return;
	if (RE::PowerArmor::ActorInPowerArmor(*player)) return;

	if (m_avActionPoints && sgs->crouchSlideAPCost > 0.0f) {
		if (player->GetActorValue(*m_avActionPoints) < sgs->crouchSlideAPCost) {
			return;
		}
	}

	// Crouch key while sprinting (the hook already verified the sprint bit at
	// the moment of the press). The engine is processing the real Sneak press
	// itself, so no synthetic toggle now.
	if (crouchPressed && sgs->crouchSlideUseCrouchKey) {
		StartSlide(player, /*a_crouchNow=*/false, "crouch key");
		return;
	}

	// Bound hotkey — works even with hold-to-sprint mods. Requires the player
	// to be sprinting (this frame or last). No real Sneak press occurred, so
	// crouch immediately.
	if (hotkeyPressed && (a_fs.sprinting || prevSprinting)) {
		StartSlide(player, /*a_crouchNow=*/true, "hotkey");
		return;
	}

	// Landing slide — on the filtered landing edge, if the player is carrying
	// enough horizontal momentum.
	if (sgs->crouchSlideLandingEnabled && a_fs.confirmedLanding) {
		if (auto* cc = GetCharController(player)) {
			RE::hkVector4f cur;
			cc->GetLinearVelocityImpl(cur);
			// Havok m/s -> game units/s for the threshold comparison.
			const float horizGame = std::sqrt(cur.x * cur.x + cur.y * cur.y) / kGameToHavok;
			if (horizGame >= sgs->crouchSlideLandingMomentum) {
				StartSlide(player, /*a_crouchNow=*/true, "landing momentum");
				return;
			}
		}
	}
}

void CrouchSlide::Manager::StartSlide(RE::PlayerCharacter* a_player, bool a_crouchNow, const char* a_reason)
{
	auto* sgs = Settings::GetSingleton();
	if (!a_player || !sgs) return;

	// Capture the motion parameters for this slide.
	m_duration = std::clamp(sgs->crouchSlideDuration, 0.3f, 5.0f);
	const float distance = std::clamp(sgs->crouchSlideDistance, 100.0f, 2000.0f);
	m_peakSpeed = distance / (m_duration * kEnvelopeIntegral);
	m_maxSteer = sgs->crouchSlideMaxSteerDegrees * 3.14159265358979323846f / 180.0f;

	// Deduct AP once at commit (damage modifier, same as Super Sprint's drain).
	if (m_avActionPoints && sgs->crouchSlideAPCost > 0.0f) {
		a_player->ModActorValue(static_cast<RE::ACTOR_VALUE_MODIFIER>(2), *m_avActionPoints, -sgs->crouchSlideAPCost);
	}

	// We own the crouch for every trigger path (crouch key, hotkey, landing):
	// while pending AND sliding we re-issue the crouch whenever it is not
	// engaged, because the jump-land recovery actively clears sneak (that is
	// the "jump slide won't stay crouched" bug). The crouch-key path must NOT
	// get an immediate synthetic toggle: the engine is processing the real
	// press this same frame, and a second toggle on top would cancel it out
	// and stand the player. Its crouch is still safeguarded by the throttled
	// re-issue in the pending/sliding states, which only fires if the real
	// press somehow never registered.
	m_needsForceCrouch = true;
	m_forceCrouchRetryTimer = 0.0f;
	if (a_crouchNow && !a_player->IsSneaking()) {
		ForceCrouch(a_player);
	}

	// Enter the pending-crouch wait. Velocity does NOT start here: driving it
	// while the player is still standing / mid-transition reads as a lunge.
	// BeginSlideMotion() runs once the crouch pose is detected (or a short
	// timeout elapses).
	m_state = State::kPendingCrouch;
	m_crouchWaitTime = 0.0f;

	logger::info("[CrouchSlide] Armed ({}) - dist={:.0f}u, dur={:.2f}s, peak={:.0f}u/s, steer={:.0f}deg - waiting for crouch",
		a_reason, distance, m_duration, m_peakSpeed, sgs->crouchSlideMaxSteerDegrees);
}

void CrouchSlide::Manager::BeginSlideMotion(RE::PlayerCharacter* a_player)
{
	auto* sgs = Settings::GetSingleton();
	if (!a_player || !sgs) { m_state = State::kIdle; return; }

	// Heading: straight ahead by default. With the omnidirectional toggle on,
	// derive the heading from the world direction the player is actually
	// moving (supports omnidirectional-sprint mods that let you sprint
	// sideways / backward). Captured now, at the moment the slide truly
	// begins, so it follows the committed facing after the crouch settles.
	const float yaw = a_player->data.angle.z;
	float heading = yaw;
	if (sgs->crouchSlideOmnidirectional) {
		if (auto* controls = RE::PlayerControls::GetSingleton()) {
			const auto& mv = controls->data.moveInputVec;
			if (std::abs(mv.x) > 0.01f || std::abs(mv.y) > 0.01f) {
				const float s = std::sin(yaw);
				const float c = std::cos(yaw);
				// right = (cos, -sin), forward = (sin, cos)
				const float worldX = c * mv.x + s * mv.y;
				const float worldY = -s * mv.x + c * mv.y;
				heading = std::atan2(worldX, worldY);
			}
		}
	}
	m_initialHeading = heading;
	m_startYaw = yaw;
	m_dirX = std::sin(heading);
	m_dirY = std::cos(heading);

	m_elapsed = 0.0f;
	m_diagTimer = 0.0f;
	m_state = State::kSliding;

	// Pin the engine's force-sneak bit for the duration of the slide. This is
	// the input-method-agnostic crouch guarantee: the same ActorState bit the
	// SetForceSneak console/script function (opcode 211) drives, so the engine
	// itself holds the actor in sneak regardless of what the input layer does
	// (jump-land recovery clearing sneak, hold-to-crouch mods toggling sneak
	// off on key release, ...). Deliberately NOT set during the pending wait:
	// it would make IsSneaking() read true and mask the toggle-retry guard
	// there, and the pending phase is what registers the PERSISTENT sneak
	// state that keeps toggle-sneak users crouched after the slide ends.
	// Restored in ClearSlideEffects.
	m_forceSneakPrev = a_player->forceSneak;
	a_player->forceSneak = 1;
	m_forceSneakPinned = true;

	// Add the OAR keyword to the player NPC base form.
	if (m_keyword) {
		if (auto* npc = a_player->GetNPC()) {
			AddKeywordTo(static_cast<RE::BGSKeywordForm*>(npc), m_keyword);
			m_keywordAdded = true;
		}
	}

	// Gun-down visual (weapon lowered). Firing overrides it automatically, so
	// the player can still shoot at any time. Resolve the action lazily.
	// Editor-ID lookup first (the proven path used by the gun-bash feature),
	// then the default-object fallback, then the static Fallout4.esm FormID
	// (0x00022A35, as ModSwitchFramework uses) as a last resort.
	if (!m_gunDownAction) {
		m_gunDownAction = RE::TESForm::GetFormByEditorID<RE::BGSAction>("ActionGunDown");
		const char* how = "editorID";
		if (!m_gunDownAction) {
			if (auto* dom = RE::BGSDefaultObjectManager::GetSingleton()) {
				m_gunDownAction = dom->GetDefaultObject<RE::BGSAction>(RE::DEFAULT_OBJECT::kActionGunDown);
				how = "defaultObject";
			}
		}
		if (!m_gunDownAction) {
			m_gunDownAction = RE::TESForm::GetFormByID<RE::BGSAction>(0x00022A35);
			how = "formID";
		}
		if (m_gunDownAction) {
			logger::info("[CrouchSlide] Resolved ActionGunDown via {} (FormID 0x{:08X})",
				how, m_gunDownAction->GetFormID());
		} else {
			logger::warn("[CrouchSlide] ActionGunDown could not be resolved (editorID, DOM, and FormID all failed)");
		}
	}
	// Attempt once now; Update retries over the first part of the slide until
	// the graph accepts it.
	m_gunDownApplied = false;
	m_gunDownRetryTime = 0.0f;
	if (m_gunDownAction) {
		m_gunDownApplied = a_player->PerformAction(m_gunDownAction, nullptr);
		logger::info("[CrouchSlide] Gun-down initial PerformAction={}", m_gunDownApplied);
	}

	// Optional i-frames for the first part of the slide.
	if (sgs->crouchSlideIFramesEnabled && sgs->crouchSlideIFramesDuration > 0.0f) {
		SetIFrames(a_player, true);
		m_iframesRemaining = sgs->crouchSlideIFramesDuration;
	}

	// Sound.
	PlaySound(a_player);

	logger::info("[CrouchSlide] Slide motion started - waited {:.2f}s for crouch, heading={:.1f}deg",
		m_crouchWaitTime, heading * 57.2958f);
}

void CrouchSlide::Manager::EndSlide(RE::PlayerCharacter* a_player)
{
	ClearSlideEffects(a_player);

	auto* sgs = Settings::GetSingleton();

	// If still crouching, ease SpeedMult back up over the ramp window so the
	// player accelerates smoothly to crouch-walk speed instead of snapping.
	// If they stood up, skip the ramp entirely (they resume at normal speed).
	if (a_player && sgs && m_avSpeedMult && sgs->crouchSlideRampUpTime > 0.0f && a_player->IsSneaking()) {
		const float baseSpeed = a_player->GetActorValue(*m_avSpeedMult);
		m_rampInitialReduction = -std::abs(baseSpeed) * 0.5f;  // start at half speed
		m_rampSpeedDelta = m_rampInitialReduction;
		m_rampElapsed = 0.0f;
		m_rampDuration = sgs->crouchSlideRampUpTime;
		a_player->ModActorValue(static_cast<RE::ACTOR_VALUE_MODIFIER>(1), *m_avSpeedMult, m_rampSpeedDelta);
		m_state = State::kRampUp;
		logger::info("[CrouchSlide] Ended - ramping crouch-walk speed over {:.2f}s", m_rampDuration);
	} else {
		m_state = State::kIdle;
		logger::info("[CrouchSlide] Ended - no speed ramp (standing or ramp disabled)");
	}
}

void CrouchSlide::Manager::CancelSlide(RE::PlayerCharacter* a_player, const char* a_reason)
{
	ClearSlideEffects(a_player);

	// Also unwind any in-progress speed ramp.
	if (a_player && m_avSpeedMult && std::abs(m_rampSpeedDelta) > 0.001f) {
		a_player->ModActorValue(static_cast<RE::ACTOR_VALUE_MODIFIER>(1), *m_avSpeedMult, -m_rampSpeedDelta);
	}
	m_rampSpeedDelta = 0.0f;

	m_state = State::kIdle;
	logger::info("[CrouchSlide] Cancelled ({})", a_reason);
}

void CrouchSlide::Manager::ClearSlideEffects(RE::PlayerCharacter* a_player)
{
	StopSound();

	// Release the force-sneak pin. The player's real sneak state (registered
	// during the pending phase) takes over: toggle users remain crouched,
	// hold-to-crouch users remain crouched only while still holding.
	if (m_forceSneakPinned && a_player) {
		a_player->forceSneak = m_forceSneakPrev;
	}
	m_forceSneakPinned = false;

	if (m_iframesActive) {
		SetIFrames(a_player, false);
	}

	if (m_keywordAdded && m_keyword && a_player) {
		if (auto* npc = a_player->GetNPC()) {
			RemoveKeywordFrom(static_cast<RE::BGSKeywordForm*>(npc), m_keyword);
		}
	}
	m_keywordAdded = false;
	m_elapsed = 0.0f;
}

void CrouchSlide::Manager::Reset()
{
	auto* player = RE::PlayerCharacter::GetSingleton();

	// Restore any borrowed state without assuming the player pointer is fully
	// valid (Reset runs on cell change / load).
	if (m_state != State::kIdle) {
		ClearSlideEffects(player);
		if (player && m_avSpeedMult && std::abs(m_rampSpeedDelta) > 0.001f) {
			player->ModActorValue(static_cast<RE::ACTOR_VALUE_MODIFIER>(1), *m_avSpeedMult, -m_rampSpeedDelta);
		}
	}
	m_rampSpeedDelta = 0.0f;
	m_state = State::kIdle;
	SneakSlideInput::s_crouchPressedWhileSprint.store(false);
	g_hotkeyPressed.store(false);
}

void CrouchSlide::Manager::SetIFrames(RE::PlayerCharacter* a_player, bool a_on)
{
	if (!a_player) { m_iframesActive = false; return; }
	auto* npc = a_player->GetNPC();
	if (!npc) { m_iframesActive = false; return; }

	auto& flags = npc->actorData.actorBaseFlags;
	if (a_on) {
		// Remember whether the flag was already set so we never clear a bit
		// something else legitimately owns.
		m_iframesWerePrev = flags.any(RE::ACTOR_BASE_DATA::Flag::kInvulnerable);
		flags.set(RE::ACTOR_BASE_DATA::Flag::kInvulnerable);
		m_iframesActive = true;
	} else {
		if (!m_iframesWerePrev) {
			flags.reset(RE::ACTOR_BASE_DATA::Flag::kInvulnerable);
		}
		m_iframesActive = false;
	}
}

void CrouchSlide::Manager::ForceCrouch(RE::PlayerCharacter* /*a_player*/)
{
	// Crouch via a synthetic Sneak press through the engine's own handler.
	// This is a TOGGLE, so every caller must guard on !IsSneaking() and
	// throttle re-issues (a second toggle before the first registers would
	// stand the player back up). Do NOT replace this with SetSneaking(bool):
	// tested in-game, that only writes the sneak bool without crouching, and
	// because IsSneaking() reads the same bool it also breaks the guards.
	SneakSlideInput::SendSyntheticSneak();
}

void CrouchSlide::Manager::PlaySound(RE::PlayerCharacter* a_player)
{
	StopSound();

	auto* sgs = Settings::GetSingleton();
	if (!sgs || sgs->crouchSlideVolume <= 0.0f) return;

	auto* audioMgr = RE::BSAudioManager::GetSingleton();
	if (!audioMgr) return;

	RE::BSResource::ID soundID;
	soundID.GenerateFromPath("F4SE\\Plugins\\FPGunplayOverhaul\\CrouchSlide\\crouchslide.wav");

	// GetSoundHandleByFile returns void; validity is read back from the
	// handle. Try usage flags 0x1A first (2D/loose-file play, per the
	// reference doc), then 0x00 as a fallback.
	audioMgr->GetSoundHandleByFile(g_slideSound, soundID, 0x1A, 0x00);
	if (!g_slideSound.IsValid()) {
		audioMgr->GetSoundHandleByFile(g_slideSound, soundID, 0x00, 0x00);
	}
	if (!g_slideSound.IsValid()) {
		logger::warn("[CrouchSlide] Could not build sound handle for crouchslide.wav (slide continues silently)");
		g_slideSoundValid = false;
		return;
	}

	// FO4 has no SetVolume: attenuation is in centibels of cut, so louder =
	// smaller value. Map slider volume (0..1) to a cut: full volume = 0 cut.
	const float vol = std::clamp(sgs->crouchSlideVolume, 0.0001f, 1.0f);
	const float atten = std::clamp(-2000.0f * std::log10(vol), 0.0f, 10000.0f);
	g_slideSound.SetStaticAttenuation(static_cast<std::uint16_t>(atten));

	bool followed = false;
	if (auto* obj3D = a_player ? a_player->Get3D() : nullptr) {
		g_slideSound.SetObjectToFollow(obj3D);
		followed = true;
	}
	const bool played = g_slideSound.Play();
	g_slideSoundValid = true;

	// Explicit confirmation: the handle built and Play() was issued. If this
	// logs but you hear nothing, the loose-WAV path is resolving but the audio
	// engine is not serving/decoding it (path-root or format issue), which is
	// the piece flagged as unverified in the reference doc.
	logger::info("[CrouchSlide] Sound: handle valid, Play()={}, followObj={}, vol={:.2f}, atten={:.0f}",
		played, followed, vol, atten);
}

void CrouchSlide::Manager::StopSound()
{
	if (g_slideSoundValid && g_slideSound.IsValid()) {
		g_slideSound.FadeOutAndRelease(120);  // ms; short tail
	}
	g_slideSoundValid = false;
}
