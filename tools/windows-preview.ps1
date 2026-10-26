param([string]$Executable, [string]$Output)
$ErrorActionPreference = 'Stop'
Add-Type -AssemblyName System.Drawing
Add-Type @'
using System;
using System.Runtime.InteropServices;
public class WindowCapture {
  [DllImport("user32.dll")] public static extern bool SetWindowPos(IntPtr h, IntPtr after, int x, int y, int w, int height, uint flags);
  [DllImport("user32.dll")] public static extern IntPtr GetDlgItem(IntPtr h, int id);
  [DllImport("user32.dll")] public static extern IntPtr SendMessage(IntPtr h, uint m, IntPtr w, IntPtr l);
  [DllImport("user32.dll")] public static extern bool PrintWindow(IntPtr hWnd, IntPtr hdc, uint flags);
  [DllImport("user32.dll")] public static extern bool GetWindowRect(IntPtr hWnd, out RECT rect);
  [StructLayout(LayoutKind.Sequential)] public struct RECT { public int Left, Top, Right, Bottom; }
}
'@
$app = Start-Process -FilePath $Executable -PassThru
try {
  Start-Sleep -Seconds 4
  $app.Refresh()
  if ($app.HasExited) { throw 'Application exited during startup' }
  if ($app.MainWindowTitle -notlike '*alpha 0.4*') { throw "Unexpected window: $($app.MainWindowTitle)" }
  if ([WindowCapture]::GetDlgItem($app.MainWindowHandle,900) -eq [IntPtr]::Zero) { throw 'Integrated library missing' }
  foreach ($view in @(@(1280,850,$Output), @(1024,700,($Output -replace '.png$','-compact.png')))) {
    [void][WindowCapture]::SetWindowPos($app.MainWindowHandle,[IntPtr]::Zero,0,0,$view[0],$view[1],4)
    Start-Sleep -Milliseconds 350
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
  [void]$app.CloseMainWindow()
  if (-not $app.WaitForExit(5000)) { throw 'Application did not close normally' }
  if ($app.ExitCode -ne 0) { throw "Application exit: $($app.ExitCode)" }
} finally { if (-not $app.HasExited) { Stop-Process -Id $app.Id -Force } }
