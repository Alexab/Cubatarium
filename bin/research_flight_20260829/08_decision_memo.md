# 08 — Decision memo (flight perf iteration D→E)

## Status: **NO-SHIP**

Hand-flight SoT [`perf_20260829-152403_29440.jsonl`](../logs/perf_20260829-152403_29440.jsonl) — FP-manual **FAIL**; autofly plateau **GO**. Manual/autofly drift is the blocker.

| Gate | Hand 152403 | Autofly plateau |
| --- | --- | --- |
| `schedule_ok` med | 1 | 4 |
| `capture_retarget` med | 16 | 0 |
| `holes_rate` | 0.58 | 0.59 |
| `miss_stuck` | 106s | 16s |
| `post_stop_visible_black_max` | 94 | — |

## Iteration A→C (LANDED, exit criteria NOT met on hand)

Code delivered; hand-flight verification failed. Root cause unchanged: **empty_fm_queue** (58% schedule_ok=0 frames), witness retarget thrash, inverted B3 VB gate.

Forensics pack: `raw/fm_refill_152403.txt`, `raw/witness_bypass_152403.txt`, `raw/fm_enqueue_drain_152403.txt`, `raw/perf_regression_142846_vs_152403.txt`

## Iteration D — Manual FM starvation (IN PROGRESS)

| Change | Target |
| --- | --- |
| FM dirty persistence (RAA park bypass) | `dirty_fm_n` med >0 |
| HoleDrain carve-out (90f, FM floor 6) | mode=3 share <60% |
| Witness hard-block promote/retarget | `capture_retarget` ≤5 |

## Iteration E — VB consume + stop drain (PENDING)

| Change | Target |
| --- | --- |
| Fix B3 inverted VB gate | `vb_no_ticket` drain |
| Miss stuck ColumnFlow enqueue | `miss_stuck` ≤15s |
| Stop-phase visible black drain | `post_stop_visible_black_max` ≤10 |

## Iteration F — Perf (PENDING)

| Change | Target |
| --- | --- |
| ColumnVisualSnapshot wire | `stream_ms` ≤50 |
| Capture slim + retarget block | retarget↓ |
| Emerge prep budget cap | `mesh_emerge_ms` ≤20 |

## Autofly verification (baseline)

| Run | Result |
| --- | --- |
| `fz-cold-enter` | FP-enter GO |
| `fz-manual-plateau` | FP-manual GO (autofly only) |
| `fz-manual-long` | FP0 GO; cruise FAIL |

## Iteration D→E — CODE LANDED (2026-08-29)

| Block | Status |
| --- | --- |
| D1 FM persistence | `fm_dirty_drain_n` telem, RAA park bypass, MarkMissing enqueue |
| D2 HoleDrain carve-out | 90f window, schedule_ok trigger, first_mesh floor 6 |
| D3 Witness hard-block | Promote guard, repair/pin promote, capture retarget block |
| E0 VB consume | inverted high-debt gate, ring top-K=3, stop 60s Capture budget |
| E1 Miss stuck | force enqueue ≥10s, FIFO miss-rim nh≤4 protect |
| E2 Stop drain | stop carve-out, VB stuck priority escalate, gates |
| F1 stream cache | FocusRingVisualSample VB/CLNM epoch fields |
| F2 Capture slim | cap=1 when schedule_ok<2, finalize nh≤2 moving, Apply floor |
| F3 emerge cap | prep_over_budget skips stale wave + admit clamp |

**Build:** `miss_first_mesh_class_test` PASS (Debug).

## Iteration I9 — RefreshPressure diet + FM enqueue + miss SLA (2026-08-29)

**Status: CODE LANDED, FP-manual NO-GO on autofly**

| Phase | Delivered |
| --- | --- |
| I9-A | `prep_refresh_*` sub-timers in `PhysicsTelemetry` + `FramePerfMonitor`; `refresh_pressure_audit.py`; `budget_reality.py` extended |
| I9-B | Cruise fast-path (darkface/facing skip), miss probe throttle 4f/8f hold, VB cadence, pending-light radius clamp, focus_dirty sample cd=8 |
| I9-C | FM reserve pre-rebuild capture; cruise ColumnFlow drain + underfeet FirstMesh when `dirty_fm==0 && schedule_ok<4` |
| I9-D | Miss SLA 1 period at nh≤1; `pin_isolated_miss(120)`; ColumnFlow FirstMesh in pressure path at miss_horiz≤1 |
| I9-E | `09_i9_parity.md` manual vs autofly protocol |

**Autofly `--replay-manual` (I9-full, `perf_20260829-230219`):**

| Metric | I8-full | I9-full | Target |
| --- | --- | --- | --- |
| `holes_rate` | 0.024 | 0.15 | ≤0.55 |
| `stream_ms` | 184 | 176 | ≤120 |
| `prep_refresh` med | ~81 | ~81 | ≤25 |
| `schedule_ok` | 0 | 0 | ≥3 |
| `miss_stuck` | 40s | 40s | ≤30s |
| `post_stop_VB` | 4 | 4 | ≤10 |

Forensics: sub-timer sum ≈11% of `prep_refresh` — dominant untimed cost is `GetColumnRenderableState` underfeet scan (~60–80ms). Skipping it on cruise caused `holes_rate` regression → reverted; underfeet scan kept every frame.

**Blocker unchanged:** `dominant_schedule_blocker=empty_fm_queue`, `dirty_fm_med=0` on cruise despite C2 enqueue.

**Next:** hand-flight World_164 ≥2 min (gate of record); tune FM admit path when `AdmitFocusVisibleMissing` returns 0 on cruise.

## Iteration I10 — Visual drain + schedule throughput + perf hold (2026-08-30)

**Status: CODE LANDED, FP-manual hand-verify pending**

| Phase | Delivered |
| --- | --- |
| I10-A | VB stop drain: stalled-aware consume, ring top-K=6, stop telem, `ticketed_consume_scan` on stop, `vb_stop_drain_audit.py` |
| I10-B | Relight FIFO rim pin nh≤4, apply throughput floor under VB debt, witness pin stickiness |
| I10-C | Miss SLA v2 telem, nh=0 fast path, SLA nh≤2, `miss_stuck_forensics.py` |
| I10-D | `skip_outside_focus` telem, classifier fix, `fm_consumer_starved` eff_cap soften |
| I10-E | HoleDrain rim-only WarmBacklog exit, mode3 schedule floor |
| I10-F | `prep_refresh_underfeet_ms`, incremental underfeet cy-band, FocusRing sticky reuse |
| I10-G | `10_i10_parity.md`, FP-manual gate protocol |

**Build:** `miss_first_mesh_class_test` — run after merge.

**Rollback trigger:** `holes_rate > 0.55` OR `stream_ms` med +20% vs manual `085951`.

## SHIP verdict: **NO-SHIP** (I10 code landed; FP-manual hand flight pending)

