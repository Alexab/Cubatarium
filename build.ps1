# Build Cubatarium — mirrors .vscode/settings.json and build-debug task.
param(
    [ValidateSet('Debug', 'Release')]
    [string]$Config = 'Debug',
    [switch]$Configure,
    [switch]$Reconfigure,
    [int]$Jobs = 8,
    [Parameter(ValueFromRemainingArguments = $true)]
    [string[]]$CMakeBuildArgs
)

$ErrorActionPreference = 'Stop'

$Root = $PSScriptRoot
$CMake = 'C:/Program Files/CMake/bin/cmake.exe'
$BuildDir = Join-Path $Root 'bin'
$VcpkgToolchain = 'E:/vcpkg/scripts/buildsystems/vcpkg.cmake'

if (-not (Test-Path $CMake)) {
    throw "CMake not found: $CMake"
}

$env:PATH = "$(Split-Path $CMake);$env:PATH"
$env:VCPKG_ROOT = 'E:/vcpkg'

$cacheFile = Join-Path $BuildDir 'CMakeCache.txt'
if ($Reconfigure -or -not (Test-Path $cacheFile)) {
    & "$Root/configure.ps1" -Config $Config
    if ($LASTEXITCODE -ne 0) { exit $LASTEXITCODE }
} elseif ($Configure) {
    & "$Root/configure.ps1" -Config $Config
    if ($LASTEXITCODE -ne 0) { exit $LASTEXITCODE }
}

$buildArgs = @(
    '--build', $BuildDir,
    '--config', $Config,
    '--parallel', $Jobs.ToString()
)
if ($CMakeBuildArgs) {
    $buildArgs += $CMakeBuildArgs
}

Write-Host ">> build ($Config): $CMake $($buildArgs -join ' ')"
& $CMake @buildArgs
exit $LASTEXITCODE
