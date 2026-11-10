# Build Cubatarium in the canonical desktop build tree (build/desktop-msvc),
# matching AGENTS.md and CI. Release is the default: Debug startup is known to
# hang and must not be used for desktop verification.
param(
    [ValidateSet('Debug', 'Release')]
    [string]$Config = 'Release',
    [switch]$Configure,
    [switch]$Reconfigure,
    [int]$Jobs = 8,
    [int]$ClMpCount = 0,
    [string]$Target = 'Cubatarium',
    [Parameter(ValueFromRemainingArguments = $true)]
    [string[]]$CMakeBuildArgs
)

$ErrorActionPreference = 'Stop'

$Root = $PSScriptRoot
$CMake = Get-Command cmake -ErrorAction SilentlyContinue
if (-not $CMake) {
    $CMakePath = 'C:/Program Files/CMake/bin/cmake.exe'
    if (Test-Path $CMakePath) { $CMake = $CMakePath } else { throw 'cmake not found on PATH' }
} else {
    $CMake = $CMake.Source
}
$BuildDir = Join-Path $Root 'build/desktop-msvc'
$VcpkgRoot = if ($env:VCPKG_ROOT) { $env:VCPKG_ROOT } else { 'E:/vcpkg' }
$env:VCPKG_ROOT = $VcpkgRoot

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
    '--target', $Target,
    '--parallel', $Jobs.ToString()
)
if ($CMakeBuildArgs) {
    $buildArgs += $CMakeBuildArgs
}

# CMake's Visual Studio backend sets CL_MPCount=1 when --parallel is used,
# which serializes /MP compilation inside this single project. Preserve the
# requested worker budget for translation units as well as MSBuild projects.
$generatorLine = Select-String -Path $cacheFile -Pattern '^CMAKE_GENERATOR:INTERNAL=' -ErrorAction SilentlyContinue
if ($generatorLine -and $generatorLine.Line -match 'Visual Studio') {
    if ($ClMpCount -le 0) {
        $ClMpCount = $Jobs
    }
    if ($ClMpCount -le 0) {
        throw 'ClMpCount must be greater than zero for Visual Studio builds.'
    }
    $buildArgs += @('--', "/p:CL_MPCount=$ClMpCount")
}

Write-Host ">> build ($Config): $CMake $($buildArgs -join ' ')"
& $CMake @buildArgs
exit $LASTEXITCODE
