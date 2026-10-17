@echo off
cd /d E:\Work\Home\Cubatarium
set PYTHONUTF8=1
python -X utf8 tools/n01_v21_scorecard.py bin/logs/perf_20260921-105534_4864.jsonl -o bin/suite_reports/g1_a10_relight/sysreset_v4_cold_score.json --label sysreset_v4_cold
python -X utf8 tools/n01_v21_scorecard.py bin/logs/perf_20260921-105727_25268.jsonl -o bin/suite_reports/g1_a10_relight/sysreset_v4_warm_score.json --label sysreset_v4_warm
python -X utf8 tools/n01_v21_scorecard.py bin/logs/perf_20260921-105919_5900.jsonl -o bin/suite_reports/g1_a10_relight/sysreset_v4_dive_score.json --label sysreset_v4_dive
python -X utf8 bin/_v4_extract_metrics.py bin/logs/perf_20260921-105534_4864.jsonl
python -X utf8 bin/_v4_extract_metrics.py bin/logs/perf_20260921-105727_25268.jsonl
python -X utf8 bin/_v4_extract_metrics.py bin/logs/perf_20260921-105919_5900.jsonl
echo DONE
