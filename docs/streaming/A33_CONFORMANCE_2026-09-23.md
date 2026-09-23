# A33 — conformance after A32 successor

Date: 2026-09-23  
Parent: [A32_CONFORMANCE_2026-09-23.md](A32_CONFORMANCE_2026-09-23.md)  
Plan: A32 successor remediation (`a32_successor_remediation_cf86279d`)  
Commits: `8f845eaf` (A31 impl) → `6d9c72d6` (S2–S5) → `43ddf085` (A33 docs + successor).

## Finding matrix (post S2–S5 + S0 AF)

| ID | Status | Notes |
|---|---|---|
| A31-01 seam | **PARTIAL** | Per-face peer gens + subscriber satisfy + FaceDebt dual-write in code. AF seam frames not yet class-attributed. |
| A31-02 fluid worker | **PARTIAL** | Flags + worker + install outcomes in code. AF still shows `dominant_spike_class=fluid_map` on all four runs. |
| A31-03 attribution | **PARTIAL** | JobStageTrace on Admit + Published/Retain/Reject. Cull/order + freeze-frame class OPEN. |
| A31-04 precision | **DEFERRED** | Far reached cx≈−57 / ~1024 blocks; no precision rebase (holes not exclusive to far). |
| A31-05 demand/stop | **PARTIAL** | GPU/`CommitGpuMeshResult` NoteInstallResult + attempt_id; production StopConverged wired. AF: `demand_stop_converged=false` on all runs → flight FAIL as intended. |
| A31-06 pub gate | **PARTIAL** | Immediate/Cross/CommitGpu validate-before-mutate. Reject-path AF UNTESTED. |
| A31-07 fluid stamp | **PARTIAL** | Y-range stamp retained from A31. |
| A30 V3 P3 retirement | **PARTIAL** | Still open → successor. |
| Ring/profile | **DEFERRED / OFF** | eye+holes not PASS. |

## A31 Gate 1–9

| Gate | Status |
|---|---|
| 1 Far repro + manifest | **DONE** (baseline under `bin/suite_reports/a32_s0/`) |
| 2 Pixel-to-artifact trace | PARTIAL (more writers; freeze UNTESTED) |
| 3 Independent frame class | OPEN |
| 4 Seam peer proof | PARTIAL (code); AF class UNTESTED |
| 5 Fluid install outcomes | PARTIAL (code); AF still fluid_map dominant |
| 6 Pre-pub holds reject | PARTIAL (paths gated); AF UNTESTED |
| 7 Stop convergence | **FAIL reproduced** (`demand_stop_converged` + post_stop fails) |
| 8 Cold/warm/eye holes=0 | **FAIL** (holes_periods_gt0 28–90; eye_proxy FAIL; operator visual UNTESTED / no `--visible`) |
| 9 Ring after above | DEFERRED |

## S0 AF baseline (SHA `43ddf085`)

Reports: `bin/suite_reports/a32_s0/{cold,warm,dive,far}_gate.json`, `baseline_summary.json`.  
Env: `CUBA_FLIGHT_FOG_ON=1`, `CUBA_GL_CAPS=desktop-gl`, no-teleport product-174657 family; hidden GLFW (no `--visible`).

| Run | pass | holes_gt0 | eye_proxy | post_stop | demand_stop_converged | fluid spike |
|---|---|---|---|---|---|---|
| cold | FAIL | 28 | FAIL | FAIL | false | fluid_map |
| warm | FAIL | 28 | FAIL | FAIL | false | fluid_map |
| dive | FAIL | 30 | FAIL | FAIL | false | fluid_map |
| far | FAIL | 90 | FAIL | FAIL | false | fluid_map |

`dirty_diff_hash` not literal `clean` (pre-existing untracked local plans/bin noise); tracked tree at HEAD has no modified sources. Defect class **reproduced** (whole-route holes + stop non-convergence + fluid_map hitch), not greenwashed.

## S6 acceptance

- ≥4 A/B cold/warm/dive/far snapshots: **yes** (baseline chain).
- whole-route holes=0 / eye PASS: **no** → Ring stays **OFF**.
- Successor: [.cursor/plans/remediation_after_a32_open.plan.md](../../.cursor/plans/remediation_after_a32_open.plan.md)

## Definition vs plan DoD

1. S-commit A31 + S2–S5 on HEAD: **yes**
2. Gate 1–9 CLOSED or honest OPEN in A33: **yes**
3. Code gaps S2–S5 on production path: **yes**
4. AF snapshot chain S0: **yes**
5. Ring OFF until eye+holes PASS: **yes**
