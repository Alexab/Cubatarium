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
| CMA-001 | high | Chase/flee A* goal uses `controlled->eyePosition`; stand-node at eye Y fails after feet-anchored collision (`0b261e7f`, `82436ab4`) → persistent `path_fail` / direct fallback | [`SimpleFsmBrain.cpp`](../src/Activity/Brain/SimpleFsmBrain.cpp); `ControlledCreatureInfo` only has `eyePosition` | open → P0 |
| CMA-002 | high | Dual motor: player = Camera `ResolveMovement`+step-up + locomotion vertical; NPC = `ResolveTerrestrialMobMovement` / all-or-nothing aerial | [`Creature.cpp`](../src/Creatures/Core/Creature.cpp), [`Camera.cpp`](../src/Render/Camera/Camera.cpp) | open → P1 |
| CMA-003 | medium | Wander uses random probe 1.25 only (no A*); stuck/idle oscillation at walls/terrain | [`WanderActivityAgent.cpp`](../src/Activity/Agents/WanderActivityAgent.cpp) | open → P2 |
| CMA-004 | medium | Habitat climb/drop hardcode 1.25 vs `jumpHeightBlocks` in terrestrial step | [`CreatureEnvironment.cpp`](../src/Creatures/Environment/CreatureEnvironment.cpp) | open (partially addressed by P1) |
| CMA-005 | medium | Aerial NPC move: accept candidate only if no block collision (no axis slide) | `ExecuteIntent` airMobility branch | open → P1 |
| CMA-006 | low | Stuck detector is XZ-only; false stuck possible for vertical aerial/aquatic | [`CreatureActivitySteering.cpp`](../src/Activity/Helpers/CreatureActivitySteering.cpp) | open → P2 |
| CMA-007 | low | Intent sticky between `activity_tick_hz` ticks (`clearOnApply=false`) | agents + ExecuteIntent | note (by design); diag marks `activityTick` |
| CMA-008 | info | Docs stale: AUDIT/DEBT still say flee/melee missing; TD-CRE-008 closed | docs | open → docs-sync |
| CMA-009 | info | No prior per-mob locomotion telemetry | fixed by diagnostics commit | closed |

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
