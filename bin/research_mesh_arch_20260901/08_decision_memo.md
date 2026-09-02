# Decision memo — mesh architecture post-R1.5 debt closure (2026-09-02)

**Status: R2.6–R4.1 LANDED — SHIP NO-GO (manual gate-of-record pending)**

## Sprint verdict

| Sprint | Verdict | Key evidence |
| --- | --- | --- |
| R1.5 capture loop | **GO** | retry 74, schedule_ok 7 on manual `173028` |
| R2.6 forensics | **GO** | completion_chain + wall_waterfall audits; analyzer stall/FPS metrics |
| R2.7 ColumnJobGraph | **GO (code)** | SyncFocusRingColumnJobStages; `column_job_graph_stage_test` PASS |
| R2.8 completion chain | **INTERIM** | `fm_finish=1` fly-heavy; 0 fz-long |
| R3.1 M4 + witness | **PARTIAL** | stage guard; diet 1–15% autofly |
| R3.0 frame budget | **INTERIM** | fz-long wall_fly 130ms (~7.7 FPS) vs 330ms manual |
| R2.9 store diet | **GO (code)** | store hit on TryGet; `capture_incremental_test` PASS |
| R4.1 harness | **GO** | trio + gates; replay teardown/travel fix |
| SHIP | **NO-GO** | holes 84%, witness &lt;2%, manual 3min not re-run |

## Manual SoT `173028` (pre-fix baseline)

| Metric | Value |
| --- | --- |
| wall_ms_fly | 330ms (~3 FPS) |
| stream_ms | 199ms |
| holes_rate | 70% |
| witness_diet | 24% |
| fm_to_gpu_finish | 0 |

## Gates

```powershell
python tools/flight_sim_phase_gate.py --phase-id MESH-R26-completion --report bin/suite_reports/manual_*.json
python tools/flight_sim_phase_gate.py --phase-id MESH-R30-fps --report bin/suite_reports/manual_*.json
python tools/flight_sim_run.py --replay-manual --phase-id MESH-R15-capture --report bin/suite_reports/mesh_debt_trio_replay.json
```

