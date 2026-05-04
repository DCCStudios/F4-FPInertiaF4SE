#include "InertiaPresets.h"
#include <fstream>
#include <sstream>
#include <algorithm>
#include <set>

namespace
{
	struct WeaponTypeInfo { WeaponType type; const char* name; const char* displayName; };

	const std::vector<WeaponTypeInfo> g_weaponTypes = {
		{ WeaponType::Unarmed,    "Unarmed",    "Unarmed / Fist" },
		{ WeaponType::Melee,      "Melee",      "Melee" },
		{ WeaponType::Pistol,     "Pistol",     "Pistol" },
		{ WeaponType::Rifle,      "Rifle",      "Rifle" },
		{ WeaponType::Heavy,      "Heavy",      "Heavy Weapon" },
		{ WeaponType::Energy,     "Energy",     "Energy Weapon" },
		{ WeaponType::Throwable,  "Throwable",  "Throwable" },
		{ WeaponType::PA_Unarmed, "PA_Unarmed", "PA: Unarmed" },
		{ WeaponType::PA_Melee,   "PA_Melee",   "PA: Melee" },
		{ WeaponType::PA_Pistol,  "PA_Pistol",  "PA: Pistol" },
		{ WeaponType::PA_Rifle,   "PA_Rifle",   "PA: Rifle" },
		{ WeaponType::PA_Heavy,   "PA_Heavy",   "PA: Heavy Weapon" },
		{ WeaponType::PA_Energy,  "PA_Energy",  "PA: Energy Weapon" },
	};
}

// ============================================================
// JSON serialization
// ============================================================
void to_json(json& j, const WeaponInertiaSettings& s)
{
	j = json{
		{"enabled", s.enabled},
		// Camera
		{"stiffness", s.stiffness}, {"damping", s.damping},
		{"maxOffset", s.maxOffset}, {"maxRotation", s.maxRotation},
		{"mass", s.mass}, {"pitchMultiplier", s.pitchMultiplier},
		{"rollMultiplier", s.rollMultiplier}, {"cameraPitchMult", s.cameraPitchMult},
		{"momentumDecay", s.momentumDecay},
		{"invertCameraPitch", s.invertCameraPitch}, {"invertCameraYaw", s.invertCameraYaw},
		// Movement
		{"movementInertiaEnabled", s.movementInertiaEnabled},
		{"movementStiffness", s.movementStiffness}, {"movementDamping", s.movementDamping},
		{"movementMaxOffset", s.movementMaxOffset}, {"movementMaxRotation", s.movementMaxRotation},
		{"movementLeftMult", s.movementLeftMult}, {"movementRightMult", s.movementRightMult},
		{"movementForwardMult", s.movementForwardMult}, {"movementBackwardMult", s.movementBackwardMult},
		{"invertMovementLateral", s.invertMovementLateral},
		{"invertMovementForwardBack", s.invertMovementForwardBack},
		// Walk direction offsets
		{"walkOffsetsEnabled", s.walkOffsetsEnabled},
		{"walkOffsetBlendInSpeed", s.walkOffsetBlendInSpeed},
		{"walkOffsetBlendOutSpeed", s.walkOffsetBlendOutSpeed},
		{"walkOffsetAdsBlendOutSpeed", s.walkOffsetAdsBlendOutSpeed},
		{"walkFwdPosX", s.walkForward.posX}, {"walkFwdPosY", s.walkForward.posY}, {"walkFwdPosZ", s.walkForward.posZ},
		{"walkFwdRotPitch", s.walkForward.rotPitch}, {"walkFwdRotYaw", s.walkForward.rotYaw}, {"walkFwdRotRoll", s.walkForward.rotRoll},
		{"walkBackPosX", s.walkBackward.posX}, {"walkBackPosY", s.walkBackward.posY}, {"walkBackPosZ", s.walkBackward.posZ},
		{"walkBackRotPitch", s.walkBackward.rotPitch}, {"walkBackRotYaw", s.walkBackward.rotYaw}, {"walkBackRotRoll", s.walkBackward.rotRoll},
		{"walkLeftPosX", s.walkLeft.posX}, {"walkLeftPosY", s.walkLeft.posY}, {"walkLeftPosZ", s.walkLeft.posZ},
		{"walkLeftRotPitch", s.walkLeft.rotPitch}, {"walkLeftRotYaw", s.walkLeft.rotYaw}, {"walkLeftRotRoll", s.walkLeft.rotRoll},
		{"walkRightPosX", s.walkRight.posX}, {"walkRightPosY", s.walkRight.posY}, {"walkRightPosZ", s.walkRight.posZ},
		{"walkRightRotPitch", s.walkRight.rotPitch}, {"walkRightRotYaw", s.walkRight.rotYaw}, {"walkRightRotRoll", s.walkRight.rotRoll},
		// Simultaneous
		{"simultaneousThreshold", s.simultaneousThreshold},
		{"simultaneousCameraMult", s.simultaneousCameraMult},
		{"simultaneousMovementMult", s.simultaneousMovementMult},
		// Sprint
		{"sprintInertiaEnabled", s.sprintInertiaEnabled},
		{"sprintStartEnabled", s.sprintStartEnabled},
		{"sprintStopEnabled", s.sprintStopEnabled},
		{"sprintImpulseX", s.sprintImpulseX}, {"sprintImpulseY", s.sprintImpulseY}, {"sprintImpulseZ", s.sprintImpulseZ},
		{"sprintRotImpulse", s.sprintRotImpulse},
		{"sprintStopImpulseX", s.sprintStopImpulseX}, {"sprintStopImpulseY", s.sprintStopImpulseY}, {"sprintStopImpulseZ", s.sprintStopImpulseZ},
		{"sprintStopRotImpulse", s.sprintStopRotImpulse},
		{"sprintImpulseBlendTime", s.sprintImpulseBlendTime},
		{"sprintStiffness", s.sprintStiffness}, {"sprintDamping", s.sprintDamping},
		// Jump
		{"jumpInertiaEnabled", s.jumpInertiaEnabled},
		{"cameraInertiaAirMult", s.cameraInertiaAirMult},
		{"jumpImpulseX", s.jumpImpulseX}, {"jumpImpulseY", s.jumpImpulseY}, {"jumpImpulseZ", s.jumpImpulseZ},
		{"jumpRotPitch", s.jumpRotPitch}, {"jumpRotYaw", s.jumpRotYaw}, {"jumpRotRoll", s.jumpRotRoll},
		{"fallImpulseX", s.fallImpulseX}, {"fallImpulseY", s.fallImpulseY}, {"fallImpulseZ", s.fallImpulseZ},
		{"fallRotPitch", s.fallRotPitch}, {"fallRotYaw", s.fallRotYaw}, {"fallRotRoll", s.fallRotRoll},
		{"jumpStiffness", s.jumpStiffness}, {"jumpDamping", s.jumpDamping},
		{"fallStiffness", s.fallStiffness}, {"fallDamping", s.fallDamping},
		{"landImpulseX", s.landImpulseX}, {"landImpulseY", s.landImpulseY}, {"landImpulseZ", s.landImpulseZ},
		{"landRotPitch", s.landRotPitch}, {"landRotYaw", s.landRotYaw}, {"landRotRoll", s.landRotRoll},
		{"landStiffness", s.landStiffness}, {"landDamping", s.landDamping},
		{"airTimeImpulseScale", s.airTimeImpulseScale},
		// Equip impulse
		{"equipImpulseEnabled", s.equipImpulseEnabled},
		{"equipImpulseX", s.equipImpulseX}, {"equipImpulseY", s.equipImpulseY}, {"equipImpulseZ", s.equipImpulseZ},
		{"equipRotImpulse", s.equipRotImpulse}, {"equipBlendTime", s.equipBlendTime},
		{"equipStiffness", s.equipStiffness}, {"equipDamping", s.equipDamping},
		// ADS enter
		{"adsEnterImpulseEnabled", s.adsEnterImpulseEnabled},
		{"adsEnterImpulseX", s.adsEnterImpulseX}, {"adsEnterImpulseY", s.adsEnterImpulseY}, {"adsEnterImpulseZ", s.adsEnterImpulseZ},
		{"adsEnterRotImpulse", s.adsEnterRotImpulse},
		{"adsEnterStiffness", s.adsEnterStiffness}, {"adsEnterDamping", s.adsEnterDamping},
		// ADS exit
		{"adsExitImpulseEnabled", s.adsExitImpulseEnabled},
		{"adsExitImpulseX", s.adsExitImpulseX}, {"adsExitImpulseY", s.adsExitImpulseY}, {"adsExitImpulseZ", s.adsExitImpulseZ},
		{"adsExitRotImpulse", s.adsExitRotImpulse},
		{"adsExitStiffness", s.adsExitStiffness}, {"adsExitDamping", s.adsExitDamping},
		// ADS multipliers
		{"adsInertiaEnabled", s.adsInertiaEnabled}, {"adsInertiaMult", s.adsInertiaMult},
		{"adsScopeInertiaEnabled", s.adsScopeInertiaEnabled},
		{"adsScopeInertiaMult", s.adsScopeInertiaMult},
		// Fire recovery (hip)
		{"fireRecoveryImpulseEnabled", s.fireRecoveryImpulseEnabled},
		{"fireRecoveryImpulseX", s.fireRecoveryImpulseX},
		{"fireRecoveryImpulseY", s.fireRecoveryImpulseY},
		{"fireRecoveryImpulseZ", s.fireRecoveryImpulseZ},
		{"fireRecoveryRotImpulse", s.fireRecoveryRotImpulse},
		{"fireRecoveryStiffness", s.fireRecoveryStiffness},
		{"fireRecoveryDamping", s.fireRecoveryDamping},
		{"fireRecoveryCooldown", s.fireRecoveryCooldown},
		// Fire recovery (ADS)
		{"adsFireRecoveryImpulseEnabled", s.adsFireRecoveryImpulseEnabled},
		{"adsFireRecoveryImpulseX", s.adsFireRecoveryImpulseX},
		{"adsFireRecoveryImpulseY", s.adsFireRecoveryImpulseY},
		{"adsFireRecoveryImpulseZ", s.adsFireRecoveryImpulseZ},
		{"adsFireRecoveryRotImpulse", s.adsFireRecoveryRotImpulse},
		{"adsFireRecoveryStiffness", s.adsFireRecoveryStiffness},
		{"adsFireRecoveryDamping", s.adsFireRecoveryDamping},
		{"adsFireRecoveryCooldown", s.adsFireRecoveryCooldown},
		{"reloadImpulseEnabled", s.reloadImpulseEnabled},
		{"reloadTriggerEvent", s.reloadTriggerEvent},
		{"reloadImpulseDelay", s.reloadImpulseDelay},
		{"reloadImpulseBlendTime", s.reloadImpulseBlendTime},
		{"reloadImpulseX", s.reloadImpulseX},
		{"reloadImpulseY", s.reloadImpulseY},
		{"reloadImpulseZ", s.reloadImpulseZ},
		{"reloadRotImpulse", s.reloadRotImpulse},
		{"reloadStiffness", s.reloadStiffness},
		{"reloadDamping", s.reloadDamping},
		{"emptyReloadImpulseEnabled", s.emptyReloadImpulseEnabled},
		{"emptyReloadTriggerEvent", s.emptyReloadTriggerEvent},
		{"emptyReloadImpulseDelay", s.emptyReloadImpulseDelay},
		{"emptyReloadImpulseBlendTime", s.emptyReloadImpulseBlendTime},
		{"emptyReloadImpulseX", s.emptyReloadImpulseX},
		{"emptyReloadImpulseY", s.emptyReloadImpulseY},
		{"emptyReloadImpulseZ", s.emptyReloadImpulseZ},
		{"emptyReloadRotImpulse", s.emptyReloadRotImpulse},
		{"emptyReloadStiffness", s.emptyReloadStiffness},
		{"emptyReloadDamping", s.emptyReloadDamping},
		{"sustainedFireBuildRate", s.sustainedFireBuildRate},
		{"sustainedFireMax", s.sustainedFireMax},
		{"sustainedFireDecay", s.sustainedFireDecay},
		// Early ADS return
		{"earlyAdsReturnEnabled", s.earlyAdsReturnEnabled},
		{"earlyAdsReturnTrigger", s.earlyAdsReturnTrigger},
		{"earlyAdsReturnDelay", s.earlyAdsReturnDelay},
		{"earlyAdsReturnBlendTime", s.earlyAdsReturnBlendTime},
		{"earlyAdsReturnBlendType", s.earlyAdsReturnBlendType},
		{"earlyAdsReturnImpulseScale", s.earlyAdsReturnImpulseScale},
		{"earlyAdsFireBlockDelay", s.earlyAdsFireBlockDelay},
		{"earlyAdsAutoFireEnabled", s.earlyAdsAutoFireEnabled},
		{"earlyAdsAutoFireWindow", s.earlyAdsAutoFireWindow},
		{"earlyAdsAutoFireMaxAttempts", s.earlyAdsAutoFireMaxAttempts},
		{"earlyFireCancelEnabled", s.earlyFireCancelEnabled},
		{"earlyEquipAdsEnabled", s.earlyEquipAdsEnabled},
		{"earlyEquipFireEnabled", s.earlyEquipFireEnabled},
		// Weight scaling
		{"weightScalingEnabled", s.weightScalingEnabled},
		{"weightScaleInfluence", s.weightScaleInfluence},
		{"weightScaleMin", s.weightScaleMin},
		{"weightScaleMax", s.weightScaleMax},
		// Pivot
		{"pivotPoint", s.pivotPoint},
		{"adsPivotPoint", s.adsPivotPoint},
		{"useBindPosePivot", s.useBindPosePivot},
		// ADS spring dampening
		{"adsTransitionDampenEnabled", s.adsTransitionDampenEnabled},
		{"adsTransitionDampenFactor", s.adsTransitionDampenFactor},
		// ADS enter transition
		{"adsEnterTransitionEnabled",      s.adsEnterTransition.enabled},
		{"adsEnterTransitionCurveType",    s.adsEnterTransition.curveType},
		{"adsEnterTransitionPeakX",        s.adsEnterTransition.peakOffsetX},
		{"adsEnterTransitionPeakY",        s.adsEnterTransition.peakOffsetY},
		{"adsEnterTransitionPeakZ",        s.adsEnterTransition.peakOffsetZ},
		{"adsEnterTransitionPeakRotPitch", s.adsEnterTransition.peakRotPitch},
		{"adsEnterTransitionPeakRotYaw",   s.adsEnterTransition.peakRotYaw},
		{"adsEnterTransitionPeakRotRoll",  s.adsEnterTransition.peakRotRoll},
		{"adsEnterTransitionPeakPos",      s.adsEnterTransition.peakPosition},
		{"adsEnterTransitionAsymmetry",    s.adsEnterTransition.asymmetry},
		{"adsEnterTransitionImpulseBlend", s.adsEnterTransition.impulseBlendFactor},
		// ADS exit transition
		{"adsExitTransitionEnabled",       s.adsExitTransition.enabled},
		{"adsExitTransitionCurveType",     s.adsExitTransition.curveType},
		{"adsExitTransitionPeakX",         s.adsExitTransition.peakOffsetX},
		{"adsExitTransitionPeakY",         s.adsExitTransition.peakOffsetY},
		{"adsExitTransitionPeakZ",         s.adsExitTransition.peakOffsetZ},
		{"adsExitTransitionPeakRotPitch",  s.adsExitTransition.peakRotPitch},
		{"adsExitTransitionPeakRotYaw",    s.adsExitTransition.peakRotYaw},
		{"adsExitTransitionPeakRotRoll",   s.adsExitTransition.peakRotRoll},
		{"adsExitTransitionPeakPos",       s.adsExitTransition.peakPosition},
		{"adsExitTransitionAsymmetry",     s.adsExitTransition.asymmetry},
		{"adsExitTransitionImpulseBlend",  s.adsExitTransition.impulseBlendFactor},
		// Lean inertia
		{"leanOffsetEnabled",       s.leanOffsetEnabled},
		{"leanOffsetDisableInADS",  s.leanOffsetDisableInADS},
		{"leanOffsetX",             s.leanOffsetX},
		{"leanOffsetY",             s.leanOffsetY},
		{"leanOffsetZ",             s.leanOffsetZ},
		{"leanOffsetBlendSpeed",    s.leanOffsetBlendSpeed},
		{"leanImpulseEnabled",      s.leanImpulseEnabled},
		{"leanImpulseDisableInADS", s.leanImpulseDisableInADS},
		{"leanImpulseX",         s.leanImpulseX},
		{"leanImpulseY",         s.leanImpulseY},
		{"leanImpulseZ",         s.leanImpulseZ},
		{"leanRotImpulsePitch",  s.leanRotImpulsePitch},
		{"leanRotImpulseYaw",    s.leanRotImpulseYaw},
		{"leanRotImpulseRoll",   s.leanRotImpulseRoll},
		{"leanStiffness",        s.leanStiffness},
		{"leanDamping",          s.leanDamping},
		// Sneak impulse
		{"sneakImpulseEnabled",   s.sneakImpulseEnabled},
		{"sneakEnterImpulseX",    s.sneakEnterImpulseX},
		{"sneakEnterImpulseY",    s.sneakEnterImpulseY},
		{"sneakEnterImpulseZ",    s.sneakEnterImpulseZ},
		{"sneakEnterRotImpulse",  s.sneakEnterRotImpulse},
		{"sneakExitImpulseX",     s.sneakExitImpulseX},
		{"sneakExitImpulseY",     s.sneakExitImpulseY},
		{"sneakExitImpulseZ",     s.sneakExitImpulseZ},
		{"sneakExitRotImpulse",   s.sneakExitRotImpulse},
		{"sneakStiffness",        s.sneakStiffness},
		{"sneakDamping",          s.sneakDamping},
	};
}

void from_json(const json& j, WeaponInertiaSettings& s)
{
	auto get = [&j](const char* k, auto& v) { if (j.contains(k)) j.at(k).get_to(v); };

	get("enabled", s.enabled);
	get("stiffness", s.stiffness); get("damping", s.damping);
	get("maxOffset", s.maxOffset); get("maxRotation", s.maxRotation);
	get("mass", s.mass); get("pitchMultiplier", s.pitchMultiplier);
	get("rollMultiplier", s.rollMultiplier); get("cameraPitchMult", s.cameraPitchMult);
	get("momentumDecay", s.momentumDecay);
	get("invertCameraPitch", s.invertCameraPitch); get("invertCameraYaw", s.invertCameraYaw);
	get("movementInertiaEnabled", s.movementInertiaEnabled);
	get("movementStiffness", s.movementStiffness); get("movementDamping", s.movementDamping);
	get("movementMaxOffset", s.movementMaxOffset); get("movementMaxRotation", s.movementMaxRotation);
	get("movementLeftMult", s.movementLeftMult); get("movementRightMult", s.movementRightMult);
	get("movementForwardMult", s.movementForwardMult); get("movementBackwardMult", s.movementBackwardMult);
	get("invertMovementLateral", s.invertMovementLateral);
	get("invertMovementForwardBack", s.invertMovementForwardBack);
	get("walkOffsetsEnabled", s.walkOffsetsEnabled);
	get("walkOffsetBlendInSpeed", s.walkOffsetBlendInSpeed);
	get("walkOffsetBlendOutSpeed", s.walkOffsetBlendOutSpeed);
	get("walkOffsetAdsBlendOutSpeed", s.walkOffsetAdsBlendOutSpeed);
	get("walkFwdPosX", s.walkForward.posX); get("walkFwdPosY", s.walkForward.posY); get("walkFwdPosZ", s.walkForward.posZ);
	get("walkFwdRotPitch", s.walkForward.rotPitch); get("walkFwdRotYaw", s.walkForward.rotYaw); get("walkFwdRotRoll", s.walkForward.rotRoll);
	get("walkBackPosX", s.walkBackward.posX); get("walkBackPosY", s.walkBackward.posY); get("walkBackPosZ", s.walkBackward.posZ);
	get("walkBackRotPitch", s.walkBackward.rotPitch); get("walkBackRotYaw", s.walkBackward.rotYaw); get("walkBackRotRoll", s.walkBackward.rotRoll);
	get("walkLeftPosX", s.walkLeft.posX); get("walkLeftPosY", s.walkLeft.posY); get("walkLeftPosZ", s.walkLeft.posZ);
	get("walkLeftRotPitch", s.walkLeft.rotPitch); get("walkLeftRotYaw", s.walkLeft.rotYaw); get("walkLeftRotRoll", s.walkLeft.rotRoll);
	get("walkRightPosX", s.walkRight.posX); get("walkRightPosY", s.walkRight.posY); get("walkRightPosZ", s.walkRight.posZ);
	get("walkRightRotPitch", s.walkRight.rotPitch); get("walkRightRotYaw", s.walkRight.rotYaw); get("walkRightRotRoll", s.walkRight.rotRoll);
	get("simultaneousThreshold", s.simultaneousThreshold);
	get("simultaneousCameraMult", s.simultaneousCameraMult);
	get("simultaneousMovementMult", s.simultaneousMovementMult);
	get("sprintInertiaEnabled", s.sprintInertiaEnabled);
	get("sprintStartEnabled", s.sprintStartEnabled);
	get("sprintStopEnabled", s.sprintStopEnabled);
	get("sprintImpulseX", s.sprintImpulseX); get("sprintImpulseY", s.sprintImpulseY); get("sprintImpulseZ", s.sprintImpulseZ);
	get("sprintRotImpulse", s.sprintRotImpulse);
	get("sprintStopImpulseX", s.sprintStopImpulseX); get("sprintStopImpulseY", s.sprintStopImpulseY); get("sprintStopImpulseZ", s.sprintStopImpulseZ);
	get("sprintStopRotImpulse", s.sprintStopRotImpulse);
	get("sprintImpulseBlendTime", s.sprintImpulseBlendTime);
	get("sprintStiffness", s.sprintStiffness); get("sprintDamping", s.sprintDamping);
	get("jumpInertiaEnabled", s.jumpInertiaEnabled);
	get("cameraInertiaAirMult", s.cameraInertiaAirMult);
	get("jumpImpulseX", s.jumpImpulseX); get("jumpImpulseY", s.jumpImpulseY); get("jumpImpulseZ", s.jumpImpulseZ);
	// Backward compat: old single-scalar rotImpulse seeds pitch
	get("jumpRotImpulse", s.jumpRotPitch);
	get("jumpRotPitch", s.jumpRotPitch); get("jumpRotYaw", s.jumpRotYaw); get("jumpRotRoll", s.jumpRotRoll);
	get("fallImpulseX", s.fallImpulseX); get("fallImpulseY", s.fallImpulseY); get("fallImpulseZ", s.fallImpulseZ);
	get("fallRotImpulse", s.fallRotPitch);
	get("fallRotPitch", s.fallRotPitch); get("fallRotYaw", s.fallRotYaw); get("fallRotRoll", s.fallRotRoll);
	get("jumpStiffness", s.jumpStiffness); get("jumpDamping", s.jumpDamping);
	get("fallStiffness", s.fallStiffness); get("fallDamping", s.fallDamping);
	get("landImpulseX", s.landImpulseX); get("landImpulseY", s.landImpulseY); get("landImpulseZ", s.landImpulseZ);
	get("landRotImpulse", s.landRotPitch);
	get("landRotPitch", s.landRotPitch); get("landRotYaw", s.landRotYaw); get("landRotRoll", s.landRotRoll);
	get("landStiffness", s.landStiffness); get("landDamping", s.landDamping);
	get("airTimeImpulseScale", s.airTimeImpulseScale);
	get("equipImpulseEnabled", s.equipImpulseEnabled);
	get("equipImpulseX", s.equipImpulseX); get("equipImpulseY", s.equipImpulseY); get("equipImpulseZ", s.equipImpulseZ);
	get("equipRotImpulse", s.equipRotImpulse); get("equipBlendTime", s.equipBlendTime);
	get("equipStiffness", s.equipStiffness); get("equipDamping", s.equipDamping);
	get("adsEnterImpulseEnabled", s.adsEnterImpulseEnabled);
	get("adsEnterImpulseX", s.adsEnterImpulseX); get("adsEnterImpulseY", s.adsEnterImpulseY); get("adsEnterImpulseZ", s.adsEnterImpulseZ);
	get("adsEnterRotImpulse", s.adsEnterRotImpulse);
	get("adsEnterStiffness", s.adsEnterStiffness); get("adsEnterDamping", s.adsEnterDamping);
	get("adsExitImpulseEnabled", s.adsExitImpulseEnabled);
	get("adsExitImpulseX", s.adsExitImpulseX); get("adsExitImpulseY", s.adsExitImpulseY); get("adsExitImpulseZ", s.adsExitImpulseZ);
	get("adsExitRotImpulse", s.adsExitRotImpulse);
	get("adsExitStiffness", s.adsExitStiffness); get("adsExitDamping", s.adsExitDamping);
	get("adsInertiaEnabled", s.adsInertiaEnabled); get("adsInertiaMult", s.adsInertiaMult);
	get("adsScopeInertiaEnabled", s.adsScopeInertiaEnabled);
	get("adsScopeInertiaMult", s.adsScopeInertiaMult);
	get("fireRecoveryImpulseEnabled", s.fireRecoveryImpulseEnabled);
	get("fireRecoveryImpulseX", s.fireRecoveryImpulseX);
	get("fireRecoveryImpulseY", s.fireRecoveryImpulseY);
	get("fireRecoveryImpulseZ", s.fireRecoveryImpulseZ);
	get("fireRecoveryRotImpulse", s.fireRecoveryRotImpulse);
	get("adsFireRecoveryImpulseEnabled", s.adsFireRecoveryImpulseEnabled);
	get("adsFireRecoveryImpulseX", s.adsFireRecoveryImpulseX);
	get("adsFireRecoveryImpulseY", s.adsFireRecoveryImpulseY);
	get("adsFireRecoveryImpulseZ", s.adsFireRecoveryImpulseZ);
	get("adsFireRecoveryRotImpulse", s.adsFireRecoveryRotImpulse);
	get("adsFireRecoveryStiffness", s.adsFireRecoveryStiffness);
	get("adsFireRecoveryDamping", s.adsFireRecoveryDamping);
	get("adsFireRecoveryCooldown", s.adsFireRecoveryCooldown);
	get("fireRecoveryStiffness", s.fireRecoveryStiffness);
	get("fireRecoveryDamping", s.fireRecoveryDamping);
	get("fireRecoveryCooldown", s.fireRecoveryCooldown);
	get("reloadImpulseEnabled", s.reloadImpulseEnabled);
	get("reloadTriggerEvent", s.reloadTriggerEvent);
	get("reloadImpulseDelay", s.reloadImpulseDelay);
	get("reloadImpulseBlendTime", s.reloadImpulseBlendTime);
	get("reloadImpulseX", s.reloadImpulseX);
	get("reloadImpulseY", s.reloadImpulseY);
	get("reloadImpulseZ", s.reloadImpulseZ);
	get("reloadRotImpulse", s.reloadRotImpulse);
	get("reloadStiffness", s.reloadStiffness);
	get("reloadDamping", s.reloadDamping);
	get("emptyReloadImpulseEnabled", s.emptyReloadImpulseEnabled);
	get("emptyReloadTriggerEvent", s.emptyReloadTriggerEvent);
	get("emptyReloadImpulseDelay", s.emptyReloadImpulseDelay);
	get("emptyReloadImpulseBlendTime", s.emptyReloadImpulseBlendTime);
	get("emptyReloadImpulseX", s.emptyReloadImpulseX);
	get("emptyReloadImpulseY", s.emptyReloadImpulseY);
	get("emptyReloadImpulseZ", s.emptyReloadImpulseZ);
	get("emptyReloadRotImpulse", s.emptyReloadRotImpulse);
	get("emptyReloadStiffness", s.emptyReloadStiffness);
	get("emptyReloadDamping", s.emptyReloadDamping);
	get("sustainedFireBuildRate", s.sustainedFireBuildRate);
	get("sustainedFireMax", s.sustainedFireMax);
	get("sustainedFireDecay", s.sustainedFireDecay);
	get("earlyAdsReturnEnabled", s.earlyAdsReturnEnabled);
	get("earlyAdsReturnTrigger", s.earlyAdsReturnTrigger);
	get("earlyAdsReturnDelay", s.earlyAdsReturnDelay);
	get("earlyAdsReturnBlendTime", s.earlyAdsReturnBlendTime);
	get("earlyAdsReturnBlendType", s.earlyAdsReturnBlendType);
	get("earlyAdsReturnImpulseScale", s.earlyAdsReturnImpulseScale);
	get("earlyAdsFireBlockDelay", s.earlyAdsFireBlockDelay);
	get("earlyFireCancelEnabled", s.earlyFireCancelEnabled);
	get("earlyEquipAdsEnabled", s.earlyEquipAdsEnabled);
	get("earlyEquipFireEnabled", s.earlyEquipFireEnabled);
	get("earlyAdsAutoFireEnabled", s.earlyAdsAutoFireEnabled);
	get("earlyAdsAutoFireWindow", s.earlyAdsAutoFireWindow);
	get("earlyAdsAutoFireMaxAttempts", s.earlyAdsAutoFireMaxAttempts);
	get("weightScalingEnabled", s.weightScalingEnabled);
	get("weightScaleInfluence", s.weightScaleInfluence);
	get("weightScaleMin", s.weightScaleMin); get("weightScaleMax", s.weightScaleMax);
	get("pivotPoint", s.pivotPoint);
	get("adsPivotPoint", s.adsPivotPoint);
	get("useBindPosePivot", s.useBindPosePivot);
	// ADS spring dampening
	get("adsTransitionDampenEnabled", s.adsTransitionDampenEnabled);
	get("adsTransitionDampenFactor",  s.adsTransitionDampenFactor);
	// ADS enter transition
	get("adsEnterTransitionEnabled",      s.adsEnterTransition.enabled);
	get("adsEnterTransitionCurveType",    s.adsEnterTransition.curveType);
	get("adsEnterTransitionPeakX",        s.adsEnterTransition.peakOffsetX);
	get("adsEnterTransitionPeakY",        s.adsEnterTransition.peakOffsetY);
	get("adsEnterTransitionPeakZ",        s.adsEnterTransition.peakOffsetZ);
	get("adsEnterTransitionPeakRotPitch", s.adsEnterTransition.peakRotPitch);
	get("adsEnterTransitionPeakRotYaw",   s.adsEnterTransition.peakRotYaw);
	get("adsEnterTransitionPeakRotRoll",  s.adsEnterTransition.peakRotRoll);
	get("adsEnterTransitionPeakPos",      s.adsEnterTransition.peakPosition);
	get("adsEnterTransitionAsymmetry",    s.adsEnterTransition.asymmetry);
	get("adsEnterTransitionImpulseBlend", s.adsEnterTransition.impulseBlendFactor);
	// ADS exit transition
	get("adsExitTransitionEnabled",       s.adsExitTransition.enabled);
	get("adsExitTransitionCurveType",     s.adsExitTransition.curveType);
	get("adsExitTransitionPeakX",         s.adsExitTransition.peakOffsetX);
	get("adsExitTransitionPeakY",         s.adsExitTransition.peakOffsetY);
	get("adsExitTransitionPeakZ",         s.adsExitTransition.peakOffsetZ);
	get("adsExitTransitionPeakRotPitch",  s.adsExitTransition.peakRotPitch);
	get("adsExitTransitionPeakRotYaw",    s.adsExitTransition.peakRotYaw);
	get("adsExitTransitionPeakRotRoll",   s.adsExitTransition.peakRotRoll);
	get("adsExitTransitionPeakPos",       s.adsExitTransition.peakPosition);
	get("adsExitTransitionAsymmetry",     s.adsExitTransition.asymmetry);
	get("adsExitTransitionImpulseBlend",  s.adsExitTransition.impulseBlendFactor);
	// Lean inertia
	get("leanOffsetEnabled",       s.leanOffsetEnabled);
	get("leanOffsetDisableInADS",  s.leanOffsetDisableInADS);
	get("leanOffsetX",             s.leanOffsetX);
	get("leanOffsetY",             s.leanOffsetY);
	get("leanOffsetZ",             s.leanOffsetZ);
	get("leanOffsetBlendSpeed",    s.leanOffsetBlendSpeed);
	get("leanImpulseEnabled",      s.leanImpulseEnabled);
	get("leanImpulseDisableInADS", s.leanImpulseDisableInADS);
	get("leanImpulseX",         s.leanImpulseX);
	get("leanImpulseY",         s.leanImpulseY);
	get("leanImpulseZ",         s.leanImpulseZ);
	get("leanRotImpulsePitch",  s.leanRotImpulsePitch);
	get("leanRotImpulseYaw",    s.leanRotImpulseYaw);
	get("leanRotImpulseRoll",   s.leanRotImpulseRoll);
	get("leanStiffness",        s.leanStiffness);
	get("leanDamping",          s.leanDamping);
	// Sneak impulse
	get("sneakImpulseEnabled",   s.sneakImpulseEnabled);
	get("sneakEnterImpulseX",    s.sneakEnterImpulseX);
	get("sneakEnterImpulseY",    s.sneakEnterImpulseY);
	get("sneakEnterImpulseZ",    s.sneakEnterImpulseZ);
	get("sneakEnterRotImpulse",  s.sneakEnterRotImpulse);
	get("sneakExitImpulseX",     s.sneakExitImpulseX);
	get("sneakExitImpulseY",     s.sneakExitImpulseY);
	get("sneakExitImpulseZ",     s.sneakExitImpulseZ);
	get("sneakExitRotImpulse",   s.sneakExitRotImpulse);
	get("sneakStiffness",        s.sneakStiffness);
	get("sneakDamping",          s.sneakDamping);
}

// ============================================================
// Type name helpers
// ============================================================
const char* InertiaPresets::GetWeaponTypeName(WeaponType a_type)
{
	for (auto& info : g_weaponTypes)
		if (info.type == a_type) return info.name;
	return "Unknown";
}

const char* InertiaPresets::GetWeaponTypeDisplayName(WeaponType a_type)
{
	for (auto& info : g_weaponTypes)
		if (info.type == a_type) return info.displayName;
	return "Unknown";
}

WeaponType InertiaPresets::ParseWeaponTypeName(const std::string& a_name)
{
	// Backward compat: legacy "Shotgun" types map to Rifle
	if (a_name == "Shotgun")    return WeaponType::Rifle;
	if (a_name == "PA_Shotgun") return WeaponType::PA_Rifle;

	for (auto& info : g_weaponTypes)
		if (a_name == info.name) return info.type;
	return WeaponType::Unarmed;
}

// ============================================================
// Paths
// ============================================================
std::filesystem::path InertiaPresets::GetPresetFolderPath() const
{
	return std::filesystem::path("Data\\F4SE\\Plugins\\FPInertia");
}

std::filesystem::path InertiaPresets::GetWeaponTypePresetsPath() const
{
	return GetPresetPath(activePresetName);
}

std::filesystem::path InertiaPresets::GetPresetPath(const std::string& a_name) const
{
	return GetPresetFolderPath() / (a_name + ".json");
}

std::filesystem::path InertiaPresets::GetSpecificWeaponPresetPath(const std::string& a_editorID) const
{
	return GetPresetFolderPath() / "Weapons" / (a_editorID + ".json");
}

std::filesystem::path InertiaPresets::GetKeywordMappingsFolderPath() const
{
	return GetPresetFolderPath() / "WeaponTypeMappings";
}

void InertiaPresets::EnsurePresetFolderExists()
{
	try {
		std::filesystem::create_directories(GetPresetFolderPath());
		std::filesystem::create_directories(GetPresetFolderPath() / "Weapons");
		std::filesystem::create_directories(GetKeywordMappingsFolderPath());
	} catch (const std::exception& e) {
		logger::error("[FPInertia] Failed to create preset folders: {}", e.what());
	}
}

// ============================================================
// Init
// ============================================================
void InertiaPresets::Init()
{
	EnsurePresetFolderExists();
	LoadKeywordMappings();
	LoadActivePresetSetting();
	LoadAllPresets();

	if (weaponTypeSettings.empty()) {
		InitializeDefaultSettings();
		SaveWeaponTypePresets();
	}

	EnsureCustomTypesInPreset();

	logger::info("[FPInertia] InertiaPresets: preset='{}', {} types, {} custom, {} specific weapons",
		activePresetName, weaponTypeSettings.size(), customWeaponTypeSettings.size(),
		specificWeaponSettings.size());
}

void InertiaPresets::InitializeDefaultSettings()
{
	auto* s = Settings::GetSingleton();
	std::unique_lock lock(presetMutex);
	weaponTypeSettings[WeaponType::Unarmed]    = s->unarmed;
	weaponTypeSettings[WeaponType::Melee]      = s->melee;
	weaponTypeSettings[WeaponType::Pistol]     = s->pistol;
	weaponTypeSettings[WeaponType::Rifle]      = s->rifle;
	weaponTypeSettings[WeaponType::Heavy]      = s->heavy;
	weaponTypeSettings[WeaponType::Energy]     = s->energy;
	weaponTypeSettings[WeaponType::Throwable]  = s->throwable;
	weaponTypeSettings[WeaponType::PA_Unarmed] = s->pa_unarmed;
	weaponTypeSettings[WeaponType::PA_Melee]   = s->pa_melee;
	weaponTypeSettings[WeaponType::PA_Pistol]  = s->pa_pistol;
	weaponTypeSettings[WeaponType::PA_Rifle]   = s->pa_rifle;
	weaponTypeSettings[WeaponType::PA_Heavy]   = s->pa_heavy;
	weaponTypeSettings[WeaponType::PA_Energy]  = s->pa_energy;
	logger::info("[FPInertia] Initialized default weapon type settings from INI");
}

// ============================================================
// Preset file save/load
// ============================================================
void InertiaPresets::SaveWeaponTypePresets()
{
	std::shared_lock lock(presetMutex);
	json j;
	for (auto& [type, settings] : weaponTypeSettings) {
		j[GetWeaponTypeName(type)] = settings;
	}
	for (auto& [name, settings] : customWeaponTypeSettings) {
		j[name] = settings;
	}

	try {
		std::ofstream file(GetWeaponTypePresetsPath());
		file << j.dump(2);
		logger::info("[FPInertia] Saved preset '{}'", activePresetName);
	} catch (const std::exception& e) {
		logger::error("[FPInertia] Failed to save preset: {}", e.what());
	}
}

void InertiaPresets::LoadWeaponTypePresets()
{
	auto path = GetWeaponTypePresetsPath();
	if (!std::filesystem::exists(path)) return;

	try {
		std::ifstream file(path);
		json j = json::parse(file);

		std::unique_lock lock(presetMutex);
		for (auto& info : g_weaponTypes) {
			auto name = std::string(info.name);
			if (j.contains(name)) {
				weaponTypeSettings[info.type] = j[name].get<WeaponInertiaSettings>();
			}
		}
		// Custom types
		for (auto& [key, val] : j.items()) {
			bool isStandard = false;
			for (auto& info : g_weaponTypes)
				if (key == info.name) { isStandard = true; break; }
			if (!isStandard && val.is_object()) {
				customWeaponTypeSettings[key] = val.get<WeaponInertiaSettings>();
				if (std::find(customWeaponTypeNames.begin(), customWeaponTypeNames.end(), key) == customWeaponTypeNames.end())
					customWeaponTypeNames.push_back(key);
			}
		}
		logger::info("[FPInertia] Loaded preset '{}'", activePresetName);
	} catch (const std::exception& e) {
		logger::error("[FPInertia] Failed to load preset: {}", e.what());
	}
}

void InertiaPresets::SaveSpecificWeaponPreset(const std::string& a_editorID)
{
	std::shared_lock lock(presetMutex);
	auto it = specificWeaponSettings.find(a_editorID);
	if (it == specificWeaponSettings.end()) return;

	try {
		auto path = GetSpecificWeaponPresetPath(a_editorID);
		auto absPath = std::filesystem::absolute(path);
		logger::info("[FPInertia] Saving weapon preset '{}' to: {}", a_editorID, absPath.string());
		std::filesystem::create_directories(path.parent_path());
		std::ofstream file(path);
		if (!file.is_open()) {
			logger::error("[FPInertia] Failed to open file for writing: {}", absPath.string());
			return;
		}

		json wrapper;
		auto overrideIt = weaponTypeOverrides.find(a_editorID);
		if (overrideIt != weaponTypeOverrides.end())
			wrapper["weaponType"] = GetWeaponTypeName(overrideIt->second);
		wrapper["settings"] = it->second;
		file << wrapper.dump(2);
		file.flush();
		file.close();
		logger::info("[FPInertia] Successfully saved weapon preset '{}' (type={}, size={} bytes)",
			a_editorID,
			overrideIt != weaponTypeOverrides.end() ? GetWeaponTypeName(overrideIt->second) : "inherited",
			std::filesystem::file_size(path));
	} catch (const std::exception& e) {
		logger::error("[FPInertia] Failed to save weapon preset '{}': {}", a_editorID, e.what());
	}
}

void InertiaPresets::LoadSpecificWeaponPreset(const std::string& a_editorID)
{
	auto path = GetSpecificWeaponPresetPath(a_editorID);
	if (!std::filesystem::exists(path)) return;

	try {
		std::ifstream file(path);
		json j = json::parse(file);
		std::unique_lock lock(presetMutex);

		// New format: { "weaponType": "Pistol", "settings": {...} }
		// Legacy format: { ...WeaponInertiaSettings fields directly... }
		if (j.contains("settings") && j["settings"].is_object()) {
			specificWeaponSettings[a_editorID] = j["settings"].get<WeaponInertiaSettings>();
			if (j.contains("weaponType") && j["weaponType"].is_string()) {
				auto wt = ParseWeaponTypeName(j["weaponType"].get<std::string>());
				weaponTypeOverrides[a_editorID] = wt;
			}
		} else {
			specificWeaponSettings[a_editorID] = j.get<WeaponInertiaSettings>();
		}
	} catch (const std::exception& e) {
		logger::error("[FPInertia] Failed to load weapon preset '{}': {}", a_editorID, e.what());
	}
}

bool InertiaPresets::HasWeaponTypeOverride(const std::string& a_editorID) const
{
	std::shared_lock lock(presetMutex);
	return weaponTypeOverrides.contains(a_editorID);
}

WeaponType InertiaPresets::GetWeaponTypeOverride(const std::string& a_editorID) const
{
	std::shared_lock lock(presetMutex);
	auto it = weaponTypeOverrides.find(a_editorID);
	if (it != weaponTypeOverrides.end()) return it->second;
	return WeaponType::Rifle;
}

void InertiaPresets::SetWeaponTypeOverride(const std::string& a_editorID, WeaponType a_type)
{
	std::unique_lock lock(presetMutex);
	weaponTypeOverrides[a_editorID] = a_type;
	settingsVersion++;
}

void InertiaPresets::LoadAllPresets()
{
	LoadWeaponTypePresets();

	// Load all specific weapon presets in the Weapons subfolder
	auto weaponsDir = GetPresetFolderPath() / "Weapons";
	if (!std::filesystem::exists(weaponsDir)) return;

	for (auto& entry : std::filesystem::directory_iterator(weaponsDir)) {
		if (entry.path().extension() == ".json") {
			auto editorID = entry.path().stem().string();
			LoadSpecificWeaponPreset(editorID);
		}
	}
}

void InertiaPresets::ResetToINIValues()
{
	{
		std::unique_lock lock(presetMutex);
		weaponTypeSettings.clear();
		specificWeaponSettings.clear();
	}
	InitializeDefaultSettings();
	isDirty = true;
	logger::info("[FPInertia] Reset all presets to INI values");
}

// ============================================================
// Preset profile management
// ============================================================
std::vector<std::string> InertiaPresets::GetAvailablePresets() const
{
	std::vector<std::string> result;
	auto folder = GetPresetFolderPath();
	if (!std::filesystem::exists(folder)) return result;

	for (auto& entry : std::filesystem::directory_iterator(folder)) {
		if (entry.is_regular_file() && entry.path().extension() == ".json") {
			result.push_back(entry.path().stem().string());
		}
	}
	std::sort(result.begin(), result.end());
	return result;
}

void InertiaPresets::SetActivePreset(const std::string& a_name)
{
	if (a_name == activePresetName) return;
	activePresetName = a_name;
	{ std::unique_lock lock(presetMutex); weaponTypeSettings.clear(); customWeaponTypeSettings.clear(); }
	LoadWeaponTypePresets();
	if (weaponTypeSettings.empty()) InitializeDefaultSettings();
	EnsureCustomTypesInPreset();
	SaveActivePresetSetting();
	IncrementSettingsVersion();
	logger::info("[FPInertia] Active preset set to '{}'", a_name);
}

void InertiaPresets::CreateNewPreset(const std::string& a_name)
{
	activePresetName = a_name;
	SaveWeaponTypePresets();
	SaveActivePresetSetting();
	IncrementSettingsVersion();
}

void InertiaPresets::DuplicatePreset(const std::string& a_src, const std::string& a_dst)
{
	try {
		std::filesystem::copy_file(GetPresetPath(a_src), GetPresetPath(a_dst),
			std::filesystem::copy_options::overwrite_existing);
	} catch (const std::exception& e) {
		logger::error("[FPInertia] Failed to duplicate preset: {}", e.what());
	}
}

void InertiaPresets::DeletePreset(const std::string& a_name)
{
	try { std::filesystem::remove(GetPresetPath(a_name)); } catch (...) {}
}

void InertiaPresets::RenamePreset(const std::string& a_old, const std::string& a_new)
{
	try { std::filesystem::rename(GetPresetPath(a_old), GetPresetPath(a_new)); } catch (...) {}
	if (activePresetName == a_old) { activePresetName = a_new; SaveActivePresetSetting(); }
}

void InertiaPresets::SaveActivePresetSetting()
{
	CSimpleIniA ini; ini.SetUnicode();
	ini.LoadFile(Settings::kSettingsPath);
	ini.SetValue("Presets", "sActivePreset", activePresetName.c_str());
	ini.SaveFile(Settings::kSettingsPath);
}

void InertiaPresets::LoadActivePresetSetting()
{
	CSimpleIniA ini; ini.SetUnicode();
	if (ini.LoadFile(Settings::kSettingsPath) >= 0) {
		const char* val = ini.GetValue("Presets", "sActivePreset", "WeaponTypes");
		if (val) activePresetName = val;
	}
}

// ============================================================
// Settings accessors
// ============================================================
const WeaponInertiaSettings& InertiaPresets::GetWeaponTypeSettings(WeaponType a_type) const
{
	std::shared_lock lock(presetMutex);
	auto it = weaponTypeSettings.find(a_type);
	if (it != weaponTypeSettings.end()) return it->second;
	return defaultSettings;
}

WeaponInertiaSettings& InertiaPresets::GetWeaponTypeSettingsMutable(WeaponType a_type)
{
	std::unique_lock lock(presetMutex);
	return weaponTypeSettings[a_type];
}

const WeaponInertiaSettings* InertiaPresets::GetSpecificWeaponSettings(const std::string& a_editorID) const
{
	std::shared_lock lock(presetMutex);
	auto it = specificWeaponSettings.find(a_editorID);
	if (it != specificWeaponSettings.end()) return &it->second;
	return nullptr;
}

WeaponInertiaSettings& InertiaPresets::GetOrCreateSpecificWeaponSettings(
	const std::string& a_editorID, WeaponType a_baseType)
{
	std::unique_lock lock(presetMutex);
	auto it = specificWeaponSettings.find(a_editorID);
	if (it != specificWeaponSettings.end()) return it->second;
	// Create from type defaults
	auto typeIt = weaponTypeSettings.find(a_baseType);
	if (typeIt != weaponTypeSettings.end())
		specificWeaponSettings[a_editorID] = typeIt->second;
	else
		specificWeaponSettings[a_editorID] = defaultSettings;
	return specificWeaponSettings[a_editorID];
}

bool InertiaPresets::HasSpecificWeaponSettings(const std::string& a_editorID) const
{
	std::shared_lock lock(presetMutex);
	return specificWeaponSettings.count(a_editorID) > 0;
}

void InertiaPresets::RemoveSpecificWeaponSettings(const std::string& a_editorID)
{
	std::unique_lock lock(presetMutex);
	specificWeaponSettings.erase(a_editorID);
	try { std::filesystem::remove(GetSpecificWeaponPresetPath(a_editorID)); } catch (...) {}
}

std::vector<std::string> InertiaPresets::GetSavedSpecificWeaponPresets() const
{
	std::shared_lock lock(presetMutex);
	std::vector<std::string> result;
	result.reserve(specificWeaponSettings.size());
	for (auto& [k, _] : specificWeaponSettings) result.push_back(k);
	std::sort(result.begin(), result.end());
	return result;
}

const WeaponInertiaSettings* InertiaPresets::GetCustomWeaponTypeSettings(const std::string& a_name) const
{
	std::shared_lock lock(presetMutex);
	auto it = customWeaponTypeSettings.find(a_name);
	if (it != customWeaponTypeSettings.end()) return &it->second;
	return nullptr;
}

WeaponInertiaSettings& InertiaPresets::GetCustomWeaponTypeSettingsMutable(const std::string& a_name)
{
	std::unique_lock lock(presetMutex);
	return customWeaponTypeSettings[a_name];
}

bool InertiaPresets::IsCustomWeaponType(const std::string& a_name) const
{
	return std::find(customWeaponTypeNames.begin(), customWeaponTypeNames.end(), a_name)
	       != customWeaponTypeNames.end();
}

const WeaponInertiaSettings& InertiaPresets::GetWeaponSettings(
	const std::string& a_editorID, WeaponType a_type) const
{
	if (!a_editorID.empty()) {
		if (auto* s = GetSpecificWeaponSettings(a_editorID)) return *s;
	}
	return GetWeaponTypeSettings(a_type);
}

const WeaponInertiaSettings& InertiaPresets::GetWeaponSettingsWithKeywords(
	const std::string& a_editorID, RE::TESObjectWEAP* a_weapon, WeaponType a_type) const
{
	// Priority 1: specific weapon by EditorID
	if (!a_editorID.empty()) {
		if (auto* s = GetSpecificWeaponSettings(a_editorID)) return *s;
	}
	// Priority 2: keyword-based custom type
	if (a_weapon) {
		std::string customType = GetBestKeywordMatch(a_weapon);
		if (!customType.empty()) {
			if (auto* s = GetCustomWeaponTypeSettings(customType)) return *s;
		}
	}
	// Priority 3: standard weapon type
	return GetWeaponTypeSettings(a_type);
}

// ============================================================
// Keyword mappings
// ============================================================
void InertiaPresets::LoadKeywordMappings()
{
	auto folder = GetKeywordMappingsFolderPath();
	if (!std::filesystem::exists(folder)) return;

	std::vector<KeywordMapping> newMappings;
	std::set<std::string> typeNames;

	for (auto& entry : std::filesystem::directory_iterator(folder)) {
		if (entry.path().extension() != ".txt") continue;

		std::ifstream file(entry.path());
		std::string line;
		while (std::getline(file, line)) {
			// Skip comments and blank lines
			if (line.empty() || line[0] == ';' || line[0] == '#') continue;

			auto eqPos = line.find('=');
			if (eqPos == std::string::npos) continue;

			std::string keywordsStr = line.substr(0, eqPos);
			std::string typeName    = line.substr(eqPos + 1);

			// Trim whitespace
			auto trim = [](std::string& s) {
				s.erase(0, s.find_first_not_of(" \t\r\n"));
				s.erase(s.find_last_not_of(" \t\r\n") + 1);
			};
			trim(keywordsStr); trim(typeName);
			if (keywordsStr.empty() || typeName.empty()) continue;

			KeywordMapping mapping;
			mapping.weaponTypeName = typeName;

			// Parse comma-separated keywords
			std::istringstream ss(keywordsStr);
			std::string kw;
			while (std::getline(ss, kw, ',')) {
				trim(kw);
				if (!kw.empty()) mapping.keywords.push_back(kw);
			}

			if (!mapping.keywords.empty()) {
				newMappings.push_back(std::move(mapping));
				typeNames.insert(typeName);
			}
		}
	}

	// Sort by specificity (more keywords = higher priority)
	std::sort(newMappings.begin(), newMappings.end());

	std::unique_lock lock(presetMutex);
	keywordMappings = std::move(newMappings);
	customWeaponTypeNames.assign(typeNames.begin(), typeNames.end());

	logger::info("[FPInertia] Loaded {} keyword mappings, {} custom types",
		keywordMappings.size(), customWeaponTypeNames.size());
}

void InertiaPresets::ResolveKeywordPointers()
{
	std::unique_lock lock(presetMutex);
	for (auto& mapping : keywordMappings) {
		if (mapping.keywordsResolved) continue;
		mapping.resolvedKeywords.clear();
		for (auto& kwName : mapping.keywords) {
			auto* kw = RE::TESForm::GetFormByEditorID<RE::BGSKeyword>(kwName.c_str());
			if (kw) {
				mapping.resolvedKeywords.push_back(kw);
			} else {
				logger::warn("[FPInertia] Keyword not found: '{}'", kwName);
				mapping.resolvedKeywords.push_back(nullptr);
			}
		}
		mapping.keywordsResolved = true;
	}
}

std::string InertiaPresets::GetBestKeywordMatch(RE::TESObjectWEAP* a_weapon) const
{
	if (!a_weapon) return "";
	std::shared_lock lock(presetMutex);
	for (auto& mapping : keywordMappings) {
		if (!mapping.keywordsResolved) continue;
		bool allMatch = true;
		for (auto* kw : mapping.resolvedKeywords) {
			if (!kw || !a_weapon->HasKeyword(kw, nullptr)) { allMatch = false; break; }
		}
		if (allMatch) return mapping.weaponTypeName;
	}
	return "";
}

void InertiaPresets::EnsureCustomTypesInPreset()
{
	std::unique_lock lock(presetMutex);
	for (auto& name : customWeaponTypeNames) {
		if (customWeaponTypeSettings.find(name) == customWeaponTypeSettings.end()) {
			customWeaponTypeSettings[name] = defaultSettings;
		}
	}
}
