# Run GPU P* tail unit tests (no GL).
$ErrorActionPreference = "Stop"
$Root = Split-Path -Parent $PSScriptRoot
$Build = Join-Path $Root "build/desktop-msvc"
$Cfg = "Release"

$targets = @(
    "gpu_skylight_merge_test",
    "fluid_surface_pack_reuse_test",
    "gpu_skylight_column_seed_test",
    "gpu_greedy_face_extract_test",
    "gpu_fluid_column_scan_test",
    "render_backend_factory_test"
)

cmake --build $Build --config $Cfg --parallel 8 @($targets | ForEach-Object { "--target"; $_ })
if ($LASTEXITCODE -ne 0) { exit $LASTEXITCODE }

$failed = 0
foreach ($t in $targets) {
    $exe = Join-Path $Build "$Cfg\$t.exe"
    if (-not (Test-Path $exe)) {
        Write-Error "Missing $exe"
        exit 1
    }
    Write-Host "==> $t"
    & $exe
    if ($LASTEXITCODE -ne 0) { $failed++ }
}

if ($failed -gt 0) {
    Write-Error "$failed test executable(s) failed"
    exit 1
}
Write-Host "gpu tail unit tests: all ok"
