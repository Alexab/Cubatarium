# Smoke-check a Windows install or staged bin/ without launching the GUI.
param(
    [string]$BinDir = ""
)

$ErrorActionPreference = "Stop"

$repoRoot = Split-Path -Parent $PSScriptRoot
if (-not $BinDir) {
    $BinDir = Join-Path $repoRoot "bin"
}

$exe = Join-Path $BinDir "Cubatarium.exe"
if (-not (Test-Path $exe)) {
    Write-Error "Cubatarium.exe not found: $exe"
}

function Invoke-CubatariumCli {
    param(
        [string]$Label,
        [string[]]$Arguments
    )
    Write-Host ">> doctor-windows: $Label"
    $proc = Start-Process -FilePath $exe -ArgumentList $Arguments `
        -WorkingDirectory $BinDir -Wait -PassThru -NoNewWindow
    if ($proc.ExitCode -ne 0) {
        throw "Cubatarium.exe $($Arguments -join ' ') failed (exit $($proc.ExitCode))"
    }
}

Invoke-CubatariumCli -Label "smoke-packs" -Arguments @("--smoke-packs")
Invoke-CubatariumCli -Label "validate-load" -Arguments @("--validate-load")

Write-Host ">> doctor-windows: PASS"
