# Systemic streaming reset — SoT 143831

Date: 2026-09-20  
Baseline «почти OK»: `27beca1c`  
Regress window: `c12dbe0c`..HEAD (`0b457e9f`)  
Operator SoT: `bin/logs/perf_20260920-143831_55228.jsonl`

## Freeze

No new SoftDefer-for-holes, remesh floors by census, fog VB latch-as-heal,
N04 remesh caps, PreferKick without publish progress, or peer remesh carve-outs.

Allowed: rev gates, FSM states, atomic publish, Unknown≠Air path fixes,
documented hysteresis, telemetry.

## Symptoms (143831 fly)

| Class | Metric | Value |
|---|---|---|
| Blacks | VB / FD repair | 45\|52 / 45\|52 |
| Empty | unfinished / nlm | max 12 |
| Wrong-tex | prior_lit_hold | 531\|633 |
| Underwater | void med | 826 |
| Fog | fog_rd thrash | ~7% (masks, does not heal) |

## Root cause

RelightReplace sole-owner + PreferKick spin + I3t/PriorLit hold without converge
+ R06 peer remesh cascade. Matches AUDIT16: fix publish ownership, do not stack
repair heuristics.

Literature map: `bin/suite_reports/voxel_streaming_failure_modes.json`.

## Execution protocol

Each code step: unit → AF (`product-174657` / `-dive`, fog ON) → scorecard →
auto-commit. On FAIL: full audit + research + plan update (no new heuristics).

Env for bisect:

- `CUBA_RELIGHT_REPLACE_OWNER=0` — RelightReplace Dirty sole-owner OFF
- `CUBA_FLIGHT_FOG_ON=1` — keep fog pull-in enabled on product-174657 AF

AF artifacts: `bin/suite_reports/g1_a10_relight/sysreset_*`.

## Phase 0 bisect result (2026-09-20)

`CUBA_RELIGHT_REPLACE_OWNER=0` + fog ON + already-neutralized PreferKick/R06/I3t/fog:

| Metric | 143831 | p0 RR OFF |
|---|---|---|
| west | — | COVERED (cx≤−4) |
| VB fly med | 45 | **95** (worse) |
| unlit max | — | 42 |
| fog_rd fly med | — | 3.0 |

Verdict: sole RelightReplace OFF **regresses** VB vs SoT. Keep owner flag default ON;
neutralize is PreferKick-without-progress + secondary skip gated on MarkRelit progress,
not a hard OFF of A10.

## Phase 1 AF corrections (2026-09-20)

1. Banning PreferKick for pending GPU → `gpu_kick≡0` / VB 116 (p1). **Restored**
   PreferKick for pending; removed only PreferKick-on-skip-already-dirty carve-outs.
2. Gating all `ShouldSkipSecondaryFullyDarkDirty` on progress → Dirty flood
   (dirty_med~179). **Reverted** legacy call sites to always-skip; World seam
   keeps progress gate so MarkRelit≡0 still allows seam Dirty.

Keep: R06 sticky-only, I3t PriorLit TTL, fog no-VB-heal, MeshPublishContract,
ColumnVisualState, `CUBA_FLIGHT_FOG_ON`.

## Final landed commits (2026-09-20)

| SHA | What |
|---|---|
| `aac4ec8e` | docs freeze + bisect env (`CUBA_RELIGHT_*`, fog ON flag) |
| `4c90c12f` | R06 sticky-only, I3t TTL, fog no VB heal, MeshPublishContract, ColumnVisualState |
| `79b7aea0` | restore RelightReplace seam skip (progress-gate Dirty flood reverted) |

AF west COVERED on successful runs; dual-lane still OPEN (VB high under fog ON
honesty). Units: `miss_first_mesh_class_test`, `mesh_neighbor_policy_test`,
`mesh_publish_contract_test` PASS. Operator gates:
[SYSRESET_OPERATOR_AF_GATES.md](SYSRESET_OPERATOR_AF_GATES.md).

## Retract + follow-on

Manual SoT `191112` shows blacks/flip/walls still OPEN. PreferKick/FSM/Accept
were overclaimed CLOSED — see [SYSRESET_V2_AUDIT_191112.md](SYSRESET_V2_AUDIT_191112.md).
Sysreset v2 owns the remaining converge path.
