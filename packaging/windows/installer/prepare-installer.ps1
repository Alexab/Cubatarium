# Stage Cubatarium files and build Windows setup via Actual Installer.
param(
    [switch]$StageOnly,
    [switch]$SkipVerify,
    [switch]$Gui
)

$ErrorActionPreference = "Stop"
$installerDir = $PSScriptRoot
$repoRoot = (Resolve-Path (Join-Path $installerDir "..\..\..")).Path
$srcBin = Join-Path $repoRoot "bin"
$out = Join-Path $installerDir "InstallSources"
$outBin = Join-Path $out "bin"
$verifyScript = Join-Path $repoRoot "scripts\build\verify-windows-exe.ps1"
$doctorScript = Join-Path $repoRoot "scripts\doctor-windows.ps1"

if (-not (Test-Path (Join-Path $srcBin "Cubatarium.exe"))) {
    Write-Error "Release/Debug build not found: $srcBin\Cubatarium.exe. Run scripts\build\windows-configure.ps1 and cmake --build build\desktop-msvc --config Release first."
}

if (-not $SkipVerify) {
    & $verifyScript -ExePath (Join-Path $srcBin "Cubatarium.exe")
    if ($LASTEXITCODE -ne 0) { exit $LASTEXITCODE }
}

# Fresh staging avoids stale logs/cache from prior doctor runs.
if (Test-Path $out) {
    Remove-Item $out -Recurse -Force
}
New-Item -ItemType Directory -Force -Path $outBin | Out-Null

Write-Host "Staging installer files into $out ..."

Copy-Item (Join-Path $srcBin "Cubatarium.exe") $outBin -Force

$iconPng = Join-Path $srcBin "icon.png"
if (Test-Path $iconPng) {
    Copy-Item $iconPng $outBin -Force
}

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
    if (Test-Path $dest) {
        Remove-Item $dest -Recurse -Force
    }
    $src = $null
    if (Test-Path $fromBin) {
        $src = $fromBin
    } elseif (Test-Path $fromRepo) {
        Write-Host "WARNING: $fromBin missing, copying from repo: $fromRepo"
        $src = $fromRepo
    } else {
        Write-Host "WARNING: $Name not found in bin or repo root - skipped"
        return
    }
    Copy-Item $src $dest -Recurse -Force
}

function Assert-StagedRuntimeFiles {
    $required = @(
        "content\worldgen_refs.json",
        "content\types.json",
        "shaders\vshader_greedy.glsl",
        "shaders\vshader_cross_instanced.glsl",
        "prefabs",
        "objects",
        "resource_packs",
        "fonts"
    )
    foreach ($rel in $required) {
        $path = Join-Path $outBin $rel
        if (-not (Test-Path $path)) {
            Write-Error "Staging incomplete: missing $rel under $outBin. Rebuild Release so CMake syncs bin/, then rerun prepare-installer."
        }
    }
}

foreach ($tree in @("shaders", "prefabs", "models", "content", "fonts", "objects")) {
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

Assert-StagedRuntimeFiles

& $doctorScript -BinDir $outBin

# Smoke tests create writable runtime junk next to the exe; do not ship it.
foreach ($junkName in @(".placeholder_cache", "logs", "cache", "worlds")) {
    $junkPath = Join-Path $outBin $junkName
    if (Test-Path $junkPath) {
        Remove-Item $junkPath -Recurse -Force
    }
}

if ($StageOnly) {
    Write-Host "Staging complete (StageOnly)."
    exit 0
}

$actinst = "C:\Program Files (x86)\Actual Installer\actinst.exe"
if (-not (Test-Path $actinst)) {
    Write-Error @"
Actual Installer not found: $actinst
Install from https://www.actualinstaller.com/ then rerun prepare-installer.ps1
"@
}

function Get-SetupArtifact {
    param([string]$SearchDir)
    Get-ChildItem $SearchDir -File -Filter "Cubatarium-*.exe" |
        Where-Object { $_.Name -notlike "CubatariumQt-*" } |
        Sort-Object LastWriteTime -Descending |
        Select-Object -First 1
}

function Open-ActualInstallerGui {
    param([string]$ProjectPath)
    Write-Host @"
>> Open Actual Installer and click the green Build button.
   Project: $ProjectPath
   Output:  $installerDir\Cubatarium-<version>.exe
"@
    Start-Process -FilePath $actinst -ArgumentList @($ProjectPath) -WorkingDirectory $installerDir
}

$aipPath = Join-Path $installerDir "Cubatarium.aip"

if ($Gui) {
    Open-ActualInstallerGui -ProjectPath $aipPath
    exit 0
}

$buildStarted = Get-Date
Write-Host "Building setup with actinst /S (may not work on Actual Installer Free; use -Gui if needed) ..."
Push-Location $installerDir
try {
    $proc = Start-Process -FilePath $actinst -ArgumentList @('/S', 'Cubatarium.aip') `
        -WorkingDirectory $installerDir -Wait -PassThru -NoNewWindow
    if ($proc.ExitCode -ne 0) {
        Write-Warning "actinst.exe exited with code $($proc.ExitCode)."
    }
}
finally {
    Pop-Location
}

$setup = Get-SetupArtifact -SearchDir $installerDir
if ($setup -and $setup.LastWriteTime -ge $buildStarted.AddSeconds(-10)) {
    $sizeMb = [math]::Round($setup.Length / 1MB, 2)
    Write-Host ">> Setup created: $($setup.FullName) ($sizeMb MB)"
    exit 0
}

Write-Warning "actinst /S did not produce an updated setup executable (known issue with Actual Installer Free)."
Open-ActualInstallerGui -ProjectPath $aipPath
exit 0
