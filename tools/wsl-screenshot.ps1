# Screenshot the running player from the Windows side.
#
#   powershell.exe -NoProfile -ExecutionPolicy Bypass \
#       -File $(wslpath -w tools/wsl-screenshot.ps1) 'C:\Users\<you>\AppData\Local\Temp\shot.png'
#
# then read it from WSL at /mnt/c/... . WSLg windows are Wayland surfaces hosted
# by msrdc, so an XWayland root grab (ffmpeg -f x11grab) returns black -- this
# goes through the Win32 desktop instead, which does see them.
#
# Captures with PrintWindow(PW_RENDERFULLCONTENT), which reads the window's own
# pixels regardless of z-order and without stealing focus. That matters: the
# earlier CopyFromScreen version grabbed a screen *region*, so whenever
# SetForegroundWindow lost -- Windows refuses focus changes requested by a
# background process -- it silently saved whichever window was sitting on top.
# A capture of someone else's app reads as a render bug in this one.
#
# CopyFromScreen is kept as a fallback for the case where PrintWindow comes back
# blank, and it now refuses to fire unless the player really is foreground.

# A modal dialog becomes the process's main window, so while one is open the
# player title matches nothing -- pass -Title 'Open media' to reach the dialog.
# $Path stays positional so existing callers that pass only an output file keep
# working -- declaring any param() at all is what stops it landing in $args.
param([Parameter(Mandatory=$true, Position=0)][string]$Path,
      [string]$Title = "Subixa")

Add-Type -AssemblyName System.Drawing
Add-Type @"
using System;
using System.Text;
using System.Runtime.InteropServices;
public class Win32 {
  [DllImport("user32.dll")] public static extern bool SetForegroundWindow(IntPtr hWnd);
  [DllImport("user32.dll")] public static extern bool ShowWindow(IntPtr hWnd, int nCmdShow);
  [DllImport("user32.dll")] public static extern bool IsIconic(IntPtr hWnd);
  [DllImport("user32.dll")] public static extern IntPtr GetForegroundWindow();
  [DllImport("user32.dll")] public static extern bool BringWindowToTop(IntPtr hWnd);
  [DllImport("user32.dll")] public static extern bool PrintWindow(IntPtr hWnd, IntPtr hdc, uint flags);
  [DllImport("user32.dll")] public static extern void keybd_event(byte vk, byte scan, uint flags, IntPtr extra);
  [DllImport("dwmapi.dll")] public static extern int DwmGetWindowAttribute(IntPtr hwnd, int attr, out RECT rect, int size);
  [DllImport("user32.dll")] public static extern bool EnumWindows(EnumProc cb, IntPtr l);
  [DllImport("user32.dll")] public static extern int GetWindowText(IntPtr h, StringBuilder s, int m);
  [DllImport("user32.dll")] public static extern bool IsWindowVisible(IntPtr h);
  public delegate bool EnumProc(IntPtr h, IntPtr l);
  [StructLayout(LayoutKind.Sequential)] public struct RECT { public int Left, Top, Right, Bottom; }
}
"@

# Find the window by enumerating top-level windows rather than asking Get-Process
# for MainWindowTitle. Two reasons: a process has only one "main" window, so the
# detached subtitle browser was unreachable, and the match used to hit any process
# whose title happened to contain the text -- an editor with tst_subtitles.cpp open
# swallowed the clicks meant for the player. WSLg titles all end in "(<distro>)",
# which is what distinguishes a real app window from a Windows-side one.
$script:found = [IntPtr]::Zero
$script:foundTitle = ""
$cb = [Win32+EnumProc]{
    param($h, $l)
    if ([Win32]::IsWindowVisible($h)) {
        $sb = New-Object System.Text.StringBuilder 512
        [Win32]::GetWindowText($h, $sb, 512) | Out-Null
        $t = $sb.ToString()
        if ($t -like "*$Title*" -and $t -match '\(.+\)$') {
            # Prefer a window whose title *starts* with the requested text. With
            # the browser detached there are two matches -- "Subixa"
            # and "Subtitles - Subixa" -- and only this tells them
            # apart.
            if ($t.StartsWith($Title)) {
                $script:found = $h
                $script:foundTitle = $t
                return $false
            }
            if ($script:found -eq [IntPtr]::Zero) {
                $script:found = $h
                $script:foundTitle = $t
            }
        }
    }
    return $true
}
[Win32]::EnumWindows($cb, [IntPtr]::Zero) | Out-Null
if ($script:found -eq [IntPtr]::Zero) { Write-Output "NOWINDOW ($Title)"; exit 1 }
$h = $script:found

function Get-Bounds($hwnd) {
    $r = New-Object Win32+RECT
    [Win32]::DwmGetWindowAttribute($hwnd, 9, [ref]$r, 16) | Out-Null   # DWMWA_EXTENDED_FRAME_BOUNDS
    return $r
}

# A capture that is one flat colour everywhere is a failed grab, not a frame.
function Test-Blank($bmp) {
    $first = $bmp.GetPixel(0, 0).ToArgb()
    for ($y = 0; $y -lt $bmp.Height; $y += [Math]::Max(1, [int]($bmp.Height / 24))) {
        for ($x = 0; $x -lt $bmp.Width; $x += [Math]::Max(1, [int]($bmp.Width / 24))) {
            if ($bmp.GetPixel($x, $y).ToArgb() -ne $first) { return $false }
        }
    }
    return $true
}

$r = Get-Bounds $h
$w = $r.Right - $r.Left
$hgt = $r.Bottom - $r.Top
if ($w -le 0 -or $hgt -le 0) { Write-Output "BADRECT"; exit 1 }

# --- primary: PrintWindow, z-order independent, leaves focus alone
$bmp = New-Object System.Drawing.Bitmap $w, $hgt
$g = [System.Drawing.Graphics]::FromImage($bmp)
$hdc = $g.GetHdc()
$ok = [Win32]::PrintWindow($h, $hdc, 2)                                # PW_RENDERFULLCONTENT
$g.ReleaseHdc($hdc)

if ($ok -and -not (Test-Blank $bmp)) {
    $bmp.Save($Path, [System.Drawing.Imaging.ImageFormat]::Png)
    Write-Output "OK $w x $hgt (printwindow) -> $($Path)"
    exit 0
}
$bmp.Dispose()

# --- fallback: raise the window for real, then grab its screen region
# Restore only a minimised window -- SW_RESTORE would un-maximise a maximised one,
# so the capture would silently document a window size nobody asked for.
if ([Win32]::IsIconic($h)) { [Win32]::ShowWindow($h, 9) | Out-Null }   # SW_RESTORE
for ($try = 0; $try -lt 3; $try++) {
    # Synthesising an ALT press lifts the foreground lock that otherwise makes
    # SetForegroundWindow a no-op for a background caller.
    [Win32]::keybd_event(0x12, 0, 0, [IntPtr]::Zero)
    [Win32]::keybd_event(0x12, 0, 2, [IntPtr]::Zero)                   # KEYEVENTF_KEYUP
    [Win32]::BringWindowToTop($h) | Out-Null
    [Win32]::SetForegroundWindow($h) | Out-Null
    Start-Sleep -Milliseconds 600
    if ([Win32]::GetForegroundWindow() -eq $h) { break }
}
if ([Win32]::GetForegroundWindow() -ne $h) {
    # Grabbing the region now would save another window's pixels. Refuse.
    Write-Output "NOTFOREGROUND (printwindow blank and window would not raise)"
    exit 1
}

$r = Get-Bounds $h
$w = $r.Right - $r.Left
$hgt = $r.Bottom - $r.Top
$bmp = New-Object System.Drawing.Bitmap $w, $hgt
$g = [System.Drawing.Graphics]::FromImage($bmp)
$g.CopyFromScreen($r.Left, $r.Top, 0, 0, $bmp.Size)
$bmp.Save($Path, [System.Drawing.Imaging.ImageFormat]::Png)
Write-Output "OK $w x $hgt (screengrab) -> $($Path)"
