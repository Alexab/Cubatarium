# A41 P6 — Gate 8 notes

Date: 2026-09-24  
Parent: [ADR_VISUAL_OBLIGATION_SOT](ADR_VISUAL_OBLIGATION_SOT_2026-09.md), [A41 plan](../../.cursor/plans/a41_visual_sot_root_cause.plan.md)

## Pre-merge status

| Check | Status |
|---|---|
| Unit classify / SoftDefer ticket / LegalDark | PASS (`miss_first`, `relight_install_planner`) |
| LightRepair open_sky Dirty once | landed; **SLA remint** after manual 132520 |
| Sole Ready (ClearPending→LitReady) | World.cpp ClearPending; sole prod writer = LitDrawableCommit |
| Release Cubatarium.exe | rebuild after SLA remint |
| Manual west `132520` | plateau FAIL — see [A41_MANUAL_132520](A41_MANUAL_132520.md) |
| Clean SHA AF cold×5/warm×2/far | **OPEN** — `cold_post_sla1` FAIL (exit=2) |
| Speed | cold/warm **scale=1 only** |
| operator_visual | FAIL on 132520 (pre-remint); re-eye required |
| merge_green | blocked until clean matrix + eye; Ring OFF |

### cold_post_sla1 (scale=1, dirty AF)

Report: `bin/suite_reports/a41/cold_post_sla1.json` · perf `134902`.

| Gate | Result |
|---|---|
| dual-lane | FAIL `unlit_max=19` (>15) |
| A24 safety | FAIL `near_focus_holes_periods_gt0=20` |
| post_stop | FAIL (missing/holes/pending/demand_stop) |
| Kick sole heal | OK (prefer_kick dod) |

### Manual 135414 (post-SLA eye)

Autopsy: [A41_MANUAL_135414_AUTOPSY](A41_MANUAL_135414_AUTOPSY.md).  
holes PASS; SLA schedule live; **DoD FAIL** — SoftDefer RemoveAt remesh Dirty under LightRepair+PL.

## Commands

```bat
set CUBA_FLIGHT_MOVE_SPEED_SCALE=1
powershell -File bin/suite_reports/a41/_run_matrix.ps1
```

## Baselines

manual 103228/102349; manual [132520](A41_MANUAL_132520.md); cold_post_a40_scale1 (holes=7); cold_post_a39.
