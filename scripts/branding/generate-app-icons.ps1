# Generate app icons for desktop (Windows/Linux), Android, and store listings.
$ErrorActionPreference = "Stop"
$repoRoot = Split-Path -Parent (Split-Path -Parent $PSScriptRoot)
$brandingDir = Join-Path $repoRoot "resources\branding"
$resRoot = Join-Path $repoRoot "platforms\android\app\src\main\res"
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
    return $bmp
}

function Save-IconFile {
    param([System.Drawing.Bitmap]$Bitmap, [string]$Path)
    $icon = [System.Drawing.Icon]::FromHandle($Bitmap.GetHicon())
    $fs = [System.IO.File]::Create($Path)
    try {
        $icon.Save($fs)
    } finally {
        $fs.Close()
        $icon.Dispose()
    }
}

New-Item -ItemType Directory -Force -Path $brandingDir | Out-Null

$icon512Path = Join-Path $brandingDir "icon-512.png"
$bmp512 = New-LauncherPng -Size 512 -OutPath $icon512Path
Write-Host "Wrote $icon512Path"

$icon256Path = Join-Path $brandingDir "icon-256.png"
$bmp256 = New-LauncherPng -Size 256 -OutPath $icon256Path
Write-Host "Wrote $icon256Path"
$bmp256.Dispose()

$icon64Path = Join-Path $brandingDir "icon-64.png"
$bmp64 = New-LauncherPng -Size 64 -OutPath $icon64Path
Write-Host "Wrote $icon64Path"
$bmp64.Dispose()

$icoPath = Join-Path $brandingDir "Cubatarium.ico"
Save-IconFile -Bitmap $bmp512 -Path $icoPath
$bmp512.Dispose()
Write-Host "Wrote $icoPath"

$mipmapSizes = @{
    "mipmap-mdpi"    = 48
    "mipmap-hdpi"    = 72
    "mipmap-xhdpi"   = 96
    "mipmap-xxhdpi"  = 144
    "mipmap-xxxhdpi" = 192
}

foreach ($entry in $mipmapSizes.GetEnumerator()) {
    $out = Join-Path $resRoot "$($entry.Key)\ic_launcher.png"
    $bmp = New-LauncherPng -Size $entry.Value -OutPath $out
    $bmp.Dispose()
    Copy-Item $out (Join-Path $resRoot "$($entry.Key)\ic_launcher_round.png") -Force
    Write-Host "Wrote $out"
}

$storeIcon = Join-Path $repoRoot "packaging\android\store-assets\icon-512.png"
Copy-Item $icon512Path $storeIcon -Force
Write-Host "Wrote $storeIcon"

$fdroidIcon = Join-Path $repoRoot "packaging\android\fdroid\metadata\en-US\images\icon.png"
Copy-Item $icon512Path $fdroidIcon -Force
Write-Host "Wrote $fdroidIcon"

$linuxIcon = Join-Path $repoRoot "packaging\linux\hicolor\256x256\apps\cubatarium.png"
New-Item -ItemType Directory -Force -Path (Split-Path $linuxIcon -Parent) | Out-Null
Copy-Item $icon256Path $linuxIcon -Force
Write-Host "Wrote $linuxIcon"
