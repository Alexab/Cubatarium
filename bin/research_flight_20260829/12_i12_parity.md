# I12 parity — witness stream diet + schedule recovery

**Baseline manual I10:** `perf_20260830-100455_33852.jsonl`  
**Baseline manual I11:** `perf_20260830-134006_30332.jsonl`  
**I12 target (FP-manual gate):**

| Metric | I10 `100455` | I11 `134006` | I12 target |
|--------|--------------|--------------|------------|
| `holes_rate` | 1.00 | 0.41 | ≤0.55 |
| `stream_ms` med | 76 | 131 | **≤90** |
| `prep_refresh` med | ~39 | 68 | **≤45** |
| `schedule_ok` med | 5 | 0 | **≥3** |
| `miss_stuck` max | 88s | 304s | ≤60s |
| `post_stop_VB` max | 32 | 28 | ≤10 |
| `fm_dirty_to_gpu_finish` max | — | 0 | ≥1 |
| `phase_budget_over` | — | 92.5% | ≤50% |

## I12 phases landed

| Phase | Delivered |
| --- | --- |
| I12-A0 | `visual_holes` SoT for diet gates, `rim_witness_idle_diet`, miss probe throttle, latch fix |
| I12-A | Rim cruise fast-path, ring reuse, witness retire (`ShouldRetireStaleRimMissWitness`) |
| I12-A7 | Incremental `focus_dirty` ring cache + reconcile telem |
| I12-B | `prep_refresh_dirty/eval/underfeet_probe_ms`, audit script v2 |
| I12-C | FM enqueue under completion stuck, `hole_drain_empty_fm` guard, classifier v4 |
| I12-D | Coord-matched `FmDirtyToGpuFinish`, post-MarkRelit reserve, witness retarget |
| I12-E | Relight apply boost gate, stop dark-face mesh_drain bind |
| I12-F | This doc, segment regression fields, FP-manual protocol |

## Verification

```powershell
cmake --build build/desktop-msvc --config Release --target Cubatarium miss_first_mesh_class_test
python tools/flight_sim_analyze.py bin/logs/perf_<ts>.jsonl --manual-idle --report bin/suite_reports/manual_<ts>_analyze.json
python bin/scripts/budget_reality.py bin/logs/perf_<ts>.jsonl
python bin/research_flight_20260829/scripts/refresh_pressure_audit.py bin/logs/perf_<ts>.jsonl
python bin/research_flight_20260829/scripts/segment_regression.py <i10_log> <i12_log>
```

**Rollback trigger:** `holes_rate > 0.55` OR `stream_ms` med не падает ≥15% vs `134006` после I12-A0+A7.
