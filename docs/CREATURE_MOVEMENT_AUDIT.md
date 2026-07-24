# Creature movement audit (baseline)

Date: 2026-07-24. Code review + instrumentation (`creature_movement_diag.v1`). In-game JSONL evidence pending manual/automated spawn runs with `creature_diag on`.

## Pipeline (reference)

1. `TickAgents` → intent (`Wander` / `Flee` / `Melee` + `USimpleFsmBrain`)
2. `Creature::ExecuteIntent` (NPC) or Camera + `CreatureLocomotionController` (player)
3. Habitat / collision resolve
4. `RebuildLocomotionFacts` → pose

Enable telemetry: `gameplay.creature_movement_diag` or console `creature_diag on|focus nearest|dump`.

## Findings

| ID | Severity | Finding | Evidence | Status |
|----|----------|---------|----------|--------|
| CMA-001 | high | Chase/flee A* goal uses eye | SimpleFsmBrain + ControlledCreatureInfo | **closed** (body/feet goals) |
| CMA-002 | high | Dual motor player vs NPC | Creature / Camera | **closed** (shared CreatureMotor) |
| CMA-003 | medium | Wander probe/stuck | WanderActivityAgent | **closed** (3D stuck + probe ~ speed) |
| CMA-004 | medium | Habitat climb/drop 1.25 vs jumpHeight | CreatureEnvironment | open (mitigated by shared step-up) |
| CMA-005 | medium | Aerial all-or-nothing | ExecuteIntent | **closed** (shared ResolveMovement slide) |
| CMA-006 | low | Stuck XZ-only | CreatureActivitySteering | **closed** (3D speed) |
| CMA-007 | low | Intent sticky between activity ticks | agents | note (by design) |
| CMA-008 | info | Docs stale on flee/melee | docs | **closed** (docs sync) |
| CMA-009 | info | No per-mob telemetry | — | **closed** (diagnostics) |

## Habitat / behavior smoke checklist

| Case | Species | Expect | Result |
|------|---------|--------|--------|
| Terrestrial wander slope/wall | pig | move or stuck→repick; no habitat leave | pending in-game |
| Flee | sheep | flee away; after P0 prefer `path_ok` on flat | pending |
| Chase | zombie | approach; after P0 `path_ok` on flat | pending |
| Aquatic | trout/shark | stay in water | pending |
| Amphibious | seal/penguin | land or water OK | pending |
| Aerial | wasp/owl | air move; no embed in blocks | pending |
| Lava | lava_flan | stay in lava | pending |
| Possess vs NPC step | pig | after P1 travel/step-up match | pending |

## Regression guards (player passability)

Do **not** weaken `IsTerrestrialStandNode` continuous clearance or feet-anchored `ResolveMovement` to “fix” chase. Nav goals must use body/feet; player motor path unchanged except shared helper extraction.

## Next

1. P0: `ControlledCreatureInfo.bodyOrigin` + brain goals from feet/body
2. P1: shared `ApplyCreatureMotorStep` for Camera + NPC
3. P2: wander/stuck on shared motor
4. Re-run checklist with JSONL samples; update this table
