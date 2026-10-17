@echo off
cd /d E:\Work\Home\Cubatarium
set CUBA_FLIGHT_FOG_ON=1
set PYTHONUTF8=1
echo === v6 AF cold ===
python -X utf8 tools/flight_sim_run.py --world World_164 --scenario product-174657
if errorlevel 1 echo WARN: cold flight exit %errorlevel%
for /f "delims=" %%F in ('dir /b /o-d bin\logs\perf_*.jsonl 2^>nul') do (
  set COLD_PERF=bin\logs\%%F
  goto :cold_score
)
:cold_score
echo cold perf=%COLD_PERF%
python -X utf8 tools/n01_v21_scorecard.py %COLD_PERF% -o bin/suite_reports/g1_a10_relight/sysreset_v6_cold_score.json --label sysreset_v6_cold

echo === v6 AF warm ===
python -X utf8 tools/flight_sim_run.py --world World_164 --scenario product-174657 --warmup-sec 20
if errorlevel 1 echo WARN: warm flight exit %errorlevel%
for /f "delims=" %%F in ('dir /b /o-d bin\logs\perf_*.jsonl 2^>nul') do (
  set WARM_PERF=bin\logs\%%F
  goto :warm_score
)
:warm_score
echo warm perf=%WARM_PERF%
python -X utf8 tools/n01_v21_scorecard.py %WARM_PERF% -o bin/suite_reports/g1_a10_relight/sysreset_v6_warm_score.json --label sysreset_v6_warm

echo === v6 AF dive ===
python -X utf8 tools/flight_sim_run.py --world World_164 --scenario product-174657-dive
if errorlevel 1 echo WARN: dive flight exit %errorlevel%
for /f "delims=" %%F in ('dir /b /o-d bin\logs\perf_*.jsonl 2^>nul') do (
  set DIVE_PERF=bin\logs\%%F
  goto :dive_score
)
:dive_score
echo dive perf=%DIVE_PERF%
python -X utf8 tools/n01_v21_scorecard.py %DIVE_PERF% -o bin/suite_reports/g1_a10_relight/sysreset_v6_dive_score.json --label sysreset_v6_dive

echo COLD=%COLD_PERF%
echo WARM=%WARM_PERF%
echo DIVE=%DIVE_PERF%
echo === v6 AF suite done ===
