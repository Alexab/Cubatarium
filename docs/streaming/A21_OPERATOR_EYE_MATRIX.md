# A21 operator eye matrix — residual R2–R4

Date: 2026-09-22  
Protocol: `operator_visual=CLOSED` only after human west-cruise eye. AF score ≠ CLOSED.

## Flights under review

| ID | Source | Role |
|---|---|---|
| `114954` | manual west cruise | residual symptom canon (eye FAIL reported) |
| `124620` | AF cold post-R2 | mid stalled ↓; end-gate still FAIL |
| `125120` | AF warm post-R3/R4 | dual-lane warm PASS; end-gate still FAIL |
| `183133` | historical anchor | mid VB≈60, stalled≈16 |

## Eye checklist (manual)

| Check | How | Gate |
|---|---|---|
| Focus FullyDark at cruise end | look west corridor after stop/slow | no persistent black drawable islands |
| Legal caves vs debt | underground equal-rev dark OK | only illegal blacks fail |
| PreferKick / remesh liveness | blacks heal within DoD TTR hypothesismeasured | decay after stop |
| Holes vs blacks | miss_horiz proxy must not hide VB | both tracked |

## Status after residual R0–R5 code

| Gate | Status | Evidence |
|---|---|---|
| Operator eye | **UNTESTED** | no CLOSED stamp; manual 114954 reported blacks |
| End-of-flight black gate | **FAIL** | `a21_residual_*_end_gate.json` — tail debt_excl_legal ≫ 0 |
| Input adequacy (AF) | PASS | cold `124620`, warm `125120` |
| Dual-lane warm | PASS | `125120` |
| Eye-proxy stop-line | FAIL | holes blink / near-focus holes |
| `merge_green` | **false** | honesty: UNTESTED eye |

## Verdict

Residual scaffolding R0–R5 landed (classify, admit reserve, cutover ON, provenance).  
Symptom at cruise end **not** visually CLOSED. Follow-up outside this matrix until operator eye PASS.
