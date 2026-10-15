[CmdletBinding()]
param(
    [ValidateRange(0, 512)]
    [int]$EndMarginBlocks = 0,
    [switch]$EnableFogPullIn,
    [string]$World = "World_M572_Cold_20261014",
    [string]$Report = "bin/suite_reports/engine_refactor/m584_m572_water_fog_margin0_20261015.json"
)

$ErrorActionPreference = "Stop"
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
        CUBA_VISUAL_BLACK_TRACE_PIXEL_MIN_X = "-4710"
        CUBA_VISUAL_BLACK_TRACE_PIXEL_MAX_X = "-4550"
        CUBA_VISUAL_BLACK_TRACE_GPU_RANGE_WITNESS = "1"
        CUBA_FLUID_RENDER_TRACE = "1"
        CUBA_FLUID_RENDER_MIN_X = "-4710"
        CUBA_FLUID_RENDER_MAX_X = "-4550"
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
    $phaseId = "m335_water_endmargin_{0}_{1}" -f $EndMarginBlocks, $pullInTag
    & python -X utf8 tools\flight_sim_run.py `
        --scenario product-174657-far `
        --world $World `
        --seconds 1840 `
        --fly-stop `
        --fly-phase-sec 1800 `
        --stop-phase-sec 20 `
        --stop-after-blocked-sec 8 `
        --idle-sec 15 `
        --visible `
        --resume `
        --product-start-position 120 56 56 `
        --cruise-eye-y 70 `
        --yaw 180 `
        --pitch -30 `
        --report $Report `
        --phase-id $phaseId
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
