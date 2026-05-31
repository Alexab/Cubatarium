# CMake configure — mirrors .vscode/settings.json and configure-debug task.
param(
    [ValidateSet('Debug', 'Release')]
    [string]$Config = 'Debug',
    [switch]$Fresh
)

$ErrorActionPreference = 'Stop'

$Root = $PSScriptRoot
$CMake = 'C:/Program Files/CMake/bin/cmake.exe'
$BuildDir = Join-Path $Root 'bin'
$VcpkgToolchain = 'E:/vcpkg/scripts/buildsystems/vcpkg.cmake'

if (-not (Test-Path $CMake)) {
    throw "CMake not found: $CMake"
}
if (-not (Test-Path $VcpkgToolchain)) {
    throw "vcpkg toolchain not found: $VcpkgToolchain"
}

$env:PATH = "$(Split-Path $CMake);$env:PATH"
$env:VCPKG_ROOT = 'E:/vcpkg'

$args = @(
    '-B', $BuildDir,
    '-S', $Root,
    "-DCMAKE_BUILD_TYPE=$Config",
    "-DCMAKE_TOOLCHAIN_FILE=$VcpkgToolchain",
    '-DCMAKE_GENERATOR_PLATFORM=x64',
    '-G', 'Visual Studio 17 2022'
)
if ($Fresh) {
    $args += '--fresh'
}

Write-Host ">> configure ($Config): $CMake $($args -join ' ')"
& $CMake @args
if ($LASTEXITCODE -ne 0) { exit $LASTEXITCODE }

Write-Host ">> configure OK -> $BuildDir"
