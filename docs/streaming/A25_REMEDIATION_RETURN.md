# A25 — Remediation P-order return (baseline)

Date: 2026-09-22  
Branch: `cursor_audit7_impl`  
Cycle start HEAD: `43373e81`  
Canon: [`ENGINE_REMEDIATION_PLAN_2026-09-22.md`](ENGINE_REMEDIATION_PLAN_2026-09-22.md)  
Plan: remediation_p-order_return (R0–R10)

## Verdict at cycle start

- A23 LightConverge: **REGRESSED** (holes + DirtyAdmit thrash).
- A24: **partial safety** (RemoveChunk pending FD OFF; remesh heal neighbors=false; equal-rev cooldown 45f).
- HEAD ≠ visual CLOSED; `operator_visual=UNTESTED` ⇒ `merge_green=false`.
- RingReadinessBudget: **OFF** (FREEZE until eye + stop-lines).
- Heal layer: **FREEZE** — no force_stale flood, no RemoveChunk pending FD, no shell-light mirror, no A25 remesh expansion.

## Acceptance gates (cycle)

| Gate | Role | Sole PASS? |
|---|---|---|
| mid FD stalled med ≤ 0 | diagnostic | **no** |
| end-of-flight black / end debt | converge (after holes stable) | **no** while holes unstable |
| `near_focus_holes` periods>0 west == 0 | A24 stop-line | required |
| `dirty_dropped/period` ≤ ~800 | A24 stop-line | required |
| PreferKick counter | heal-owner honesty only | **no** (counter≡0 observed) |
| AF fog scorecard | proxy | **no** — AF ≠ manual eye |
| `operator_visual=PASS` | product | required for merge_green |
| RingReadiness ON | P7 | forbidden until eye+stop-SLA |

## KEEP / FREEZE (summary)

**KEEP:** P0 harness; P1 cull/order/shell/credit/material/fluid-id; P2 demand store+cutover ON+sole writer+peer FaceDebt+multi-Y Ready; P3 hot-path epochs/provenance; LegalDark policy; MaxCriticalUnitMs=4; FluidColumnSummary+stamp-hit; A24 safety helpers; Ring code with flag OFF.

**FREEZE:** A22 every-apply force_stale; A23 RemoveChunk pending FD; A23 seamed neighbors=true heal; A23 D3 shell-light mirror; Ring ON; PreferKick chase as sole DoD; mid-stall as CLOSED; end-debt cosmetics before holes-stable.

## Equal-rev contract (three states)

1. **LegalDark** — equal-rev settled, no repair ticket → remesh forbidden.
2. **Dirty+FD** — already Dirty → equal-rev remesh allowed (`0894711d`).
3. **PL+FD cooldown** — narrow Invalidate/force_stale on 45f cooldown (`43373e81`), never every-apply.

## Archaeology `dcc02e27` → `43373e81` (33 commits)

| Cluster | Commits | Effect |
|---|---|---|
| P0 | `3b4036f9`, `1f18c216` | manifest, schema v2, JobStageTrace, adequacy; CurrentContractRepro + FAIL ADRs |
| P1 | `2e44d7c4`, `35302c5b` | cull/order/shell/credit/material/fluid identity |
| P2 | `bd764b4c`…`60f57bb9` | demand store → cutover ON → sole writer → multi-Y Ready |
| P3 | `d84d2b13`, `46f5d6df`, `73bb980b`, `0ea10c2f` | epochs, light rev links, provenance, live expected |
| P4 policy | `240e70f7`, `e7a82e63`, `0c3dd439`, `520e5ccd` | LightValidity / LegalDark / boundary docs |
| P5 | `05eed994`, `0ea10c2f` | MaxCriticalUnitMs=4, shared relight slots |
| P6 | `247ae0b8`, `0ea10c2f` | FluidColumnSummary + stamp-hit |
| P7 | `240e70f7`, `0c73a30e`, `811f64b0` | RingEvaluate; flag OFF |
| Residual | `bc3e5c16`, `8d5a22f1`, `0894711d`, `8c26d5e9`… | PreferKick path, DirtyAdmit floor, equal-rev Dirty remesh, end-gate |
| A22 | `ef45f63c`… | PL keep + force_stale flood (end-debt regress) |
| A23 | `a7acc856` | LightConverge bifurcate — REGRESSED |
| A24 | `43373e81` | RemoveChunk OFF; neighbors=false; cooldown 45f |

## Delta-rebase P0–P8 @ `43373e81`

| Phase | Status | Already | Remainder / AMENDED |
|---|---|---|---|
| P0 | PARTIAL | harness | 5×A/B, eye, job_trace rev honesty |
| P1 | LANDED / gate soft | cull/order/shell/… | differential shell full matrix; visual CLOSED |
| P2 | LANDED structure / liveness OPEN | cutover ON, multi-Y | orphan/end converge; P2.8 cleanup; event suite; heal-owner honesty |
| P3 | PARTIAL | hot-path stamps | all callers; retirement; publication_audit ADR |
| P4 | POLICY + AMENDED | LegalDark; A24 narrow | reference light; seam extract; finite converge; FREEZE heal |
| P5 | PARTIAL | 4ms, slots, DirtyAdmit floor | resumable; unified pools; dirty_dropped≤800 |
| P6 | PARTIAL | summary, stamp-hit | worker rebuild; tiled map; spike gate |
| P7 | CODE + FREEZE OFF | Evaluate | enable after eye+stop-lines |
| P8 | OPEN | profile gates doc | opts only with profile |

## Surprises (must address in Rn)

1. PreferKick counter ≡0 while Dirty/Invalidate/Relight is real heal path.
2. DirtyAdmit thrash (`dirty_dropped` ~891 soft-miss vs ≤800).
3. Equal-rev oscillation across LegalDark / Dirty+FD / cooldown.
4. Multi-Y Ready changes emerge semantics.
5. AF fog ≠ manual eye.
6. Mid-stall PASS with end-gate FAIL.
7. PL keep × RemoveChunk interaction (A23).
8. P2 cutover ON ≠ product CLOSED.
9. ArtifactManifest header ≠ P3 CLOSED.
10. Ring Evaluate runs with flag OFF — accidental enable risk.

## Rn volume amendments (order §5 unchanged)

- **R1** = P2 remainder on cutover ON + multi-Y + job_trace rev honesty.
- **R2** = P3 finish callers + publication_audit ADR.
- **R3** = P4 + FREEZE A22–A24 + reference/seam (A23 = failed experiment).
- **R4** = P5 + dirty_dropped ≤800 AMENDED.
- **R5** = P6 worker/strip (foundation KEEP).
- **R6** = eye + frame A/B + end-gate → merge_green.
- **R7** = Ring ON only after R6.
- **R8** = profile opts or explicit SKIP.
- **R9** = conformance audit vs this doc + ENGINE_REMEDIATION.
- **R10** = next plan on OPEN/AMENDED only.

## Heal-owner honesty (DoD note)

Product liveness DoD is **not** `prefer_kick_n > 0`. Documented heal owners: Dirty / Invalidate / Relight bifurcate / PendingLight spans progressing to Published (job_trace desired/published revs non-zero on live obligations). PreferKick remains an optional accelerator; counter≡0 is a telemetry/wiring gap, not proof of zero execution.
