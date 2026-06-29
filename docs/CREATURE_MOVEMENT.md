# Creature movement and spawn

NPC locomotion uses a single movement contract shared by wander AI, intent execution, and spawn placement.

## Modules (`src/Creatures/Movement/`)

| Module | Role |
|--------|------|
| `CreatureFootprint` | Size-scaled ground sampling (`SampleCreatureFootprint`, `DepenetrateCreatureFromGround`) |
| `CreatureHabitatPolicy` | `HabitatContext` + `HabitatAllows()` / `TerrestrialCanWalkOn()` |
| `CreatureBodyProbe` | `ProbeMove()` / `EvaluateResolvedMove()` — resolve, step-up fallback, habitat at target |
| `CreatureBodyStepUp` | 1-block ledge for terrestrial biped/quadruped when `IsStepUpEnabled()` |
| `CreatureBodySeparation` | Horizontal nudge + ground snap when mobs overlap blocks or each other |
| `CreaturePlacement` | Ring search spawn origin (`FindSpawnOrigin`) with `SpawnCollisionPolicy` |
| `CreatureMovementLog` | `[Spawn]`, `[Wander]`, `[MoveProbe]` lines in `cubatarium.log` |

## Per-frame NPC tick

```
ActivityDirector.TickAgents  →  WanderActivityAgent sets Intent
TickCreatureBehaviors(dt)    →  ExecuteIntent per NPC
```

Terrestrial/amphibious NPCs on land **do not** run player `UpdateLocomotion` (no gravity / DepenetrateEye stack jumps).

## Habitat contexts

| Context | Terrestrial / amphibious on land |
|---------|----------------------------------|
| `WanderCurrent` | Not in water/lava (air OK) |
| `WanderTarget` / `MoveApply` | `TerrestrialCanWalkOn`: not in water/lava and (`onSolidGround` or `bodyBlocked`) |
| `Spawn` | `HabitatAllowsAtForSpawn` via placement |

Wander probe distance and minimum step scale with body size (`WanderProbeDistance`, `MinWanderProbeXZ`).

## NPC step-up

Walking terrestrial biped/quadruped mobs use the same 1-block rise band as the player (`0.45`–`1.05` blocks) when global `StepUpEnabled` is on and the mob is not in fluid. `ProbeMove` tries `TryCreatureStepUp` after a blocked horizontal resolve for `WanderTarget` and `MoveApply`.

## Tests

- Unit: `creature_footprint_test`, `creature_step_up_test`, `creature_habitat_policy_test`, `creature_body_probe_test`, `creature_placement_test`, `creature_separation_test`
- Integration: `--creature-spawn-smoke`, `--creature-wander-smoke`, `--creature-movement-smoke`, `--creature-step-up-smoke`, `--creature-stack-smoke`
- Diagnostic: `--creature-movement-diagnose <species>`

See also [`CREATURE_AGENTS.md`](CREATURE_AGENTS.md).
