@echo off
cd /d E:\Work\Home\Cubatarium
set CUBA_FLIGHT_FOG_ON=1
set PYTHONUTF8=1
set FAIL=0
echo === AF cold product-174657 fog-ON ===
python -X utf8 tools/flight_sim_run.py --world World_164 --scenario product-174657
if errorlevel 1 set FAIL=1
echo === AF warm product-174657 fog-ON ===
python -X utf8 tools/flight_sim_run.py --world World_164 --scenario product-174657 --warmup-sec 20
if errorlevel 1 set FAIL=1
echo === AF dive product-174657-dive fog-ON ===
python -X utf8 tools/flight_sim_run.py --world World_164 --scenario product-174657-dive
if errorlevel 1 set FAIL=1
echo === AF suite done FAIL=%FAIL% ===
exit /b 0
