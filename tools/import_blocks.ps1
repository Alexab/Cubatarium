param(
    [string]$Source = "E:\Work\Home\CubatariumTextures\blocks",
    [string]$RepoRoot = (Resolve-Path "$PSScriptRoot\..").Path
)

$ErrorActionPreference = "Stop"
$manifestPath = Join-Path $RepoRoot "tools\block_manifest.json"
if (-not (Test-Path $manifestPath)) {
    Write-Error "Manifest not found: $manifestPath"
    exit 1
}

$manifest = Get-Content $manifestPath -Raw | ConvertFrom-Json
$supplementPath = Join-Path $RepoRoot "tools\block_manifest_supplement.json"
if (Test-Path $supplementPath) {
    $supplement = Get-Content $supplementPath -Raw | ConvertFrom-Json
    if ($supplement.blocks) {
        $manifest.blocks = @($manifest.blocks) + @($supplement.blocks)
    }
}
$animatedManifestPath = Join-Path $RepoRoot "tools\block_manifest_animated.json"
if (Test-Path $animatedManifestPath) {
    $animated = Get-Content $animatedManifestPath -Raw | ConvertFrom-Json
    if ($animated.blocks) {
        $manifest.blocks = @($manifest.blocks) + @($animated.blocks)
    }
}
if ($manifest.source_dir_default -and (Test-Path $manifest.source_dir_default)) {
    $Source = $manifest.source_dir_default
}

# Copy static PNGs; animated stems imported via manifest only.
$animatedStems = @('water', 'lava', 'fire_0', 'fire_1')
$skipBulkStems = @('water_flow', 'lava_flow', 'portal') + $animatedStems
$overlayStems = 0..9 | ForEach-Object { "destroy_$_" }
$skipBulkCopy = [System.Collections.Generic.HashSet[string]]::new([string[]]($skipBulkStems + $overlayStems))

$texturesOut = Join-Path $RepoRoot $manifest.textures_out
$modelsOut = Join-Path $RepoRoot $manifest.models_out
New-Item -ItemType Directory -Force -Path $texturesOut | Out-Null
New-Item -ItemType Directory -Force -Path $modelsOut | Out-Null

$binTextures = Join-Path $RepoRoot "bin\textures\blocks"
if (Test-Path $binTextures) {
    Copy-Item -Path (Join-Path $binTextures "*.png") -Destination $texturesOut -Force -ErrorAction SilentlyContinue
}

if (Test-Path $Source) {
    $bulkCopied = 0
    Get-ChildItem -Path $Source -Filter "*.png" | ForEach-Object {
        $stem = $_.BaseName
        if ($skipBulkCopy.Contains($stem)) { return }
        Copy-Item $_.FullName (Join-Path $texturesOut "$stem.png") -Force
        $bulkCopied++
    }
    Write-Host "Bulk-copied $bulkCopied static PNGs from pack"
}

function Read-McmetaAnimation($stem) {
    $mcmetaPath = Join-Path $Source "$stem.png.mcmeta"
    if (-not (Test-Path $mcmetaPath)) {
        $mcmetaPath = Join-Path $texturesOut "$stem.png.mcmeta"
    }
    if (-not (Test-Path $mcmetaPath)) {
        return $null
    }
    try {
        $raw = Get-Content $mcmetaPath -Raw | ConvertFrom-Json
        if ($raw.animation) { return $raw.animation }
    } catch { }
    return $null
}

function Get-PngFrameCount($stem) {
    $pngPath = Join-Path $texturesOut "$stem.png"
    if (-not (Test-Path $pngPath)) {
        $pngPath = Join-Path $Source "$stem.png"
    }
    if (-not (Test-Path $pngPath)) { return 1 }
    Add-Type -AssemblyName System.Drawing
    $img = [System.Drawing.Image]::FromFile((Resolve-Path $pngPath).Path)
    try {
        $w = $img.Width
        $h = $img.Height
        if ($w -le 0) { return 1 }
        $fc = [int]($h / $w)
        if ($fc -lt 1) { return 1 }
        return $fc
    } finally {
        $img.Dispose()
    }
}

$stems = [System.Collections.Generic.HashSet[string]]::new()
$ids = @{}
$names = @{}

function Get-StemsForBlock($block) {
    $result = @()
    if ($block.uniform) { $result += $block.uniform }
    if ($block.faces) { $result += @($block.faces) }
    return $result
}

foreach ($block in $manifest.blocks) {
    if ($ids.ContainsKey([int]$block.id)) {
        Write-Error "Duplicate id $($block.id) for $($block.name)"
        exit 1
    }
    if ($names.ContainsKey($block.name)) {
        Write-Error "Duplicate name $($block.name)"
        exit 1
    }
    $ids[[int]$block.id] = $true
    $names[$block.name] = $true
    foreach ($s in (Get-StemsForBlock $block)) {
        [void]$stems.Add([string]$s)
    }
}

$copied = 0
$missing = @()
foreach ($stem in $stems) {
    $dest = Join-Path $texturesOut "$stem.png"
    $src = Join-Path $Source "$stem.png"
    if (Test-Path $src) {
        Copy-Item $src $dest -Force
        $srcMc = Join-Path $Source "$stem.png.mcmeta"
        if (Test-Path $srcMc) {
            Copy-Item $srcMc (Join-Path $texturesOut "$stem.png.mcmeta") -Force
        }
        $copied++
    } elseif (-not (Test-Path $dest)) {
        $missing += $stem
    }
}

if ($missing.Count -gt 0) {
    Write-Error "Missing PNG for stems: $($missing -join ', ')"
    exit 1
}

$imported = 0
foreach ($block in $manifest.blocks) {
    $texArray = @()
    if ($block.uniform) {
        $u = [string]$block.uniform
        $texArray = @($u, $u, $u, $u, $u, $u)
    } else {
        $texArray = @($block.faces | ForEach-Object { [string]$_ })
    }
    if ($texArray.Count -ne 6 -and $texArray.Count -ne 12) {
        Write-Error "Block $($block.name) must have 6 or 12 face textures"
        exit 1
    }

    $obj = [ordered]@{
        name     = [string]$block.name
        id       = [int]$block.id
        textures = $texArray
    }

    $isAnimated = $null -ne $block.physics_preset -or $null -ne $block.render_transparent
    if ($block.physics_preset) {
        $obj.physics = @{ preset = [string]$block.physics_preset }
    }
    if ($block.render_transparent) {
        $obj.render = @{ transparent = $true }
    }

    $animStem = if ($block.uniform) { [string]$block.uniform } else { $texArray[0] }
    $mcAnim = Read-McmetaAnimation $animStem
    $frameCount = 1
    $frametime = 2
    if ($mcAnim) {
        if ($mcAnim.frametime) { $frametime = [int]$mcAnim.frametime }
        if ($mcAnim.frames -and $mcAnim.frames.Count -gt 0) {
            $frameCount = $mcAnim.frames.Count
        }
    }
    if ($texArray.Count -eq 6 -and $animStem) {
        $stripFrames = Get-PngFrameCount $animStem
        if ($stripFrames -gt 1) { $frameCount = $stripFrames }
    } elseif ($texArray.Count -eq 12) {
        $frameCount = 2
    }
    if ($frameCount -gt 1 -or $isAnimated) {
        $obj.animation = @{
            frame_count = $frameCount
            frametime   = $frametime
        }
        if ($mcAnim -and $mcAnim.interpolate) {
            $obj.animation.interpolate = $true
        }
    }

    $jsonPath = Join-Path $modelsOut "$($block.name).json"
    ($obj | ConvertTo-Json -Depth 8) + "`n" | Set-Content $jsonPath -Encoding UTF8 -NoNewline
    $imported++
}

Write-Host "Imported $imported blocks, copied $copied PNGs from pack (textures in $texturesOut)"
