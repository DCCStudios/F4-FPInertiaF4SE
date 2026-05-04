;/ Decompiled by Champollion V1.0.6
PEX format v3.9 GameID: 2
Source   : C:\program files (x86)\steam\steamapps\common\Fallout 4\Data\Scripts\Source\User\sneakvig1.psc
Modified : 2023-02-20 22:15:24
Compiled : 2023-02-20 22:15:27
User     : This PC
Computer : DESKTOP-UP9FJB4
/;
ScriptName sneakvig1 extends Quest

;-- Properties --------------------------------------
GlobalVariable Property g0 Auto
Spell Property s0 Auto

;-- Variables ---------------------------------------
Actor PlayerRef

;-- Functions ---------------------------------------

Function Unregister()
	If (PlayerRef.Hasspell(s0 as Form) == True)
		PlayerRef.Removespell(s0)
	EndIf
EndFunction

Function MCM0()
	Self.Unregister()
	Self.stop()
	Self.start()
	Debug.MessageBox("Refresh")
EndFunction

Event OnInit()
	PlayerRef = Game.GetPlayer()
	Self.Register()
	Debug.trace("OnInit", 0)
EndEvent

Function MCM1()
	If (Self.IsRunning() == True)
		Self.Unregister()
		Self.stop()
		Debug.MessageBox("Disable")
	EndIf
EndFunction

Function MCM2()
	If (Self.IsRunning() == False)
		Self.start()
		Debug.MessageBox("Restart")
	EndIf
EndFunction

Function Register()
	If (PlayerRef.Hasspell(s0 as Form) == False)
		PlayerRef.Addspell(s0, False)
	EndIf
EndFunction
