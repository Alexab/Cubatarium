# Stage Cubatarium files and build Windows setup via Actual Installer.
param(
    [switch]$StageOnly
)

$ErrorActionPreference = "Stop"
$installerDir = $PSScriptRoot
$repoRoot = (Resolve-Path (Join-Path $installerDir "..\..\..")).Path
$srcBin = Join-Path $repoRoot "bin"
$out = Join-Path $installerDir "InstallSourcesQt"
$outBin = Join-Path $out "bin"

if (-not (Test-Path (Join-Path $srcBin "Cubatarium.exe"))) {
    Write-Error "Release/Debug build not found: $srcBin\Cubatarium.exe. Run scripts\build\windows-configure.ps1 and cmake --build bin --config Release first."
}

New-Item -ItemType Directory -Force -Path $outBin | Out-Null

Write-Host "Staging installer files into $out ..."

Copy-Item (Join-Path $srcBin "Cubatarium.exe") $outBin -Force

# Remove stale DLLs from a previous dynamic staging
Get-ChildItem $outBin -Filter "*.dll" -ErrorAction SilentlyContinue | Remove-Item -Force

$configSrc = Join-Path $srcBin "config.json"
if (Test-Path $configSrc) {
    Copy-Item $configSrc $outBin -Force
} elseif (Test-Path (Join-Path $repoRoot "config.json.example")) {
    Copy-Item (Join-Path $repoRoot "config.json.example") (Join-Path $outBin "config.json") -Force
}

function CopyGameTree {
    param([string]$Name)
    $fromBin = Join-Path $srcBin $Name
    $fromRepo = Join-Path $repoRoot $Name
    $dest = Join-Path $outBin $Name
    if (Test-Path $fromBin) {
        Copy-Item $fromBin $dest -Recurse -Force
    } elseif (Test-Path $fromRepo) {
        Write-Host "WARNING: $fromBin missing, copying from repo: $fromRepo"
        Copy-Item $fromRepo $dest -Recurse -Force
    } else {
        Write-Host "WARNING: $Name not found in bin or repo root - skipped"
    }
}

foreach ($tree in @("shaders", "prefabs", "models", "textures", "content", "fonts")) {
    CopyGameTree $tree
}

# resource_packs: exclude minecraft_legacy_16 (same as Android syncAssets)
$packSrcBin = Join-Path $srcBin "resource_packs"
$packSrcRepo = Join-Path $repoRoot "resource_packs"
$packDest = Join-Path $outBin "resource_packs"
$packSrc = if (Test-Path $packSrcBin) { $packSrcBin } elseif (Test-Path $packSrcRepo) { $packSrcRepo } else { $null }
if ($packSrc) {
    if (Test-Path $packDest) { Remove-Item $packDest -Recurse -Force }
    New-Item -ItemType Directory -Force -Path $packDest | Out-Null
    Get-ChildItem $packSrc -Directory | Where-Object { $_.Name -ne "minecraft_legacy_16" } | ForEach-Object {
        Copy-Item $_.FullName (Join-Path $packDest $_.Name) -Recurse -Force
    }
} else {
    Write-Host "WARNING: resource_packs not found in bin or repo root - skipped"
}

if ($StageOnly) {
    Write-Host "Staging complete (StageOnly)."
    exit 0
}

$actinst = "C:\Program Files (x86)\Actual Installer\actinst.exe"
if (-not (Test-Path $actinst)) {
    Write-Error "Actual Installer not found: $actinst"
}

Write-Host "Building setup with Actual Installer ..."
Push-Location $installerDir
try {
    & $actinst /S "CubatariumQt.aip"
    if ($LASTEXITCODE -ne 0) { exit $LASTEXITCODE }
}
finally {
    Pop-Location
}
