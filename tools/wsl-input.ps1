# Click at window-relative (X,Y) in the running player and optionally type text,
# so a UI path can be driven end to end from WSL:
#
#   powershell.exe -NoProfile -ExecutionPolicy Bypass \
#       -File $(wslpath -w tools/wsl-input.ps1) -X 968 -Y 111 -Text 'albatross'
#
# Coordinates are measured from the window frame, which is exactly the origin of
# the image wsl-screenshot.ps1 produces -- read them straight off a screenshot.

param([int]$X, [int]$Y, [string]$Text = "")

Add-Type -AssemblyName System.Windows.Forms
Add-Type @"
using System;
using System.Runtime.InteropServices;
public class Inp {
  [DllImport("user32.dll")] public static extern bool SetForegroundWindow(IntPtr hWnd);
  [DllImport("user32.dll")] public static extern bool SetCursorPos(int x, int y);
  [DllImport("user32.dll")] public static extern void mouse_event(uint f, int dx, int dy, uint d, IntPtr e);
  [DllImport("dwmapi.dll")] public static extern int DwmGetWindowAttribute(IntPtr hwnd, int attr, out RECT rect, int size);
  [StructLayout(LayoutKind.Sequential)] public struct RECT { public int Left, Top, Right, Bottom; }
}
"@

$p = Get-Process | Where-Object { $_.MainWindowTitle -like "*custom media player*" } | Select-Object -First 1
if (-not $p) { Write-Output "NOWINDOW"; exit 1 }
[Inp]::SetForegroundWindow($p.MainWindowHandle) | Out-Null
Start-Sleep -Milliseconds 500

$r = New-Object Inp+RECT
[Inp]::DwmGetWindowAttribute($p.MainWindowHandle, 9, [ref]$r, 16) | Out-Null

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
