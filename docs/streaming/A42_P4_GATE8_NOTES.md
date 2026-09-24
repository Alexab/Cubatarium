# A42 P4 — Gate 8 notes

Date: 2026-09-24  
Parent: [A42 conformance](A42_CONFORMANCE_2026-09-24.md), [ADR](ADR_VISUAL_OBLIGATION_SOT_2026-09.md)

## Pre-merge status

| Check | Status |
|---|---|
| Unit SoftDefer×LightRepair | PASS (`miss_first_mesh_class_test`) |
| Schedule fallthrough under PL SoftDefer | landed (Cache + Emerge callback) |
| ClearPending / Accept unchanged | verified P2 |
| Manual west vs 135414 | **OPEN** — operator re-eye on new exe |
| AF cold1 scale=1 | FAIL — see below; merge_green OPEN |
| merge_green | blocked until clean SHA + eye; Ring OFF |

### cold1 (dirty AF, scale=1)

Report: `bin/suite_reports/a42/cold1.json` · perf `182422`.

| Gate | Result |
|---|---|
| dual-lane | FAIL `unlit_max=32`, mid FD stalled |
| A24 safety | FAIL `near_focus_holes_periods_gt0=13` |
| eye-proxy mid | PASS (blink=0; not merge_green) |
| Kick sole heal | OK |

## Commands

```bat
set CUBA_FLIGHT_MOVE_SPEED_SCALE=1
powershell -File bin/suite_reports/a42/_run_matrix.ps1
```

## Baselines

manual 135414 (SoftDefer RemoveAt plateau); 132520 (SLA dead); 103228.
