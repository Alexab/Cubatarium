# A40 Gate 8 matrix — Release bin/Cubatarium.exe
# Cold/warm use speed scale 1 (operator-comparable). Far may raise scale only
# for distance stress — do not use ×12 on cold (distorts holes/VB/pending).
$ErrorActionPreference = "Continue"
$Root = (Resolve-Path (Join-Path $PSScriptRoot "..\..\..")).Path
Set-Location $Root
$Out = Join-Path $Root "bin\suite_reports\a40"
New-Item -ItemType Directory -Force -Path $Out | Out-Null
$py = "python"
$tool = Join-Path $Root "tools\flight_sim_run.py"
$env:CUBA_FLIGHT_MOVE_SPEED_SCALE = "1"
# Iteration AF allowed while A40 uncommitted; Gate 8 needs clean SHA rebind.
if (-not $env:CUBA_ALLOW_DIRTY_AF) { $env:CUBA_ALLOW_DIRTY_AF = "1" }

function Run-AF([string]$name, [string[]]$extra) {
  $json = Join-Path $Out "$name.json"
  $log = Join-Path $Out "af_$name.log"
  Write-Host "=== $name scale=$($env:CUBA_FLIGHT_MOVE_SPEED_SCALE) $(Get-Date -Format o) ==="
  $all = @("-X","utf8",$tool,"--scenario") + $extra + @("--visible","--report",$json)
  & $py @all *> $log
  Write-Host "exit=$LASTEXITCODE -> $json"
  return $LASTEXITCODE
}

$fails = 0
1..5 | ForEach-Object {
  $env:CUBA_FLIGHT_MOVE_SPEED_SCALE = "1"
  if ((Run-AF "cold$_" @("product-174657")) -ne 0) { $fails++ }
}
1..2 | ForEach-Object {
  $env:CUBA_FLIGHT_MOVE_SPEED_SCALE = "1"
  $env:CUBA_FLIGHT_WARM = "1"
  if ((Run-AF "warm$_" @("product-174657","--warmup-sec","20")) -ne 0) { $fails++ }
  Remove-Item Env:CUBA_FLIGHT_WARM -ErrorAction SilentlyContinue
}
# Far distance stress: harness default scale 28 if unset; keep explicit for log.
$env:CUBA_FLIGHT_MOVE_SPEED_SCALE = "28"
if ((Run-AF "far1" @("product-174657-far")) -ne 0) { $fails++ }
$env:CUBA_FLIGHT_MOVE_SPEED_SCALE = "1"
Write-Host "DONE fails=$fails (cold/warm scale=1; far scale=28; dirty AF ≠ merge_green)"
exit $fails
