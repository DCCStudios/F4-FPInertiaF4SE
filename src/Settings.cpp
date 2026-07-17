#include "Settings.h"
#include "InertiaPresets.h"

// ============================================================
// WeaponInertiaSettings::Load
// ============================================================
void WeaponInertiaSettings::Load(CSimpleIniA& a_ini, const char* a_section)
{
	auto getB = [&](const char* key, bool def) { return a_ini.GetBoolValue(a_section, key, def); };
	auto getF = [&](const char* key, float def) { return static_cast<float>(a_ini.GetDoubleValue(a_section, key, def)); };
	auto getI = [&](const char* key, int def) { return static_cast<int>(a_ini.GetLongValue(a_section, key, def)); };

	// Master toggle
	enabled = getB("bEnabled", enabled);

	// Camera inertia
	stiffness         = getF("fStiffness", stiffness);
	damping           = getF("fDamping", damping);
	maxOffset         = getF("fMaxOffset", maxOffset);
	maxRotation       = getF("fMaxRotation", maxRotation);
	mass              = getF("fMass", mass);
	pitchMultiplier   = getF("fPitchMultiplier", pitchMultiplier);
	rollMultiplier    = getF("fRollMultiplier", rollMultiplier);
	cameraPitchMult   = getF("fCameraPitchMult", cameraPitchMult);
	momentumDecay     = getF("fMomentumDecay", momentumDecay);
	invertCameraPitch = getB("bInvertCameraPitch", invertCameraPitch);
	invertCameraYaw   = getB("bInvertCameraYaw", invertCameraYaw);

	// Movement inertia
	movementInertiaEnabled   = getB("bMovementInertiaEnabled", movementInertiaEnabled);
	movementStiffness        = getF("fMovementStiffness", movementStiffness);
	movementDamping          = getF("fMovementDamping", movementDamping);
	movementMaxOffset        = getF("fMovementMaxOffset", movementMaxOffset);
	movementMaxRotation      = getF("fMovementMaxRotation", movementMaxRotation);
	movementLeftMult         = getF("fMovementLeftMult", movementLeftMult);
	movementRightMult        = getF("fMovementRightMult", movementRightMult);
	movementForwardMult      = getF("fMovementForwardMult", movementForwardMult);
	movementBackwardMult     = getF("fMovementBackwardMult", movementBackwardMult);
	invertMovementLateral    = getB("bInvertMovementLateral", invertMovementLateral);
	invertMovementForwardBack = getB("bInvertMovementForwardBack", invertMovementForwardBack);

	// Walk direction offsets
	walkOffsetsEnabled       = getB("bWalkOffsetsEnabled", walkOffsetsEnabled);
	walkOffsetBlendInSpeed   = getF("fWalkOffsetBlendInSpeed", walkOffsetBlendInSpeed);
	walkOffsetBlendOutSpeed  = getF("fWalkOffsetBlendOutSpeed", walkOffsetBlendOutSpeed);
	walkOffsetAdsBlendOutSpeed = getF("fWalkOffsetAdsBlendOutSpeed", walkOffsetAdsBlendOutSpeed);

	auto loadDir = [&](const char* prefix, WalkDirectionOffset& d) {
		d.posX     = getF((std::string(prefix) + "PosX").c_str(), d.posX);
		d.posY     = getF((std::string(prefix) + "PosY").c_str(), d.posY);
		d.posZ     = getF((std::string(prefix) + "PosZ").c_str(), d.posZ);
		d.rotPitch = getF((std::string(prefix) + "RotPitch").c_str(), d.rotPitch);
		d.rotYaw   = getF((std::string(prefix) + "RotYaw").c_str(), d.rotYaw);
		d.rotRoll  = getF((std::string(prefix) + "RotRoll").c_str(), d.rotRoll);
	};
	loadDir("fWalkFwd",   walkForward);
	loadDir("fWalkBack",  walkBackward);
	loadDir("fWalkLeft",  walkLeft);
	loadDir("fWalkRight", walkRight);

	// Simultaneous
	simultaneousThreshold   = getF("fSimultaneousThreshold", simultaneousThreshold);
	simultaneousCameraMult  = getF("fSimultaneousCameraMult", simultaneousCameraMult);
	simultaneousMovementMult = getF("fSimultaneousMovementMult", simultaneousMovementMult);

	// Sprint
	sprintInertiaEnabled   = getB("bSprintInertiaEnabled", sprintInertiaEnabled);
	sprintStartEnabled     = getB("bSprintStartEnabled", sprintStartEnabled);
	sprintStopEnabled      = getB("bSprintStopEnabled", sprintStopEnabled);
	sprintImpulseX         = getF("fSprintImpulseX", sprintImpulseX);
	sprintImpulseY         = getF("fSprintImpulseY", sprintImpulseY);
	sprintImpulseZ         = getF("fSprintImpulseZ", sprintImpulseZ);
	sprintRotImpulse       = getF("fSprintRotImpulse", sprintRotImpulse);
	sprintStopImpulseX     = getF("fSprintStopImpulseX", sprintStopImpulseX);
	sprintStopImpulseY     = getF("fSprintStopImpulseY", sprintStopImpulseY);
	sprintStopImpulseZ     = getF("fSprintStopImpulseZ", sprintStopImpulseZ);
	sprintStopRotImpulse   = getF("fSprintStopRotImpulse", sprintStopRotImpulse);
	sprintImpulseBlendTime = getF("fSprintImpulseBlendTime", sprintImpulseBlendTime);
	sprintStiffness        = getF("fSprintStiffness", sprintStiffness);
	sprintDamping          = getF("fSprintDamping", sprintDamping);

	// Jump/land
	jumpInertiaEnabled   = getB("bJumpInertiaEnabled", jumpInertiaEnabled);
	cameraInertiaAirMult = getF("fCameraInertiaAirMult", cameraInertiaAirMult);
	jumpImpulseX         = getF("fJumpImpulseX", jumpImpulseX);
	jumpImpulseY         = getF("fJumpImpulseY", jumpImpulseY);
	jumpImpulseZ         = getF("fJumpImpulseZ", jumpImpulseZ);
	// Backward compat: migrate old single-scalar fJumpRotImpulse to pitch
	{ float legacy = getF("fJumpRotImpulse", jumpRotPitch); jumpRotPitch = legacy; }
	jumpRotPitch         = getF("fJumpRotPitch", jumpRotPitch);
	jumpRotYaw           = getF("fJumpRotYaw",   jumpRotYaw);
	jumpRotRoll          = getF("fJumpRotRoll",  jumpRotRoll);
	fallImpulseX         = getF("fFallImpulseX", fallImpulseX);
	fallImpulseY         = getF("fFallImpulseY", fallImpulseY);
	fallImpulseZ         = getF("fFallImpulseZ", fallImpulseZ);
	{ float legacy = getF("fFallRotImpulse", fallRotPitch); fallRotPitch = legacy; }
	fallRotPitch         = getF("fFallRotPitch", fallRotPitch);
	fallRotYaw           = getF("fFallRotYaw",   fallRotYaw);
	fallRotRoll          = getF("fFallRotRoll",  fallRotRoll);
	jumpStiffness        = getF("fJumpStiffness", jumpStiffness);
	jumpDamping          = getF("fJumpDamping", jumpDamping);
	fallStiffness        = getF("fFallStiffness", fallStiffness);
	fallDamping          = getF("fFallDamping", fallDamping);
	landImpulseX         = getF("fLandImpulseX", landImpulseX);
	landImpulseY         = getF("fLandImpulseY", landImpulseY);
	landImpulseZ         = getF("fLandImpulseZ", landImpulseZ);
	{ float legacy = getF("fLandRotImpulse", landRotPitch); landRotPitch = legacy; }
	landRotPitch         = getF("fLandRotPitch", landRotPitch);
	landRotYaw           = getF("fLandRotYaw",   landRotYaw);
	landRotRoll          = getF("fLandRotRoll",  landRotRoll);
	landStiffness        = getF("fLandStiffness", landStiffness);
	landDamping          = getF("fLandDamping", landDamping);
	airTimeImpulseScale  = getF("fAirTimeImpulseScale", airTimeImpulseScale);

	// Equip impulse
	equipImpulseEnabled = getB("bEquipImpulseEnabled", equipImpulseEnabled);
	equipImpulseX       = getF("fEquipImpulseX", equipImpulseX);
	equipImpulseY       = getF("fEquipImpulseY", equipImpulseY);
	equipImpulseZ       = getF("fEquipImpulseZ", equipImpulseZ);
	equipRotImpulse     = getF("fEquipRotImpulse", equipRotImpulse);
	equipBlendTime      = getF("fEquipBlendTime", equipBlendTime);
	equipStiffness      = getF("fEquipStiffness", equipStiffness);
	equipDamping        = getF("fEquipDamping", equipDamping);

	// ADS enter impulse
	adsEnterImpulseEnabled = getB("bAdsEnterImpulseEnabled", adsEnterImpulseEnabled);
	adsEnterImpulseX       = getF("fAdsEnterImpulseX", adsEnterImpulseX);
	adsEnterImpulseY       = getF("fAdsEnterImpulseY", adsEnterImpulseY);
	adsEnterImpulseZ       = getF("fAdsEnterImpulseZ", adsEnterImpulseZ);
	adsEnterRotImpulse     = getF("fAdsEnterRotImpulse", adsEnterRotImpulse);
	adsEnterStiffness      = getF("fAdsEnterStiffness", adsEnterStiffness);
	adsEnterDamping        = getF("fAdsEnterDamping", adsEnterDamping);

	// ADS exit impulse
	adsExitImpulseEnabled = getB("bAdsExitImpulseEnabled", adsExitImpulseEnabled);
	adsExitImpulseX       = getF("fAdsExitImpulseX", adsExitImpulseX);
	adsExitImpulseY       = getF("fAdsExitImpulseY", adsExitImpulseY);
	adsExitImpulseZ       = getF("fAdsExitImpulseZ", adsExitImpulseZ);
	adsExitRotImpulse     = getF("fAdsExitRotImpulse", adsExitRotImpulse);
	adsExitStiffness      = getF("fAdsExitStiffness", adsExitStiffness);
	adsExitDamping        = getF("fAdsExitDamping", adsExitDamping);

	// ADS inertia multipliers
	adsInertiaEnabled     = getB("bAdsInertiaEnabled", adsInertiaEnabled);
	adsInertiaMult        = getF("fAdsInertiaMult", adsInertiaMult);
	adsScopeInertiaEnabled = getB("bAdsScopeInertiaEnabled", adsScopeInertiaEnabled);
	adsScopeInertiaMult   = getF("fAdsScopeInertiaMult", adsScopeInertiaMult);

	// Fire recovery impulse (hip)
	fireRecoveryImpulseEnabled = getB("bFireRecoveryImpulseEnabled", fireRecoveryImpulseEnabled);
	fireRecoveryImpulseX       = getF("fFireRecoveryImpulseX", fireRecoveryImpulseX);
	fireRecoveryImpulseY       = getF("fFireRecoveryImpulseY", fireRecoveryImpulseY);
	fireRecoveryImpulseZ       = getF("fFireRecoveryImpulseZ", fireRecoveryImpulseZ);
	fireRecoveryRotImpulse     = getF("fFireRecoveryRotImpulse", fireRecoveryRotImpulse);
	fireRecoveryStiffness      = getF("fFireRecoveryStiffness", fireRecoveryStiffness);
	fireRecoveryDamping        = getF("fFireRecoveryDamping", fireRecoveryDamping);
	fireRecoveryCooldown       = getF("fFireRecoveryCooldown", fireRecoveryCooldown);
	// Fire recovery impulse (ADS)
	adsFireRecoveryImpulseEnabled = getB("bAdsFireRecoveryImpulseEnabled", adsFireRecoveryImpulseEnabled);
	adsFireRecoveryImpulseX       = getF("fAdsFireRecoveryImpulseX", adsFireRecoveryImpulseX);
	adsFireRecoveryImpulseY       = getF("fAdsFireRecoveryImpulseY", adsFireRecoveryImpulseY);
	adsFireRecoveryImpulseZ       = getF("fAdsFireRecoveryImpulseZ", adsFireRecoveryImpulseZ);
	adsFireRecoveryRotImpulse     = getF("fAdsFireRecoveryRotImpulse", adsFireRecoveryRotImpulse);
	adsFireRecoveryStiffness      = getF("fAdsFireRecoveryStiffness", adsFireRecoveryStiffness);
	adsFireRecoveryDamping        = getF("fAdsFireRecoveryDamping", adsFireRecoveryDamping);
	adsFireRecoveryCooldown       = getF("fAdsFireRecoveryCooldown", adsFireRecoveryCooldown);
	// Reload impulse
	reloadImpulseEnabled   = getB("bReloadImpulseEnabled", reloadImpulseEnabled);
	reloadTriggerEvent     = getI("iReloadTriggerEvent", reloadTriggerEvent);
	reloadImpulseDelay     = getF("fReloadImpulseDelay", reloadImpulseDelay);
	reloadImpulseBlendTime = getF("fReloadImpulseBlendTime", reloadImpulseBlendTime);
	reloadImpulseX         = getF("fReloadImpulseX", reloadImpulseX);
	reloadImpulseY         = getF("fReloadImpulseY", reloadImpulseY);
	reloadImpulseZ         = getF("fReloadImpulseZ", reloadImpulseZ);
	reloadRotImpulse       = getF("fReloadRotImpulse", reloadRotImpulse);
	reloadStiffness        = getF("fReloadStiffness", reloadStiffness);
	reloadDamping          = getF("fReloadDamping", reloadDamping);
	// Empty reload impulse
	emptyReloadImpulseEnabled   = getB("bEmptyReloadImpulseEnabled", emptyReloadImpulseEnabled);
	emptyReloadTriggerEvent     = getI("iEmptyReloadTriggerEvent", emptyReloadTriggerEvent);
	emptyReloadImpulseDelay     = getF("fEmptyReloadImpulseDelay", emptyReloadImpulseDelay);
	emptyReloadImpulseBlendTime = getF("fEmptyReloadImpulseBlendTime", emptyReloadImpulseBlendTime);
	emptyReloadImpulseX         = getF("fEmptyReloadImpulseX", emptyReloadImpulseX);
	emptyReloadImpulseY         = getF("fEmptyReloadImpulseY", emptyReloadImpulseY);
	emptyReloadImpulseZ         = getF("fEmptyReloadImpulseZ", emptyReloadImpulseZ);
	emptyReloadRotImpulse       = getF("fEmptyReloadRotImpulse", emptyReloadRotImpulse);
	emptyReloadStiffness        = getF("fEmptyReloadStiffness", emptyReloadStiffness);
	emptyReloadDamping          = getF("fEmptyReloadDamping", emptyReloadDamping);
	sustainedFireBuildRate     = getF("fSustainedFireBuildRate", sustainedFireBuildRate);
	sustainedFireMax           = getF("fSustainedFireMax", sustainedFireMax);
	sustainedFireDecay         = getF("fSustainedFireDecay", sustainedFireDecay);

	// Early ADS return
	earlyAdsReturnEnabled   = getB("bEarlyAdsReturnEnabled", earlyAdsReturnEnabled);
	earlyAdsReturnTrigger   = getI("iEarlyAdsReturnTrigger", earlyAdsReturnTrigger);
	earlyAdsReturnDelay     = getF("fEarlyAdsReturnDelay", earlyAdsReturnDelay);
	earlyAdsReturnBlendTime = getF("fEarlyAdsReturnBlendTime", earlyAdsReturnBlendTime);
	earlyAdsReturnBlendType        = getI("iEarlyAdsReturnBlendType", earlyAdsReturnBlendType);
	earlyAdsReturnImpulseScale     = getF("fEarlyAdsReturnImpulseScale", earlyAdsReturnImpulseScale);
	earlyAdsFireBlockDelay         = getF("fEarlyAdsFireBlockDelay", earlyAdsFireBlockDelay);
	earlyAdsAutoFireEnabled        = getB("bEarlyAdsAutoFireEnabled", earlyAdsAutoFireEnabled);
	earlyAdsAutoFireWindow         = getF("fEarlyAdsAutoFireWindow", earlyAdsAutoFireWindow);
	earlyAdsAutoFireMaxAttempts    = getI("iEarlyAdsAutoFireMaxAttempts", earlyAdsAutoFireMaxAttempts);
	earlyFireCancelEnabled         = getB("bEarlyFireCancelEnabled", earlyFireCancelEnabled);
	earlyEquipAdsEnabled           = getB("bEarlyEquipAdsEnabled", earlyEquipAdsEnabled);
	earlyEquipFireEnabled          = getB("bEarlyEquipFireEnabled", earlyEquipFireEnabled);

	// Weight scaling
	weightScalingEnabled = getB("bWeightScalingEnabled", weightScalingEnabled);
	weightScaleInfluence = getF("fWeightScaleInfluence", weightScaleInfluence);
	weightScaleMin       = getF("fWeightScaleMin", weightScaleMin);
	weightScaleMax       = getF("fWeightScaleMax", weightScaleMax);

	// Pivot
	pivotPoint = getI("iPivotPoint", pivotPoint);
	adsPivotPoint = getI("iAdsPivotPoint", adsPivotPoint);
	useBindPosePivot = getB("bUseBindPosePivot", useBindPosePivot);

	// ADS spring dampening
	adsTransitionDampenEnabled = getB("bAdsTransitionDampenEnabled", adsTransitionDampenEnabled);
	adsTransitionDampenFactor  = getF("fAdsTransitionDampenFactor",  adsTransitionDampenFactor);

	// ADS enter transition
	adsEnterTransition.enabled          = getB("bAdsEnterTransitionEnabled",     adsEnterTransition.enabled);
	adsEnterTransition.curveType        = getI("iAdsEnterTransitionCurveType",   adsEnterTransition.curveType);
	adsEnterTransition.peakOffsetX      = getF("fAdsEnterTransitionPeakX",       adsEnterTransition.peakOffsetX);
	adsEnterTransition.peakOffsetY      = getF("fAdsEnterTransitionPeakY",       adsEnterTransition.peakOffsetY);
	adsEnterTransition.peakOffsetZ      = getF("fAdsEnterTransitionPeakZ",       adsEnterTransition.peakOffsetZ);
	adsEnterTransition.peakRotPitch     = getF("fAdsEnterTransitionPeakRotPitch",adsEnterTransition.peakRotPitch);
	adsEnterTransition.peakRotYaw       = getF("fAdsEnterTransitionPeakRotYaw",  adsEnterTransition.peakRotYaw);
	adsEnterTransition.peakRotRoll      = getF("fAdsEnterTransitionPeakRotRoll", adsEnterTransition.peakRotRoll);
	adsEnterTransition.peakPosition     = getF("fAdsEnterTransitionPeakPos",     adsEnterTransition.peakPosition);
	adsEnterTransition.asymmetry        = getF("fAdsEnterTransitionAsymmetry",   adsEnterTransition.asymmetry);
	adsEnterTransition.impulseBlendFactor = getF("fAdsEnterTransitionImpulseBlend", adsEnterTransition.impulseBlendFactor);

	// ADS exit transition
	adsExitTransition.enabled           = getB("bAdsExitTransitionEnabled",      adsExitTransition.enabled);
	adsExitTransition.curveType         = getI("iAdsExitTransitionCurveType",    adsExitTransition.curveType);
	adsExitTransition.peakOffsetX       = getF("fAdsExitTransitionPeakX",        adsExitTransition.peakOffsetX);
	adsExitTransition.peakOffsetY       = getF("fAdsExitTransitionPeakY",        adsExitTransition.peakOffsetY);
	adsExitTransition.peakOffsetZ       = getF("fAdsExitTransitionPeakZ",        adsExitTransition.peakOffsetZ);
	adsExitTransition.peakRotPitch      = getF("fAdsExitTransitionPeakRotPitch", adsExitTransition.peakRotPitch);
	adsExitTransition.peakRotYaw        = getF("fAdsExitTransitionPeakRotYaw",   adsExitTransition.peakRotYaw);
	adsExitTransition.peakRotRoll       = getF("fAdsExitTransitionPeakRotRoll",  adsExitTransition.peakRotRoll);
	adsExitTransition.peakPosition      = getF("fAdsExitTransitionPeakPos",      adsExitTransition.peakPosition);
	adsExitTransition.asymmetry         = getF("fAdsExitTransitionAsymmetry",    adsExitTransition.asymmetry);
	adsExitTransition.impulseBlendFactor = getF("fAdsExitTransitionImpulseBlend", adsExitTransition.impulseBlendFactor);

	// Lean inertia
	leanOffsetEnabled       = getB("bLeanOffsetEnabled",       leanOffsetEnabled);
	leanOffsetDisableInADS  = getB("bLeanOffsetDisableInADS",  leanOffsetDisableInADS);
	leanOffsetX             = getF("fLeanOffsetX",             leanOffsetX);
	leanOffsetY             = getF("fLeanOffsetY",             leanOffsetY);
	leanOffsetZ             = getF("fLeanOffsetZ",             leanOffsetZ);
	leanOffsetBlendSpeed    = getF("fLeanOffsetBlendSpeed",    leanOffsetBlendSpeed);
	leanImpulseEnabled      = getB("bLeanImpulseEnabled",      leanImpulseEnabled);
	leanImpulseDisableInADS = getB("bLeanImpulseDisableInADS", leanImpulseDisableInADS);
	leanImpulseX         = getF("fLeanImpulseX",         leanImpulseX);
	leanImpulseY         = getF("fLeanImpulseY",         leanImpulseY);
	leanImpulseZ         = getF("fLeanImpulseZ",         leanImpulseZ);
	leanRotImpulsePitch  = getF("fLeanRotImpulsePitch",  leanRotImpulsePitch);
	leanRotImpulseYaw    = getF("fLeanRotImpulseYaw",    leanRotImpulseYaw);
	leanRotImpulseRoll   = getF("fLeanRotImpulseRoll",   leanRotImpulseRoll);
	leanStiffness        = getF("fLeanStiffness",        leanStiffness);
	leanDamping          = getF("fLeanDamping",          leanDamping);

	// Sneak impulse
	sneakImpulseEnabled   = getB("bSneakImpulseEnabled",   sneakImpulseEnabled);
	sneakEnterImpulseX    = getF("fSneakEnterImpulseX",    sneakEnterImpulseX);
	sneakEnterImpulseY    = getF("fSneakEnterImpulseY",    sneakEnterImpulseY);
	sneakEnterImpulseZ    = getF("fSneakEnterImpulseZ",    sneakEnterImpulseZ);
	sneakEnterRotImpulse  = getF("fSneakEnterRotImpulse",  sneakEnterRotImpulse);
	sneakExitImpulseX     = getF("fSneakExitImpulseX",     sneakExitImpulseX);
	sneakExitImpulseY     = getF("fSneakExitImpulseY",     sneakExitImpulseY);
	sneakExitImpulseZ     = getF("fSneakExitImpulseZ",     sneakExitImpulseZ);
	sneakExitRotImpulse   = getF("fSneakExitRotImpulse",   sneakExitRotImpulse);
	sneakStiffness        = getF("fSneakStiffness",        sneakStiffness);
	sneakDamping          = getF("fSneakDamping",          sneakDamping);

	// Clamp all values to sensible ranges
	stiffness            = std::clamp(stiffness, 10.0f, 1000.0f);
	damping              = std::clamp(damping, 1.0f, 100.0f);
	maxOffset            = std::clamp(maxOffset, 0.0f, 50.0f);
	maxRotation          = std::clamp(maxRotation, 0.0f, 90.0f);
	mass                 = std::clamp(mass, 0.1f, 10.0f);
	pitchMultiplier      = std::clamp(pitchMultiplier, 0.0f, 5.0f);
	rollMultiplier       = std::clamp(rollMultiplier, 0.0f, 5.0f);
	cameraPitchMult      = std::clamp(cameraPitchMult, 0.0f, 5.0f);
	momentumDecay        = std::clamp(momentumDecay, 0.5f, 50.0f);
	movementStiffness    = std::clamp(movementStiffness, 10.0f, 500.0f);
	movementDamping      = std::clamp(movementDamping, 1.0f, 50.0f);
	movementMaxOffset    = std::clamp(movementMaxOffset, 0.0f, 50.0f);
	movementMaxRotation  = std::clamp(movementMaxRotation, 0.0f, 90.0f);
	walkOffsetBlendInSpeed     = std::clamp(walkOffsetBlendInSpeed, 0.5f, 30.0f);
	walkOffsetBlendOutSpeed    = std::clamp(walkOffsetBlendOutSpeed, 0.5f, 30.0f);
	walkOffsetAdsBlendOutSpeed = std::clamp(walkOffsetAdsBlendOutSpeed, 1.0f, 50.0f);
	sprintImpulseX       = std::clamp(sprintImpulseX, -50.0f, 50.0f);
	sprintImpulseY       = std::clamp(sprintImpulseY, -50.0f, 50.0f);
	sprintImpulseZ       = std::clamp(sprintImpulseZ, -50.0f, 50.0f);
	sprintStopImpulseX   = std::clamp(sprintStopImpulseX, -50.0f, 50.0f);
	sprintStopImpulseY   = std::clamp(sprintStopImpulseY, -50.0f, 50.0f);
	sprintStopImpulseZ   = std::clamp(sprintStopImpulseZ, -50.0f, 50.0f);
	cameraInertiaAirMult = std::clamp(cameraInertiaAirMult, 0.0f, 1.0f);
	adsInertiaMult       = std::clamp(adsInertiaMult, 0.0f, 2.0f);
	adsScopeInertiaMult  = std::clamp(adsScopeInertiaMult, 0.0f, 2.0f);
	weightScaleInfluence = std::clamp(weightScaleInfluence, 0.0f, 1.0f);
	weightScaleMin       = std::clamp(weightScaleMin, 0.1f, 1.0f);
	weightScaleMax       = std::clamp(weightScaleMax, 1.0f, 5.0f);
	pivotPoint           = std::clamp(pivotPoint, 0, 2);
	adsPivotPoint        = std::clamp(adsPivotPoint, 0, 2);
	fireRecoveryCooldown          = std::clamp(fireRecoveryCooldown, 0.01f, 1.0f);
	adsFireRecoveryCooldown       = std::clamp(adsFireRecoveryCooldown, 0.01f, 1.0f);
	reloadTriggerEvent            = std::clamp(reloadTriggerEvent, 0, 2);
	reloadImpulseDelay            = std::clamp(reloadImpulseDelay, 0.0f, 2.0f);
	reloadImpulseBlendTime        = std::clamp(reloadImpulseBlendTime, 0.0f, 1.0f);
	reloadStiffness               = std::clamp(reloadStiffness, 10.0f, 500.0f);
	reloadDamping                 = std::clamp(reloadDamping, 1.0f, 50.0f);
	emptyReloadTriggerEvent       = std::clamp(emptyReloadTriggerEvent, 0, 2);
	emptyReloadImpulseDelay       = std::clamp(emptyReloadImpulseDelay, 0.0f, 2.0f);
	emptyReloadImpulseBlendTime   = std::clamp(emptyReloadImpulseBlendTime, 0.0f, 1.0f);
	emptyReloadStiffness          = std::clamp(emptyReloadStiffness, 10.0f, 500.0f);
	emptyReloadDamping            = std::clamp(emptyReloadDamping, 1.0f, 50.0f);
	earlyAdsReturnTrigger         = std::clamp(earlyAdsReturnTrigger, 0, 2);
	earlyAdsReturnDelay           = std::clamp(earlyAdsReturnDelay, 0.0f, 3.0f);
	earlyAdsReturnBlendTime       = std::clamp(earlyAdsReturnBlendTime, 0.0f, 2.0f);
	earlyAdsReturnBlendType       = std::clamp(earlyAdsReturnBlendType, 0, 3);
	earlyAdsReturnImpulseScale    = std::clamp(earlyAdsReturnImpulseScale, 0.0f, 2.0f);
	earlyAdsFireBlockDelay        = std::clamp(earlyAdsFireBlockDelay, 0.0f, 1.0f);
	earlyAdsAutoFireWindow        = std::clamp(earlyAdsAutoFireWindow, 0.1f, 2.0f);
	earlyAdsAutoFireMaxAttempts   = std::clamp(earlyAdsAutoFireMaxAttempts, 1, 10);
	sustainedFireBuildRate     = std::clamp(sustainedFireBuildRate, 0.0f, 20.0f);
	sustainedFireMax           = std::clamp(sustainedFireMax, 1.0f, 10.0f);
	sustainedFireDecay         = std::clamp(sustainedFireDecay, 0.0f, 20.0f);

	adsTransitionDampenFactor  = std::clamp(adsTransitionDampenFactor, 0.0f, 1.0f);
	adsEnterTransition.curveType     = std::clamp(adsEnterTransition.curveType, 0, 3);
	adsEnterTransition.peakPosition  = std::clamp(adsEnterTransition.peakPosition, 0.01f, 0.99f);
	adsEnterTransition.asymmetry     = std::clamp(adsEnterTransition.asymmetry, -1.0f, 1.0f);
	adsEnterTransition.impulseBlendFactor = std::clamp(adsEnterTransition.impulseBlendFactor, 0.0f, 1.0f);
	adsExitTransition.curveType      = std::clamp(adsExitTransition.curveType, 0, 3);
	adsExitTransition.peakPosition   = std::clamp(adsExitTransition.peakPosition, 0.01f, 0.99f);
	adsExitTransition.asymmetry      = std::clamp(adsExitTransition.asymmetry, -1.0f, 1.0f);
	adsExitTransition.impulseBlendFactor = std::clamp(adsExitTransition.impulseBlendFactor, 0.0f, 1.0f);
}

// ============================================================
// WeaponInertiaSettings::Save
// ============================================================
void WeaponInertiaSettings::Save(CSimpleIniA& a_ini, const char* a_section) const
{
	auto setB = [&](const char* key, bool v) { a_ini.SetLongValue(a_section, key, v ? 1 : 0); };
	auto setF = [&](const char* key, float v) { a_ini.SetDoubleValue(a_section, key, static_cast<double>(v)); };
	auto setI = [&](const char* key, int v) { a_ini.SetLongValue(a_section, key, static_cast<long>(v)); };

	setB("bEnabled", enabled);

	// Camera
	setF("fStiffness", stiffness);           setF("fDamping", damping);
	setF("fMaxOffset", maxOffset);           setF("fMaxRotation", maxRotation);
	setF("fMass", mass);                     setF("fPitchMultiplier", pitchMultiplier);
	setF("fRollMultiplier", rollMultiplier); setF("fCameraPitchMult", cameraPitchMult);
	setF("fMomentumDecay", momentumDecay);
	setB("bInvertCameraPitch", invertCameraPitch); setB("bInvertCameraYaw", invertCameraYaw);

	// Movement
	setB("bMovementInertiaEnabled", movementInertiaEnabled);
	setF("fMovementStiffness", movementStiffness); setF("fMovementDamping", movementDamping);
	setF("fMovementMaxOffset", movementMaxOffset); setF("fMovementMaxRotation", movementMaxRotation);
	setF("fMovementLeftMult", movementLeftMult);   setF("fMovementRightMult", movementRightMult);
	setF("fMovementForwardMult", movementForwardMult); setF("fMovementBackwardMult", movementBackwardMult);
	setB("bInvertMovementLateral", invertMovementLateral);
	setB("bInvertMovementForwardBack", invertMovementForwardBack);

	// Walk direction offsets
	setB("bWalkOffsetsEnabled", walkOffsetsEnabled);
	setF("fWalkOffsetBlendInSpeed", walkOffsetBlendInSpeed);
	setF("fWalkOffsetBlendOutSpeed", walkOffsetBlendOutSpeed);
	setF("fWalkOffsetAdsBlendOutSpeed", walkOffsetAdsBlendOutSpeed);

	auto saveDir = [&](const char* prefix, const WalkDirectionOffset& d) {
		setF((std::string(prefix) + "PosX").c_str(), d.posX);
		setF((std::string(prefix) + "PosY").c_str(), d.posY);
		setF((std::string(prefix) + "PosZ").c_str(), d.posZ);
		setF((std::string(prefix) + "RotPitch").c_str(), d.rotPitch);
		setF((std::string(prefix) + "RotYaw").c_str(), d.rotYaw);
		setF((std::string(prefix) + "RotRoll").c_str(), d.rotRoll);
	};
	saveDir("fWalkFwd",   walkForward);
	saveDir("fWalkBack",  walkBackward);
	saveDir("fWalkLeft",  walkLeft);
	saveDir("fWalkRight", walkRight);

	// Simultaneous
	setF("fSimultaneousThreshold", simultaneousThreshold);
	setF("fSimultaneousCameraMult", simultaneousCameraMult);
	setF("fSimultaneousMovementMult", simultaneousMovementMult);

	// Sprint
	setB("bSprintInertiaEnabled", sprintInertiaEnabled);
	setB("bSprintStartEnabled", sprintStartEnabled);
	setB("bSprintStopEnabled", sprintStopEnabled);
	setF("fSprintImpulseX", sprintImpulseX);
	setF("fSprintImpulseY", sprintImpulseY); setF("fSprintImpulseZ", sprintImpulseZ);
	setF("fSprintRotImpulse", sprintRotImpulse);
	setF("fSprintStopImpulseX", sprintStopImpulseX);
	setF("fSprintStopImpulseY", sprintStopImpulseY); setF("fSprintStopImpulseZ", sprintStopImpulseZ);
	setF("fSprintStopRotImpulse", sprintStopRotImpulse);
	setF("fSprintImpulseBlendTime", sprintImpulseBlendTime);
	setF("fSprintStiffness", sprintStiffness); setF("fSprintDamping", sprintDamping);

	// Jump/land
	setB("bJumpInertiaEnabled", jumpInertiaEnabled);
	setF("fCameraInertiaAirMult", cameraInertiaAirMult);
	setF("fJumpImpulseX", jumpImpulseX);
	setF("fJumpImpulseY", jumpImpulseY); setF("fJumpImpulseZ", jumpImpulseZ);
	setF("fJumpRotPitch", jumpRotPitch); setF("fJumpRotYaw", jumpRotYaw); setF("fJumpRotRoll", jumpRotRoll);
	setF("fFallImpulseX", fallImpulseX);
	setF("fFallImpulseY", fallImpulseY); setF("fFallImpulseZ", fallImpulseZ);
	setF("fFallRotPitch", fallRotPitch); setF("fFallRotYaw", fallRotYaw); setF("fFallRotRoll", fallRotRoll);
	setF("fJumpStiffness", jumpStiffness); setF("fJumpDamping", jumpDamping);
	setF("fFallStiffness", fallStiffness); setF("fFallDamping", fallDamping);
	setF("fLandImpulseX", landImpulseX);
	setF("fLandImpulseY", landImpulseY);  setF("fLandImpulseZ", landImpulseZ);
	setF("fLandRotPitch", landRotPitch); setF("fLandRotYaw", landRotYaw); setF("fLandRotRoll", landRotRoll);
	setF("fLandStiffness", landStiffness); setF("fLandDamping", landDamping);
	setF("fAirTimeImpulseScale", airTimeImpulseScale);

	// Equip impulse
	setB("bEquipImpulseEnabled", equipImpulseEnabled);
	setF("fEquipImpulseX", equipImpulseX);
	setF("fEquipImpulseY", equipImpulseY); setF("fEquipImpulseZ", equipImpulseZ);
	setF("fEquipRotImpulse", equipRotImpulse); setF("fEquipBlendTime", equipBlendTime);
	setF("fEquipStiffness", equipStiffness);   setF("fEquipDamping", equipDamping);

	// ADS enter
	setB("bAdsEnterImpulseEnabled", adsEnterImpulseEnabled);
	setF("fAdsEnterImpulseX", adsEnterImpulseX);
	setF("fAdsEnterImpulseY", adsEnterImpulseY); setF("fAdsEnterImpulseZ", adsEnterImpulseZ);
	setF("fAdsEnterRotImpulse", adsEnterRotImpulse);
	setF("fAdsEnterStiffness", adsEnterStiffness); setF("fAdsEnterDamping", adsEnterDamping);

	// ADS exit
	setB("bAdsExitImpulseEnabled", adsExitImpulseEnabled);
	setF("fAdsExitImpulseX", adsExitImpulseX);
	setF("fAdsExitImpulseY", adsExitImpulseY); setF("fAdsExitImpulseZ", adsExitImpulseZ);
	setF("fAdsExitRotImpulse", adsExitRotImpulse);
	setF("fAdsExitStiffness", adsExitStiffness); setF("fAdsExitDamping", adsExitDamping);

	// ADS multipliers
	setB("bAdsInertiaEnabled", adsInertiaEnabled);
	setF("fAdsInertiaMult", adsInertiaMult);
	setB("bAdsScopeInertiaEnabled", adsScopeInertiaEnabled);
	setF("fAdsScopeInertiaMult", adsScopeInertiaMult);

	// Fire recovery (hip)
	setB("bFireRecoveryImpulseEnabled", fireRecoveryImpulseEnabled);
	setF("fFireRecoveryImpulseX", fireRecoveryImpulseX);
	setF("fFireRecoveryImpulseY", fireRecoveryImpulseY);
	setF("fFireRecoveryImpulseZ", fireRecoveryImpulseZ);
	setF("fFireRecoveryRotImpulse", fireRecoveryRotImpulse);
	setF("fFireRecoveryStiffness", fireRecoveryStiffness);
	setF("fFireRecoveryDamping", fireRecoveryDamping);
	setF("fFireRecoveryCooldown", fireRecoveryCooldown);
	// Fire recovery (ADS)
	setB("bAdsFireRecoveryImpulseEnabled", adsFireRecoveryImpulseEnabled);
	setF("fAdsFireRecoveryImpulseX", adsFireRecoveryImpulseX);
	setF("fAdsFireRecoveryImpulseY", adsFireRecoveryImpulseY);
	setF("fAdsFireRecoveryImpulseZ", adsFireRecoveryImpulseZ);
	setF("fAdsFireRecoveryRotImpulse", adsFireRecoveryRotImpulse);
	setF("fAdsFireRecoveryStiffness", adsFireRecoveryStiffness);
	setF("fAdsFireRecoveryDamping", adsFireRecoveryDamping);
	setF("fAdsFireRecoveryCooldown", adsFireRecoveryCooldown);
	// Reload impulse
	setB("bReloadImpulseEnabled", reloadImpulseEnabled);
	setI("iReloadTriggerEvent", reloadTriggerEvent);
	setF("fReloadImpulseDelay", reloadImpulseDelay);
	setF("fReloadImpulseBlendTime", reloadImpulseBlendTime);
	setF("fReloadImpulseX", reloadImpulseX);
	setF("fReloadImpulseY", reloadImpulseY);
	setF("fReloadImpulseZ", reloadImpulseZ);
	setF("fReloadRotImpulse", reloadRotImpulse);
	setF("fReloadStiffness", reloadStiffness);
	setF("fReloadDamping", reloadDamping);
	// Empty reload impulse
	setB("bEmptyReloadImpulseEnabled", emptyReloadImpulseEnabled);
	setI("iEmptyReloadTriggerEvent", emptyReloadTriggerEvent);
	setF("fEmptyReloadImpulseDelay", emptyReloadImpulseDelay);
	setF("fEmptyReloadImpulseBlendTime", emptyReloadImpulseBlendTime);
	setF("fEmptyReloadImpulseX", emptyReloadImpulseX);
	setF("fEmptyReloadImpulseY", emptyReloadImpulseY);
	setF("fEmptyReloadImpulseZ", emptyReloadImpulseZ);
	setF("fEmptyReloadRotImpulse", emptyReloadRotImpulse);
	setF("fEmptyReloadStiffness", emptyReloadStiffness);
	setF("fEmptyReloadDamping", emptyReloadDamping);
	setF("fSustainedFireBuildRate", sustainedFireBuildRate);
	setF("fSustainedFireMax", sustainedFireMax);
	setF("fSustainedFireDecay", sustainedFireDecay);

	// Early ADS return
	setB("bEarlyAdsReturnEnabled", earlyAdsReturnEnabled);
	setI("iEarlyAdsReturnTrigger", earlyAdsReturnTrigger);
	setF("fEarlyAdsReturnDelay", earlyAdsReturnDelay);
	setF("fEarlyAdsReturnBlendTime", earlyAdsReturnBlendTime);
	setI("iEarlyAdsReturnBlendType", earlyAdsReturnBlendType);
	setF("fEarlyAdsReturnImpulseScale", earlyAdsReturnImpulseScale);
	setF("fEarlyAdsFireBlockDelay", earlyAdsFireBlockDelay);
	setB("bEarlyAdsAutoFireEnabled", earlyAdsAutoFireEnabled);
	setF("fEarlyAdsAutoFireWindow", earlyAdsAutoFireWindow);
	setI("iEarlyAdsAutoFireMaxAttempts", earlyAdsAutoFireMaxAttempts);
	setB("bEarlyFireCancelEnabled", earlyFireCancelEnabled);
	setB("bEarlyEquipAdsEnabled", earlyEquipAdsEnabled);
	setB("bEarlyEquipFireEnabled", earlyEquipFireEnabled);

	// Weight scaling
	setB("bWeightScalingEnabled", weightScalingEnabled);
	setF("fWeightScaleInfluence", weightScaleInfluence);
	setF("fWeightScaleMin", weightScaleMin);
	setF("fWeightScaleMax", weightScaleMax);

	// Pivot
	setI("iPivotPoint", pivotPoint);
	setI("iAdsPivotPoint", adsPivotPoint);
	setB("bUseBindPosePivot", useBindPosePivot);

	// ADS spring dampening
	setB("bAdsTransitionDampenEnabled", adsTransitionDampenEnabled);
	setF("fAdsTransitionDampenFactor",  adsTransitionDampenFactor);

	// ADS enter transition
	setB("bAdsEnterTransitionEnabled",      adsEnterTransition.enabled);
	setI("iAdsEnterTransitionCurveType",    adsEnterTransition.curveType);
	setF("fAdsEnterTransitionPeakX",        adsEnterTransition.peakOffsetX);
	setF("fAdsEnterTransitionPeakY",        adsEnterTransition.peakOffsetY);
	setF("fAdsEnterTransitionPeakZ",        adsEnterTransition.peakOffsetZ);
	setF("fAdsEnterTransitionPeakRotPitch", adsEnterTransition.peakRotPitch);
	setF("fAdsEnterTransitionPeakRotYaw",   adsEnterTransition.peakRotYaw);
	setF("fAdsEnterTransitionPeakRotRoll",  adsEnterTransition.peakRotRoll);
	setF("fAdsEnterTransitionPeakPos",      adsEnterTransition.peakPosition);
	setF("fAdsEnterTransitionAsymmetry",    adsEnterTransition.asymmetry);
	setF("fAdsEnterTransitionImpulseBlend", adsEnterTransition.impulseBlendFactor);

	// ADS exit transition
	setB("bAdsExitTransitionEnabled",       adsExitTransition.enabled);
	setI("iAdsExitTransitionCurveType",     adsExitTransition.curveType);
	setF("fAdsExitTransitionPeakX",         adsExitTransition.peakOffsetX);
	setF("fAdsExitTransitionPeakY",         adsExitTransition.peakOffsetY);
	setF("fAdsExitTransitionPeakZ",         adsExitTransition.peakOffsetZ);
	setF("fAdsExitTransitionPeakRotPitch",  adsExitTransition.peakRotPitch);
	setF("fAdsExitTransitionPeakRotYaw",    adsExitTransition.peakRotYaw);
	setF("fAdsExitTransitionPeakRotRoll",   adsExitTransition.peakRotRoll);
	setF("fAdsExitTransitionPeakPos",       adsExitTransition.peakPosition);
	setF("fAdsExitTransitionAsymmetry",     adsExitTransition.asymmetry);
	setF("fAdsExitTransitionImpulseBlend",  adsExitTransition.impulseBlendFactor);

	// Lean inertia
	setB("bLeanOffsetEnabled",       leanOffsetEnabled);
	setB("bLeanOffsetDisableInADS",  leanOffsetDisableInADS);
	setF("fLeanOffsetX",             leanOffsetX);
	setF("fLeanOffsetY",             leanOffsetY);
	setF("fLeanOffsetZ",             leanOffsetZ);
	setF("fLeanOffsetBlendSpeed",    leanOffsetBlendSpeed);
	setB("bLeanImpulseEnabled",      leanImpulseEnabled);
	setB("bLeanImpulseDisableInADS", leanImpulseDisableInADS);
	setF("fLeanImpulseX",         leanImpulseX);
	setF("fLeanImpulseY",         leanImpulseY);
	setF("fLeanImpulseZ",         leanImpulseZ);
	setF("fLeanRotImpulsePitch",  leanRotImpulsePitch);
	setF("fLeanRotImpulseYaw",    leanRotImpulseYaw);
	setF("fLeanRotImpulseRoll",   leanRotImpulseRoll);
	setF("fLeanStiffness",        leanStiffness);
	setF("fLeanDamping",          leanDamping);

	// Sneak impulse
	setB("bSneakImpulseEnabled",   sneakImpulseEnabled);
	setF("fSneakEnterImpulseX",    sneakEnterImpulseX);
	setF("fSneakEnterImpulseY",    sneakEnterImpulseY);
	setF("fSneakEnterImpulseZ",    sneakEnterImpulseZ);
	setF("fSneakEnterRotImpulse",  sneakEnterRotImpulse);
	setF("fSneakExitImpulseX",     sneakExitImpulseX);
	setF("fSneakExitImpulseY",     sneakExitImpulseY);
	setF("fSneakExitImpulseZ",     sneakExitImpulseZ);
	setF("fSneakExitRotImpulse",   sneakExitRotImpulse);
	setF("fSneakStiffness",        sneakStiffness);
	setF("fSneakDamping",          sneakDamping);
}

// ============================================================
// Default presets per weapon type
// ============================================================
static void ApplyWeaponDefaults(WeaponInertiaSettings& ws,
	float stiff, float damp, float maxOff, float maxRot, float mass,
	float pitchMult, float rollMult, float camPitchMult,
	float mvStiff, float mvDamp, float mvMaxOff, float mvMaxRot,
	float mvLeft, float mvRight, int pivot,
	bool sprintOn = true, bool equipOn = true, bool adsOn = true, bool fireOn = true)
{
	ws.stiffness = stiff; ws.damping = damp; ws.maxOffset = maxOff;
	ws.maxRotation = maxRot; ws.mass = mass;
	ws.pitchMultiplier = pitchMult; ws.rollMultiplier = rollMult;
	ws.cameraPitchMult = camPitchMult;
	ws.invertCameraPitch = false; ws.invertCameraYaw = false;

	ws.movementInertiaEnabled = true;
	ws.movementStiffness = mvStiff; ws.movementDamping = mvDamp;
	ws.movementMaxOffset = mvMaxOff; ws.movementMaxRotation = mvMaxRot;
	ws.movementLeftMult = mvLeft; ws.movementRightMult = mvRight;
	ws.movementForwardMult = 0.5f; ws.movementBackwardMult = 0.5f;
	ws.invertMovementLateral = false; ws.invertMovementForwardBack = false;
	ws.simultaneousThreshold = 0.5f;
	ws.simultaneousCameraMult = 1.0f; ws.simultaneousMovementMult = 1.0f;

	ws.sprintInertiaEnabled = sprintOn;
	ws.sprintStartEnabled = true; ws.sprintStopEnabled = true;
	ws.sprintImpulseX = 0.0f; ws.sprintImpulseY = 8.0f; ws.sprintImpulseZ = 3.0f; ws.sprintRotImpulse = 5.0f;
	ws.sprintStopImpulseX = 0.0f; ws.sprintStopImpulseY = -4.0f; ws.sprintStopImpulseZ = -1.5f; ws.sprintStopRotImpulse = -2.5f;
	ws.sprintImpulseBlendTime = 0.1f; ws.sprintStiffness = 60.0f; ws.sprintDamping = 5.0f;

	ws.jumpInertiaEnabled = true;
	ws.cameraInertiaAirMult = 0.3f;
	ws.jumpImpulseY = 4.0f; ws.jumpImpulseZ = 6.0f;
	ws.jumpRotPitch = 3.0f; ws.jumpRotYaw = 0.0f; ws.jumpRotRoll = 0.0f;
	ws.fallImpulseY = 1.5f; ws.fallImpulseZ = 2.0f;
	ws.fallRotPitch = 1.0f; ws.fallRotYaw = 0.0f; ws.fallRotRoll = 0.0f;
	ws.jumpStiffness = 40.0f; ws.jumpDamping = 3.0f;
	ws.fallStiffness = 40.0f; ws.fallDamping = 3.0f;
	ws.landImpulseY = 3.0f; ws.landImpulseZ = 10.0f;
	ws.landRotPitch = 5.0f; ws.landRotYaw = 0.0f; ws.landRotRoll = 0.0f;
	ws.landStiffness = 120.0f; ws.landDamping = 10.0f; ws.airTimeImpulseScale = 1.5f;

	ws.equipImpulseEnabled = equipOn;
	ws.equipImpulseY = 5.0f; ws.equipImpulseZ = 3.0f; ws.equipRotImpulse = 4.0f;
	ws.equipBlendTime = 0.15f; ws.equipStiffness = 80.0f; ws.equipDamping = 6.0f;

	ws.adsEnterImpulseEnabled = adsOn;
	ws.adsEnterImpulseY = 3.0f; ws.adsEnterImpulseZ = -2.0f; ws.adsEnterRotImpulse = 2.0f;
	ws.adsEnterStiffness = 100.0f; ws.adsEnterDamping = 8.0f;

	ws.adsExitImpulseEnabled = adsOn;
	ws.adsExitImpulseY = -3.0f; ws.adsExitImpulseZ = 2.0f; ws.adsExitRotImpulse = 2.0f;
	ws.adsExitStiffness = 80.0f; ws.adsExitDamping = 6.0f;

	ws.adsInertiaEnabled = true;
	ws.adsInertiaMult = 0.5f;
	ws.adsScopeInertiaEnabled = true;
	ws.adsScopeInertiaMult = 0.3f;

	ws.fireRecoveryImpulseEnabled = fireOn;
	ws.fireRecoveryImpulseX = 0.0f; ws.fireRecoveryImpulseY = 2.0f; ws.fireRecoveryImpulseZ = 1.5f;
	ws.fireRecoveryRotImpulse = 1.5f;
	ws.fireRecoveryStiffness = 120.0f; ws.fireRecoveryDamping = 9.0f;
	ws.fireRecoveryCooldown = 0.08f;
	ws.adsFireRecoveryImpulseEnabled = true;
	ws.adsFireRecoveryImpulseX = 0.0f; ws.adsFireRecoveryImpulseY = 1.0f; ws.adsFireRecoveryImpulseZ = 0.8f;
	ws.adsFireRecoveryRotImpulse = 0.8f;
	ws.adsFireRecoveryStiffness = 150.0f; ws.adsFireRecoveryDamping = 10.0f;
	ws.adsFireRecoveryCooldown = 0.08f;

	ws.earlyAdsReturnEnabled   = true;
	ws.earlyAdsReturnTrigger   = 0;
	ws.earlyAdsReturnDelay     = 0.0f;
	ws.earlyAdsReturnBlendTime = 0.25f;
	ws.earlyAdsReturnBlendType        = 0;
	ws.earlyAdsReturnImpulseScale     = 0.25f;
	ws.earlyAdsFireBlockDelay         = 0.15f;
	ws.earlyAdsAutoFireEnabled        = true;
	ws.earlyAdsAutoFireWindow         = 0.5f;
	ws.earlyAdsAutoFireMaxAttempts    = 3;
	ws.earlyFireCancelEnabled         = true;
	ws.earlyEquipAdsEnabled           = false;
	ws.earlyEquipFireEnabled          = false;

	ws.reloadImpulseEnabled   = true;
	ws.reloadTriggerEvent     = 0;
	ws.reloadImpulseDelay     = 0.0f;
	ws.reloadImpulseBlendTime = 0.1f;
	ws.reloadImpulseX = 0.0f; ws.reloadImpulseY = 3.0f; ws.reloadImpulseZ = 2.0f;
	ws.reloadRotImpulse = 1.0f;
	ws.reloadStiffness = 80.0f; ws.reloadDamping = 6.0f;

	ws.emptyReloadImpulseEnabled = true;
	ws.emptyReloadTriggerEvent   = 0;
	ws.emptyReloadImpulseDelay   = 0.0f;
	ws.emptyReloadImpulseBlendTime = 0.15f;
	ws.emptyReloadImpulseX = 0.0f; ws.emptyReloadImpulseY = 5.0f; ws.emptyReloadImpulseZ = 3.0f;
	ws.emptyReloadRotImpulse = 1.5f;
	ws.emptyReloadStiffness = 70.0f; ws.emptyReloadDamping = 5.0f;

	ws.weightScalingEnabled = true;
	ws.weightScaleInfluence = 0.5f; ws.weightScaleMin = 0.5f; ws.weightScaleMax = 2.0f;

	ws.pivotPoint = pivot;
	ws.adsPivotPoint = 2;

	// Lean inertia defaults
	ws.leanOffsetEnabled       = true;
	ws.leanOffsetDisableInADS  = false;
	ws.leanOffsetX = 0.0f; ws.leanOffsetY = 0.0f; ws.leanOffsetZ = 0.5f;
	ws.leanOffsetBlendSpeed = 6.0f;
	ws.leanImpulseEnabled      = true;
	ws.leanImpulseDisableInADS = false;
	ws.leanImpulseX = 0.0f; ws.leanImpulseY = 0.0f; ws.leanImpulseZ = 1.0f;
	ws.leanRotImpulsePitch = 0.0f; ws.leanRotImpulseYaw = 0.0f; ws.leanRotImpulseRoll = 0.5f;
	ws.leanStiffness = 80.0f; ws.leanDamping = 6.0f;

	// Sneak impulse defaults (disabled — opt-in)
	ws.sneakImpulseEnabled = false;
	ws.sneakEnterImpulseX = 0.0f; ws.sneakEnterImpulseY = 0.0f; ws.sneakEnterImpulseZ = 0.0f;
	ws.sneakEnterRotImpulse = 1.0f;
	ws.sneakExitImpulseX = 0.0f;  ws.sneakExitImpulseY = 0.0f;  ws.sneakExitImpulseZ = 0.0f;
	ws.sneakExitRotImpulse = -1.0f;
	ws.sneakStiffness = 80.0f; ws.sneakDamping = 6.0f;
}

// ============================================================
// Settings::Load
// ============================================================
void Settings::Load()
{
	CSimpleIniA ini;
	ini.SetUnicode();

	bool loaded = (ini.LoadFile(kSettingsPath) >= 0);
	if (!loaded) {
		logger::warn("[FPGunplayOverhaul] No INI found at '{}' - using built-in defaults", kSettingsPath);
	}

	// General
	enabled             = ini.GetBoolValue("General", "bEnabled", true);
	inertiaEnabled      = ini.GetBoolValue("General", "bInertiaEnabled", true);
	enablePosition      = ini.GetBoolValue("General", "bEnablePosition", true);
	enableRotation      = ini.GetBoolValue("General", "bEnableRotation", true);
	requireWeaponDrawn  = ini.GetBoolValue("General", "bRequireWeaponDrawn", true);
	globalIntensity     = static_cast<float>(ini.GetDoubleValue("General", "fGlobalIntensity", 1.0));
	smoothingFactor     = static_cast<float>(ini.GetDoubleValue("General", "fSmoothingFactor", 0.5));

	// Weapon Based FOV
	wbfovEnabled        = ini.GetBoolValue("WBFOV", "bEnabled", true);
	wbfovLoadRetries    = static_cast<int>(ini.GetLongValue("WBFOV", "iLoadRetries", 3));

	// Fire on Empty
	fireOnEmptyEnabled  = ini.GetBoolValue("Extras", "bFireOnEmptyEnabled", true);

	// Air walk prevention
	disableAirWalk      = ini.GetBoolValue("Extras", "bDisableAirWalk", false);

	// Settling
	settleDelay       = static_cast<float>(ini.GetDoubleValue("Settling", "fSettleDelay", 0.3));
	settleSpeed       = static_cast<float>(ini.GetDoubleValue("Settling", "fSettleSpeed", 2.0));
	settleDampingMult = static_cast<float>(ini.GetDoubleValue("Settling", "fSettleDampingMult", 3.0));

	settleDelay       = std::clamp(settleDelay, 0.0f, 2.0f);
	settleSpeed       = std::clamp(settleSpeed, 0.5f, 10.0f);
	settleDampingMult = std::clamp(settleDampingMult, 1.0f, 10.0f);

	// Movement
	movementInertiaEnabled   = ini.GetBoolValue("MovementInertia", "bEnabled", true);
	movementInertiaStrength  = static_cast<float>(ini.GetDoubleValue("MovementInertia", "fStrength", 3.0));
	movementInertiaThreshold = static_cast<float>(ini.GetDoubleValue("MovementInertia", "fThreshold", 30.0));
	forwardBackInertia       = ini.GetBoolValue("MovementInertia", "bForwardBackInertia", false);
	disableVanillaSway       = ini.GetBoolValue("MovementInertia", "bDisableVanillaSway", false);
	movementInertiaStrength  = std::clamp(movementInertiaStrength, 0.0f, 20.0f);
	movementInertiaThreshold = std::clamp(movementInertiaThreshold, 0.0f, 200.0f);

	// Action blend
	blendDuringFiring        = ini.GetBoolValue("ActionBlend", "bBlendDuringFiring", false);
	blendFiringMinIntensity  = static_cast<float>(ini.GetDoubleValue("ActionBlend", "fBlendFiringMinIntensity", 0.2));
	blendDuringReload        = ini.GetBoolValue("ActionBlend", "bBlendDuringReload", true);
	blendReloadMinIntensity  = static_cast<float>(ini.GetDoubleValue("ActionBlend", "fBlendReloadMinIntensity", 0.2));
	blendDuringMelee         = ini.GetBoolValue("ActionBlend", "bBlendDuringMelee", true);
	blendMeleeMinIntensity   = static_cast<float>(ini.GetDoubleValue("ActionBlend", "fBlendMeleeMinIntensity", 0.2));
	actionBlendSpeed         = static_cast<float>(ini.GetDoubleValue("ActionBlend", "fActionBlendSpeed", 5.0));
	actionBlendBackLeadTime  = static_cast<float>(ini.GetDoubleValue("ActionBlend", "fActionBlendBackLeadTime", 0.35));
	blendFiringMinIntensity  = std::clamp(blendFiringMinIntensity, 0.0f, 1.0f);
	blendReloadMinIntensity  = std::clamp(blendReloadMinIntensity, 0.0f, 1.0f);
	blendMeleeMinIntensity   = std::clamp(blendMeleeMinIntensity, 0.0f, 1.0f);
	actionBlendSpeed         = std::clamp(actionBlendSpeed, 1.0f, 20.0f);
	actionBlendBackLeadTime  = std::clamp(actionBlendBackLeadTime, 0.0f, 2.0f);
	// Backward compat: migrate old global actionMinIntensity if per-action keys absent
	if (!ini.GetValue("ActionBlend", "fBlendFiringMinIntensity") &&
	    !ini.GetValue("ActionBlend", "fBlendReloadMinIntensity") &&
	    !ini.GetValue("ActionBlend", "fBlendMeleeMinIntensity")) {
		float oldMin = static_cast<float>(ini.GetDoubleValue("ActionBlend", "fActionMinIntensity", 0.2));
		oldMin = std::clamp(oldMin, 0.0f, 1.0f);
		blendFiringMinIntensity = oldMin;
		blendReloadMinIntensity = oldMin;
		blendMeleeMinIntensity  = oldMin;
	}

	// Power Armor
	enableInPowerArmor    = ini.GetBoolValue("PowerArmor", "bEnabled", true);
	usePASeparateProfiles = ini.GetBoolValue("PowerArmor", "bSeparateProfiles", true);
	powerArmorMult        = static_cast<float>(ini.GetDoubleValue("PowerArmor", "fMult", 1.2));
	powerArmorMult        = std::clamp(powerArmorMult, 0.0f, 5.0f);

	// Super Sprint
	superSprintEnabled         = ini.GetBoolValue("Extras", "bSuperSprintEnabled", true);
	superSprintDoubleTapWindow = static_cast<float>(ini.GetDoubleValue("Extras", "fSuperSprintDoubleTapWindow", 0.3));
	superSprintSpeedMult       = static_cast<float>(ini.GetDoubleValue("Extras", "fSuperSprintSpeedMult", 1.25));
	superSprintAPCostMult      = static_cast<float>(ini.GetDoubleValue("Extras", "fSuperSprintAPCostMult", 1.4));
	superSprintAnimSpeedMult   = static_cast<float>(ini.GetDoubleValue("Extras", "fSuperSprintAnimSpeedMult", 1.25));
	superSprintStaminaThresholdEnabled = ini.GetBoolValue("Extras", "bSuperSprintStaminaThresholdEnabled", false);
	superSprintStaminaThreshold = static_cast<float>(ini.GetDoubleValue("Extras", "fSuperSprintStaminaThreshold", 20.0));
	superSprintDoubleTapWindow = std::clamp(superSprintDoubleTapWindow, 0.1f, 1.0f);
	superSprintSpeedMult       = std::clamp(superSprintSpeedMult, 1.0f, 3.0f);
	superSprintAPCostMult      = std::clamp(superSprintAPCostMult, 1.0f, 5.0f);
	superSprintAnimSpeedMult   = std::clamp(superSprintAnimSpeedMult, 1.0f, 3.0f);
	superSprintStaminaThreshold = std::clamp(superSprintStaminaThreshold, 0.0f, 100.0f);

	// Debug
	debugLogging       = ini.GetBoolValue("Debug", "bDebugLogging", false);
	debugOnScreen      = ini.GetBoolValue("Debug", "bDebugOnScreen", false);
	// Auto-fire sound fade-out (ms applied to all phantom-override exit paths)
	autoFireSoundFadeEnabled = ini.GetBoolValue("AutoFire", "bSoundFadeEnabled", true);
	autoFireSoundFadeMs = static_cast<int>(ini.GetLongValue("AutoFire", "iSoundFadeMs", 100));
	autoFireSoundFadeMs = std::clamp(autoFireSoundFadeMs, 0, 5000);

	// Set built-in defaults before loading per-weapon INI sections
	// Unarmed - fast, light
	ApplyWeaponDefaults(unarmed,  200.0f, 15.0f, 5.0f, 10.0f, 0.5f, 1.0f, 1.0f, 1.0f,
	                    120.0f, 8.0f, 8.0f, 15.0f, 1.0f, 1.0f, 0, true, false, false, false);
	// Melee - heavy, slow
	ApplyWeaponDefaults(melee,    110.0f, 9.0f, 14.0f, 22.0f, 2.0f, 1.2f, 1.0f, 1.0f,
	                    55.0f, 4.5f, 18.0f, 28.0f, 1.0f, 1.0f, 0, true, true, false, false);
	// Pistol - light, fast
	ApplyWeaponDefaults(pistol,   160.0f, 13.0f, 7.0f, 12.0f, 0.8f, 0.9f, 1.0f, 1.0f,
	                    90.0f, 7.0f, 10.0f, 18.0f, 1.0f, 1.0f, 0, true, true, true, true);
	// Rifle - medium
	ApplyWeaponDefaults(rifle,    130.0f, 11.0f, 9.0f, 16.0f, 1.2f, 1.0f, 1.0f, 1.0f,
	                    75.0f, 6.0f, 12.0f, 20.0f, 1.0f, 1.0f, 0, true, true, true, true);
	// Heavy - very heavy
	ApplyWeaponDefaults(heavy,    85.0f, 8.0f, 16.0f, 24.0f, 2.5f, 1.3f, 1.2f, 1.0f,
	                    45.0f, 4.0f, 20.0f, 30.0f, 1.0f, 1.0f, 0, true, true, true, true);
	// Energy - medium, smooth
	ApplyWeaponDefaults(energy,   140.0f, 12.0f, 8.0f, 14.0f, 1.0f, 0.9f, 1.0f, 1.0f,
	                    80.0f, 6.5f, 11.0f, 19.0f, 1.0f, 1.0f, 0, true, true, true, true);
	// Throwable - very light, minimal inertia
	ApplyWeaponDefaults(throwable, 200.0f, 15.0f, 5.0f, 8.0f, 0.4f, 0.8f, 1.0f, 1.0f,
	                    120.0f, 8.0f, 7.0f, 12.0f, 1.0f, 1.0f, 0, false, true, false, false);

	// PA variants - start from base, scale up for heavier feel
	pa_unarmed  = unarmed;  pa_unarmed.stiffness *= 0.8f;  pa_unarmed.mass *= 1.5f;
	pa_melee    = melee;    pa_melee.stiffness   *= 0.75f; pa_melee.mass   *= 1.6f;
	pa_pistol   = pistol;   pa_pistol.stiffness  *= 0.8f;  pa_pistol.mass  *= 1.4f;
	pa_rifle    = rifle;    pa_rifle.stiffness   *= 0.78f; pa_rifle.mass   *= 1.5f;
	pa_heavy    = heavy;    pa_heavy.stiffness   *= 0.72f; pa_heavy.mass   *= 1.6f;
	pa_energy   = energy;   pa_energy.stiffness  *= 0.8f;  pa_energy.mass  *= 1.4f;

	// Load per-weapon INI overrides
	unarmed.Load(ini, "Unarmed");       melee.Load(ini, "Melee");
	pistol.Load(ini, "Pistol");         rifle.Load(ini, "Rifle");
	heavy.Load(ini, "Heavy");
	energy.Load(ini, "Energy");         throwable.Load(ini, "Throwable");
	pa_unarmed.Load(ini, "PA_Unarmed"); pa_melee.Load(ini, "PA_Melee");
	pa_pistol.Load(ini, "PA_Pistol");   pa_rifle.Load(ini, "PA_Rifle");
	pa_heavy.Load(ini, "PA_Heavy");
	pa_energy.Load(ini, "PA_Energy");

	globalIntensity = std::clamp(globalIntensity, 0.0f, 5.0f);
	smoothingFactor = std::clamp(smoothingFactor, 0.0f, 1.0f);

	// Detect if anything meaningful changed (for log suppression during periodic reloads)
	bool changed = (enabled != prevEnabled) ||
		(std::abs(globalIntensity - prevIntensity) > 0.001f) ||
		(debugLogging != prevDebugLogging);

	if (changed) {
		logger::info("[FPGunplayOverhaul] Settings updated: enabled={}, intensity={:.2f}, debug={}",
			enabled, globalIntensity, debugLogging);
	}
	prevEnabled = enabled;
	prevIntensity = globalIntensity;
	prevDebugLogging = debugLogging;

}

// ============================================================
// Settings::Save
// ============================================================
void Settings::Save()
{
	CSimpleIniA ini;
	ini.SetUnicode();
	// Load existing first to preserve unrelated keys
	ini.LoadFile(kSettingsPath);

	auto setBool = [&](const char* sec, const char* key, bool v) { ini.SetLongValue(sec, key, v ? 1 : 0); };

	setBool("General", "bEnabled", enabled);
	setBool("General", "bInertiaEnabled", inertiaEnabled);
	setBool("General", "bEnablePosition", enablePosition);
	setBool("General", "bEnableRotation", enableRotation);
	setBool("General", "bRequireWeaponDrawn", requireWeaponDrawn);
	ini.SetDoubleValue("General", "fGlobalIntensity", globalIntensity);
	ini.SetDoubleValue("General", "fSmoothingFactor", smoothingFactor);

	setBool("WBFOV", "bEnabled", wbfovEnabled);
	ini.SetLongValue("WBFOV", "iLoadRetries", wbfovLoadRetries);

	setBool("Extras", "bFireOnEmptyEnabled", fireOnEmptyEnabled);

	setBool("Extras", "bDisableAirWalk", disableAirWalk);

	ini.SetDoubleValue("Settling", "fSettleDelay", settleDelay);
	ini.SetDoubleValue("Settling", "fSettleSpeed", settleSpeed);
	ini.SetDoubleValue("Settling", "fSettleDampingMult", settleDampingMult);

	setBool("MovementInertia", "bEnabled", movementInertiaEnabled);
	ini.SetDoubleValue("MovementInertia", "fStrength", movementInertiaStrength);
	ini.SetDoubleValue("MovementInertia", "fThreshold", movementInertiaThreshold);
	setBool("MovementInertia", "bForwardBackInertia", forwardBackInertia);
	setBool("MovementInertia", "bDisableVanillaSway", disableVanillaSway);

	setBool("ActionBlend", "bBlendDuringFiring", blendDuringFiring);
	ini.SetDoubleValue("ActionBlend", "fBlendFiringMinIntensity", blendFiringMinIntensity);
	setBool("ActionBlend", "bBlendDuringReload", blendDuringReload);
	ini.SetDoubleValue("ActionBlend", "fBlendReloadMinIntensity", blendReloadMinIntensity);
	setBool("ActionBlend", "bBlendDuringMelee", blendDuringMelee);
	ini.SetDoubleValue("ActionBlend", "fBlendMeleeMinIntensity", blendMeleeMinIntensity);
	ini.SetDoubleValue("ActionBlend", "fActionBlendSpeed", actionBlendSpeed);
	ini.SetDoubleValue("ActionBlend", "fActionBlendBackLeadTime", actionBlendBackLeadTime);

	setBool("PowerArmor", "bEnabled", enableInPowerArmor);
	setBool("PowerArmor", "bSeparateProfiles", usePASeparateProfiles);
	ini.SetDoubleValue("PowerArmor", "fMult", powerArmorMult);

	// Super Sprint
	setBool("Extras", "bSuperSprintEnabled", superSprintEnabled);
	ini.SetDoubleValue("Extras", "fSuperSprintDoubleTapWindow", superSprintDoubleTapWindow);
	ini.SetDoubleValue("Extras", "fSuperSprintSpeedMult", superSprintSpeedMult);
	ini.SetDoubleValue("Extras", "fSuperSprintAPCostMult", superSprintAPCostMult);
	ini.SetDoubleValue("Extras", "fSuperSprintAnimSpeedMult", superSprintAnimSpeedMult);
	setBool("Extras", "bSuperSprintStaminaThresholdEnabled", superSprintStaminaThresholdEnabled);
	ini.SetDoubleValue("Extras", "fSuperSprintStaminaThreshold", superSprintStaminaThreshold);

	setBool("Debug", "bDebugLogging", debugLogging);
	setBool("Debug", "bDebugOnScreen", debugOnScreen);
	ini.SetBoolValue("AutoFire", "bSoundFadeEnabled", autoFireSoundFadeEnabled);
	ini.SetLongValue("AutoFire", "iSoundFadeMs", autoFireSoundFadeMs);

	unarmed.Save(ini, "Unarmed");       melee.Save(ini, "Melee");
	pistol.Save(ini, "Pistol");         rifle.Save(ini, "Rifle");
	heavy.Save(ini, "Heavy");
	energy.Save(ini, "Energy");         throwable.Save(ini, "Throwable");
	pa_unarmed.Save(ini, "PA_Unarmed"); pa_melee.Save(ini, "PA_Melee");
	pa_pistol.Save(ini, "PA_Pistol");   pa_rifle.Save(ini, "PA_Rifle");
	pa_heavy.Save(ini, "PA_Heavy");
	pa_energy.Save(ini, "PA_Energy");

	// Ensure directory exists
	std::filesystem::create_directories(std::filesystem::path(kSettingsPath).parent_path());

	if (ini.SaveFile(kSettingsPath) >= 0) {
		logger::info("[FPGunplayOverhaul] Settings saved to '{}'", kSettingsPath);
	} else {
		logger::error("[FPGunplayOverhaul] Failed to save settings to '{}'", kSettingsPath);
	}
}

// ============================================================
// Settings accessors
// ============================================================
const char* Settings::GetWeaponTypeSectionName(WeaponType a_type)
{
	switch (a_type) {
	case WeaponType::Unarmed:   return "Unarmed";
	case WeaponType::Melee:     return "Melee";
	case WeaponType::Pistol:    return "Pistol";
	case WeaponType::Rifle:     return "Rifle";
	case WeaponType::Heavy:     return "Heavy";
	case WeaponType::Energy:    return "Energy";
	case WeaponType::Throwable: return "Throwable";
	case WeaponType::PA_Unarmed: return "PA_Unarmed";
	case WeaponType::PA_Melee:   return "PA_Melee";
	case WeaponType::PA_Pistol:  return "PA_Pistol";
	case WeaponType::PA_Rifle:   return "PA_Rifle";
	case WeaponType::PA_Heavy:   return "PA_Heavy";
	case WeaponType::PA_Energy:  return "PA_Energy";
	default: return "Unarmed";
	}
}

const WeaponInertiaSettings& Settings::GetWeaponSettings(WeaponType a_type) const
{
	switch (a_type) {
	case WeaponType::Unarmed:    return unarmed;
	case WeaponType::Melee:      return melee;
	case WeaponType::Pistol:     return pistol;
	case WeaponType::Rifle:      return rifle;
	case WeaponType::Heavy:      return heavy;
	case WeaponType::Energy:     return energy;
	case WeaponType::Throwable:  return throwable;
	case WeaponType::PA_Unarmed: return pa_unarmed;
	case WeaponType::PA_Melee:   return pa_melee;
	case WeaponType::PA_Pistol:  return pa_pistol;
	case WeaponType::PA_Rifle:   return pa_rifle;
	case WeaponType::PA_Heavy:   return pa_heavy;
	case WeaponType::PA_Energy:  return pa_energy;
	default:                     return unarmed;
	}
}

WeaponInertiaSettings& Settings::GetWeaponSettingsMutable(WeaponType a_type)
{
	switch (a_type) {
	case WeaponType::Unarmed:    return unarmed;
	case WeaponType::Melee:      return melee;
	case WeaponType::Pistol:     return pistol;
	case WeaponType::Rifle:      return rifle;
	case WeaponType::Heavy:      return heavy;
	case WeaponType::Energy:     return energy;
	case WeaponType::Throwable:  return throwable;
	case WeaponType::PA_Unarmed: return pa_unarmed;
	case WeaponType::PA_Melee:   return pa_melee;
	case WeaponType::PA_Pistol:  return pa_pistol;
	case WeaponType::PA_Rifle:   return pa_rifle;
	case WeaponType::PA_Heavy:   return pa_heavy;
	case WeaponType::PA_Energy:  return pa_energy;
	default:                     return unarmed;
	}
}
