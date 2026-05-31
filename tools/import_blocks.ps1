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
if ($manifest.source_dir_default -and (Test-Path $manifest.source_dir_default)) {
    $Source = $manifest.source_dir_default
}

# Copy all static PNGs from the pack (except animated fluids/fire/portal and crack overlays).
$animatedStems = @('water', 'water_flow', 'lava', 'lava_flow', 'fire_0', 'fire_1', 'portal')
$overlayStems = 0..9 | ForEach-Object { "destroy_$_" }
$skipBulkCopy = [System.Collections.Generic.HashSet[string]]::new([string[]]($animatedStems + $overlayStems))

$texturesOut = Join-Path $RepoRoot $manifest.textures_out
$modelsOut = Join-Path $RepoRoot $manifest.models_out
New-Item -ItemType Directory -Force -Path $texturesOut | Out-Null
New-Item -ItemType Directory -Force -Path $modelsOut | Out-Null

# Seed from bin if present (legacy textures not in external pack)
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

$stems = [System.Collections.Generic.HashSet[string]]::new()
$ids = @{}
$names = @{}

function Get-StemsForBlock($block) {
    $result = @()
    if ($block.uniform) {
        $result += $block.uniform
    }
    if ($block.faces) {
        $result += @($block.faces)
    }
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
    if ($texArray.Count -ne 6) {
        Write-Error "Block $($block.name) must have 6 face textures"
        exit 1
    }

    $obj = [ordered]@{
        name     = [string]$block.name
        id       = [int]$block.id
        textures = $texArray
    }
    $jsonPath = Join-Path $modelsOut "$($block.name).json"
    ($obj | ConvertTo-Json -Depth 5) + "`n" | Set-Content $jsonPath -Encoding UTF8 -NoNewline
    $imported++
}

Write-Host "Imported $imported blocks, copied $copied PNGs from pack (textures in $texturesOut)"
