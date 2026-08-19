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

## Interim Findings

- Helped:
  - Reduced low-cy miss residuals (`post_stop_miss_low_cy`/`tail_miss_low_cy`) on land.
  - Improved gate count for `land-stand`/`fly-clean`.
  - Reduced `soft_defer_empty_stuck_sec` in several runs.
- Not solved:
  - `miss_end` remains `1.0` in key stop-tail cases.
  - `unfinished_visual` remains non-zero at tail despite ownership/escalation.
  - `ocean-cruise` relight debt remains high (`pending_light_focus_med` far above target).

## Next Planned Steps

1. F3: increase ocean relight FIFO throughput and focus-ring priority.
2. F3 verify: `--only ocean-cruise`, target `pending_light_focus_med<=10`.
3. F4: add dirty backpressure under sustained pressure and verify full suite.
4. F5: reduce opaque churn after F2/F4 stabilization.
