# Phase Execution Log

Автоматический учёт прогонов фаз streaming (autofly `--teleport-cruise`).

| Phase | Branch | Commit | Report | sticky | nr_end | fd_end | holes_rate | wall_med | F2 gate | Notes |
|-------|--------|--------|--------|--------|--------|--------|------------|----------|---------|-------|
| baseline_3589c59f | perf | 55c54a18 | bin/phase_baseline_3589c59f.json | 9 | 90 | 604 | 0.56 | 169 | FAIL | post P0-v1+GUI; nrΔ+54 регресс |
| P0-v2 | streaming/phase-p0 | b119887d | bin/phase_P0_v2.json | 0 | 28 | 371 | 0.55 | 75 | FAIL | nrΔ−36; лучший P0 |
| F2_v1 | streaming/phase-f2 | 82ae3279 | bin/phase_F2_v1.json | 0 | 37 | 403 | 0.54 | 83 | FAIL | ранний порог fd регресс |
| final_combined | streaming/phase-4-unified | ba98cfb9 | bin/phase_final_combined.json | 0 | 25 | 358 | 0.54 | 76 | FAIL | **лучший** nrΔ−62 fdΔ−59 |

Полный отчёт: `PHASE_EXECUTION_REPORT.md`.

Формат дополнения: после каждого autofly запуска `python tools/phase_run_record.py --phase <id> --report <json> --note "<text>"`.
