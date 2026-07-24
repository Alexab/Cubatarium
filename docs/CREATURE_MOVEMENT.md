# Creature movement contract

Runtime locomotion for player and NPC after the 2026 movement audit.

## Layers

1. **Strategy** — `CreatureActivityDirector` / agents write `CreatureIntent`
2. **Tactics** — steering + terrestrial A* (`SteerCreatureAlongPath`)
3. **Motor** — shared [`CreatureMotor`](../src/Creatures/Locomotion/CreatureMotor.h) (`ApplyCreatureMotorHorizontal` / `ApplyCreatureMotorStep`)
4. **Habitat** — NPC post-move `HabitatAllowsMovementAt`; reject reverts body
5. **Presentation** — `CreatureLocomotionFacts` → pose / glTF clips

Player Camera and NPC `ExecuteIntent` both use the shared motor (resolve + fluid drag + step-up). Camera keeps step-up **animation**; NPC applies step-up **instantly**.

## Navigation goals

A* stand-nodes are **feet/body**, never eye. `ControlledCreatureInfo.bodyOrigin` feeds chase/flee goals. Do not weaken `IsTerrestrialStandNode` clearance to make eye goals work.

## Diagnostics

- Schema: `creature_movement_diag.v1` → `creature_movement_diag.jsonl`
- Config: `gameplay.creature_movement_diag`
- Console: `creature_diag on|off|clear|path|dump|focus <id|nearest|clear>`

Events: `intent`, `stuck`, `blocked`, `habitat_reject`, `path_ok` / `path_fail`, `activity_skip`.

## Related

- [`CREATURE_AGENTS.md`](CREATURE_AGENTS.md) — activity orchestration
- [`CREATURE_MOVEMENT_AUDIT.md`](CREATURE_MOVEMENT_AUDIT.md) — findings CMA-*
- [`CREATURE_POST_B.md`](CREATURE_POST_B.md) — entity collision
