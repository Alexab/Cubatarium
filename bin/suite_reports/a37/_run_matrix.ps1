# A37 H6 acceptance matrix (Kick OFF). Requires bin/Cubatarium.exe built.
# Dirty trees need CUBA_ALLOW_DIRTY_AF=1 until a clean commit.
$ErrorActionPreference = "Continue"
$Root = (Resolve-Path (Join-Path $PSScriptRoot "..\..\..")).Path
Set-Location $Root
$Out = Join-Path $Root "bin\suite_reports\a37"
New-Item -ItemType Directory -Force -Path $Out | Out-Null
$env:CUBA_ALLOW_DIRTY_AF = "1"
$py = "python"
$tool = Join-Path $Root "tools\flight_sim_run.py"

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
  if ((Run-AF "warm$_" @("product-174657","--warmup-sec","20")) -ne 0) { $fails++ }
}
if ((Run-AF "far1" @("product-174657-far")) -ne 0) { $fails++ }

Copy-Item (Join-Path $Root "bin\suite_reports\a36\_summarize_matrix.py") (Join-Path $Out "_summarize_matrix.py") -Force
& $py (Join-Path $Out "_summarize_matrix.py")
Write-Host "DONE fails=$fails"
exit $fails
