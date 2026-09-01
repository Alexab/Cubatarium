# H0 Baseline — autofly vs manual

HEAD `f62041bc` (M0–M5 implementation + H1 harness).

## Pre-change (hotfix3 vs manual 124859)

| Metric | Manual 124859 (fly) | Autofly hotfix3 | Ratio |
| --- | ---: | ---: | ---: |
| wall_ms_fly_med | 277.8 | 375.0 | 1.35× |
| stream_ms | 283.0 | 338.9 | 1.20× |
| mesh_emerge_ms | 122.3 | 40.6 | 0.33× |
| holes_rate | 95.2% | 39.8% | 0.42× |
| schedule_ok_zero_rate | 32.2% | 48.9% | 1.52× |
| dominant_schedule_blocker | unknown_no_skip | empty_fm_queue | — |
| witness_latch_diet_share | 61.3% | 20.3% | 0.33× |

## Post M0–M2 (2026-09-01 trio)

| Metric | Manual 124859 | replay-manual | fly-heavy | fz-manual-long |
| --- | ---: | ---: | ---: | ---: |
| wall_ms_fly_med | 277.8 | 44.3 | 92.0 | 83.1 |
| stream_ms | 283.0 | 10.1 | 10.1 | — |
| mesh_emerge_ms | 122.3 | 47.4 | 58.0 | — |
| holes_rate | 95.2% | 100% | 100% | 100% |
| mesh_snapshot_ms (main) | — | 0.0 | 0.0 | 0.0 |
| mesh_waterfall_drain_med | — | 103.7 | 100.2 | — |
| dominant_schedule_blocker | — | empty_fm_queue | empty_fm_queue | empty_fm_queue |
| chunks_traveled | — | 11 | 32 | 125 |
| parity_within_2x | — | PASS | PASS | PASS |

## Parity gap (quantified)

1. **Wall/stream improved** vs hotfix3 (fly-heavy wall 92ms vs 375ms) but **holes_rate regressed to 100%** on all autofly profiles.
2. **M2 success:** main `mesh_snapshot_ms=0`; waterfall drain dominates emerge (~100ms).
3. **FM starvation persists:** `dominant_schedule_blocker=empty_fm_queue`, `cruise_dirty_fm_med=0`.
4. **Witness diet inactive:** `witness_latch_diet_share=0%` on autofly vs 61% manual.

## Segment medians

```bash
python bin/research_mesh_arch_20260901/scripts/segment_medians.py bin/logs/perf_20260901-214457_12548.jsonl
python bin/research_mesh_arch_20260901/scripts/mesh_waterfall_audit.py bin/logs/perf_20260901-214457_12548.jsonl
```
