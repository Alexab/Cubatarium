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

## Interim Findings

- Helped:
  - Reduced low-cy miss residuals (`post_stop_miss_low_cy`/`tail_miss_low_cy`) on land.
  - Improved gate count for `land-stand`/`fly-clean`.
  - Reduced `soft_defer_empty_stuck_sec` in several runs.
- Not solved:
  - `miss_end` remains `1.0` in key stop-tail cases.
  - `unfinished_visual` remains non-zero at tail despite ownership/escalation.
  - `ocean-cruise` relight debt remains high (`pending_light_focus_med` far above target).
  - F3a full-focus FIFO protect (reverted): cannot shed in-ring tickets, `red_rate=1`, drops rise, `completed_med` stays 0.
  - F5a remesh skip without a real light-diff metric (reverted).
  - F4b far-populate skip under Red created a generation/relight death spiral (always-Red).
- Throughput note: `cruise_relight_completed_med=0` on ocean/land cruise means Capture is not finishing columns. Apply-batch and capture-floor did not move this. Next useful F3 is skip-noop Capture / why completed stays 0, not more FIFO protect.
- SOTA vs zoo:
  - SOTA-ish: ColumnFlow single ticket, pressure caps, SoftDefer ownership, adaptive stop emerge.
  - Zoo: stacked miss-heal pins (F2 B3–B7) + capture-floor overrides + dirty-admit clamps that fight each other.

## Next Planned Steps

1. Diagnose `cruise_relight_completed_med=0` (Capture finish path) before more FIFO policy.
2. F3b: skip no-op relight BFS / no-op Capture if lighting already matches.
3. Do not re-land: Red NearLoadRadius clamp, Red far-populate skip, full-focus FIFO protect, remesh skip via FullyDark proxy.
4. Keep F4a-v2 dirty admit=1.
5. Full-plan audit from this log (planned vs done, SOTA vs zoo).
