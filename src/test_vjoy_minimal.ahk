#Requires AutoHotkey v2.0
Persistent

; Minimal isolated vJoy test - no shifter logic, just DLL load + acquire + toggle button 1

vJoyDLL := "C:\Program Files\vJoy\x64\vJoyInterface.dll"
vJoyDeviceID := 1

logFile := A_ScriptDir "\vjoy_test_log.txt"
if FileExist(logFile)
    FileDelete(logFile)

Log(msg) {
    global logFile
    FileAppend(FormatTime(A_Now, "HH:mm:ss") " - " msg "`n", logFile)
}

try
{
    Log("Script start. A_PtrSize=" A_PtrSize)

    hModule := DllCall("LoadLibrary", "Str", vJoyDLL, "Ptr")
    if !hModule
    {
        Log("LoadLibrary FAILED, A_LastError=" A_LastError)
        ExitApp()
    }
    Log("LoadLibrary OK, handle=" hModule)

    enabled := DllCall(vJoyDLL . "\vJoyEnabled", "Int")
    Log("vJoyEnabled returned: " enabled)

    status := DllCall(vJoyDLL . "\GetVJDStatus", "UInt", vJoyDeviceID, "Int")
    Log("GetVJDStatus(" vJoyDeviceID ") returned: " status " (0=OWN,1=FREE,2=BUSY,3=MISS)")

    Log("About to call AcquireVJD...")
    acquired := DllCall(vJoyDLL . "\AcquireVJD", "UInt", vJoyDeviceID, "Int")
    Log("AcquireVJD returned: " acquired)

    if (acquired)
    {
        Log("Toggling button 1 ON")
        r1 := DllCall(vJoyDLL . "\SetBtn", "Int", 1, "UInt", vJoyDeviceID, "UChar", 1, "Int")
        Log("SetBtn ON returned: " r1)
        Sleep 500
        Log("Toggling button 1 OFF")
        r2 := DllCall(vJoyDLL . "\SetBtn", "Int", 0, "UInt", vJoyDeviceID, "UChar", 1, "Int")
        Log("SetBtn OFF returned: " r2)

        DllCall(vJoyDLL . "\RelinquishVJD", "UInt", vJoyDeviceID)
        Log("Relinquished device")
    }

    Log("Script end - exiting in 2s")
}
catch as e
{
    Log("EXCEPTION: " e.Message " (what=" e.What ", extra=" e.Extra ")")
}

Sleep 2000
ExitApp()
