# Verify Cubatarium.exe has only expected system DLL dependencies (static /MT build).
param(
    [string]$ExePath = ""
)

$ErrorActionPreference = "Stop"

$repoRoot = Split-Path -Parent (Split-Path -Parent $PSScriptRoot)
if (-not $ExePath) {
    $ExePath = Join-Path $repoRoot "bin\Cubatarium.exe"
}

if (-not (Test-Path $ExePath)) {
    Write-Error "Executable not found: $ExePath"
}

function Find-Dumpbin {
    $vswhere = "${env:ProgramFiles(x86)}\Microsoft Visual Studio\Installer\vswhere.exe"
    if (Test-Path $vswhere) {
        $vsPath = & $vswhere -latest -products * -requires Microsoft.VisualStudio.Component.VC.Tools.x86.x64 -property installationPath 2>$null
        if ($vsPath) {
            $candidates = Get-ChildItem (Join-Path $vsPath "VC\Tools\MSVC") -Directory -ErrorAction SilentlyContinue |
                Sort-Object Name -Descending |
                ForEach-Object { Join-Path $_.FullName "bin\Hostx64\x64\dumpbin.exe" } |
                Where-Object { Test-Path $_ }
            if ($candidates) { return $candidates[0] }
        }
    }
    $fallback = Get-ChildItem "C:\Program Files\Microsoft Visual Studio\2022" -Recurse -Filter dumpbin.exe -ErrorAction SilentlyContinue |
        Select-Object -First 1 -ExpandProperty FullName
    if ($fallback) { return $fallback }
    throw "dumpbin.exe not found. Install Visual Studio C++ build tools."
}

$dumpbin = Find-Dumpbin
Write-Host ">> verify-windows-exe: $ExePath"
Write-Host ">> dumpbin: $dumpbin"

$raw = & $dumpbin /dependents $ExePath 2>&1 | Out-String
$deps = [regex]::Matches($raw, '^\s+(\S+\.dll)\s*$', 'Multiline') |
    ForEach-Object { $_.Groups[1].Value } |
    ForEach-Object { $_.ToLowerInvariant() } |
    Select-Object -Unique

if (-not $deps) {
    Write-Error "No DLL dependencies parsed from dumpbin output.`n$raw"
}

$forbiddenPatterns = @(
    'vcruntime',
    'msvcp',
    'concrt',
    'glfw3',
    'glew32',
    'freetype',
    'zlib1',
    'libpng',
    'brotli',
    'bz2',
    'harfbuzz'
)

$bad = @()
foreach ($dep in $deps) {
    foreach ($pat in $forbiddenPatterns) {
        if ($dep -like "*$pat*") {
            $bad += $dep
            break
        }
    }
}

Write-Host "Dependencies:"
$deps | ForEach-Object { Write-Host "  $_" }

if ($bad.Count -gt 0) {
    Write-Error @"
Cubatarium.exe is not a static desktop build. Forbidden DLL dependencies:
  $($bad -join ', ')

Reconfigure with x64-windows-static triplet and /MT CRT, then rebuild:
  .\configure.ps1 -Config Release -Fresh
  cmake --build build\desktop-msvc --config Release

VS Code / Cursor: Ctrl+Shift+P -> CMake: Delete Cache and Reconfigure
  (preset must be 'windows-msvc', then Build)
"@
}

Write-Host ">> verify-windows-exe: PASS (static build, $($deps.Count) system DLL(s))"
exit 0
