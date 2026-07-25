# Screenshot the running player from the Windows side.
#
#   powershell.exe -NoProfile -ExecutionPolicy Bypass \
#       -File $(wslpath -w tools/wsl-screenshot.ps1) 'C:\Users\<you>\AppData\Local\Temp\shot.png'
#
# then read it from WSL at /mnt/c/... . WSLg windows are Wayland surfaces hosted
# by msrdc, so an XWayland root grab (ffmpeg -f x11grab) returns black -- this
# goes through the Win32 desktop instead, which does see them.

Add-Type -AssemblyName System.Drawing
Add-Type @"
using System;
using System.Runtime.InteropServices;
public class Win32 {
  [DllImport("user32.dll")] public static extern bool SetForegroundWindow(IntPtr hWnd);
  [DllImport("user32.dll")] public static extern bool ShowWindow(IntPtr hWnd, int nCmdShow);
  [DllImport("dwmapi.dll")] public static extern int DwmGetWindowAttribute(IntPtr hwnd, int attr, out RECT rect, int size);
  [StructLayout(LayoutKind.Sequential)] public struct RECT { public int Left, Top, Right, Bottom; }
}
"@

$p = Get-Process | Where-Object { $_.MainWindowTitle -like "*custom media player*" } | Select-Object -First 1
if (-not $p) { Write-Output "NOWINDOW"; exit 1 }

$h = $p.MainWindowHandle
[Win32]::ShowWindow($h, 9) | Out-Null   # SW_RESTORE
[Win32]::SetForegroundWindow($h) | Out-Null
Start-Sleep -Milliseconds 900

$r = New-Object Win32+RECT
[Win32]::DwmGetWindowAttribute($h, 9, [ref]$r, 16) | Out-Null   # DWMWA_EXTENDED_FRAME_BOUNDS
$w = $r.Right - $r.Left
$hgt = $r.Bottom - $r.Top
if ($w -le 0 -or $hgt -le 0) { Write-Output "BADRECT"; exit 1 }

$bmp = New-Object System.Drawing.Bitmap $w, $hgt
$g = [System.Drawing.Graphics]::FromImage($bmp)
$g.CopyFromScreen($r.Left, $r.Top, 0, 0, $bmp.Size)
$bmp.Save($args[0], [System.Drawing.Imaging.ImageFormat]::Png)
Write-Output "OK $w x $hgt -> $($args[0])"
