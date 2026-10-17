# Phase 5.7R7 auto verify (2026-09-09)

Locus pin World_164 (120, 57.312599182128906, 56) yaw90; Tracy OFF; no teleport.

## Code cuts
- A: `ShouldProbeFailOpenAabb` period 6 on cruise; force under underfeet/VB
- B: `ShouldCadenceScheduleHeavyWalk` DropRemesh cadence (never shed under miss)
- C: remesh `DirtyRemeshScanCap` + skip_sort when schedule_ok>0; SoftDefer outer RemoveAt
- D: transparent ApplyGpuCompactCull period 6

## Auto scorecards

| Run | wall_med | opaque_cull | SoftDefer stuck | fifo | schedule_ok | holes_frac | PRODUCT |
|---|---:|---:|---:|---:|---:|---:|---|
| fly-heavy 144100 | 47.6 | **7.1** | 0 | 1 | 2 | 0.17 | OK |
| land 144807 | 64.4 | 17.2 | 0 | 0 | 5 | 0.05 | OK |
| fz 145559 | 67.6 | 16.4 | 0 | 0 | 0.5 | 0.34 | OK |

vs manual 131116: wall~143 cull~20 SoftDefer=0 fifo=14
vs SoT 093857: wall~98 cull~13

## Gate verdicts
- Gate-A: PASS (cull 7 << 20; SoftDefer=0; fifo<20)
- Gate-B: PASS (SoftDefer=0; fifo=0; schedule_ok≥4 on land)
- Gate-C: covered by fly-heavy (schedule_ok cruise med 4 in flight_sim metrics)
- Full trio: auto PRODUCT_OK; **mandatory manual** still required for ship (eye no flicker vs 131116/194141/SoT)

## Manual ship checklist
1. Pin locus; Tracy OFF; Release bin/Cubatarium.exe
2. Cruise spd~6 at World_164 yaw90 ≥60s
3. Compare wall vs 131116 (~143); SoftDefer stuck=0; eye no chunk flicker
4. AnalyzePhase57Scorecard --teleport false vs SoT 093857
