# Generate legacy mipmap launcher PNGs (API 24-25 fallback; adaptive icon used on API 26+).
$ErrorActionPreference = "Stop"
$resRoot = Join-Path $PSScriptRoot "app\src\main\res"
Add-Type -AssemblyName System.Drawing

function New-LauncherPng {
    param([int]$Size, [string]$OutPath)
    $bmp = New-Object System.Drawing.Bitmap $Size, $Size
    $g = [System.Drawing.Graphics]::FromImage($bmp)
    $g.SmoothingMode = [System.Drawing.Drawing2D.SmoothingMode]::AntiAlias
    $g.Clear([System.Drawing.Color]::FromArgb(255, 27, 58, 75))

    $top = [System.Drawing.Color]::FromArgb(255, 124, 179, 66)
    $left = [System.Drawing.Color]::FromArgb(255, 139, 195, 74)
    $right = [System.Drawing.Color]::FromArgb(255, 78, 115, 41)
    $scale = $Size / 108.0
    $cx = $Size / 2.0
    $cy = $Size / 2.0

    $topFace = @(
        [System.Drawing.PointF]::new($cx, ($cy - 30 * $scale)),
        [System.Drawing.PointF]::new($cx + 24 * $scale, ($cy - 16 * $scale)),
        [System.Drawing.PointF]::new($cx, ($cy - 2 * $scale)),
        [System.Drawing.PointF]::new($cx - 24 * $scale, ($cy - 16 * $scale))
    )
    $leftFace = @(
        [System.Drawing.PointF]::new($cx, ($cy - 2 * $scale)),
        [System.Drawing.PointF]::new($cx - 24 * $scale, ($cy - 16 * $scale)),
        [System.Drawing.PointF]::new($cx - 24 * $scale, ($cy + 8 * $scale)),
        [System.Drawing.PointF]::new($cx, ($cy + 22 * $scale))
    )
    $rightFace = @(
        [System.Drawing.PointF]::new($cx, ($cy - 2 * $scale)),
        [System.Drawing.PointF]::new($cx + 24 * $scale, ($cy - 16 * $scale)),
        [System.Drawing.PointF]::new($cx + 24 * $scale, ($cy + 8 * $scale)),
        [System.Drawing.PointF]::new($cx, ($cy + 22 * $scale))
    )

    $g.FillPolygon((New-Object System.Drawing.SolidBrush $right), $rightFace)
    $g.FillPolygon((New-Object System.Drawing.SolidBrush $left), $leftFace)
    $g.FillPolygon((New-Object System.Drawing.SolidBrush $top), $topFace)

    $dir = Split-Path $OutPath -Parent
    if (-not (Test-Path $dir)) { New-Item -ItemType Directory -Path $dir -Force | Out-Null }
    $bmp.Save($OutPath, [System.Drawing.Imaging.ImageFormat]::Png)
    $g.Dispose()
    $bmp.Dispose()
}

$sizes = @{
    "mipmap-mdpi"    = 48
    "mipmap-hdpi"    = 72
    "mipmap-xhdpi"   = 96
    "mipmap-xxhdpi"  = 144
    "mipmap-xxxhdpi" = 192
}

foreach ($entry in $sizes.GetEnumerator()) {
    $out = Join-Path $resRoot "$($entry.Key)\ic_launcher.png"
    New-LauncherPng -Size $entry.Value -OutPath $out
    Copy-Item $out (Join-Path $resRoot "$($entry.Key)\ic_launcher_round.png") -Force
    Write-Host "Wrote $out"
}

$storeDir = Join-Path $PSScriptRoot "store-assets"
$storeIcon = Join-Path $storeDir "icon-512.png"
New-LauncherPng -Size 512 -OutPath $storeIcon
Write-Host "Wrote $storeIcon (Play Store listing)"
