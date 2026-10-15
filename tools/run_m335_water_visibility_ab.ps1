[CmdletBinding()]
param(
    [ValidateRange(0, 512)]
    [int]$EndMarginBlocks = 0,
    [switch]$EnableFogPullIn,
    [string]$World = "World_M572_Cold_20261014",
    [int]$ProbeMinCameraX = -4710,
    [int]$ProbeMaxCameraX = -4550,
    [int]$StartX = 120,
    [int]$StartY = 56,
    [int]$StartZ = 56,
    [int]$CruiseEyeY = 70,
    [int]$Yaw = 180,
    [int]$Pitch = -30,
    [ValidateRange(30, 1800)]
    [int]$FlyPhaseSeconds = 1800,
    [ValidateRange(20, 300)]
    [int]$StopPhaseSeconds = 20,
    [ValidateRange(8, 300)]
    [int]$StopAfterBlockedSeconds = 8,
    [ValidateRange(8, 300)]
    [int]$IdleSeconds = 15,
    [string]$PhaseId,
    [string]$Report = "bin/suite_reports/engine_refactor/m584_m572_water_fog_margin0_20261015.json"
)

$ErrorActionPreference = "Stop"
if ($ProbeMinCameraX -ge $ProbeMaxCameraX) {
    throw "ProbeMinCameraX must be smaller than ProbeMaxCameraX"
}
$repoRoot = (Resolve-Path (Join-Path $PSScriptRoot "..")).Path
$configPath = Join-Path $repoRoot "bin\config.json"
$configOriginal = [System.IO.File]::ReadAllBytes($configPath)
$utf8NoBom = [System.Text.UTF8Encoding]::new($false)
$environmentNames = @(
    "CUBA_DEMAND_SHADOW",
    "CUBA_FLIGHT_FOG_ON",
    "CUBA_WORLD_COLUMN_SOURCE_TRACE",
    "CUBA_VISUAL_BLACK_TRACE",
    "CUBA_VISUAL_BLACK_TRACE_DENSE_PIXELS",
    "CUBA_VISUAL_BLACK_TRACE_PIXEL_MIN_X",
    "CUBA_VISUAL_BLACK_TRACE_PIXEL_MAX_X",
    "CUBA_VISUAL_BLACK_TRACE_GPU_RANGE_WITNESS",
    "CUBA_FLUID_RENDER_TRACE",
    "CUBA_FLUID_RENDER_MIN_X",
    "CUBA_FLUID_RENDER_MAX_X",
    "CUBA_DEBUG_MARK_TRANSPARENT_FRAGMENTS",
    "CUBA_DEBUG_FLUID_FRAGMENT_MARKER",
    "CUBA_DEBUG_TRANSPARENT_SINGLE_PASS",
    "CUBA_DEBUG_DISABLE_OPAQUE_DEPTH_GUARD",
    "CUBA_FOCUS_SLICE_TRACE_PER_STATE",
    "CUBA_VISUAL_BLACK_TRACE_PIXEL_ON_SCREEN_RAY",
    "CUBA_VISUAL_BLACK_TRACE_PIXEL_ON_STALLED_CENSUS",
    "CUBA_VISUAL_BLACK_TRACE_FOCUS_PROBES"
)
$environmentOriginal = @{}
foreach ($name in $environmentNames) {
    $environmentOriginal[$name] = [Environment]::GetEnvironmentVariable($name, "Process")
}

$locationPushed = $false
$runExitCode = 1
try {
    Push-Location $repoRoot
    $locationPushed = $true

    $config = [System.Text.Encoding]::UTF8.GetString($configOriginal) | ConvertFrom-Json -AsHashtable
    $config["render"]["distance_fog_end_margin_blocks"] = $EndMarginBlocks
    $configJson = ConvertTo-Json -InputObject $config -Depth 100
    [System.IO.File]::WriteAllText($configPath, $configJson + "`n", $utf8NoBom)

    $environmentValues = @{
        CUBA_DEMAND_SHADOW = "0"
        CUBA_WORLD_COLUMN_SOURCE_TRACE = "1"
        CUBA_VISUAL_BLACK_TRACE = "1"
        CUBA_VISUAL_BLACK_TRACE_DENSE_PIXELS = "0"
        CUBA_VISUAL_BLACK_TRACE_PIXEL_MIN_X = [string]$ProbeMinCameraX
        CUBA_VISUAL_BLACK_TRACE_PIXEL_MAX_X = [string]$ProbeMaxCameraX
        CUBA_VISUAL_BLACK_TRACE_GPU_RANGE_WITNESS = "1"
        CUBA_FLUID_RENDER_TRACE = "1"
        CUBA_FLUID_RENDER_MIN_X = [string]$ProbeMinCameraX
        CUBA_FLUID_RENDER_MAX_X = [string]$ProbeMaxCameraX
        CUBA_DEBUG_MARK_TRANSPARENT_FRAGMENTS = "0"
        CUBA_DEBUG_FLUID_FRAGMENT_MARKER = "0"
        CUBA_DEBUG_TRANSPARENT_SINGLE_PASS = "0"
    }
    foreach ($entry in $environmentValues.GetEnumerator()) {
        [Environment]::SetEnvironmentVariable($entry.Key, $entry.Value, "Process")
    }
    if ($EnableFogPullIn) {
        [Environment]::SetEnvironmentVariable("CUBA_FLIGHT_FOG_ON", "1", "Process")
    }
    $environmentToClear = @(
        "CUBA_FLIGHT_FOG_ON",
        "CUBA_DEBUG_DISABLE_OPAQUE_DEPTH_GUARD",
        "CUBA_FOCUS_SLICE_TRACE_PER_STATE",
        "CUBA_VISUAL_BLACK_TRACE_PIXEL_ON_SCREEN_RAY",
        "CUBA_VISUAL_BLACK_TRACE_PIXEL_ON_STALLED_CENSUS",
        "CUBA_VISUAL_BLACK_TRACE_FOCUS_PROBES"
    )
    if ($EnableFogPullIn) {
        $environmentToClear = $environmentToClear | Where-Object { $_ -ne "CUBA_FLIGHT_FOG_ON" }
    }
    foreach ($name in $environmentToClear) {
        [Environment]::SetEnvironmentVariable($name, $null, "Process")
    }

    $pullInTag = if ($EnableFogPullIn) { "pullin_on" } else { "pullin_off" }
    if ([string]::IsNullOrWhiteSpace($PhaseId)) {
        if ($StartX -eq 120 -and $StartY -eq 56 -and $StartZ -eq 56 -and
            $FlyPhaseSeconds -eq 1800 -and $StopPhaseSeconds -eq 20 -and
            $IdleSeconds -eq 15) {
            $PhaseId = "m335_water_endmargin_{0}_{1}" -f $EndMarginBlocks, $pullInTag
        } else {
            $PhaseId = "water_target_x{0}_fly{1}_{2}" -f $StartX, $FlyPhaseSeconds, $pullInTag
        }
    }
    $runSeconds = $IdleSeconds + $FlyPhaseSeconds + $StopPhaseSeconds + 5
    & python -X utf8 tools\flight_sim_run.py `
        --scenario product-174657-far `
        --world $World `
        --seconds $runSeconds `
        --fly-stop `
        --fly-phase-sec $FlyPhaseSeconds `
        --stop-phase-sec $StopPhaseSeconds `
        --stop-after-blocked-sec $StopAfterBlockedSeconds `
        --idle-sec $IdleSeconds `
        --visible `
        --resume `
        --product-start-position $StartX $StartY $StartZ `
        --cruise-eye-y $CruiseEyeY `
        --yaw $Yaw `
        --pitch $Pitch `
        --report $Report `
        --phase-id $PhaseId
    $runExitCode = $LASTEXITCODE
}
finally {
    [System.IO.File]::WriteAllBytes($configPath, $configOriginal)
    foreach ($name in $environmentNames) {
        [Environment]::SetEnvironmentVariable($name, $environmentOriginal[$name], "Process")
    }
    if ($locationPushed) {
        Pop-Location
    }
}

exit $runExitCode
