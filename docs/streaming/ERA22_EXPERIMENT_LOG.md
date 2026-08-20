# ERA22 Experiment Log

## Scope

Track iterative experiments for land/ocean streaming stabilization with reproducible suite runs.

## Baseline and Goals

- Baseline suite: `20260819-135324` (historical reference in plan)
- Current hard blocker: `miss_end=1.0` on land stop-tail scenarios
- F3 target: `pending_light_focus_med <= 10` on `ocean-cruise`

## Experiment Timeline

| Step | Phase ID | Change | Result |
| --- | --- | --- | --- |
| B3 | `Era22_B3_stopTailOwnership_v1` | Stop-tail ownership mode for pinned `FirstMesh`; column-targeted pin/heal | `land-stand 9/23`, `fly-clean 10/23`, `miss_end=1.0` |
| B4 | `Era22_B4_stopTailFullColumn_v1` | Full-column dirty (`max_y`) + faster re-mark cadence in stop-tail | `land-stand 9/23`, `fly-clean 10/23`, `miss_end=1.0` |
| B5 | `Era22_B5_softDeferStuckHeal_v1` | Forced stuck SoftDefer witness full-column `FirstMesh` | `land-stand 9/23`, `fly-clean 9/23`, `miss_end=1.0` |
| B6 | `Era22_B6_stopMissLightGate_v1` | Allow miss-heal under small pending light debt; keep stop emerge floor | `land-stand 10/23`, `fly-clean 9/23`, `miss_end=1.0` |
| B7 | `Era22_B7_stopTailRingScan_v1` | Stop-tail full-ring scan with `cy=-2` | `land-stand 12/23`, `fly-clean 11/23`, `miss_end=1.0` |
| Full check | `Era22_F2_final_check_v1` | Full suite snapshot after F2 iterations | `0/7 passed`; best `land-south 12/23`, `ocean-cruise 7/23` |
| F3c-v1 | `Era22_F3c_oceanRelightFloor_v1` | Stronger moving capture floor under red FIFO + high pending light | `ocean-cruise 9/23`, `pending_light_focus_med=29` (target not met) |
| F4a-v1 | `Era22_F4a_dirtyBackpressure_v1` | Hard dirty backpressure (`Dirty > soft_cap*1.1` on red) | Full suite `0/7`; regression on `land-cruise` (`7/23`, `PL med=38`) |
| F4a-v2 | `Era22_F4a_v2_dirtyBackpressureSoft_v1` | Softened F4a: keep minimal dirty admit=1 | `land-cruise 9/23`, `ocean-cruise 10/23`, `fly-clean 10/23` |
| F4b-v1 | `Era22_F4b_redSkipFarPopulate_v1` | Red: clamp populate scan to focus (≤4) | Regression: `land-cruise 7/23` `red_rate=1.0` `PL med=29` `dirty_med=542`. **Reverted.** |
| F4b-v2 | `Era22_F4b_v2_skipOuterRdRings_v1` | Red: skip only outer 2 VisualRD rings | Still Red-lock: `land-cruise 7/23` `PL med=33`; `ocean-cruise 7/23`. **Reverted.** |
| F5a | `Era22_F5a_skipNoLightDeltaRemesh_v1` | Skip remesh-after-lit when drawable + no delta | `land-stand 6/23` `PL=33`, `idle-clean 9/23` opaque=192, `fly-clean 9/23` opaque=152. Target `opaque<=120` missed. **Reverted:** `light_or_voxel_delta` is FullyDark settle proxy, not a real light delta — skip was too broad. |
| F3a+F3c | `Era22_F3ac_focusProtect_apply2x_v1` | Trim protect full focus radius; overflow drop farthest; 2x cheap apply when PL>20 | Regression: `land-cruise 8/23` `PL=34` `red_rate=1.0` `fifo_dropped=556`; `ocean-cruise 7/23` `PL=38`; `completed_med` still 0. **Reverted.** Protecting the whole focus ring stops FIFO from shedding; Red-lock like F4b. |
| Capture audit | `tmp_capture_audit.py` on F4a/F3ac ocean+land | Perf jsonl: Capture vs Apply timing, fifo drops, completed ring | **Capture OK** (~0.4ms med, 100% cruise frames). **No Capture perf regression** vs pre-F4. Bottleneck = **Apply ~1 col/frame** + **FIFO overflow drops ~560/cruise**. `cruise_relight_completed_med=0` is **misleading** (ring occupancy, drained same frame). |
| F3b+starve | `Era22_F3b_starveFix_v1` | Fix starve gate (apply_ms_prev+inflight); TryEnqueue skip lit-settled; analyzer uses apply_ms | `land-cruise 9/23` **PL med=1** (was 34); `fly-clean 10/23`; `ocean-cruise 7/23` PL=39 fifo_drop=566 unchanged. **Keep starve+F3b.** |
| F3d | `Era22_F3d_deferFarEnqueue_v1` | Defer far relight enqueue when fifo≥admit_frac×cap; admit on pin-ring entry | `ocean-cruise 7/23` **PL med=22** (was 39) **fifo_drop=286** (was 566); `land-cruise 9/23` PL=3 fifo_drop=15. **Keep F3d.** |
| F3d-pending | `Era22_F3dPendingNoteFix_v1` | F3d defer branch: `NotePendingLightBeforeMesh` before return; suite/analyzer add `effective_holes_rate` + blinkiness | Suite `0/4` `EH%=100` all scenarios; `land-cruise PL=3` `ocean PL=33`; **Capture med still sub-ms** (land 0.60, ocean 0.73) but **wall_med regressed** (land 119 vs 104, ocean 135 vs 108). Manual `perf_20260820-09*` confirms `EH%=100` + high visible_black — matches user flicker/holes report. **Keep invariant fix**; visual recovery needs follow-up (not blink, stable holes). |
| F3d-pending-v2 | `Era22_F3dPendingNoteFix_v2` | Complete pending-gate on ALL defer paths (Persistence disk-load, gen hook); align NotePending band with enqueue_relight; apply partial/final + deferred_far telemetry; blink on visible_black/black_sticky | Manual re-analyze: `effective_holes_blink=0` `visible_black_blink=0` (stable bad state); **`black_sticky_blink=12–20%`** matches subjective flicker better. Partial capture rate 2–8% only — root cause is stable unfinished+VB, not partial Y-band starvation alone. |
| Sticky-settle | `Era22_StickySettleFix` | MarkRelit settle telemetry; Classify: stale FullyDark+gpu→Schedule not PreferKick; VisualReady skip only when !stale; column_settled excludes fully_dark+still_stale; suppress blocked while sticky_owned | Manual `perf_20260820-120308`: **PL med 45→1**, VB med 63→19, unfinished max 53→24, BS blink 35%→20%; telemetry **schedule_n=57 skip_quiesce=0 suppress=0** (was ownership loop). EH still 100% (stable unfinished_visual / miss_end) — separate from settle ping-pong. |
| Sticky-settle-v2 | `Era22_StickySettleFix_v2` | Manual `151735` proved v1 only helped enter: cruise **af=86 / mark_relit_decisions≈1**, PL/VB same as pre-fix. Root: already-Dirty Skip drops RAA; cruise MarkDirtySeamed never called schedule. Fix: latch RAA on SkipDirty/Inflight/PreferKick + schedule drawable on every finalize | Manual `160656` (~9min): latch **works** (skip_dirty=644) but **no visual fix**. Stand: VB=81/PL=40/dirty=217/**stale=1114 forever**, opaque churn max 436. |
| Idle-flicker | `Era22_IdleFlickerBreak` | Root: recover every 2 frames on `DarkFaceNearN>500` re-Enqueues RelightThenMesh+RemeshSeam forever while standing. Fix: idle dark watchdog 45f; no RemeshSeam on idle dark_n alone (sticky only); no Relight re-enqueue if pending/ticket owns light; RAA latch only FullyDark/stale | Manual 172208: underfeet still flickers; reason=GpuInFlight; only player column |
| Underfeet-proxy | `Era22_UnderfeetProxyRemesh` | Root: `SyncIdleFocusGreedyRemesh` fallback remeshed focus ring (dist=0=player) when sticky empty/busy; every-frame `DrainRemeshSeamBudget` + Enqueue(focus,RemeshSeam) as sticky proxy. Fix: no focus-ring fallback; idle SyncIdle only on sticky drain cadence; Enqueue RemeshSeam only if focus itself sticky | Manual 175310: flicker **worse**; 2 chunks (feet+neighbor); `underfeet_need=1` forever; `dirty_revisit≈168`; opaque_present flips |
| Underfeet-plug | `Era22_UnderfeetPlugNoHide` | Root: hide FullyDark nh≤1 on pending/stale flip blanks Satisfying plugs; `underfeet_need` latched by neighbor missing/pending (horiz≤1). Fix: no hide stale / nh≤1 FullyDark; `underfeet_need` = feet column only | pending re-validate |

## Capture / Apply Audit (2026-08-20)

Perf source: `bin/logs/perf_20260820-073931_22444.jsonl` (F4a ocean), tool `bin/tmp_capture_audit.py`.

| Metric | F4a ocean | F3ac ocean | pre-F4 072930 |
| --- | --- | --- | --- |
| capture_ms med/p90 | 0.39 / 0.54 | 0.41 / 0.63 | 0.39 / 0.50 |
| apply_ms med/p90 | 3.97 / 6.91 | 3.50 / 5.68 | 5.57 / 7.53 |
| drain_ms med | 4.38 | 3.91 | 5.98 |
| capture>0 frames | 100% | 100% | 100% |
| relight_apply_n/frame | ~1 (max 3) | ~1 (max 3) | ~1 (max 2) |
| fifo_drop sum/cruise | 564 | 561 | 607 |
| async_inflight med | 0 | 0 | 0 |

Findings:
- Capture is **not starved**: runs every cruise frame, ~0.4ms (budget 3–6ms moving).
- **No Capture slowdown** from recent ERA22 patches; Apply med actually improved vs pre-F4.
- Debt driver: **enqueue rate >> apply rate** → FIFO at soft_cap (~72), ~550 drops/cruise, PL~33.
- Telemetry trap: `RelightCompletedN` = completed **ring occupancy** at sample (Apply drains immediately) → `cruise_relight_completed_med=0` does **not** mean Capture broken.
- Suspect bug: `ShouldBoostRelightDrainUnderFifoMissStarve(..., completed_n)` treats ring occupancy as throughput; `completed_n<=0` is almost always true → boost fires whenever fifo full + holes.

## Interim Findings

- Helped:
  - Reduced low-cy miss residuals (`post_stop_miss_low_cy`/`tail_miss_low_cy`) on land.
  - Improved gate count for `land-stand`/`fly-clean`.
  - Reduced `soft_defer_empty_stuck_sec` in several runs.
- Not solved:
  - `miss_end` remains `1.0` in key stop-tail cases.
  - `unfinished_visual` remains non-zero at tail despite ownership/escalation.
  - `ocean-cruise` relight debt improved (PL med 39→22) but still above target ≤10.
  - F3a full-focus FIFO protect (reverted): cannot shed in-ring tickets, `red_rate=1`, drops rise, `completed_med` stays 0.
  - F5a remesh skip without a real light-diff metric (reverted).
  - F4b far-populate skip under Red created a generation/relight death spiral (always-Red).
  - Throughput note: `cruise_relight_completed_med=0` is ring occupancy, not Capture failure (see Capture audit).
  - Real debt driver: Apply ~1 col/frame vs terrain enqueue flood + FIFO overflow drops.
- SOTA vs zoo:
  - SOTA-ish: ColumnFlow single ticket, pressure caps, SoftDefer ownership, adaptive stop emerge.
  - Zoo: stacked miss-heal pins (F2 B3–B7) + capture-floor overrides + dirty-admit clamps that fight each other.

## Next Planned Steps

1. Ocean: PL med=22 — tune defer/admit or cheap Apply batch when PL 15–25.
2. Keep F3b+F3d+F4a-v2 stack.
