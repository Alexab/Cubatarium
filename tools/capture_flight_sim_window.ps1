param(
    [Parameter(Mandatory = $true)]
    [int]$ProcessId,

    [Parameter(Mandatory = $true)]
    [string]$OutputPath
)

$ErrorActionPreference = 'Stop'
Add-Type -AssemblyName System.Drawing

if (-not ('CubaFlightCapture.Native' -as [type])) {
    Add-Type -TypeDefinition @'
using System;
using System.Runtime.InteropServices;

namespace CubaFlightCapture {
    [StructLayout(LayoutKind.Sequential)]
    public struct RECT {
        public int Left;
        public int Top;
        public int Right;
        public int Bottom;
    }

    [StructLayout(LayoutKind.Sequential)]
    public struct POINT {
        public int X;
        public int Y;
    }

    public static class Native {
        [DllImport("user32.dll", SetLastError = true)]
        public static extern bool SetProcessDPIAware();

        [DllImport("user32.dll", SetLastError = true)]
        public static extern bool SetWindowPos(
            IntPtr hWnd,
            IntPtr insertAfter,
            int x,
            int y,
            int width,
            int height,
            uint flags
        );

        [DllImport("user32.dll", SetLastError = true)]
        public static extern bool ShowWindow(IntPtr hWnd, int command);

        [DllImport("user32.dll", SetLastError = true)]
        public static extern bool SetForegroundWindow(IntPtr hWnd);

        [DllImport("user32.dll", SetLastError = true)]
        public static extern bool IsWindowVisible(IntPtr hWnd);

        [DllImport("user32.dll", SetLastError = true)]
        public static extern bool GetWindowRect(IntPtr hWnd, out RECT rect);
    }
}
'@
}

[CubaFlightCapture.Native]::SetProcessDPIAware() | Out-Null

$process = Get-Process -Id $ProcessId -ErrorAction Stop
$handle = $process.MainWindowHandle
if ($handle -eq [IntPtr]::Zero) {
    throw "Process $ProcessId has no main window."
}

[CubaFlightCapture.Native]::ShowWindow($handle, 9) | Out-Null
[CubaFlightCapture.Native]::SetWindowPos(
    $handle,
    [IntPtr]::new(-1),
    0,
    0,
    0,
    0,
    [uint32]0x53
) | Out-Null
[CubaFlightCapture.Native]::SetForegroundWindow($handle) | Out-Null
Start-Sleep -Milliseconds 300
if (-not [CubaFlightCapture.Native]::IsWindowVisible($handle)) {
    throw "Process $ProcessId main window is not visible."
}

$bounds = [CubaFlightCapture.RECT]::new()
if (-not [CubaFlightCapture.Native]::GetWindowRect($handle, [ref]$bounds)) {
    throw "Could not read the window bounds for process $ProcessId."
}

$width = $bounds.Right - $bounds.Left
$height = $bounds.Bottom - $bounds.Top
if ($width -le 0 -or $height -le 0) {
    throw "Process $ProcessId has an empty client area ($width x $height)."
}

$fullPath = [System.IO.Path]::GetFullPath($OutputPath)
$directory = [System.IO.Path]::GetDirectoryName($fullPath)
[System.IO.Directory]::CreateDirectory($directory) | Out-Null

$bitmap = [System.Drawing.Bitmap]::new(
    $width,
    $height,
    [System.Drawing.Imaging.PixelFormat]::Format32bppArgb
)
$graphics = [System.Drawing.Graphics]::FromImage($bitmap)
try {
    $graphics.CopyFromScreen(
        $bounds.Left,
        $bounds.Top,
        0,
        0,
        [System.Drawing.Size]::new($width, $height),
        [System.Drawing.CopyPixelOperation]::SourceCopy
    )
    $bitmap.Save($fullPath, [System.Drawing.Imaging.ImageFormat]::Png)
}
finally {
    $graphics.Dispose()
    $bitmap.Dispose()
    [CubaFlightCapture.Native]::SetWindowPos(
        $handle,
        [IntPtr]::new(-2),
        0,
        0,
        0,
        0,
        [uint32]0x53
    ) | Out-Null
}

[pscustomobject]@{
    process_id = $ProcessId
    output = $fullPath
    width = $width
    height = $height
    captured_at = [DateTimeOffset]::Now.ToString('o')
}
