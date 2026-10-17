# Phase 5.7R7.1 auto verify

## Code
- SoftDefer outer !drawable: leave-in restored (R7 RemoveAt reverted)
- skip_sort only when MissingMemo known-clear
- remesh scan_cap skipped under StarveRemesh / focus_missing
- ShouldForceScheduleHeavyWalk: focus_missing | nh<=2 | schedule_ok<4

## Auto fly-heavy (155214) PRODUCT_OK
| Metric | R7.1 auto | 153045 manual regress | 131116 KEEP |
|---|---:|---:|---:|
| SoftDefer stuck | 0 | 2 | 0 |
| holes_frac | 0.24 | 0.63 | 0.43 |
| schedule_ok | 3 | 1 | 4 |
| fifo | 0 | 11 | 14 |
| wall | 68.7 | 126.8 | 142.9 |
| cull | 17.6 | 12.6 | 20.2 |

Unit: miss_first_mesh_class_test OK.

## Ship
Mandatory manual still required vs 131116 + 153045 (eye holes, SoftDefer stuck=0, schedule_ok≥4, wall≤131116).
