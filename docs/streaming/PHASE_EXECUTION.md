# Phase Execution Log

Автоматический учёт прогонов фаз streaming (autofly `--teleport-cruise`).

| Phase | Branch | Commit | Report | sticky | nr_end | fd_end | holes_rate | wall_med | F2 gate | Notes |
|-------|--------|--------|--------|--------|--------|--------|------------|----------|---------|-------|
| baseline_3589c59f | perf | 55c54a18 | bin/phase_baseline_3589c59f.json | 9 | 90 | 604 | 0.56 | 169 | FAIL | post P0-v1+GUI; nrΔ+54 регресс |
| P0-v1 | perf | 3589c59f | — | 0 | — | — | — | — | — | committed P0+F2 partial (session) |
| P0-v2 | streaming/phase-p0 | TBD | TBD | | | | | | | cruise cold relight ingress |

Формат дополнения: после каждого autofly запуска `python tools/phase_run_record.py`.
