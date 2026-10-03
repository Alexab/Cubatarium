# A21/A25 acceptance — remediation return cycle

Date: 2026-09-22  
Branch: `cursor_audit7_impl`  
Baseline doc: [`A25_REMEDIATION_RETURN.md`](A25_REMEDIATION_RETURN.md)

## Prior A22 S0–S6 (historical)

| Step | Status | Notes |
|---|---|---|
| S0 evidence + visible baseline | DONE | `A22_MANUAL_133440.md`; debt 8 |
| S1 PendingLight heal | DONE (partial) | mid stall ↓; end-gate FAIL |
| S2 sole demand writer | DONE | `kChunkDemandShadow=false` |
| S3 publish validator | DONE | expected from live revs |
| S4 LightValidity ADR | DONE | `A22_ADR_LIGHT_SEAM.md` |
| S5 fluid stamp-hit | DONE | cache before GetBlock |
| S6 critical unit 4ms | DONE | `MaxCriticalUnitMs` default 4 |
| S7 RingReadiness | **DEFERRED / FREEZE OFF** | until eye + stop-SLA |
| S8 operator eye | **UNTESTED** | merge_green=false |

## A23 / A24

| Step | Status | Notes |
|---|---|---|
| A23 LightConverge | **REGRESSED** | holes + DirtyAdmit thrash |
| A24 RemoveChunk OFF + cooldown | PARTIAL safety | stop-lines: holes / dirty_dropped |

## A25 return cycle (R0–R10)

| Phase | Status | Notes |
|---|---|---|
| R0 baseline + freeze | **DONE** | A25_REMEDIATION_RETURN + A24 AF safety stop-line |
| R1 P2 hygiene/liveness | **PARTIAL** | orphan cancel; NoteStageProgress always; writers inventory; PreferKick honesty |
| R2 P3 publish finish | **PARTIAL** | A25_P3_PUBLICATION_ADR; order/table epoch reject tests |
| R3 P4 light+seam | **PARTIAL** | A24 FREEZE asserts; FillReferenceSkyBlockLight; seam peer tests KEEP |
| R4 P5 + dirty_dropped | **PARTIAL** | CapDirtyAdmitUnderThrash; A24 ≤800 gate in AF |
| R5 P6 fluid | **PARTIAL** | ShouldDeferFluidFullColumnScan + FrameDeadline defer before tall GetBlock |
| R6 eye + budget | **OPEN / UNTESTED** | protocol `A25_R6_EYE_PROTOCOL.md`; eye not run |
| R7 Ring ON | **DEFERRED** | A25_P7_RING_DEFERRED — flag OFF |
| R8 profile | **SKIP** | A25_P8_SKIP — no profile evidence |
| R9 conformance | **DONE** | `A25_CONFORMANCE_AUDIT_2026-09-22.md` |
| R10 next plan | **DONE** | `.cursor/plans/remediation_next_open_amended.plan.md` |

## Hard rules

- `operator_visual=UNTESTED` ⇒ `merge_green=false`.
- Mid FD stalled is diagnostic only — never sole PASS.
- PreferKick counter is not sole heal DoD (see A25 heal-owner honesty).
- A24 stop-lines required on every AF/manual until P4 CLOSED.
- RingReadiness stays OFF until R6 eye PASS + stop-SLA.
