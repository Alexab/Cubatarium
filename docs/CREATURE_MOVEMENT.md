# Creature movement contract

Runtime locomotion for player and NPC after the 2026 movement audit.

## Layers

1. **Strategy** — `CreatureActivityDirector` / agents write `CreatureIntent`
2. **Tactics** — steering + terrestrial A* (`SteerCreatureAlongPath`)
3. **Motor** — shared [`CreatureMotor`](../src/Creatures/Locomotion/CreatureMotor.h) (`ApplyCreatureMotorHorizontal` / `ApplyCreatureMotorStep`)
4. **Habitat** — post-motor soft gate: actual-body ground support + block collision (not A* `IsTerrestrialStandNode` veto). Climb/drop ≤ `jumpHeightBlocks`; partial XZ recover when needed
5. **Presentation** — `CreatureLocomotionFacts` → **exactly one** visual backend (see below)

Player Camera and NPC `ExecuteIntent` both use the shared motor. Grounded NPCs with zero wish skip the full motor (idle early-out). Walk/Run suggestedAnim ignored at ~zero horizontal speed.

## Three visual backends (no cross-talk)

| `visual.backend` | Pose path | Do not |
|------------------|-----------|--------|
| `bone_skeleton` | `BoneSkeletonPoseEngine` + `animation_profile` | PosePresenter / glTF clips |
| `gltf_skeleton` | baked clips + `state_map` | BoneSkeletonPoseEngine |
| `rigid_voxels` | `PosePresenterRegistry` by `locomotion_archetype` | bone profiles |

Sprite override: `fire_spirit` (`sprite.billboard`) — billboard, no gait.

Locomotion **archetype** ≠ visual **backend**. Facts are shared; gait implementation is backend-exclusive.

## Navigation goals

A* stand-nodes are **feet/body**, never eye. Start/goal snap only within `jumpHeight` of body Y (eye goals stay invalid). Chase on `path_fail`: probed direct → slide → soft direct (not permanent idle).

## Diagnostics

- Schema: `creature_movement_diag.v1` → `creature_movement_diag.jsonl`
- Default on: important events always; `intent` stream focus-throttled; batch flush
- Console: `creature_diag on|off|clear|path|dump|flush|verbose on|off|focus <id|nearest|clear>`
- Path events include `creatureId`/`typeId` and `reason` = `start_invalid|goal_invalid|search_exhausted|recalc_ok`

## Related

- [`CREATURE_AGENTS.md`](CREATURE_AGENTS.md)
- [`CREATURE_MOVEMENT_AUDIT.md`](CREATURE_MOVEMENT_AUDIT.md)
- [`CREATURE_POST_B.md`](CREATURE_POST_B.md)
