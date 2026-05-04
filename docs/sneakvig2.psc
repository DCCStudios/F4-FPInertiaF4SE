;/ Decompiled by Champollion V1.0.6
PEX format v3.9 GameID: 2
Source   : C:\program files (x86)\steam\steamapps\common\Fallout 4\Data\Scripts\Source\User\sneakvig2.psc
Modified : 2023-02-21 15:21:53
Compiled : 2023-02-21 15:22:03
User     : This PC
Computer : DESKTOP-UP9FJB4
/;
ScriptName sneakvig2 extends activemagiceffect

;-- Properties --------------------------------------
Actor Property PlayerRef Auto Const
VisualEffect Property VFXA Auto Const
VisualEffect Property VFXB Auto Const
bool Property bUseCamFX = False Auto Const
bool Property bUseEndEvent = True Auto Const
bool Property bUseSwap = True Auto Const
ImageSpaceModifier Property ImgSpaceMod Auto Const
bool Property bUseImageSpaceMod = False Auto Const
bool Property bUseEndEffect = True Auto Const
bool Property bUseInOutImgSpaceMod = False Auto Const
bool Property bUseFormList = False Auto Const
float Property fImgsPaceModIntensity = 1 Auto Const

;-- Variables ---------------------------------------
bool isVFXA = False
bool isVFXB = False

;-- Functions ---------------------------------------

Event OnMenuOpenCloseEvent(string asMenuName, bool abOpening)
	If (asMenuName == "PipboyMenu")
		If (abOpening)
			If (bUseCamFX)
				Self.HardStopVFX()
			EndIf
		ElseIf (bUseCamFX)
			Self.ToggleVFX()
			Self.RegisterEvents()
		EndIf
	EndIf
	If (asMenuName == "LooksMenu")
		If (abOpening)
			Self.RegisterEvents()
		Else
			Self.RegisterEvents()
		EndIf
	EndIf
EndEvent

Function RegisterEvents()
	Self.RegisterForMenuOpenCloseEvent("PipboyMenu")
	Self.RegisterForMenuOpenCloseEvent("LooksMenu")
	Self.RegisterForPlayerTeleport()
EndFunction

Event OnPlayerTeleport()
	If (bUseCamFX)
		Self.ToggleVFX()
		Self.RegisterEvents()
	EndIf
EndEvent

Function ModImgs(bool Remove)
	If (bUseInOutImgSpaceMod == False)
		If (Remove)
			If (bUseFormList == False)
				ImgSpaceMod.Remove()
			EndIf
		Else
			ImgSpaceMod.Remove()
			ImgSpaceMod.Apply(fImgsPaceModIntensity)
		EndIf
	EndIf
EndFunction

Event OnEffectFinish(Actor target, Actor Caster)
	If (bUseCamFX && bUseEndEvent)
		Self.HardStopVFX()
		isVFXA = False
		isVFXB = False
	EndIf
	If (bUseImageSpaceMod && bUseEndEffect)
		Self.ModImgs(True)
	EndIf
EndEvent

Event OnEffectStart(Actor target, Actor Caster)
	If (bUseCamFX)
		VFXA.Play(PlayerRef as ObjectReference, -1, None)
		isVFXA = True
		isVFXB = False
		Self.RegisterEvents()
	EndIf
	If (bUseImageSpaceMod)
		Self.ModImgs(False)
	EndIf
EndEvent

Function HardStopVFX()
	VFXA.Stop(PlayerRef as ObjectReference)
	VFXB.Stop(PlayerRef as ObjectReference)
EndFunction

Function ToggleVFX()
	If (bUseSwap)
		If (isVFXB && !isVFXA)
			VFXB.Stop(PlayerRef as ObjectReference)
			VFXA.Play(PlayerRef as ObjectReference, -1, None)
			isVFXA = True
			isVFXB = False
		ElseIf (isVFXA && !isVFXB)
			VFXA.Stop(PlayerRef as ObjectReference)
			VFXB.Play(PlayerRef as ObjectReference, -1, None)
			isVFXA = False
			isVFXB = True
		EndIf
	Else
		VFXA.Play(PlayerRef as ObjectReference, -1, None)
	EndIf
EndFunction
