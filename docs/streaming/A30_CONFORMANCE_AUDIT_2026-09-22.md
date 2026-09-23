# A30 CONFORMANCE AUDIT — after A29 OPEN

Date: 2026-09-22  
Parent: remediation_after_a29_open (V1–V5)  
Canon: [`ENGINE_REMEDIATION_PLAN_2026-09-22.md`](ENGINE_REMEDIATION_PLAN_2026-09-22.md)  
Prior: [`A29_CONFORMANCE_AUDIT_2026-09-22.md`](A29_CONFORMANCE_AUDIT_2026-09-22.md)

## Verdict

**Thrash gate CLOSED** (dirty_dropped delta median ≤800; af6=432).  
**Holes OPEN** (A29 best warm AF `near_focus_holes` periods>0 = **2**; A30 V1 probes regressed to 6–13 and were reverted).  
`operator_visual=UNTESTED` ⇒ `merge_green=false`. Ring OFF. P8 SKIP.

## V1–V5

| Item | Status | Notes |
|---|---|---|
| V1 | **OPEN** | probes reverted; see [`A30_V1_HOLES.md`](A30_V1_HOLES.md); best still A29=2 |
| V2 | **OPEN / UNTESTED** | eye + A/B — [`A30_V2_EYE.md`](A30_V2_EYE.md) |
| V3 | **PARTIAL** | provisional seam + fluid ready-drain — [`A30_V3_P3_P6.md`](A30_V3_P3_P6.md) |
| V4 | **DEFERRED OFF** | holes + eye not PASS |
| V5 | **SKIP** | no profile after eye |

## Drift compliance

No heal-loop expand; no Ring ON; no shell-light mirror; CapDirty thrash kept.

## ENGINE_REMEDIATION vs HEAD

| P | Status | Skew |
|---|---|---|
| P2 liveness | CLOSED prior | — |
| P3 publish | PARTIAL | provisional seam extract; residual retirement open |
| P4 light/seam | PARTIAL | seam commit via provisional; shell mirror FREEZE |
| P5 bounded | PARTIAL | dirty≤800 PASS; holes block A24 |
| P6 fluid | PARTIAL | ready-drain honesty; hitch enqueue; MT rescan residual |
| P7 Ring | DEFERRED | V1+V2 gate |
| P8 profile | SKIP | needs eye + baseline |

## Successor

[`.cursor/plans/remediation_after_a30_open.plan.md`](../../.cursor/plans/remediation_after_a30_open.plan.md) — holes-first without PreferKick thrash.
