#Requires -Version 5.1
<#
.SYNOPSIS
  Build resource_packs/minecraft_legacy_16 from repo models/blocks and textures/blocks.
#>
param(
    [string]$RepoRoot = (Split-Path -Parent $PSScriptRoot),
    [string]$OutPack = "resource_packs/minecraft_legacy_16"
)

$ErrorActionPreference = "Stop"

function Write-Utf8NoBom {
    param([string]$Path, [string]$Content)
    $utf8 = New-Object System.Text.UTF8Encoding $false
    [System.IO.File]::WriteAllText($Path, $Content, $utf8)
}

$srcModels = Join-Path $RepoRoot "models/blocks"
$srcTextures = Join-Path $RepoRoot "textures/blocks"
$srcPrefabs = Join-Path $RepoRoot "prefabs"
$dest = Join-Path $RepoRoot $OutPack

if (-not (Test-Path $srcModels)) {
    Write-Error "Missing models/blocks - restore legacy assets or use git history."
}
if (-not (Test-Path $srcTextures)) {
    Write-Error "Missing $srcTextures"
}

if (Test-Path $dest) {
    Remove-Item -Recurse -Force $dest
}
New-Item -ItemType Directory -Force -Path (Join-Path $dest "blocks") | Out-Null
New-Item -ItemType Directory -Force -Path (Join-Path $dest "textures/blocks") | Out-Null

$packJson = @{
    id = "minecraft_legacy_16"
    name = "Minecraft Legacy 16px"
    version = 1
    license = "PROPRIETARY-LOCAL"
    resolution = 16
    priority = 0
} | ConvertTo-Json -Depth 4
Write-Utf8NoBom (Join-Path $dest "pack.json") $packJson
Write-Utf8NoBom (Join-Path $dest "LICENSE.txt") "Minecraft-derived assets. Local use only. Not for distribution.`n"

$count = 0
Get-ChildItem $srcModels -Filter "*.json" | ForEach-Object {
    $raw = Get-Content $_.FullName -Raw -Encoding UTF8 | ConvertFrom-Json
    if (-not $raw.name) { return }
    $out = @{}
    foreach ($p in $raw.PSObject.Properties) {
        if ($p.Name -ne "id") {
            $out[$p.Name] = $p.Value
        }
    }
    $outPath = Join-Path $dest "blocks/$($raw.name).json"
    Write-Utf8NoBom $outPath (($out | ConvertTo-Json -Depth 10) + "`n")
    $count++
}

Copy-Item -Path (Join-Path $srcTextures "*") -Destination (Join-Path $dest "textures/blocks") -Recurse -Force

if (Test-Path $srcPrefabs) {
    Copy-Item -Path $srcPrefabs -Destination (Join-Path $dest "prefabs") -Recurse -Force
}

Write-Host "Migrated $count blocks to $dest"
$validate = Join-Path $RepoRoot "tools/validate_resource_pack.py"
if (Test-Path $validate) {
    python $validate $dest
}
