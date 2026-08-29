# 08 — Decision memo (flight perf iteration A→C)

## Iteration A — Manual parity (LANDED)

| Change | Files |
| --- | --- |
| FM refill forensics scripts | `scripts/fm_refill_forensics.py`, `witness_bypass_audit.py` |
| FM enqueue telemetry | `PhysicsTelemetry`, `MarkRelitInstall`, `ColumnFlowExecutor` |
| RAA bypass HoleDrain nh≤4 | `MeshApplyPolicy`, `ChunkMeshCache` |
| Cruise MarkMissing on moving holes | `RelightFifoPolicy`, `MarkRelitInstall` |
| Witness pin promote redirect | `ColumnFlowExecutor`, `WorldStreaming` |
| HoleDrain Warm carve-out | `MeshWorkAdmission` |
| FP-manual gate | `flight_sim_phase_gate.py`, `flight_sim_analyze.py` |

**Forensics root cause:** `schedule_ok=0` → 100% `empty_fm_queue` (not skip blocking).

## Iteration B — Relight chain (LANDED)

| Change | Files |
| --- | --- |
| FP1 gate uses `relight_apply_final` | `flight_sim_phase_gate.py` |
| Miss finalize nh≤4 (lit drawable ring) | `WorldPersistence` |
| FIFO priority front-insert pin | `WorldPersistence` |
| Ticketed VB consume enqueue | `ColumnFlowExecutor` |
| keep-until-bind pending relight helper | `RelightFifoPolicy` |

## Iteration C — SHIP slice (PARTIAL)

| Change | Status |
| --- | --- |
| Research 01/05/06/02_baseline_long | DONE (long autofly pending) |
| ColumnJobGraph job stage map | DONE |
| ColumnVisualSnapshot header | DONE |
| MarkDirty lint script | DONE |
| SeedAtCommit telem | DONE |
| BackpressureLevel telem | DONE |
| Full FP-SHIP suite | **pending verification** |

## SHIP status

**PARTIAL** — code landed; await autofly FP-enter + FP-manual + fz-manual-long for gate PASS.
