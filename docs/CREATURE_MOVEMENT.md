# Creature movement contract

Runtime locomotion for player and NPC after the 2026 movement audit.

## Layers

1. **Strategy** — `CreatureActivityDirector` / agents write `CreatureIntent`
2. **Tactics** — steering + terrestrial A* (`SteerCreatureAlongPath`)
3. **Motor** — shared [`CreatureMotor`](../src/Creatures/Locomotion/CreatureMotor.h) (`ApplyCreatureMotorHorizontal` / `ApplyCreatureMotorStep`)
4. **Habitat** — NPC post-move `HabitatAllowsMovementAt` (climb/drop ≤ `jumpHeightBlocks`); partial XZ accept + feet snap when possible (full revert only if stand invalid)
5. **Presentation** — `CreatureLocomotionFacts` → pose / glTF clips (Walk/Run hints ignored at ~zero horizontal speed)

Player Camera and NPC `ExecuteIntent` both use the shared motor (resolve + fluid drag + step-up). Camera keeps step-up **animation**; NPC applies step-up **instantly**. Grounded NPCs with zero wish skip the full motor (idle early-out).

## Navigation goals

A* stand-nodes are **feet/body**, never eye. `ControlledCreatureInfo.bodyOrigin` feeds chase/flee goals. Do not weaken `IsTerrestrialStandNode` clearance to make eye goals work. Chase `path_fail` idles or lateral-slides instead of wall-bashing direct XZ.

## Diagnostics

- Schema: `creature_movement_diag.v1` → `creature_movement_diag.jsonl` (next to executable)
- Config default: `gameplay.creature_movement_diag` = **true**
- Policy (safe default-on):
  - Always (focus-filtered when set): `blocked`, `habitat_reject`, `stuck`, `path_fail`, `path_ok`
  - Stream `intent` / `activity_skip`: **focus only**, throttled ~2 Hz (or all with verbose)
  - JSONL: batch flush (32 lines / 0.5 s); important events flush immediately
- Console:
  - `creature_diag on|off|clear|path|dump|flush`
  - `creature_diag focus <id|nearest|clear>`
  - `creature_diag verbose on|off` — full intent stream (still focus-filtered when set)

## Related

- [`CREATURE_AGENTS.md`](CREATURE_AGENTS.md) — activity orchestration
- [`CREATURE_MOVEMENT_AUDIT.md`](CREATURE_MOVEMENT_AUDIT.md) — findings CMA-*
- [`CREATURE_POST_B.md`](CREATURE_POST_B.md) — entity collision
