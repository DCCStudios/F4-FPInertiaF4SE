#include "ADSReload.h"
#include "Settings.h"

namespace
{
	// gunState nibble values (mirror of GunStateLocal in Inertia.cpp; the
	// vendored CommonLib exposes the field as a raw 4-bit int without the
	// named enum). Only the values this feature cares about.
	constexpr std::uint32_t kGunReloading   = 4;
	constexpr std::uint32_t kGunSighted     = 6;
	constexpr std::uint32_t kGunFireSighted = 8;

	// Absolute cap on a single hold. Generous enough for the slowest
	// modded reloads; exists so a missed disengage edge (interrupted
	// reload with aim stuck held by a hardware fault, etc.) can never pin
	// the zoom forever.
	constexpr float kMaxHoldSeconds = 15.0f;

	// Fallback zoom-out duration used when the engine's own
	// fovAdjustPerSec reads ~0 at release time (rate = |adjust| / this).
	constexpr float kFallbackZoomOutSeconds = 0.2f;

	// ------------------------------------------------------------
	// Runtime keyword list helpers — same pattern as CrouchSlide:
	// grow/shrink the BGSKeywordForm array on the player's TESNPC base
	// form. The old array is intentionally leaked on growth because the
	// engine may hold references to it (one-time, tiny).
	// ------------------------------------------------------------
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
		if (!a_form || !a_kw || a_form->numKeywords == 0) return;
		std::int64_t idx = -1;
		for (std::uint32_t i = 0; i < a_form->numKeywords; ++i) {
			if (a_form->keywords[i] == a_kw) { idx = i; break; }
		}
		if (idx < 0) return;
		for (std::uint32_t i = static_cast<std::uint32_t>(idx); i < a_form->numKeywords - 1; ++i) {
			a_form->keywords[i] = a_form->keywords[i + 1];
		}
		a_form->numKeywords--;
	}
}

void ADSReload::Manager::Init()
{
	// Create (or reuse) the runtime keyword OAR conditions resolve by
	// editor ID. Naming parallels the shipped CrouchSlide keyword
	// ("AnimsCrouchSlideKeyword") so animation authors get a familiar
	// convention: HasKeyword("AnimsADSReloadKeyword").
	m_keyword = RE::TESForm::GetFormByEditorID<RE::BGSKeyword>("AnimsADSReloadKeyword");
	if (!m_keyword) {
		if (auto* factory = RE::ConcreteFormFactory<RE::BGSKeyword, RE::ENUM_FORM_ID::kKYWD>::GetFormFactory()) {
			m_keyword = factory->Create();
			if (m_keyword) {
				m_keyword->SetFormEditorID("AnimsADSReloadKeyword");
				logger::info("[ADSReload] Created runtime keyword 'AnimsADSReloadKeyword' (FormID 0x{:08X})",
					m_keyword->GetFormID());
			}
		}
		if (!m_keyword) {
			logger::error("[ADSReload] Failed to create runtime keyword - OAR keyword condition will not work");
		}
	} else {
		logger::info("[ADSReload] Found existing keyword 'AnimsADSReloadKeyword' (FormID 0x{:08X})",
			m_keyword->GetFormID());
	}
}

void ADSReload::Manager::AddKeyword(RE::PlayerCharacter* a_player)
{
	if (!m_keyword || m_keywordAdded || !a_player) return;
	if (auto* npc = a_player->GetNPC()) {
		AddKeywordTo(static_cast<RE::BGSKeywordForm*>(npc), m_keyword);
		m_keywordAdded = true;
		m_keywordTarget = a_player;
	}
}

void ADSReload::Manager::RemoveKeyword()
{
	if (!m_keywordAdded) return;
	if (m_keyword && m_keywordTarget) {
		if (auto* npc = m_keywordTarget->GetNPC()) {
			RemoveKeywordFrom(static_cast<RE::BGSKeywordForm*>(npc), m_keyword);
		}
	}
	m_keywordAdded = false;
	m_keywordTarget = nullptr;
}

void ADSReload::Manager::Disengage(RE::PlayerCamera* a_camera, bool a_engineSighted, const char* a_reason)
{
	if (a_camera && !a_engineSighted) {
		// Hand the zoom back to the engine's interpolator: target back to
		// the non-sighted adjust (0) and make sure the transition rate is
		// sane so the zoom-out animates instead of hanging. The engine set
		// fovAdjustPerSec when it started its own exit transition at reload
		// start (we never wrote that field), so it normally still holds the
		// correct rate here.
		a_camera->fovAdjustTarget = 0.0f;
		if (std::fabs(a_camera->fovAdjustPerSec) < 0.01f) {
			const float rate = std::max(1.0f, std::fabs(m_holdFov) / kFallbackZoomOutSeconds);
			a_camera->fovAdjustPerSec = rate;
			logger::debug("[ADSReload] fovAdjustPerSec was ~0 at release; set fallback rate {:.1f}", rate);
		}
	}
	RemoveKeyword();
	m_holdActive  = false;
	m_regainTimer = 0.0f;
	m_safetyTimer = 0.0f;
	logger::info("[ADSReload] Hold released — {} (engineSighted={})", a_reason, a_engineSighted);
}

void ADSReload::Manager::ForceRelease(const char* a_reason)
{
	if (!m_holdActive) {
		// No zoom hold, but a pre-armed keyword (applied while sighted with
		// aim held) may still be on the player — e.g. the POV switched to
		// third person mid-aim. Update() no longer runs outside first
		// person, so drop it here.
		RemoveKeyword();
		return;
	}
	auto* camera = RE::PlayerCamera::GetSingleton();
	auto* player = RE::PlayerCharacter::GetSingleton();
	// If the player is genuinely sighted again the engine owns the zoom
	// values; otherwise restore the zoom-out target so FOV can't stick.
	bool engineSighted = false;
	if (player) {
		const auto gs = static_cast<std::uint32_t>(player->gunState) & 0xFu;
		engineSighted = (gs == kGunSighted || gs == kGunFireSighted);
	}
	Disengage(camera, engineSighted, a_reason);
}

void ADSReload::Manager::OnGameLoaded()
{
	// Fresh camera + graph after a load: drop all state without touching
	// camera fields (the engine reinitializes them itself).
	RemoveKeyword();
	m_holdActive       = false;
	m_regainTimer      = 0.0f;
	m_safetyTimer      = 0.0f;
	m_lastAdsFovAdjust = 0.0f;
	m_lastAdsFovPerSec = 0.0f;
}

void ADSReload::Manager::Update(RE::PlayerCharacter* a_player,
	RE::PlayerCamera*                               a_camera,
	float                                           a_delta,
	std::uint32_t                                   a_gunState,
	bool                                            a_adsAimHeld,
	bool                                            a_wasADS,
	bool                                            a_wasScoped,
	bool                                            a_reloadStarted,
	float                                           a_sightedTransitionSeconds)
{
	if (!a_player || !a_camera) {
		if (m_holdActive) ForceRelease("player/camera unavailable");
		return;
	}

	auto* s = Settings::GetSingleton();

	const bool sightedNow = (a_gunState == kGunSighted || a_gunState == kGunFireSighted);

	// ---- Continuous capture of the sighted zoom adjust ----
	// Sampled while genuinely sighted because by the time reloadStateEnter
	// arrives the engine has already reset fovAdjustTarget for its zoom-out.
	// Take whichever of current/target has the larger magnitude: while an
	// enter transition is still in flight the target holds the full zoom,
	// and once settled they're equal.
	if (sightedNow) {
		const float cur = a_camera->fovAdjustCurrent;
		const float tgt = a_camera->fovAdjustTarget;
		m_lastAdsFovAdjust = (std::fabs(cur) > std::fabs(tgt)) ? cur : tgt;
		if (std::fabs(a_camera->fovAdjustPerSec) > 0.01f) {
			m_lastAdsFovPerSec = a_camera->fovAdjustPerSec;
		}
	}

	// ---- Keyword pre-arm ----
	// OAR evaluates a replacement's conditions at the instant the reload
	// CLIP ACTIVATES, which happens earlier in the frame than this hook
	// (verified in-game 2026-08-03: with the keyword applied only on
	// reloadStateEnter, an ADS submod and its negated twin both agreed the
	// keyword was absent at activation). Racing that evaluation from here
	// is unwinnable, so instead the keyword goes ON while the player is
	// sighted with aim held — i.e. while an ADS reload COULD start — and
	// stays on through the hold itself. Any reload actually started from
	// ADS (reload key or fire-to-empty auto reload) then evaluates with
	// the keyword already present; releasing aim removes it, so hip
	// reloads still see it absent.
	const bool scopedExcluded = a_wasScoped && !s->adsReloadAllowScoped;
	const bool preArm = s->adsReloadEnabled && a_adsAimHeld && sightedNow && !scopedExcluded;
	if (preArm || m_holdActive) {
		AddKeyword(a_player);
	} else {
		RemoveKeyword();
	}

	// ============================================================
	// Not holding: decide whether this frame's reload start engages.
	// ============================================================
	if (!m_holdActive) {
		if (!s->adsReloadEnabled || !a_reloadStarted || !a_adsAimHeld) return;

		// Must have actually been sighted going into the reload. The same
		// frame can already read gunState 4 (SightedStateExit and
		// reloadStateEnter fire together), so accept last frame's ADS too.
		if (!a_wasADS && !sightedNow) {
			logger::debug("[ADSReload] Reload started with aim held but not from ADS (gs={}) — not engaging",
				a_gunState);
			return;
		}

		// Scope-overlay weapons: the full-screen ScopeMenu closes when the
		// graph leaves sighted, so holding the raw zoom shows a zoomed
		// world with no scope. Off by default.
		if (a_wasScoped && !s->adsReloadAllowScoped) {
			logger::debug("[ADSReload] Not engaging — weapon has a scope overlay (allow-scoped is off)");
			return;
		}

		m_holdActive  = true;
		m_holdFov     = m_lastAdsFovAdjust;
		m_regainTimer = 0.0f;
		m_safetyTimer = 0.0f;
		AddKeyword(a_player);

		// Pin immediately so the engine's exit interpolation never gets a
		// visible frame of zoom-out.
		a_camera->fovAdjustCurrent = m_holdFov;
		a_camera->fovAdjustTarget  = m_holdFov;

		logger::info("[ADSReload] Hold engaged — fovAdjust pinned at {:.3f} (gs={}, keyword={})",
			m_holdFov, a_gunState, m_keywordAdded);
		return;
	}

	// ============================================================
	// Holding: check the disengage edges, otherwise keep pinning.
	// ============================================================
	m_safetyTimer += a_delta;

	if (!s->adsReloadEnabled) {
		Disengage(a_camera, false, "feature disabled mid-hold");
		return;
	}
	// The user contract: releasing the aim input at any point exits ADS.
	if (!a_adsAimHeld) {
		Disengage(a_camera, false, "aim input released");
		return;
	}
	// Engine re-entered sighted (natural post-reload re-aim, or an Early
	// Fire Cancel recovery landed in fireSighted). It has set its own
	// target/rate; ours already equal the zoom, so the FOV hand-off is
	// seamless — just stop writing.
	if (a_gunState == kGunSighted || a_gunState == kGunFireSighted) {
		// The POSE hand-off is not seamless on its own: the engine plays
		// the aim-enter blend (post-reload hip pose -> sights) in real
		// time, so the viewmodel visibly dips and re-raises under the held
		// zoom. Complete that blend before it renders with a single big
		// graph step — the same UpdateAnimation fast-forward the Dry Fire /
		// ADS Dry Fire stops use (the UpdateAnimation hook's reentrancy
		// guard skips the manager for this nested call). Only for the
		// plain sighted re-entry: a fireSighted (8) recovery is mid-shot,
		// and skipping graph time there would eat weaponFire annotations
		// the phantom-fire logic depends on.
		if (a_gunState == kGunSighted && a_sightedTransitionSeconds > 0.0f) {
			const float ff = a_sightedTransitionSeconds + 0.05f;
			a_player->UpdateAnimation(ff);
			logger::info("[ADSReload] Fast-forwarded aim-enter blend by {:.2f}s", ff);
		}
		Disengage(a_camera, true, "engine re-sighted");
		return;
	}
	// Outside the reload state (reload finished or was interrupted —
	// stagger, weapon switch, grenade throw): give the engine a short
	// window to re-sight on its own, then let go. Reset while gunState
	// reads reloading so long modded reloads never trip it.
	if (a_gunState != kGunReloading) {
		m_regainTimer += a_delta;
		if (m_regainTimer >= s->adsReloadRegainTimeout) {
			Disengage(a_camera, false, "re-sight window elapsed");
			return;
		}
	} else {
		m_regainTimer = 0.0f;
	}
	if (m_safetyTimer >= kMaxHoldSeconds) {
		Disengage(a_camera, false, "safety cap");
		return;
	}

	// Keep the zoom pinned. Writing BOTH fields makes the engine's own
	// interpolation a no-op (current == target) regardless of where its
	// camera update lands relative to this hook in the frame.
	a_camera->fovAdjustCurrent = m_holdFov;
	a_camera->fovAdjustTarget  = m_holdFov;
}
