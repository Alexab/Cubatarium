# H0 Baseline — autofly vs manual

HEAD `51f8dba7` (hotfix3 autofly vs manual 124859).

| Metric | Manual 124859 (fly) | Autofly hotfix3 | Ratio |
| --- | ---: | ---: | ---: |
| wall_ms_fly_med | 277.8 | 375.0 | 1.35× |
| stream_ms | 283.0 | 338.9 | 1.20× |
| mesh_emerge_ms | 122.3 | 40.6 | 0.33× |
| holes_rate | 95.2% | 39.8% | 0.42× |
| schedule_ok_zero_rate | 32.2% | 48.9% | 1.52× |
| dominant_schedule_blocker | unknown_no_skip | empty_fm_queue | — |
| pool_unsync_uploads_med | — | 184 | — |
| miss_stuck_max_run_sec | — | — | — |
| witness_latch_diet_share | 61.3% | 20.3% | 0.33× |

## Parity gap (quantified)

1. **Holes telemetry mismatch:** manual effective_holes 95% vs autofly 40% with `visual_holes_telemetry_mismatch_frames` 93/83 periods — autofly under-reports hole class.
2. **Schedule starvation:** autofly `empty_fm_queue` blocker 49% `schedule_ok_zero` vs manual `dirty_fm_med=4`.
3. **Wall fly:** autofly +35% vs manual despite lower emerge — stream/prep debt dominates.
4. **Diet ineffectiveness:** witness_latch_diet_share 20% autofly vs 61% manual.

## Segment medians

Run: `python bin/research_mesh_arch_20260901/scripts/segment_medians.py`
