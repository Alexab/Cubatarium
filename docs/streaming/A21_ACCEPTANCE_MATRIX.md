# A21/A22 acceptance matrix — honesty freeze 2026-09-22

## Residual R0–R5 vs ENGINE plan

Residual scaffolding **landed**; ENGINE_REMEDIATION P0–P8 gates **not CLOSED**.  
Manual `133440` eye FAIL (blacks). See `A22_MANUAL_133440.md`.

| Phase | Residual claim | Actual |
|---|---|---|
| P0 | PARTIAL | PARTIAL — no 5×A/B soak |
| P1 | local fixes | PARTIAL — no e2e visual |
| P2 | cutover ON | PARTIAL — shadow dual-path remains |
| P3 | provenance | PARTIAL — validator self-check |
| P4 | LightValidity | PARTIAL/Deferred |
| P5 | DirtyAdmit reserve | PARTIAL — MaxCriticalUnitMs=0 |
| P6 | fluid identity | PARTIAL — sync full scan |
| P7 | Evaluate wired | **OFF** |
| P8 / DoD | R5 honest | UNTESTED eye; end-gate FAIL |

## A22 status

| Step | Status |
|---|---|
| S0 evidence + visible baseline | IN PROGRESS |
| S1–S8 | PENDING |
| `operator_visual` | **UNTESTED** |
| `merge_green` | **false** |

AF adequacy ≠ CLOSED. Visible `--visible` flights required per A22 Loop.
