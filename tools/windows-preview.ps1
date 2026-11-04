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
  if ($app.MainWindowTitle -notlike '*alpha 0.8.0*') { throw "Unexpected window: $($app.MainWindowTitle)" }
  if ([WindowCapture]::GetDlgItem($app.MainWindowHandle,900) -eq [IntPtr]::Zero) { throw 'Integrated library missing' }
  $h = $app.MainWindowHandle
  function Read-Probe([int]$id) { return [WindowCapture]::SendMessage($h,[uint32](0x8000+$id),[IntPtr]::Zero,[IntPtr]::Zero).ToInt64() }
  for ($i=0; $i -lt 100 -and (Read-Probe 24) -ne 1; $i++) { Start-Sleep -Milliseconds 100 }
  if ((Read-Probe 24) -ne 1 -or (Read-Probe 22) -lt 380000) { throw 'Prepared song did not load' }
  $beforeHits = Read-Probe 27
  [void][WindowCapture]::SendMessage($h,0x111,[IntPtr]801,[IntPtr]::Zero)
  if ((Read-Probe 27) -ne ($beforeHits+1) -or (Read-Probe 24) -ne 1) { throw 'Reload of active song unnecessarily read from disk' }

  for ($i=0; $i -lt 100 -and (((Read-Probe 28) -band 2) -eq 0 -or (Read-Probe 29) -lt 63000); $i++) { Start-Sleep -Milliseconds 100 }
  if (((Read-Probe 28) -band 2) -eq 0) { throw 'Next song was not prefetched to RAM' }
  if ([Math]::Abs((Read-Probe 29)-64000) -gt 20) { throw 'Total duration of the eight-song library is wrong' }
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
  Start-Sleep -Milliseconds 150
  if ([WindowCapture]::SendMessage($h,0x801e,[IntPtr]::Zero,[IntPtr]::Zero).ToInt64() -le 1000) { throw 'Channel meter did not detect audio' }
  $switchTime = [System.Diagnostics.Stopwatch]::StartNew()
  [void][WindowCapture]::SendMessage($h,0x111,[IntPtr]16,[IntPtr]::Zero)
  $switchTime.Stop()
  if ((Read-Probe 27) -lt 1 -or (Read-Probe 24) -ne 2) { throw 'Prepared RAM song was not installed synchronously' }
  Write-Output "PASS RAM switch: $($switchTime.ElapsedMilliseconds) ms (test machine)"
  if ((Read-Probe 31) -gt 1073741824) { throw 'Warm cache exceeded its budget' }
  for ($i=0; $i -lt 100 -and (Read-Probe 24) -ne 2; $i++) { Start-Sleep -Milliseconds 100 }
  if ((Read-Probe 24) -ne 2 -or (Read-Probe 25) -ne 1) { throw 'Next did not load and play the next prepared song' }
  [void][WindowCapture]::SendMessage($h,0x111,[IntPtr]11,[IntPtr]::Zero)
  [void][WindowCapture]::SendMessage($h,0x111,[IntPtr]1000,[IntPtr]::Zero)
  if ((Read-Probe 26) -ne 0) { throw 'Output menu selection failed' }
  Write-Output 'PASS REC ARM Solo, Space Play/Pause, Bank Left, Next and output menu'
  # Session save/new/open exercises the actual serializer and UI commands without file pickers in test mode.
  [void][WindowCapture]::SendMessage($h,0x111,[IntPtr]870,[IntPtr]::Zero)
  [void][WindowCapture]::SendMessage($h,0x111,[IntPtr]873,[IntPtr]::Zero)
  if ([WindowCapture]::SendMessage($list,0x18b,[IntPtr]::Zero,[IntPtr]::Zero).ToInt64() -ne 0) { throw 'New library did not clear entries' }
  [void][WindowCapture]::SendMessage($h,0x111,[IntPtr]872,[IntPtr]::Zero)
  if ([WindowCapture]::SendMessage($list,0x18b,[IntPtr]::Zero,[IntPtr]::Zero).ToInt64() -ne 8) { throw 'Session reopen lost entries' }
  if ([Math]::Abs((Read-Probe 29)-64000) -gt 20) { throw 'Session reopen lost duration' }
  [void][WindowCapture]::SendMessage($list,0x186,[IntPtr]::Zero,[IntPtr]::Zero)
  [void][WindowCapture]::SendMessage($h,0x111,[IntPtr]906,[IntPtr]::Zero)
  if ([WindowCapture]::SendMessage($list,0x188,[IntPtr]::Zero,[IntPtr]::Zero).ToInt64() -ne 1) { throw 'Library down arrow failed' }
  [void][WindowCapture]::PostMessage($list,0x100,[IntPtr]38,[IntPtr]::Zero)
  Start-Sleep -Milliseconds 100
  if ([WindowCapture]::SendMessage($list,0x188,[IntPtr]::Zero,[IntPtr]::Zero).ToInt64() -ne 0) { throw 'Keyboard Up did not navigate library' }
  $rail = [WindowCapture]::GetDlgItem($h,907)
  if ($rail -eq [IntPtr]::Zero) { throw 'Custom library scrollbar missing' }
  $rr = New-Object WindowCapture+RECT
  [void][WindowCapture]::GetClientRect($rail,[ref]$rr)
  $rp = [IntPtr]((($rr.Bottom-2) -shl 16) -bor 8)
  [void][WindowCapture]::SendMessage($rail,0x201,[IntPtr]1,$rp)
  [void][WindowCapture]::SendMessage($rail,0x202,[IntPtr]::Zero,$rp)
  if ([WindowCapture]::SendMessage($list,0x18e,[IntPtr]::Zero,[IntPtr]::Zero).ToInt64() -le 0) { throw 'Custom library scrollbar did not scroll' }
  [void][WindowCapture]::PostMessage($list,0x100,[IntPtr]36,[IntPtr]::Zero)
  Start-Sleep -Milliseconds 100
  [void][WindowCapture]::SendMessage($list,0x186,[IntPtr]::Zero,[IntPtr]::Zero)
  [void][WindowCapture]::SendMessage($h,0x111,[IntPtr]801,[IntPtr]::Zero)
  for ($i=0; $i -lt 100 -and (Read-Probe 24) -ne 1; $i++) { Start-Sleep -Milliseconds 100 }
  if ((Read-Probe 24) -ne 1) { throw 'Reopened session song could not load' }
  Start-Sleep -Milliseconds 500
  Write-Output 'PASS session save/new/open, library duration, arrows, scrollbar and per-channel meter'

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
  [void][WindowCapture]::SendMessage($h,0x111,[IntPtr]10,[IntPtr]::Zero)
  Start-Sleep -Milliseconds 250
  $mr = New-Object WindowCapture+RECT
  [void][WindowCapture]::GetWindowRect($h,[ref]$mr)
  $mb = New-Object System.Drawing.Bitmap(($mr.Right-$mr.Left),($mr.Bottom-$mr.Top))
  $mg = [System.Drawing.Graphics]::FromImage($mb); $mdc = $mg.GetHdc()
  try { [void][WindowCapture]::PrintWindow($h,$mdc,2) } finally { $mg.ReleaseHdc($mdc) }
  $mb.Save(($Output -replace '.png$','-meters.png'),[System.Drawing.Imaging.ImageFormat]::Png)
  $mg.Dispose(); $mb.Dispose()
  [void][WindowCapture]::SendMessage($h,0x111,[IntPtr]11,[IntPtr]::Zero)
  # New modes exercise actual file decoder, shared library, Tap and duplex null backend.
  [void][WindowCapture]::SendMessage($h,0x111,[IntPtr]881,[IntPtr]::Zero)
  for ($i=0; $i -lt 150 -and (Read-Probe 32) -ne 1; $i++) { Start-Sleep -Milliseconds 100 }
  if ((Read-Probe 32) -ne 1 -or (Read-Probe 22) -lt 380000) { throw 'Bands file mode did not load original audio' }
  [void][WindowCapture]::SendMessage($h,0x111,[IntPtr]884,[IntPtr]::Zero)
  Start-Sleep -Milliseconds 500
  [void][WindowCapture]::SendMessage($h,0x111,[IntPtr]884,[IntPtr]::Zero)
  if ((Read-Probe 34) -lt 450 -or (Read-Probe 34) -gt 650) { throw 'Tap echo timing failed' }
  [void][WindowCapture]::SendMessage($h,0x111,[IntPtr]885,[IntPtr]::Zero)
  if ((Read-Probe 34) -lt 225 -or (Read-Probe 34) -gt 325) { throw 'Tap division failed' }
  [void][WindowCapture]::SendMessage($h,0x111,[IntPtr]10,[IntPtr]::Zero)
  Start-Sleep -Milliseconds 300
  if ((Read-Probe 25) -ne 1) { throw 'Bands playback failed' }
  $br = New-Object WindowCapture+RECT
  [void][WindowCapture]::SetWindowPos($h,[IntPtr]::Zero,0,0,1280,850,4)
  Start-Sleep -Milliseconds 200
  [void][WindowCapture]::GetWindowRect($h,[ref]$br)
  $bb = New-Object System.Drawing.Bitmap(($br.Right-$br.Left),($br.Bottom-$br.Top))
  $bg = [System.Drawing.Graphics]::FromImage($bb); $bdc = $bg.GetHdc()
  try { [void][WindowCapture]::PrintWindow($h,$bdc,2) } finally { $bg.ReleaseHdc($bdc) }
  $bb.Save(($Output -replace '.png$','-bands.png'),[System.Drawing.Imaging.ImageFormat]::Png)
  $bg.Dispose();$bb.Dispose()
  [void][WindowCapture]::SendMessage($h,0x111,[IntPtr]11,[IntPtr]::Zero)
  [void][WindowCapture]::SendMessage($h,0x111,[IntPtr]883,[IntPtr]::Zero)
  if ((Read-Probe 33) -ne 1 -or (Read-Probe 32) -ne 1) { throw 'USB source did not enable bands' }
  [void][WindowCapture]::SendMessage($h,0x111,[IntPtr]10,[IntPtr]::Zero)
  Start-Sleep -Milliseconds 300
  if ((Read-Probe 25) -ne 1) { throw 'Duplex null device did not start' }
  [void][WindowCapture]::SendMessage($h,0x111,[IntPtr]882,[IntPtr]::Zero)
  if ((Read-Probe 33) -ne 0 -or (Read-Probe 25) -ne 0) { throw 'Source switch must pause capture' }
  [void][WindowCapture]::SendMessage($h,0x111,[IntPtr]870,[IntPtr]::Zero)
  [void][WindowCapture]::SendMessage($h,0x111,[IntPtr]873,[IntPtr]::Zero)
  [void][WindowCapture]::SendMessage($h,0x111,[IntPtr]872,[IntPtr]::Zero)
  if ((Read-Probe 32) -ne 1) { throw 'Session did not retain bands mode' }
  [void][WindowCapture]::SendMessage($list,0x186,[IntPtr]::Zero,[IntPtr]::Zero)
  [void][WindowCapture]::SendMessage($h,0x111,[IntPtr]801,[IntPtr]::Zero)
  for ($i=0; $i -lt 150 -and (Read-Probe 24) -ne 1; $i++) { Start-Sleep -Milliseconds 100 }
  [void][WindowCapture]::SendMessage($h,0x111,[IntPtr]880,[IntPtr]::Zero)
  for ($i=0; $i -lt 150 -and (Read-Probe 32) -ne 0; $i++) { Start-Sleep -Milliseconds 100 }
  if ((Read-Probe 32) -ne 0) { throw 'Return to stems failed' }
  Write-Output 'PASS bands decoder, playback, Tap Echo, divisions, duplex capture, source switches and session mode'
  # Full SEND ALL must toggle the layer without moving Master or any effect value.
  $masterBefore = Read-Probe 37
  foreach ($cc in @((16..31) + (46..62))) {
    [void][WindowCapture]::PostMessage($h,0x8001,[IntPtr](0xb0 -bor ($cc -shl 8) -bor (127 -shl 16)),[IntPtr]::Zero)
  }
  Start-Sleep -Milliseconds 150
  if ((Read-Probe 35) -ne 1 -or (Read-Probe 37) -ne $masterBefore) { throw 'SEND ALL did not isolate snapshot or enable SHIFT' }
  $holdBefore = Read-Probe 36
  [void][WindowCapture]::PostMessage($h,0x8001,[IntPtr](0xb0 -bor (54 -shl 8)),[IntPtr]::Zero)
  Start-Sleep -Milliseconds 80
  [void][WindowCapture]::PostMessage($h,0x8001,[IntPtr](0xb0 -bor (54 -shl 8) -bor (127 -shl 16)),[IntPtr]::Zero)
  Start-Sleep -Milliseconds 80
  if ((Read-Probe 36) -ne 127) { throw 'SHIFT knob pickup failed' }
  [void][WindowCapture]::SendMessage($h,0x111,[IntPtr]732,[IntPtr]::Zero)
  if ((Read-Probe 35) -ne 0) { throw 'On-screen SHIFT did not disable layer' }
  # Drag active track below two entries; playback must retain its identity.
  [void][WindowCapture]::SendMessage($list,0x197,[IntPtr]::Zero,[IntPtr]::Zero)
  [void][WindowCapture]::SendMessage($list,0x186,[IntPtr]::Zero,[IntPtr]::Zero)
  [void][WindowCapture]::SendMessage($h,0x111,[IntPtr]801,[IntPtr]::Zero)
  for ($i=0; $i -lt 100 -and (Read-Probe 24) -ne 1; $i++) { Start-Sleep -Milliseconds 100 }
  [void][WindowCapture]::SendMessage($h,0x111,[IntPtr]10,[IntPtr]::Zero)
  $rowHeight = [WindowCapture]::SendMessage($list,0x1a1,[IntPtr]::Zero,[IntPtr]::Zero).ToInt32()
  $fromPoint = [IntPtr]((10 -shl 16) -bor 30)
  $toPoint = [IntPtr]((([int]($rowHeight*2.8)) -shl 16) -bor 30)
  [void][WindowCapture]::SendMessage($list,0x201,[IntPtr]1,$fromPoint)
  [void][WindowCapture]::SendMessage($list,0x200,[IntPtr]1,$toPoint)
  [void][WindowCapture]::SendMessage($list,0x202,[IntPtr]::Zero,$toPoint)
  if ((Read-Probe 24) -ne 3 -or (Read-Probe 25) -ne 1) { throw 'Drag changed active song or failed to reorder' }
  # Seek close to EOF via the actual waveform, then observe next in reordered queue.
  $client = New-Object WindowCapture+RECT
  [void][WindowCapture]::GetClientRect($h,[ref]$client)
  $x = [int](1244*$client.Right/1280); $y = [int](88*$client.Bottom/800)
  $point = [IntPtr](($y -shl 16) -bor $x)
  [void][WindowCapture]::SendMessage($h,0x201,[IntPtr]1,$point)
  [void][WindowCapture]::SendMessage($h,0x202,[IntPtr]::Zero,$point)
  for ($i=0; $i -lt 60 -and (Read-Probe 24) -ne 4; $i++) { Start-Sleep -Milliseconds 100 }
  if ((Read-Probe 24) -ne 4 -or (Read-Probe 25) -ne 1) { throw 'Automatic next failed in reordered library' }
  [void][WindowCapture]::SendMessage($h,0x111,[IntPtr]11,[IntPtr]::Zero)
  [void][WindowCapture]::SendMessage($list,0x186,[IntPtr]7,[IntPtr]::Zero)
  [void][WindowCapture]::SendMessage($h,0x111,[IntPtr]801,[IntPtr]::Zero)
  for ($i=0; $i -lt 100 -and (Read-Probe 24) -ne 8; $i++) { Start-Sleep -Milliseconds 100 }
  [void][WindowCapture]::SendMessage($h,0x111,[IntPtr]10,[IntPtr]::Zero)
  [void][WindowCapture]::SendMessage($h,0x201,[IntPtr]1,$point)
  [void][WindowCapture]::SendMessage($h,0x202,[IntPtr]::Zero,$point)
  Start-Sleep -Milliseconds 800
  if ((Read-Probe 24) -ne 8 -or (Read-Probe 25) -ne 0) { throw 'Final track did not stop at queue end' }
  Write-Output 'PASS SEND ALL snapshot isolation, SHIFT pickup, mouse reorder, autoplay and queue end'
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
