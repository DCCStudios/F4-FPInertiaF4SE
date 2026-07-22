#pragma once

// Procedural ADS transition motion settings (per enter and per exit)
struct ADSTransitionSettings
{
	bool  enabled{ false };

	// Curve types:
	// 0 = Sine      (sin(pi*t) bell -- smooth, symmetric)
	// 1 = EaseInOut (cubic hermite -- acceleration/deceleration)
	// 2 = Bounce    (overshoot then settle at end)
	// 3 = Overshoot (peak overshoots then returns to 0)
	int   curveType{ 0 };

	// Additive peak offsets at the normalized peak position (bone-local: X=vert, Y=fwd, Z=lat)
	float peakOffsetX{ 0.0f };
	float peakOffsetY{ 0.0f };
	float peakOffsetZ{ 0.0f };

	// Additive peak rotations (pitch, yaw, roll in degrees)
	float peakRotPitch{ 0.0f };
	float peakRotYaw{ 0.0f };
	float peakRotRoll{ 0.0f };

	// Where in the [0,1] normalized transition the peak occurs (0.5 = midpoint)
	float peakPosition{ 0.5f };

	// Asymmetry: positive = front-loaded (peaks early), negative = back-loaded
	float asymmetry{ 0.0f };

	// How strongly the curve's endpoints blend with the ADS enter/exit impulse spring
	float impulseBlendFactor{ 0.0f };
};

// Per-direction additive walk offset (position + rotation)
// These blend in/out linearly when the player walks in that direction,
// and blend together when moving diagonally.
struct WalkDirectionOffset
{
	float posX{ 0.0f };  // vertical
	float posY{ 0.0f };  // forward
	float posZ{ 0.0f };  // lateral
	float rotPitch{ 0.0f };  // degrees
	float rotYaw{ 0.0f };
	float rotRoll{ 0.0f };
};

// Fallout 4 weapon categories (keyword-based detection)
enum class WeaponType : int
{
	Unarmed = 0,
	Melee = 1,       // WeapTypeSword, WeapTypeBlunt, etc. (non-unarmed melee)
	Pistol = 2,      // WeapTypePistol / grip keyword
	Rifle = 3,       // WeapTypeRifle / grip keyword (+ shotguns)
	Heavy = 4,       // WeapTypeHeavyGun
	Energy = 5,      // Energy-based weapons (laser, plasma, etc.)
	Throwable = 6,   // Grenades / mines
	// Power Armor variants - same weapon type logic but separate profiles
	PA_Unarmed = 7,
	PA_Melee = 8,
	PA_Pistol = 9,
	PA_Rifle = 10,
	PA_Heavy = 11,
	PA_Energy = 12,
};

constexpr int kWeaponTypeCount = 13;

// Per-weapon-type inertia settings (all configurable, saved to JSON presets)
struct WeaponInertiaSettings
{
	// === MASTER TOGGLE ===
	bool  enabled{ true };

	// === CAMERA INERTIA (responds to looking around) ===
	float stiffness{ 150.0f };
	float damping{ 12.0f };
	float maxOffset{ 8.0f };
	float maxRotation{ 15.0f };
	float mass{ 1.0f };
	float pitchMultiplier{ 1.0f };
	float rollMultiplier{ 1.0f };
	float cameraPitchMult{ 1.0f };
	float momentumDecay{ 6.0f };  // exponential lag rate (units/sec); higher = snappier, lower = heavier
	bool  invertCameraPitch{ false };
	bool  invertCameraYaw{ false };

	// === MOVEMENT INERTIA (responds to strafing / walking) ===
	bool  movementInertiaEnabled{ true };
	float movementStiffness{ 80.0f };
	float movementDamping{ 6.0f };
	float movementMaxOffset{ 12.0f };
	float movementMaxRotation{ 20.0f };
	float movementLeftMult{ 1.0f };
	float movementRightMult{ 1.0f };
	float movementForwardMult{ 0.5f };
	float movementBackwardMult{ 0.5f };
	bool  invertMovementLateral{ false };
	bool  invertMovementForwardBack{ false };

	// === WALK DIRECTION OFFSETS (additive pose per direction) ===
	bool  walkOffsetsEnabled{ false };
	float walkOffsetBlendInSpeed{ 4.0f };   // how fast offsets blend in (units/sec)
	float walkOffsetBlendOutSpeed{ 6.0f };  // how fast offsets blend out (faster for snappy return)
	float walkOffsetAdsBlendOutSpeed{ 15.0f }; // rapid blend-out when entering ADS
	WalkDirectionOffset walkForward;
	WalkDirectionOffset walkBackward;
	WalkDirectionOffset walkLeft;
	WalkDirectionOffset walkRight;

	// === SIMULTANEOUS BLEND SCALING ===
	float simultaneousThreshold{ 0.5f };
	float simultaneousCameraMult{ 1.0f };
	float simultaneousMovementMult{ 1.0f };

	// === SPRINT INERTIA ===
	bool  sprintInertiaEnabled{ true };
	bool  sprintStartEnabled{ true };
	bool  sprintStopEnabled{ true };
	float sprintImpulseX{ 0.0f };
	float sprintImpulseY{ 8.0f };
	float sprintImpulseZ{ 3.0f };
	float sprintRotImpulse{ 5.0f };
	float sprintStopImpulseX{ 0.0f };
	float sprintStopImpulseY{ -4.0f };
	float sprintStopImpulseZ{ -1.5f };
	float sprintStopRotImpulse{ -2.5f };
	float sprintImpulseBlendTime{ 0.1f };
	float sprintStiffness{ 60.0f };
	float sprintDamping{ 5.0f };

	// === JUMP/LAND INERTIA ===
	bool  jumpInertiaEnabled{ true };
	float cameraInertiaAirMult{ 0.3f };
	float jumpImpulseX{ 0.0f };
	float jumpImpulseY{ 4.0f };
	float jumpImpulseZ{ 6.0f };
	float jumpRotPitch{ 3.0f };
	float jumpRotYaw{ 0.0f };
	float jumpRotRoll{ 0.0f };
	float fallImpulseX{ 0.0f };
	float fallImpulseY{ 4.0f };
	float fallImpulseZ{ 6.0f };
	float fallRotPitch{ 3.0f };
	float fallRotYaw{ 0.0f };
	float fallRotRoll{ 0.0f };
	float jumpStiffness{ 40.0f };
	float jumpDamping{ 3.0f };
	float fallStiffness{ 40.0f };
	float fallDamping{ 3.0f };
	float landImpulseX{ 0.0f };
	float landImpulseY{ 3.0f };
	float landImpulseZ{ 10.0f };
	float landRotPitch{ 5.0f };
	float landRotYaw{ 0.0f };
	float landRotRoll{ 0.0f };
	float landStiffness{ 120.0f };
	float landDamping{ 10.0f };
	float airTimeImpulseScale{ 1.5f };

	// === EQUIP IMPULSE (on weapon draw) ===
	bool  equipImpulseEnabled{ true };
	float equipImpulseX{ 0.0f };
	float equipImpulseY{ 5.0f };
	float equipImpulseZ{ 3.0f };
	float equipRotImpulse{ 4.0f };
	float equipBlendTime{ 0.15f };
	float equipStiffness{ 80.0f };
	float equipDamping{ 6.0f };

	// === ADS ENTER IMPULSE ===
	bool  adsEnterImpulseEnabled{ true };
	float adsEnterImpulseX{ 0.0f };
	float adsEnterImpulseY{ 3.0f };
	float adsEnterImpulseZ{ -2.0f };
	float adsEnterRotImpulse{ 2.0f };
	float adsEnterStiffness{ 100.0f };
	float adsEnterDamping{ 8.0f };

	// === ADS EXIT IMPULSE ===
	bool  adsExitImpulseEnabled{ true };
	float adsExitImpulseX{ 0.0f };
	float adsExitImpulseY{ -3.0f };
	float adsExitImpulseZ{ 2.0f };
	float adsExitRotImpulse{ 2.0f };
	float adsExitStiffness{ 80.0f };
	float adsExitDamping{ 6.0f };

	// === ADS INERTIA MULTIPLIERS ===
	bool  adsInertiaEnabled{ true };
	float adsInertiaMult{ 0.5f };
	bool  adsScopeInertiaEnabled{ true };
	float adsScopeInertiaMult{ 0.3f };

	// === FIRE RECOVERY IMPULSE — Hip-fire (post-shot / post-burst) ===
	bool  fireRecoveryImpulseEnabled{ true };
	float fireRecoveryImpulseX{ 0.0f };
	float fireRecoveryImpulseY{ 2.0f };
	float fireRecoveryImpulseZ{ 1.5f };
	float fireRecoveryRotImpulse{ 1.5f };
	float fireRecoveryStiffness{ 120.0f };
	float fireRecoveryDamping{ 9.0f };
	float fireRecoveryCooldown{ 0.08f };

	// === FIRE RECOVERY IMPULSE — ADS (separate values when firing while ADS) ===
	bool  adsFireRecoveryImpulseEnabled{ true };
	float adsFireRecoveryImpulseX{ 0.0f };
	float adsFireRecoveryImpulseY{ 1.0f };
	float adsFireRecoveryImpulseZ{ 0.8f };
	float adsFireRecoveryRotImpulse{ 0.8f };
	float adsFireRecoveryStiffness{ 150.0f };
	float adsFireRecoveryDamping{ 10.0f };
	float adsFireRecoveryCooldown{ 0.08f };

	// === TACTICAL RELOAD IMPULSE (rounds still in magazine) ===
	// reloadTriggerEvent: 0=ReloadEnd, 1=InitiateStart, 2=ReloadComplete
	bool  reloadImpulseEnabled{ true };
	int   reloadTriggerEvent{ 0 };
	float reloadImpulseDelay{ 0.0f };
	float reloadImpulseBlendTime{ 0.1f };
	float reloadImpulseX{ 0.0f };
	float reloadImpulseY{ 3.0f };
	float reloadImpulseZ{ 2.0f };
	float reloadRotImpulse{ 1.0f };
	float reloadStiffness{ 80.0f };
	float reloadDamping{ 6.0f };

	// === EMPTY RELOAD IMPULSE (no rounds in magazine) ===
	bool  emptyReloadImpulseEnabled{ true };
	int   emptyReloadTriggerEvent{ 0 };
	float emptyReloadImpulseDelay{ 0.0f };
	float emptyReloadImpulseBlendTime{ 0.15f };
	float emptyReloadImpulseX{ 0.0f };
	float emptyReloadImpulseY{ 5.0f };
	float emptyReloadImpulseZ{ 3.0f };
	float emptyReloadRotImpulse{ 1.5f };
	float emptyReloadStiffness{ 70.0f };
	float emptyReloadDamping{ 5.0f };

	float sustainedFireBuildRate{ 2.0f };
	float sustainedFireMax{ 3.0f };
	float sustainedFireDecay{ 4.0f };

	// === WEIGHT-BASED MOD SCALING ===
	bool  weightScalingEnabled{ true };
	float weightScaleInfluence{ 0.5f };
	float weightScaleMin{ 0.5f };
	float weightScaleMax{ 2.0f };

	// === EARLY ADS RETURN ===
	// Blend back into ADS before reload animation fully finishes.
	// earlyAdsReturnTrigger: 0=ReloadComplete (default), 1=ReloadEnd, 2=InitiateStart
	// earlyAdsReturnBlendType: 0=Linear, 1=EaseIn, 2=EaseOut, 3=EaseInOut
	bool  earlyAdsReturnEnabled{ true };
	int   earlyAdsReturnTrigger{ 0 };
	float earlyAdsReturnDelay{ 0.0f };
	float earlyAdsReturnBlendTime{ 0.25f };
	int   earlyAdsReturnBlendType{ 0 };
	float earlyAdsReturnImpulseScale{ 0.25f };
	float earlyAdsFireBlockDelay{ 0.5f };  // extra delay (seconds) after Early ADS before allowing fire
	bool  earlyAdsAutoFireEnabled{ true }; // auto-fire recovery: send attackStop to unstick auto weapons after Early ADS
	float earlyAdsAutoFireWindow{ 0.5f }; // seconds to monitor for stuck auto-fire after force-idle completes
	int   earlyAdsAutoFireMaxAttempts{ 3 }; // max attackStop resets within the window

	// === EARLY FIRE CANCEL ===
	// Mirrors the Early ADS Return flow but triggered by the FIRE input held
	// during a reload (no ADS involved).
	// Trigger event / delay are shared with Early ADS Return.
	bool  earlyFireCancelEnabled{ true };

	// === EARLY EQUIP ===
	// Allow ADS or hipfire before the equip animation (WPNEquip / WPNEquipFast)
	// fully completes.  Triggered by InitiateStart anim event during equip.
	// Delay and blend settings are shared with Early ADS Return.
	bool  earlyEquipAdsEnabled{ false };
	bool  earlyEquipFireEnabled{ false };

	// === PIVOT POINT ===
	// 0=Chest/Spine, 1=RightHand, 2=Weapon node
	int pivotPoint{ 0 };
	int adsPivotPoint{ 2 };  // Weapon node by default when ADS for tighter feel
	bool useBindPosePivot{ false };  // Use skeleton rest-pose pivot instead of animated position

	// === ADS SPRING DAMPENING ON TRANSITION ===
	// When entering or exiting ADS, multiply all continuous spring velocities by this
	// factor to reduce residual motion noise during the transition.
	bool  adsTransitionDampenEnabled{ true };
	float adsTransitionDampenFactor{ 0.25f };  // 0=kill all, 1=keep all

	// === PROCEDURAL ADS TRANSITION MOTION ===
	ADSTransitionSettings adsEnterTransition;  // motion added during hip->ADS
	ADSTransitionSettings adsExitTransition;   // motion added during ADS->hip

	// === LEAN INERTIA (UneducatedShooter integration) ===
	// Detected via CameraInserted1st bone rotation — gracefully no-ops if
	// UneducatedShooter is not installed (bone simply won't be found).
	// Axis convention: X=vertical, Y=forward, Z=lateral (bone-local).
	bool  leanOffsetEnabled{ true };
	bool  leanOffsetDisableInADS{ false };
	float leanOffsetX{ 0.0f };          // vertical additive offset at full lean
	float leanOffsetY{ 0.0f };          // forward additive offset at full lean
	float leanOffsetZ{ 0.5f };          // lateral additive offset at full lean (signed with lean direction)
	float leanOffsetBlendSpeed{ 6.0f }; // units/sec, how fast the offset tracks the lean weight
	bool  leanImpulseEnabled{ true };
	bool  leanImpulseDisableInADS{ false };
	float leanImpulseX{ 0.0f };         // vertical impulse on lean start/stop
	float leanImpulseY{ 0.0f };         // forward impulse
	float leanImpulseZ{ 1.0f };         // lateral impulse (signed with lean direction)
	float leanRotImpulsePitch{ 0.0f };  // rotation on lean start/stop
	float leanRotImpulseYaw{ 0.0f };
	float leanRotImpulseRoll{ 0.5f };   // roll toward the lean side
	float leanStiffness{ 80.0f };
	float leanDamping{ 6.0f };

	// === SNEAK IMPULSE ===
	// Optional spring impulse when the player enters or exits sneak mode.
	// Detected via moveMode & 0x0020 (same bitfield approach as sprint 0x100).
	bool  sneakImpulseEnabled{ false };
	float sneakEnterImpulseX{ 0.0f };   // vertical impulse when crouching
	float sneakEnterImpulseY{ 0.0f };   // forward impulse
	float sneakEnterImpulseZ{ 0.0f };   // lateral impulse
	float sneakEnterRotImpulse{ 1.0f }; // rotation (forward pitch down) when crouching
	float sneakExitImpulseX{ 0.0f };
	float sneakExitImpulseY{ 0.0f };
	float sneakExitImpulseZ{ 0.0f };
	float sneakExitRotImpulse{ -1.0f }; // rotation (pitches back up) when uncrouching
	float sneakStiffness{ 80.0f };
	float sneakDamping{ 6.0f };

	void Load(CSimpleIniA& a_ini, const char* a_section);
	void Save(CSimpleIniA& a_ini, const char* a_section) const;
};

class Settings
{
public:
	static Settings* GetSingleton()
	{
		static Settings singleton;
		return &singleton;
	}

	void Load();
	void Save();

	// Convert WeaponType to MCM INI section name
	static const char* GetWeaponTypeSectionName(WeaponType a_type);

	// General settings
	bool  enabled{ true };           // Master switch — disables ALL features
	bool  inertiaEnabled{ true };    // Inertia-only switch — extras (WBFOV, ChamberExclusion) still run
	bool  enablePosition{ true };
	bool  enableRotation{ true };
	bool  requireWeaponDrawn{ true };
	float globalIntensity{ 1.0f };
	float smoothingFactor{ 0.5f };

	// Weapon Based FOV (extras feature)
	bool  wbfovEnabled{ true };
	int   wbfovLoadRetries{ 3 };  // max load-retry attempts (1-5)

	// Fire on Empty (extras feature) — master toggle. Even when enabled,
	// the feature only acts on weapons that have an opt-in JSON entry
	// (see FireOnEmpty::Manager). Defaults on because per-weapon entries
	// already gate the behavior.
	bool  fireOnEmptyEnabled{ true };

	// Prevent walking/running animations while airborne
	bool  disableAirWalk{ false };

	// Settling behavior
	float settleDelay{ 0.3f };
	float settleSpeed{ 2.0f };
	float settleDampingMult{ 3.0f };

	// Movement inertia global settings
	bool  movementInertiaEnabled{ true };
	float movementInertiaStrength{ 3.0f };
	float movementInertiaThreshold{ 30.0f };
	bool  forwardBackInertia{ false };
	bool  disableVanillaSway{ false };

	// Action blending — per-action enable + min intensity
	bool  blendDuringFiring{ false };
	float blendFiringMinIntensity{ 0.2f };
	bool  blendDuringReload{ true };
	float blendReloadMinIntensity{ 0.2f };
	bool  blendDuringMelee{ true };
	float blendMeleeMinIntensity{ 0.2f };
	float actionBlendSpeed{ 5.0f };
	float actionBlendBackLeadTime{ 0.35f };

	// Power Armor
	bool  enableInPowerArmor{ true };
	bool  usePASeparateProfiles{ true };  // Use PA_ weapon type variants
	float powerArmorMult{ 1.2f };         // Fallback multiplier if no PA profile

	// === SUPER SPRINT ===
	// Double-tap sprint to engage a faster sprint mode with increased AP cost,
	// boosted animation speed, and a runtime keyword for OAR-driven animations.
	bool  superSprintEnabled{ true };
	float superSprintDoubleTapWindow{ 0.3f };  // seconds between sprint-stop and sprint-start to detect double-tap
	float superSprintSpeedMult{ 1.25f };       // multiplicative movement speed boost (1.25 = 25% faster)
	float superSprintAPCostMult{ 1.4f };       // multiplicative AP drain increase (1.4 = 40% more drain, 0-based mods stay 0)
	float superSprintAnimSpeedMult{ 1.25f };   // behavior graph Speed variable multiplier (animation playback rate)
	bool  superSprintStaminaThresholdEnabled{ false }; // disengage super sprint when AP% drops below threshold
	float superSprintStaminaThreshold{ 20.0f };        // AP percentage (0-100) below which super sprint is cancelled

	// === REPEATABLE GUN BASH ===
	// Gun bashes can combo: a Melee press during an active bash queues a
	// follow-up, which fires once the bash's HitFrame anim event + delay
	// have elapsed (before the previous animation fully ends).
	bool  bashComboEnabled{ true };
	float bashComboDelay{ 0.2f };     // seconds after HitFrame before a follow-up may start
	int   bashComboMaxQueue{ 2 };     // max queued follow-up bashes (1-2)
	bool  bashComboStaminaThresholdEnabled{ true };  // block combos below the AP threshold
	float bashComboStaminaThreshold{ 25.0f };        // AP percentage (0-100)
	// Visual blend across the follow-up's graph reset: a measured, decaying
	// viewmodel offset masks the pose snap (GunMover-style; no OAR needed).
	bool  bashComboBlendEnabled{ true };
	float bashComboBlendTime{ 0.18f };               // seconds (0.05-0.50)

	// Debug settings
	bool debugLogging{ false };
	bool debugOnScreen{ false };

	// === AUTO-FIRE SOUND FADE-OUT ===
	// When the phantom-fire override exits (player releases trigger, safety
	// timeout, or "no more anim events" gap), we explicitly fade out the
	// equipped weapon's loop sound handles via BSSoundHandle::FadeOutAndRelease.
	// 0 = instant cut, ~100ms = perceptually clean spin-down, larger values
	// give a softer tail.  Default 100 was chosen by ear: short enough that
	// the gun feels responsive when you let go of the trigger, but long
	// enough that the auto-fire loop doesn't get audibly chopped.
	bool autoFireSoundFadeEnabled{ true };
	int autoFireSoundFadeMs{ 100 };

	// Per-weapon-type settings
	WeaponInertiaSettings unarmed;
	WeaponInertiaSettings melee;
	WeaponInertiaSettings pistol;
	WeaponInertiaSettings rifle;
	WeaponInertiaSettings heavy;
	WeaponInertiaSettings energy;
	WeaponInertiaSettings throwable;
	// Power Armor variants
	WeaponInertiaSettings pa_unarmed;
	WeaponInertiaSettings pa_melee;
	WeaponInertiaSettings pa_pistol;
	WeaponInertiaSettings pa_rifle;
	WeaponInertiaSettings pa_heavy;
	WeaponInertiaSettings pa_energy;

	const WeaponInertiaSettings& GetWeaponSettings(WeaponType a_type) const;
	WeaponInertiaSettings& GetWeaponSettingsMutable(WeaponType a_type);

	// INI path — direct plugin path, no MCM
	static constexpr const char* kSettingsPath = "Data\\F4SE\\Plugins\\FPGunplayOverhaul.ini";

private:
	Settings() = default;
	Settings(const Settings&) = delete;
	Settings(Settings&&) = delete;
	~Settings() = default;
	Settings& operator=(const Settings&) = delete;
	Settings& operator=(Settings&&) = delete;

	// Change tracking
	bool prevEnabled{ true };
	float prevIntensity{ 1.0f };
	bool prevDebugLogging{ false };
};
