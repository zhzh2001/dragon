# Drive x99-windows' desktop from SSH: click, type, bring a window forward, capture.
#
#   tools/x99/desktop_act.ps1 -Steps "front:3DMark2001;click:348,472;wait:2;shot:a.png;burst:f,40,1"
#
# Run through tools/x99/run_interactive.ps1 (the console desktop, not SSH's session
# 0), each call with its own -Dir: a program an earlier call started inherits
# that call's log handle and keeps the file locked. Shots land in D:\scratch\3dm.
# GDI capture sees a D3D8 game running fullscreen on the X550, which is how the
# 3DMark2001 Dragothic reference frames were taken (docs/PORTING.md).
param([string]$Steps)
Add-Type -AssemblyName System.Windows.Forms, System.Drawing
Add-Type @"
using System; using System.Runtime.InteropServices;
public static class M {
  [DllImport("user32.dll")] public static extern bool SetCursorPos(int x, int y);
  [DllImport("user32.dll")] public static extern void mouse_event(uint f, uint dx, uint dy, uint d, IntPtr e);
  [DllImport("kernel32.dll")] public static extern IntPtr GetConsoleWindow();
  [DllImport("user32.dll")] public static extern bool ShowWindow(IntPtr h, int c);
}
"@
# The task's own console window would sit over everything and take the clicks.
[M]::ShowWindow([M]::GetConsoleWindow(), 6) | Out-Null
$wsh = New-Object -ComObject WScript.Shell
foreach ($s in $Steps.Split(';')) {
  $k, $v = $s.Split(':', 2)
  switch ($k) {
    'click' { $x, $y = $v.Split(','); [M]::SetCursorPos([int]$x, [int]$y); Start-Sleep -Milliseconds 150; [M]::mouse_event(2,0,0,0,[IntPtr]::Zero); [M]::mouse_event(4,0,0,0,[IntPtr]::Zero); Start-Sleep -Milliseconds 400 }
    'dbl'   { $x, $y = $v.Split(','); [M]::SetCursorPos([int]$x, [int]$y); for ($i=0;$i -lt 2;$i++){ [M]::mouse_event(2,0,0,0,[IntPtr]::Zero); [M]::mouse_event(4,0,0,0,[IntPtr]::Zero) }; Start-Sleep -Milliseconds 400 }
    'keys'  { [System.Windows.Forms.SendKeys]::SendWait($v); Start-Sleep -Milliseconds 300 }
    'front' { $wsh.AppActivate($v) | Out-Null; Start-Sleep -Milliseconds 500 }
    'burst' { $pre, $n, $dt = $v.Split(','); for ($i = 0; $i -lt [int]$n; $i++) { $b = [System.Windows.Forms.SystemInformation]::VirtualScreen; $bmp = New-Object System.Drawing.Bitmap $b.Width, $b.Height; $g = [System.Drawing.Graphics]::FromImage($bmp); $g.CopyFromScreen($b.Left, $b.Top, 0, 0, $bmp.Size); $bmp.Save(("D:\scratch\3dm\{0}{1:D3}.png" -f $pre, $i), [System.Drawing.Imaging.ImageFormat]::Png); $g.Dispose(); $bmp.Dispose(); Start-Sleep -Seconds ([double]$dt) } }
    'wait'  { Start-Sleep -Seconds ([double]$v) }
    'shot'  { $b = [System.Windows.Forms.SystemInformation]::VirtualScreen; $bmp = New-Object System.Drawing.Bitmap $b.Width, $b.Height; $g = [System.Drawing.Graphics]::FromImage($bmp); $g.CopyFromScreen($b.Left, $b.Top, 0, 0, $bmp.Size); $bmp.Save("D:\scratch\3dm\$v", [System.Drawing.Imaging.ImageFormat]::Png); $g.Dispose(); $bmp.Dispose() }
  }
}
