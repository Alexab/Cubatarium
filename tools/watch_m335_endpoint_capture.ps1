param(
    [Parameter(Mandatory = $true)]
    [int]$ProcessId,

    [Parameter(Mandatory = $true)]
    [string]$PerfLogPath,

    [Parameter(Mandatory = $true)]
    [string]$OutputDirectory,

    [int]$PollSeconds = 5,
    [int]$TimeoutMinutes = 70,
    [int]$ApproachFocusCx = -877,
    [int]$DeepApproachFocusCx = -883,
    [int]$StopFocusCx = -880
)

$ErrorActionPreference = 'Stop'
$perfPath = [System.IO.Path]::GetFullPath($PerfLogPath)
$outputDir = [System.IO.Path]::GetFullPath($OutputDirectory)
[System.IO.Directory]::CreateDirectory($outputDir) | Out-Null
$captureScript = Join-Path $PSScriptRoot 'capture_flight_sim_window.ps1'
$fileStream = [System.IO.File]::Open(
    $perfPath,
    [System.IO.FileMode]::Open,
    [System.IO.FileAccess]::Read,
    [System.IO.FileShare]::ReadWrite
)
$reader = [System.IO.StreamReader]::new($fileStream, [System.Text.Encoding]::UTF8)
$pending = ''
$sawNearEnd = $false
$sawStop = $false
$stationaryPeriods = 0
$deadline = [DateTimeOffset]::Now.AddMinutes($TimeoutMinutes)

function Save-FlightFrame([string]$Name) {
    $path = Join-Path $outputDir $Name
    $result = & powershell -NoProfile -File $captureScript -ProcessId $ProcessId -OutputPath $path
    if ($LASTEXITCODE -ne 0) {
        throw "Capture failed for $Name with exit code $LASTEXITCODE."
    }
    Write-Output ($result | Out-String).Trim()
}

try {
    Write-Output "Watching $perfPath for M335 samples at approach cx=$ApproachFocusCx, deep cx=$DeepApproachFocusCx, stop cx=$StopFocusCx."
    while ([DateTimeOffset]::Now -lt $deadline) {
        while ($null -ne ($line = $reader.ReadLine())) {
            if (-not $line.EndsWith('}')) {
                $pending += $line
                continue
            }

            $line = $pending + $line
            $pending = ''
            if ($line -notmatch '"kind"\s*:\s*"period"') {
                continue
            }

            $focusMatch = [regex]::Match($line, '"focus_cx"\s*:\s*(-?\d+(?:\.\d+)?)')
            if (-not $focusMatch.Success) {
                continue
            }
            $focus = [double]::Parse(
                $focusMatch.Groups[1].Value,
                [System.Globalization.CultureInfo]::InvariantCulture
            )

            $moveMatch = [regex]::Match($line, '"camera_move_requested_xz"\s*:\s*(-?\d+(?:\.\d+)?)')
            $moveRequested = if ($moveMatch.Success) {
                [double]::Parse($moveMatch.Groups[1].Value, [System.Globalization.CultureInfo]::InvariantCulture)
            } else { 0.0 }

            if (-not $sawNearEnd -and $focus -le $ApproachFocusCx) {
                $approachName = if ($ApproachFocusCx -eq -877) {
                    'approach_cx-877.png'
                } else {
                    "approach_cx$($focus).png"
                }
                Save-FlightFrame $approachName
                $sawNearEnd = $true
                Write-Output "Captured approach at focus_cx=$focus."
            }

            if ($sawNearEnd -and -not $sawStop -and $focus -le $DeepApproachFocusCx) {
                $deepApproachName = if ($DeepApproachFocusCx -eq -883) {
                    'approach_cx-883.png'
                } else {
                    "approach_cx$($focus).png"
                }
                Save-FlightFrame $deepApproachName
                $sawStop = $true
                Write-Output "Captured deep-route approach at focus_cx=$focus."
            }

            if ($sawStop -and $focus -le $StopFocusCx -and [Math]::Abs($moveRequested) -lt 0.0001) {
                $stationaryPeriods++
            } elseif ($focus -le $StopFocusCx) {
                $stationaryPeriods = 0
            }

            if ($sawStop -and $stationaryPeriods -ge 2) {
                $stopName = if ($StopFocusCx -eq -880) {
                    'stop_cx-888.png'
                } else {
                    "stop_cx$($focus).png"
                }
                Save-FlightFrame $stopName
                Write-Output "Captured stationary endpoint after $stationaryPeriods periods at focus_cx=$focus."
                return
            }
        }

        if (-not (Get-Process -Id $ProcessId -ErrorAction SilentlyContinue)) {
            Write-Output "Flight process exited before the stationary endpoint capture. near_end=$sawNearEnd stop_sample=$sawStop."
            return
        }
        Start-Sleep -Seconds $PollSeconds
    }

    Write-Output "Watcher timed out. near_end=$sawNearEnd stop_sample=$sawStop stationary_periods=$stationaryPeriods."
}
finally {
    $reader.Dispose()
    $fileStream.Dispose()
}
