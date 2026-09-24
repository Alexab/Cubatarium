# A39 Gate 8 matrix — Release bin/Cubatarium.exe
$ErrorActionPreference = "Continue"
$Root = (Resolve-Path (Join-Path $PSScriptRoot "..\..\..")).Path
Set-Location $Root
$Out = Join-Path $Root "bin\suite_reports\a39"
New-Item -ItemType Directory -Force -Path $Out | Out-Null
$py = "python"
$tool = Join-Path $Root "tools\flight_sim_run.py"
$env:CUBA_FLIGHT_MOVE_SPEED_SCALE = "12"
# Iteration AF allowed while A39 uncommitted; Gate 8 needs clean SHA rebind.
if (-not $env:CUBA_ALLOW_DIRTY_AF) { $env:CUBA_ALLOW_DIRTY_AF = "1" }

function Run-AF([string]$name, [string[]]$extra) {
  $json = Join-Path $Out "$name.json"
  $log = Join-Path $Out "af_$name.log"
  Write-Host "=== $name $(Get-Date -Format o) ==="
  $all = @("-X","utf8",$tool,"--scenario") + $extra + @("--visible","--report",$json)
  & $py @all *> $log
  Write-Host "exit=$LASTEXITCODE -> $json"
  return $LASTEXITCODE
}

$fails = 0
1..5 | ForEach-Object {
  if ((Run-AF "cold$_" @("product-174657")) -ne 0) { $fails++ }
}
1..2 | ForEach-Object {
  $env:CUBA_FLIGHT_WARM = "1"
  if ((Run-AF "warm$_" @("product-174657","--warmup-sec","20")) -ne 0) { $fails++ }
  Remove-Item Env:CUBA_FLIGHT_WARM -ErrorAction SilentlyContinue
}
$env:CUBA_FLIGHT_MOVE_SPEED_SCALE = "28"
if ((Run-AF "far1" @("product-174657-far")) -ne 0) { $fails++ }
$env:CUBA_FLIGHT_MOVE_SPEED_SCALE = "12"
Write-Host "DONE fails=$fails (dirty AF — not merge_green)"
exit $fails
