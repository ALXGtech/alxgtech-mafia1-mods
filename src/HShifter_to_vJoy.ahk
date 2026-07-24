#Requires AutoHotkey v2.0
Persistent

; ============================================================
; Mafia: The City of Lost Heaven
; H-Shifter -> Sequential Gearbox Translator (via virtual joystick)
;
; Physical H-shifter = Joystick 3
; Virtual output      = vJoy Device 1, Button 1 (Up) / Button 2 (Down)
;
; Button 1 = 1st gear
; Button 2 = 2nd gear
; Button 3 = 3rd gear
; Button 4 = 4th gear
; Button 5 = 5th gear
; Button 6 = 6th gear
; Button 7 = Hard reset: spam down to reverse, then up to neutral
; Button 8 = Reverse:    spam down to reverse (brute-force, no gear tracking)
;
; No button pressed for 1 second = auto-neutral (brute-force via reverse, same as Button 7)
;
; WHY THIS EXISTS:
; Mafia (2002) reads real DirectInput joystick devices directly.
; It does NOT respond to synthetic keyboard events sent via
; Send/SendInput/SendEvent (those are injected input, and this
; engine's controller polling ignores them). vJoy creates an
; actual virtual DirectInput joystick that Windows and the game
; treat exactly like a real controller, so button presses sent
; to it are recognized in-game. Bind the game's "shift up" /
; "shift down" actions to vJoy Device 1, Button 1 / Button 2.
; ============================================================

; ---- Config ----
PhysicalJoystick := 3   ; your real H-shifter
vJoyDeviceID := 1       ; vJoy device number configured in vJoyConf
vJoyUpButton := 1       ; button on vJoy device mapped to "Gear Up" in-game
vJoyDownButton := 2     ; button on vJoy device mapped to "Gear Down" in-game

ShiftDelay := 40                ; ms between sequential button presses
ButtonPulse := 30               ; ms the vJoy button stays "pressed"
NeutralDelay := 600             ; ms of no-gear before auto-neutral (forward shifts)
ReverseNeutralDelay := 1500     ; ms of no-gear before auto-neutral when last gear was R
                                ; (long traverse R -> 1st must not trip auto-neutral midway)
MaxShiftDownToReverse := 8      ; gear 6 needs 7 downs to reach reverse; 8 gives margin

; ---- vJoy DLL ----
vJoyDLL := "C:\Program Files\vJoy\x64\vJoyInterface.dll"

if !DllCall("LoadLibrary", "Str", vJoyDLL, "Ptr")
{
    MsgBox("Could not load vJoyInterface.dll from: " vJoyDLL, "Error", 16)
    ExitApp()
}

if !DllCall(vJoyDLL . "\vJoyEnabled", "Int")
{
    MsgBox("vJoy driver not detected. Install/enable vJoy first.", "Error", 16)
    ExitApp()
}

vjStatus := DllCall(vJoyDLL . "\GetVJDStatus", "UInt", vJoyDeviceID, "Int")
if (vjStatus != 0 && vjStatus != 1)  ; not OWN and not FREE
{
    MsgBox("vJoy device " vJoyDeviceID " is unavailable (status=" vjStatus ", 2=BUSY by another app, 3=MISSING). Check vJoyConf.", "Error", 16)
    ExitApp()
}

if !DllCall(vJoyDLL . "\AcquireVJD", "UInt", vJoyDeviceID, "Int")
{
    MsgBox("Could not acquire vJoy device " vJoyDeviceID ". Check vJoyConf.", "Error", 16)
    ExitApp()
}

; ---- Internal State ----
CurrentGear := 0       ; -1=Reverse, 0=Neutral, 1-6=Forward gears (used only by ShiftTo for diff calculation)
LastRealGear := 0      ; last non-neutral position engaged (-1=R, 1-6). Picks the neutral grace window.
LastTarget := -999
NeutralTimerRunning := false

SetTimer(CheckShifter, 10)

; ============================================================
; Main Polling Loop
; ============================================================

CheckShifter()
{
    global LastTarget, NeutralTimerRunning, NeutralDelay, ReverseNeutralDelay, LastRealGear

    target := DetectGear()

    if (target = 0)
    {
        LastTarget := 0
        if (!NeutralTimerRunning)
        {
            NeutralTimerRunning := true
            ; long grace if we just left reverse (R -> 1st is a long lever traverse),
            ; short grace otherwise (skip neutral on 1<->2<->3 forward shifts)
            delay := (LastRealGear = -1) ? ReverseNeutralDelay : NeutralDelay
            SetTimer(NeutralTimeout, -delay)
        }
        return
    }

    SetTimer(NeutralTimeout, 0)
    NeutralTimerRunning := false

    if (target = LastTarget)
        return

    LastTarget := target

    switch target
    {
        case 99: ResetViaReverse()   ; Joy7 - hard reset through reverse to neutral
        case -1: GoToReverse()       ; Joy8 - brute-force to reverse
        default: ShiftTo(target)     ; gears 1-6
    }
}

; ============================================================
; Read H-Shifter
; ============================================================

DetectGear()
{
    global PhysicalJoystick

    if GetKeyState(PhysicalJoystick "Joy1")
        return 1
    if GetKeyState(PhysicalJoystick "Joy2")
        return 2
    if GetKeyState(PhysicalJoystick "Joy3")
        return 3
    if GetKeyState(PhysicalJoystick "Joy4")
        return 4
    if GetKeyState(PhysicalJoystick "Joy5")
        return 5
    if GetKeyState(PhysicalJoystick "Joy6")
        return 6
    if GetKeyState(PhysicalJoystick "Joy7")
        return 99
    if GetKeyState(PhysicalJoystick "Joy8")
        return -1

    return 0
}

; ============================================================
; Pulse a button on the virtual joystick
; ============================================================

PressVJoyButton(btnNum)
{
    global vJoyDLL, vJoyDeviceID, ButtonPulse

    DllCall(vJoyDLL . "\SetBtn", "Int", 1, "UInt", vJoyDeviceID, "UChar", btnNum, "Int")
    Sleep ButtonPulse
    DllCall(vJoyDLL . "\SetBtn", "Int", 0, "UInt", vJoyDeviceID, "UChar", btnNum, "Int")
}

; ============================================================
; Move Game Gearbox To Target Gear (trusts CurrentGear)
; ============================================================

ShiftTo(Target)
{
    global CurrentGear, vJoyUpButton, vJoyDownButton, ShiftDelay, LastRealGear
    Critical   ; atomic press-sequence: the 10ms poller can't interrupt mid-shift

    LastRealGear := Target   ; forward gear engaged; next neutral gap uses the short grace

    if (Target = CurrentGear)
        return

    diff := Target - CurrentGear

    if (diff > 0)
    {
        Loop diff
        {
            PressVJoyButton(vJoyUpButton)
            Sleep ShiftDelay
        }
    }
    else
    {
        Loop Abs(diff)
        {
            PressVJoyButton(vJoyDownButton)
            Sleep ShiftDelay
        }
    }

    CurrentGear := Target

    ToolTip(
        CurrentGear = -1
            ? "Gear: Reverse"
            : CurrentGear = 0
                ? "Gear: Neutral"
                : "Gear: " CurrentGear
    )
    SetTimer(() => ToolTip(), -1000)
}

; ============================================================
; Brute-Force To Reverse (Joy8)
; Spams shift-down regardless of CurrentGear, always lands at reverse.
; ============================================================

GoToReverse()
{
    global CurrentGear, vJoyDownButton, ShiftDelay, MaxShiftDownToReverse
    global NeutralTimerRunning, LastRealGear
    Critical   ; atomic down-spam: the 10ms poller can't interrupt mid-sequence

    ; R preempts: kill any pending auto-neutral so it can't fire around reverse
    SetTimer(NeutralTimeout, 0)
    NeutralTimerRunning := false
    LastRealGear := -1       ; next neutral gap (leaving R) uses the long grace

    Loop MaxShiftDownToReverse
    {
        PressVJoyButton(vJoyDownButton)
        Sleep ShiftDelay
    }

    CurrentGear := -1
    ToolTip("Gear: Reverse")
    SetTimer(() => ToolTip(), -1000)
}

; ============================================================
; Hard Reset Via Reverse (Joy7 / F10)
; Spams down to reverse, then one up to neutral.
; Safe from any gear - does not trust CurrentGear.
; ============================================================

; rAware=true (auto-neutral path): if the lever reached R during the down-spam, stay in
; reverse instead of finishing up to neutral. rAware=false (Joy7/F10): always land neutral.
ResetViaReverse(rAware := false)
{
    global CurrentGear, vJoyUpButton, vJoyDownButton, ShiftDelay, MaxShiftDownToReverse, LastRealGear, LastTarget
    Critical   ; atomic sequence: the 10ms poller can't interrupt mid-reset

    Loop MaxShiftDownToReverse
    {
        PressVJoyButton(vJoyDownButton)
        Sleep ShiftDelay
    }

    ; R-aware: the down-spam already landed the game in reverse. If the physical lever
    ; is now on R, that IS what you want - skip the return-up, stay in reverse.
    if (rAware && DetectGear() = -1)
    {
        CurrentGear := -1
        LastRealGear := -1       ; exit from R uses the long grace
        LastTarget := -1         ; already on R - stop CheckShifter re-firing GoToReverse
        ToolTip("Gear: Reverse")
        SetTimer(() => ToolTip(), -1000)
        return
    }

    PressVJoyButton(vJoyUpButton)

    CurrentGear := 0
    ToolTip("Gear: Neutral (reset)")
    SetTimer(() => ToolTip(), -1500)
}

; ============================================================
; Automatic Neutral After Delay (trusts CurrentGear)
; ============================================================

NeutralTimeout()
{
    global NeutralTimerRunning
    NeutralTimerRunning := false

    if (DetectGear() = 0)
    {
        NeutralTimerRunning := true  ; block CheckShifter from queuing a new timer during the reset's Sleep calls
        ResetViaReverse(true)        ; R-aware: land in reverse if the lever reached R mid-reset
    }
}

; ============================================================
; Emergency Hotkeys
; ============================================================

F9::
{
    global CurrentGear
    MsgBox("Current internal gear: " CurrentGear)
}

F10::
{
    ResetViaReverse()
}

OnExit(ReleaseVJoy)
ReleaseVJoy(*)
{
    global vJoyDLL, vJoyDeviceID
    DllCall(vJoyDLL . "\RelinquishVJD", "UInt", vJoyDeviceID)
}
