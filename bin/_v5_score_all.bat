@echo off
cd /d E:\Work\Home\Cubatarium
set PYTHONUTF8=1
python -X utf8 tools/n01_v21_scorecard.py bin/logs/perf_20260921-131206_21928.jsonl -o bin/suite_reports/g1_a10_relight/sysreset_v5_cold_score.json --label sysreset_v5_cold
python -X utf8 tools/n01_v21_scorecard.py bin/logs/perf_20260921-131404_24492.jsonl -o bin/suite_reports/g1_a10_relight/sysreset_v5_warm_score.json --label sysreset_v5_warm
python -X utf8 tools/n01_v21_scorecard.py bin/logs/perf_20260921-131604_11948.jsonl -o bin/suite_reports/g1_a10_relight/sysreset_v5_dive_score.json --label sysreset_v5_dive
python -X utf8 bin/_v4_extract_metrics.py bin/logs/perf_20260921-131206_21928.jsonl
python -X utf8 bin/_v4_extract_metrics.py bin/logs/perf_20260921-131404_24492.jsonl
python -X utf8 bin/_v4_extract_metrics.py bin/logs/perf_20260921-131604_11948.jsonl
python -X utf8 bin/_v4_extract_metrics.py bin/logs/perf_20260921-112357_29496.jsonl
echo DONE
