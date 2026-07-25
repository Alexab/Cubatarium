# Creature movement contract

Runtime locomotion for player and NPC after the 2026 movement audit / systemic locomotion rewrite (CMA-022).

## Layers

1. **Strategy** — `CreatureActivityDirector` / agents write `CreatureIntent` (~20 Hz)
2. **Tactics** — voxel A* (`SteerCreatureAlongPath`: partial path, stuck repath, exhaust backoff) + steering (path-follow / seek → **wall feelers** → separation)
3. **Motor** — shared [`CreatureMotor`](../src/Creatures/Locomotion/CreatureMotor.h) (`ApplyCreatureMotorHorizontal` / `ApplyCreatureMotorStep`)
4. **Habitat / traverse** — shared [`CanCreatureStandAt` / `CanCreatureStep`](../src/Creatures/Environment/CreatureTraverseQueries.h) for post-motor gate, A* stand nodes, and probes (actual body AABB + column ground + stand skin `0.01`, not footprint multi-sample)
5. **Presentation** — `CreatureLocomotionFacts` → **exactly one** visual backend (see below)

Player Camera and NPC `ExecuteIntent` both use the shared motor. Grounded NPCs with zero wish skip the full motor (idle early-out). Walk/Run suggestedAnim ignored at ~zero horizontal speed.

## Three visual backends (no cross-talk)

| `visual.backend` | Pose path | Do not |
|------------------|-----------|--------|
| `bone_skeleton` | `BoneSkeletonPoseEngine` + `animation_profile` | PosePresenter / glTF clips |
| `gltf_skeleton` | baked clips + `state_map` | BoneSkeletonPoseEngine |
| `rigid_voxels` | `PosePresenterRegistry` by `locomotion_archetype` | bone profiles |

Sprite override: `fire_spirit` (`sprite.billboard`) — billboard, no gait.

Locomotion **archetype** ≠ visual **backend**. Facts are shared; gait implementation is backend-exclusive. Walk/Run thresholds come from horizontal speed facts after travel is non-zero.

## Navigation

- A* stand-nodes are **feet/body**, never eye. Start/goal snap only within `jumpHeight` of body Y.
- **Partial path**: if goal unreachable / expand budget cut → closest-by-heuristic corridor (`partial`, still `valid`).
- **Stuck → repath**: low XZ travel for ~1.5 s invalidates path.
- **Exhaust backoff**: `search_exhausted` / `budget_exhausted` → 0.5→2→4 s + steering-only until retry.
- **Path expand budget**: global per activity tick (`UNavigationPathBudget`); distant LOD skips A* beyond ~80 blocks from player.
- Chase/flee: follow path or approach → **feelers** → separation (not raw soft seek into walls).
- Entity clearance API: `CreaturesClearAt` (entities only). Block clearance: `AreBlocksClearAt` / shared stand.

## Diagnostics

- Schema: `creature_movement_diag.v1` → `creature_movement_diag.jsonl`
- **Default off** in play/release; enable via config `gameplay.creature_movement_diag` or `creature_diag on` for audit runs. Saved `true` in `bin/config.json` from older defaults will keep diag on until flipped — leave it **off** for normal play (JSONL I/O alone can tank FPS).
- A* stand checks use **nav-fast** stand (ground + block AABB); full fluid volume probe is **not** on the path expand hot path.
- Path `reason`: `recalc_ok|partial|start_invalid|goal_invalid|search_exhausted|budget_exhausted|…`

## Related

- [`CREATURE_AGENTS.md`](CREATURE_AGENTS.md)
- [`CREATURE_MOVEMENT_AUDIT.md`](CREATURE_MOVEMENT_AUDIT.md)
- [`CREATURE_POST_B.md`](CREATURE_POST_B.md)
