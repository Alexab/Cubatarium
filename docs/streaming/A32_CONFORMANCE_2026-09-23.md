# A32 — conformance after A31 remediation implementation

Date: 2026-09-23  
Implementation plan: `.cursor/plans/a31_remediation_impl_9cdcbd77.plan.md`  
Basis audits: [A31_REAUDIT_2026-09-23.md](A31_REAUDIT_2026-09-23.md), [A30_V3_P3_P6.md](A30_V3_P3_P6.md), [remediation_after_a30_open.plan.md](../../.cursor/plans/remediation_after_a30_open.plan.md).

## Status legend

- **CLOSED** — production callsite fixed + unit/contract evidence
- **PARTIAL** — code path fixed; AF/pixel evidence still required
- **OPEN** — not closed
- **UNTESTED** — needs clean-SHA no-teleport AF / operator eye
- **DEFERRED** — intentionally after holes/eye gates (Ring/profile) or after attribution

## Finding matrix

| ID | Topic | Status | Evidence / residual |
|---|---|---|---|
| A31-01 | Seam peer generation fabricates `1` | **PARTIAL** | `ChunkEmergeCoordinator`: no `: 1ull`; `GetMeshPublishRevs`; `SeamCoverageFullySatisfied`; `NoteFaceDebt` dual-write. AF seam frames UNTESTED. |
| A31-02 | Fluid defer drain/discard + sync fallthrough | **PARTIAL** | Defer enqueue without immediate discard; `BuildFluidSurfaceColumnSlice` returns on deferred; queue-full returns false. Dedicated background worker still thin (drain-on-get). AF hitch UNTESTED. |
| A31-03 | Hole/dark proxies ≠ pixel cause | **OPEN / tooling** | `JobStageTrace` fields expanded; classification still needs AF freeze frames. |
| A31-04 | Far-distance precision | **DEFERRED** | Scenario `product-174657-far` + far distance fields in west coverage. No floating-origin rebase. |
| A31-05 | Demand store ownership | **PARTIAL** | Predicates fixed (`published_coverage_gen`, attempt gate, face monotonic, Stop/Retain live successor). Writers enabled via `kChunkDemandShadow()` default ON (`CUBA_DEMAND_SHADOW=1` rollback). World switch `Clear()`. AF ShadowMismatch / stop SLA UNTESTED. |
| A31-06 | Validator post-commit | **PARTIAL** | CPU validate-before-assign; GPU validate-before-swap per `published_ok`; reject keeps prior. Cross/shell/Immediate residual may remain. Fault-injection in `MeshPublishContractTest`. AF UNTESTED. |
| A31-07 | Fluid pack Y=0 stamp | **PARTIAL** | `ContentRevOf` stamps Y range; `ResetFluidSurfacePackReuseCache` on MarkAllDirty / world switch / invalidate. |
| A30 V3 P3 | ArtifactManifest retirement | **PARTIAL** | Prior validate kept; full retirement still open → successor. |
| Open P7 | Ring / profile | **DEFERRED** | `RingReadinessBudgetEnabled` remains false. |

## Harness (P0)

| Item | Status |
|---|---|
| Whole-route holes in A24 gate | CLOSED in `flight_sim_run.compute_a24_safety_stop_line` |
| `eye_proxy` secondary vs whole-route | Documented; cascade still runs eye_proxy then a24 then post_stop |
| Post-stop convergence in cascade | CLOSED (`compute_post_stop_convergence`) |
| Warm ≠ `cold_or_unspecified` | CLOSED (adequacy fail on warm stem) |
| Far ≠ west COVERED | CLOSED (`far_flight`, checkpoints) |
| Manifest completeness | PARTIAL (`manifest_required_empty`, `manifest_acceptance_ok`) |
| Progress snapshot | CLOSED (`a31_progress_snapshot`) |
| Scenario `product-174657-far` | CLOSED (longer no-teleport fly) |
| Clean-SHA baseline AF | **UNTESTED** in this workspace (env / operator run required) |

## Acceptance gates (A31 Gate 1–9) — current

| Gate | Status |
|---|---|
| 1 Far-route repro + full manifest | PARTIAL (scenario+fields; AF UNTESTED) |
| 2 Pixel-to-artifact trace | PARTIAL (trace fields; freeze UNTESTED) |
| 3 Independent frame class | OPEN |
| 4 Seam peer proof | PARTIAL (code); AF UNTESTED |
| 5 Fluid install outcomes | PARTIAL (code); AF UNTESTED |
| 6 Pre-pub validator holds reject | PARTIAL (CPU/GPU Replace); AF UNTESTED |
| 7 Stop convergence incl. coverage/face | PARTIAL (store semantics); flight StopConverged UNTESTED |
| 8 Cold/warm/eye + holes=0 whole route | UNTESTED |
| 9 Ring/P8 only after above | DEFERRED (correctly OFF) |

## Stop-lines respected

No SoftDefer owned scan, PreferKick/PromoteRelight nh≤1, MissWitness age heal, shell-light mirror, force-stale, pending-FD RemoveChunk, Ring ON.

## Successor

See [.cursor/plans/remediation_after_a31_open.plan.md](../../.cursor/plans/remediation_after_a31_open.plan.md).
