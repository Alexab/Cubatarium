# CMake configure — mirrors .vscode/settings.json and configure-debug task.
param(
    [ValidateSet('Debug', 'Release')]
    [string]$Config = 'Debug',
    [switch]$Fresh
)

$ErrorActionPreference = 'Stop'

$Root = Split-Path -Parent (Split-Path -Parent $PSScriptRoot)
$CMake = 'C:/Program Files/CMake/bin/cmake.exe'
$BuildDir = Join-Path $Root 'build\desktop-msvc'
$VcpkgRoot = if ($env:VCPKG_ROOT) { $env:VCPKG_ROOT } else { 'E:/vcpkg' }
$VcpkgToolchain = Join-Path $VcpkgRoot 'scripts\buildsystems\vcpkg.cmake'

if (-not (Test-Path $CMake)) {
    throw "CMake not found: $CMake"
}
if (-not (Test-Path $VcpkgToolchain)) {
    throw "vcpkg toolchain not found: $VcpkgToolchain (set VCPKG_ROOT)"
}

$env:PATH = "$(Split-Path $CMake);$env:PATH"
$env:VCPKG_ROOT = $VcpkgRoot

$cmakeArgs = @(
    '-B', $BuildDir,
    '-S', $Root,
    "-DCMAKE_BUILD_TYPE=$Config",
    "-DCMAKE_TOOLCHAIN_FILE=$VcpkgToolchain",
    '-DVCPKG_MANIFEST_MODE=ON',
    '-DVCPKG_TARGET_TRIPLET=x64-windows-static',
    '-DCMAKE_GENERATOR_PLATFORM=x64',
    '-G', 'Visual Studio 17 2022'
)
if ($Fresh) {
    if (Test-Path $BuildDir) {
        Remove-Item -Recurse -Force $BuildDir
    }
}

Write-Host ">> configure ($Config): $CMake $($cmakeArgs -join ' ')"
& $CMake @cmakeArgs
if ($LASTEXITCODE -ne 0) { exit $LASTEXITCODE }

Write-Host ">> configure OK -> $BuildDir (runtime: $(Join-Path $Root 'bin'))"
