param(
  [ValidateRange(1, 64)]
  [int]$Jobs = 8,
  [switch]$CleanFirst
)

$repoRoot = Split-Path -Parent $PSScriptRoot
$buildDir = Join-Path $repoRoot 'bin'
if (-not (Test-Path -LiteralPath (Join-Path $buildDir 'CMakeCache.txt'))) {
  Write-Error "No configured CMake build found at $buildDir"
  exit 2
}

$buildArgs = @(
  '--build', $buildDir,
  '--config', 'Release',
  '--target', 'Cubatarium',
  '--parallel', "$Jobs"
)
if ($CleanFirst) {
  $buildArgs += '--clean-first'
}
# CMake's Visual Studio generator currently sets CL_MPCount=1 even when
# --parallel is greater than one. Forward the same worker count after CMake's
# generated property so the MSVC /MP compiler actually uses multiple workers.
$buildArgs += @('--', "/p:CL_MPCount=$Jobs")

Push-Location $repoRoot
try {
  & cmake @buildArgs
  $buildExitCode = $LASTEXITCODE
}
finally {
  Pop-Location
}

exit $buildExitCode
