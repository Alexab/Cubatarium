# A39 conformance — remaining remediation after A38 / manual 092417

Date: 2026-09-24  
Parent: [A39_P0_MANUAL_BASELINE](A39_P0_MANUAL_BASELINE_2026-09-24.md), [A38_CONFORMANCE](A38_CONFORMANCE_2026-09-23.md)

## Code landed

| Item | Change |
|---|---|
| P0a | Manual baseline doc; dirty A37 AF matrix invalid for Gate 8 |
| P1 | Ban light `+1` invent; RelightOnly → `EnqueueTerrainColumnRelight`; desire=`content_light` |
| P2 | Admit focus-sort, Y-cap, skip active Uploading, `CapDirtyAdmitUnderThrash` |
| P3 | UnknownPeer: no `need=1` raise/clear; waiting0+mask blocks peer_gen=0 clear |
| P4 | PreferGpu Pending keeps `FluidSurfaceDirty`; enqueue Enqueued\|Coalesced\|RejectedRetryable; no empty-flags |
| P5 | Cross `RefreshPass(candidate, expected)`; CrossBatches expected from demand desire; fault-inject unit |
| P6 | Attribution: mesh holes ≠ unfinished `holes_rate`; eye protocol below |

## Stop-lines

Ring OFF; Kick OFF; no SoftDefer / PreferKick blanket / FullyDark mass remesh.

## Gate 8 / merge_green

| Gate | Status |
|---|---|
| Clean SHA AF cold×5/warm×2/far | P0b — run after Release rebuild on this A39 tree |
| `near_focus_holes` periods_gt0==0 whole-route | mesh holes only — **not** unfinished hole_key |
| dual `unlit_max≤15` | expect improve from P1 Relight |
| `demand_stop_converged` post_stop | expect improve from P1–P3 |
| dirty-drop **delta** median ≤800 | P2 thrash cap |
| `operator_visual` | **UNTESTED** until human PASS on same exe — not merge_green |
| Ring | OFF |

## Attribution (A31-03)

- `ClassifyChunkDefect` / period `defect_class_primary` — primary VB class
- Scorecard `holes_rate` with hole_key=unfinished ≠ Gate 8 mesh holes
- `eye_proxy` mid_corridor ≠ merge_green

## Unit gates

- `chunk_render_demand_test` OK (UnknownPeer + coverage)
- `mesh_publish_contract_test` OK (Cross got≠expected; empty-flags reject)

## P0b AF

| Run | Notes |
|---|---|
| `bin/suite_reports/a39/cold1.json` | dirty_diff non-empty (A39 uncommitted); exit≠0 |
| unfinished_visual med | **16** (manual baseline was 81) |
| chunk_meshed_unlit_med | **10.5** (≤15 med); dual FAIL on `unlit_max=25` |
| demand_stop_converged | still false |
| near_focus_holes periods_gt0 | 25 — Gate 8 OPEN |
| hole_key | still `unfinished_visual` in scorecard — ≠ mesh-hole Gate 8 |
| fluid hitch | ~0.07 |
| operator_visual | UNTESTED |
| Ring | OFF |

Full cold×5/warm×2/far: `bin/suite_reports/a39/_run_matrix.ps1`. merge_green requires **commit** (clean SHA) + eye PASS — see [A39_P6_GATE8_NOTES](A39_P6_GATE8_NOTES.md).