param([string]$Executable, [string]$Output)
$ErrorActionPreference = 'Stop'
Add-Type -AssemblyName System.Drawing
Add-Type @'
using System;
using System.Runtime.InteropServices;
public class WindowCapture {
  [DllImport("user32.dll")] public static extern bool PostMessage(IntPtr h, uint m, IntPtr w, IntPtr l);
  [DllImport("user32.dll")] public static extern bool GetClientRect(IntPtr h, out RECT rect);
  [DllImport("user32.dll", CharSet=CharSet.Unicode)] public static extern IntPtr FindWindow(string cls, string title);
  [DllImport("user32.dll")] public static extern bool SetWindowPos(IntPtr h, IntPtr after, int x, int y, int w, int height, uint flags);
  [DllImport("user32.dll")] public static extern IntPtr GetDlgItem(IntPtr h, int id);
  [DllImport("user32.dll")] public static extern IntPtr SendMessage(IntPtr h, uint m, IntPtr w, IntPtr l);
  [DllImport("user32.dll")] public static extern bool PrintWindow(IntPtr hWnd, IntPtr hdc, uint flags);
  [DllImport("user32.dll")] public static extern bool GetWindowRect(IntPtr hWnd, out RECT rect);
  [StructLayout(LayoutKind.Sequential)] public struct RECT { public int Left, Top, Right, Bottom; }
}
'@
$app = Start-Process -FilePath $Executable -ArgumentList "--ui-test" -PassThru
try {
  Start-Sleep -Seconds 4
  $app.Refresh()
  if ($app.HasExited) { throw 'Application exited during startup' }
  if ($app.MainWindowTitle -notlike '*alpha 0.5.0*') { throw "Unexpected window: $($app.MainWindowTitle)" }
  if ([WindowCapture]::GetDlgItem($app.MainWindowHandle,900) -eq [IntPtr]::Zero) { throw 'Integrated library missing' }
  $h = $app.MainWindowHandle
  function Read-Probe([int]$id) { return [WindowCapture]::SendMessage($h,[uint32](0x8000+$id),[IntPtr]::Zero,[IntPtr]::Zero).ToInt64() }
  for ($i=0; $i -lt 100 -and (Read-Probe 24) -ne 1; $i++) { Start-Sleep -Milliseconds 100 }
  if ((Read-Probe 24) -ne 1 -or (Read-Probe 22) -lt 380000) { throw 'Prepared song did not load' }
  foreach ($note in @(3,6,9,12)) {
    [void][WindowCapture]::SendMessage($h,0x8001,[IntPtr](0x90 -bor ($note -shl 8) -bor (127 -shl 16)),[IntPtr]::Zero)
    [void][WindowCapture]::SendMessage($h,0x8001,[IntPtr](0x80 -bor ($note -shl 8)),[IntPtr]::Zero)
  }
  if ((Read-Probe 23) -ne 15) { throw 'REC ARM 1-4 did not activate all four Solo buttons' }
  [void][WindowCapture]::SendMessage($h,0x111,[IntPtr]24,[IntPtr]::Zero)
  if ((Read-Probe 23) -ne 0) { throw 'Clear Solo failed' }
  # Space is intercepted even when the song list has focus; auto-repeat must not toggle again.
  $list = [WindowCapture]::GetDlgItem($h,900)
  [void][WindowCapture]::PostMessage($list,0x100,[IntPtr]32,[IntPtr]::Zero)
  [void][WindowCapture]::PostMessage($list,0x100,[IntPtr]32,[IntPtr]0x40000000)
  [void][WindowCapture]::PostMessage($list,0x101,[IntPtr]32,[IntPtr]::Zero)
  Start-Sleep -Milliseconds 200
  if ((Read-Probe 25) -ne 1) { throw 'Space Play or repeat suppression failed' }
  [void][WindowCapture]::PostMessage($list,0x100,[IntPtr]32,[IntPtr]::Zero)
  [void][WindowCapture]::PostMessage($list,0x101,[IntPtr]32,[IntPtr]::Zero)
  Start-Sleep -Milliseconds 100
  if ((Read-Probe 25) -ne 0) { throw 'Space Pause failed' }
  [void][WindowCapture]::SendMessage($h,0x8001,[IntPtr](0x90 -bor (25 -shl 8) -bor (127 -shl 16)),[IntPtr]::Zero)
  [void][WindowCapture]::SendMessage($h,0x8001,[IntPtr](0x80 -bor (25 -shl 8)),[IntPtr]::Zero)
  if ((Read-Probe 25) -ne 1) { throw 'Bank Left did not start playback' }
  [void][WindowCapture]::SendMessage($h,0x111,[IntPtr]16,[IntPtr]::Zero)
  for ($i=0; $i -lt 100 -and (Read-Probe 24) -ne 2; $i++) { Start-Sleep -Milliseconds 100 }
  if ((Read-Probe 24) -ne 2 -or (Read-Probe 25) -ne 1) { throw 'Next did not load and play the next prepared song' }
  [void][WindowCapture]::SendMessage($h,0x111,[IntPtr]11,[IntPtr]::Zero)
  [void][WindowCapture]::SendMessage($h,0x111,[IntPtr]1000,[IntPtr]::Zero)
  if ((Read-Probe 26) -ne 0) { throw 'Output menu selection failed' }
  Write-Output 'PASS REC ARM Solo, Space Play/Pause, Bank Left, Next and output menu'
  foreach ($view in @(@(1280,850,$Output), @(1024,700,($Output -replace '.png$','-compact.png')))) {
    [void][WindowCapture]::SetWindowPos($app.MainWindowHandle,[IntPtr]::Zero,0,0,$view[0],$view[1],4)
    Start-Sleep -Milliseconds 350
    $client = New-Object WindowCapture+RECT
    [void][WindowCapture]::GetClientRect($h,[ref]$client)
    $x = [int]((240+1012*0.7)*$client.Right/1280)
    $y = [int](88*$client.Bottom/800)
    $point = [IntPtr](($y -shl 16) -bor $x)
    [void][WindowCapture]::SendMessage($h,0x201,[IntPtr]1,$point)
    [void][WindowCapture]::SendMessage($h,0x202,[IntPtr]::Zero,$point)
    Start-Sleep -Milliseconds 150
    $fraction = (Read-Probe 21) / [double](Read-Probe 22)
    if ($fraction -lt 0.68 -or $fraction -gt 0.72) { throw "Wave seeking at resized width failed: $fraction" }
    $before = [WindowCapture]::SendMessage($app.MainWindowHandle,0x8014,[IntPtr]::Zero,[IntPtr]::Zero).ToInt64()
    Start-Sleep -Milliseconds 1200
    $after = [WindowCapture]::SendMessage($app.MainWindowHandle,0x8014,[IntPtr]::Zero,[IntPtr]::Zero).ToInt64()
    if (($after - $before) -gt 2) { throw "Idle console is repainting continuously: $($after-$before) paints" }
    Write-Output "PASS idle repaint: $($after-$before) paints in 1.2s"
    $rect = New-Object WindowCapture+RECT
    [void][WindowCapture]::GetWindowRect($app.MainWindowHandle, [ref]$rect)
    $bitmap = New-Object System.Drawing.Bitmap(($rect.Right-$rect.Left), ($rect.Bottom-$rect.Top))
    $graphics = [System.Drawing.Graphics]::FromImage($bitmap)
    $hdc = $graphics.GetHdc()
    try { if (-not [WindowCapture]::PrintWindow($app.MainWindowHandle, $hdc, 2)) { throw 'Window capture failed' } }
    finally { $graphics.ReleaseHdc($hdc) }
    $bitmap.Save([string]$view[2], [System.Drawing.Imaging.ImageFormat]::Png)
    $graphics.Dispose(); $bitmap.Dispose()
    $bounds = New-Object WindowCapture+RECT
    [void][WindowCapture]::GetWindowRect([WindowCapture]::GetDlgItem($app.MainWindowHandle,610),[ref]$bounds)
    if ($bounds.Bottom -gt $rect.Bottom -or $bounds.Right -gt $rect.Right) { throw 'Master outside window after resize' }
  }
  [void][WindowCapture]::PostMessage($h,0x111,[IntPtr]860,[IntPtr]::Zero)
  Start-Sleep -Milliseconds 400
  $notice = [WindowCapture]::FindWindow('DubNotice',$null)
  if ($notice -eq [IntPtr]::Zero) { throw 'Themed notice did not open' }
  $nr = New-Object WindowCapture+RECT
  [void][WindowCapture]::GetWindowRect($notice,[ref]$nr)
  $b = New-Object System.Drawing.Bitmap(($nr.Right-$nr.Left),($nr.Bottom-$nr.Top))
  $g = [System.Drawing.Graphics]::FromImage($b); $dc = $g.GetHdc()
  try { [void][WindowCapture]::PrintWindow($notice,$dc,2) } finally { $g.ReleaseHdc($dc) }
  $b.Save(($Output -replace '.png$','-notice.png'),[System.Drawing.Imaging.ImageFormat]::Png)
  $g.Dispose(); $b.Dispose()
  [void][WindowCapture]::SendMessage($notice,0x111,[IntPtr]1,[IntPtr]::Zero)
  Write-Output 'PASS responsive waveform seek and themed notice'
  [void]$app.CloseMainWindow()
  if (-not $app.WaitForExit(5000)) { throw 'Application did not close normally' }
  if ($app.ExitCode -ne 0) { throw "Application exit: $($app.ExitCode)" }
} finally { if (-not $app.HasExited) { Stop-Process -Id $app.Id -Force } }
