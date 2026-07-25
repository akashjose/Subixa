# Click at window-relative (X,Y) in the running player and optionally type text,
# so a UI path can be driven end to end from WSL:
#
#   powershell.exe -NoProfile -ExecutionPolicy Bypass \
#       -File $(wslpath -w tools/wsl-input.ps1) -X 968 -Y 111 -Text 'albatross'
#
# Coordinates are measured from the window frame, which is exactly the origin of
# the image wsl-screenshot.ps1 produces -- read them straight off a screenshot.
#
# This moves the real cursor and clicks wherever it lands, so the player must
# genuinely be the foreground window first. Windows refuses focus changes asked
# for by a background process, and the earlier version ignored that: when
# SetForegroundWindow lost, the click went into whatever app was on top instead.
# Foreground is now forced, verified, and the script aborts rather than clicking
# into someone else's window.

# A modal dialog becomes the process's main window, so while one is open the
# player title matches nothing -- pass -Title 'Open media' to reach the dialog.
param([int]$X, [int]$Y, [string]$Text = "", [string]$Title = "custom media player")

Add-Type -AssemblyName System.Windows.Forms
Add-Type @"
using System;
using System.Runtime.InteropServices;
public class Inp {
  [DllImport("user32.dll")] public static extern bool SetForegroundWindow(IntPtr hWnd);
  [DllImport("user32.dll")] public static extern bool ShowWindow(IntPtr hWnd, int nCmdShow);
  [DllImport("user32.dll")] public static extern bool IsIconic(IntPtr hWnd);
  [DllImport("user32.dll")] public static extern IntPtr GetForegroundWindow();
  [DllImport("user32.dll")] public static extern bool BringWindowToTop(IntPtr hWnd);
  [DllImport("user32.dll")] public static extern bool SetCursorPos(int x, int y);
  [DllImport("user32.dll")] public static extern void mouse_event(uint f, int dx, int dy, uint d, IntPtr e);
  [DllImport("user32.dll")] public static extern void keybd_event(byte vk, byte scan, uint flags, IntPtr extra);
  [DllImport("dwmapi.dll")] public static extern int DwmGetWindowAttribute(IntPtr hwnd, int attr, out RECT rect, int size);
  [StructLayout(LayoutKind.Sequential)] public struct RECT { public int Left, Top, Right, Bottom; }
}
"@

$p = Get-Process | Where-Object { $_.MainWindowTitle -like "*$Title*" } | Select-Object -First 1
if (-not $p) { Write-Output "NOWINDOW ($Title)"; exit 1 }
$h = $p.MainWindowHandle

# SW_RESTORE only when actually minimised: on a *maximised* window it un-maximises,
# which silently destroys the one state a fullscreen/wide-window test is checking --
# and then these window-relative coordinates point outside the shrunken window.
if ([Inp]::IsIconic($h)) { [Inp]::ShowWindow($h, 9) | Out-Null }       # SW_RESTORE
for ($try = 0; $try -lt 3 -and [Inp]::GetForegroundWindow() -ne $h; $try++) {
    # The synthetic ALT press lifts the foreground lock; without it
    # SetForegroundWindow silently does nothing for a background caller.
    # Only when the window is not already ours -- see the keyup note below.
    [Inp]::keybd_event(0x12, 0, 0, [IntPtr]::Zero)
    [Inp]::keybd_event(0x12, 0, 2, [IntPtr]::Zero)                     # KEYEVENTF_KEYUP
    [Inp]::BringWindowToTop($h) | Out-Null
    [Inp]::SetForegroundWindow($h) | Out-Null
    Start-Sleep -Milliseconds 600
}
if ([Inp]::GetForegroundWindow() -ne $h) {
    Write-Output "NOTFOREGROUND (refusing to click, another window is on top)"
    exit 1
}

# The ALT keyup above lands in the *old* foreground window, so the app can be
# left believing ALT is still held -- and then every -Text character arrives as
# an ALT accelerator (Alt+A, Alt+L, ...) which a QML text field simply ignores.
# Symptom is a click that clearly worked next to typing that vanished. Release
# it again now that the player has focus.
[Inp]::keybd_event(0x12, 0, 2, [IntPtr]::Zero)                         # KEYEVENTF_KEYUP
Start-Sleep -Milliseconds 120

$r = New-Object Inp+RECT
[Inp]::DwmGetWindowAttribute($h, 9, [ref]$r, 16) | Out-Null

[Inp]::SetCursorPos($r.Left + $X, $r.Top + $Y) | Out-Null
Start-Sleep -Milliseconds 200
[Inp]::mouse_event(0x0002, 0, 0, 0, [IntPtr]::Zero)   # LEFTDOWN
Start-Sleep -Milliseconds 60
[Inp]::mouse_event(0x0004, 0, 0, 0, [IntPtr]::Zero)   # LEFTUP
Start-Sleep -Milliseconds 300

if ($Text -ne "") {
  [System.Windows.Forms.SendKeys]::SendWait($Text)
  Start-Sleep -Milliseconds 500
}
Write-Output "clicked $($r.Left + $X),$($r.Top + $Y) text='$Text'"
