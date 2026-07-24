# Launch Mafia with the H-shifter script.
#
# Starts the AutoHotkey script, waits a moment, launches the game, and stops the
# script again when the game exits. Edit the four paths below to match your machine.
#
# Run it by double-clicking "Launch Mafia with Shifter.bat" next to this file.

# ---- Edit these ----------------------------------------------------------------------
$gamePath  = 'C:\GOG Games\Mafia\Game.exe'
$ahkExe    = 'C:\Program Files\AutoHotkey\v2\AutoHotkey64.exe'
$ahkScript = Join-Path (Split-Path -Parent $PSScriptRoot) 'src\HShifter_to_vJoy.ahk'
# --------------------------------------------------------------------------------------

$gameDir = Split-Path -Parent $gamePath

function Fail ($msg) {
    Write-Host ""
    Write-Host "ERROR: $msg"
    Write-Host ""
    Read-Host "Press Enter to close"
    exit 1
}

if (-not (Test-Path $gamePath))  { Fail "Game not found at $gamePath. Edit `$gamePath at the top of this script." }
if (-not (Test-Path $ahkExe))    { Fail "AutoHotkey v2 not found at $ahkExe. Install it, or edit `$ahkExe." }
if (-not (Test-Path $ahkScript)) { Fail "Shifter script not found at $ahkScript." }

$ahkProc = $null
try {
    $ahkProc = Start-Process $ahkExe -ArgumentList "`"$ahkScript`"" -PassThru
    Write-Host "H-shifter script started (PID $($ahkProc.Id))"

    Start-Sleep -Seconds 2

    Write-Host "Launching Mafia..."
    $gameProc = Start-Process $gamePath -WorkingDirectory $gameDir -PassThru
    $gameProc.WaitForExit()
    Write-Host "Mafia closed."
}
finally {
    # Stop the script even if the launch failed or the window was closed. A shifter
    # script left running keeps holding the vJoy device.
    if ($ahkProc -and -not $ahkProc.HasExited) {
        try { Stop-Process -Id $ahkProc.Id -Force -ErrorAction Stop } catch { }
        Write-Host "H-shifter script stopped."
    }
    Start-Sleep -Milliseconds 1500
}
