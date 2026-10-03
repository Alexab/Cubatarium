# I14 parity — visual stability без потери edit-readiness

**Baseline manual I12:** `perf_20260830-150840_16380.jsonl`  
**Baseline manual I13-C:** `perf_20260830-194104_35424.jsonl`  
**I14 target (FP-manual gate):**

| Metric | I12 `150840` | I13-C `194104` | I14 target |
|--------|--------------|----------------|------------|
| `stream_ms` med | 82 | 83 | **≤90** |
| `prep_refresh` med | ~41 | ~43 | **≤45** |
| `mesh_emerge_ms` | 16 | **37** | **≤22** |
| `emerge_spike_frac` | 1.9% | **13.3%** | **≤5%** |
| `opaque_idle_churn_max` | 260 | **450** | **≤120** |
| `chunk_not_ready` med | — | **6** | **≤4** |
| `schedule_ok` med | 4 | 0 | **≥2** |
| `post_stop_VB` max | 95 | 89 | **≤10** |
| `edit_immediate` med | — | 2 | **сохранить** |

## I14 phases landed

| Phase | Delivered |
| --- | --- |
| I14-A | Rim witness diet hysteresis (`ShouldUseRimWitnessIdleDiet`), ring reuse TTL 16f |
| I14-B | Ingress drawable hold + retarget rate-limit 8f (`ShouldHoldDrawableDuringIngressGpuPending`) |
| I14-C | Cruise ingress seam remesh damp, `ShouldCapRemeshOnCruiseIngress` |
| I14-D | Visual sub-gates (`emerge_spike_frac`, `chunk_not_ready_med`), `chunk_swap_audit.py` |
| I14-E | `empty_fm_schedule_starved` speed clamp 0.85 |
| I14-F | Stop VB mesh_drain floor when `VisibleBlackFocusN>0` |

## Verification

```powershell
cmake --build build/desktop-msvc --config Release --target Cubatarium miss_first_mesh_class_test
python tools/flight_sim_analyze.py bin/logs/perf_<ts>.jsonl --manual-idle --report bin/suite_reports/manual_<ts>_analyze.json
python tools/flight_sim_phase_gate.py --phase-id FP-manual --report bin/suite_reports/manual_<ts>_analyze.json
python bin/research_flight_20260829/scripts/chunk_swap_audit.py bin/logs/perf_<ts>.jsonl bin/logs/perf_20260830-194104_35424.jsonl
python bin/research_flight_20260829/scripts/segment_regression.py bin/logs/perf_20260830-194104_35424.jsonl bin/logs/perf_<ts>.jsonl
```

**Rollback trigger:** `mesh_emerge_ms` med не падает ≥30% vs `194104` после I14-A+B, OR `chunk_meshed_unlit` med регрессирует >1 на ingress-heavy route.

**Status:** I14 code откачен (2026-08-30). См. [`14_i14_bisect.md`](14_i14_bisect.md) и [`.cursor/plans/i14b_vb_spiral.plan.md`](../../.cursor/plans/i14b_vb_spiral.plan.md).
