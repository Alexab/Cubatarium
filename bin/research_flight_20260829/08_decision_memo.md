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

## Autofly verification (2026-08-29 post A→C)

| Run | Result | Highlights |
| --- | --- | --- |
| `fz-manual-plateau` | **FP-manual GO** | schedule_ok **4**, capture_retarget **0**, holes **0.51** |
| FP1 | NO-GO | miss_stuck 16s (limit 15s) |
| FP2 | NO-GO | stream_ms 104 (autofly overhead) |

## SHIP status

**PARTIAL** — FP-manual autofly PASS; manual hand-flight re-verify recommended; fz-manual-long pending.
