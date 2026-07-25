# Creature movement audit (baseline)

Date: 2026-07-24 (code + instrumentation). Manual follow-ups: **2026-07-25** (09:40, 10:17, 12:40).

## Pipeline

1. `TickAgents` → intent
2. `Creature::ExecuteIntent` (NPC) or Camera + controller (player)
3. Soft habitat gate (actual body) / collision resolve
4. `RebuildLocomotionFacts` → **one** of bone / glTF / rigid pose paths

## Manual runs 2026-07-25

| Session | Finding |
|---------|---------|
| 09:40 | `do_movement` p50 ~34 ms; diag off |
| 10:17 | diag on; `path_ok=0`; march/idle chase |
| 12:40 | zombie **1878× habitat_reject + zero_travel** (chase intent + motor, A* stand-node veto) |

## Findings

| ID | Severity | Finding | Status |
|----|----------|---------|--------|
| CMA-001 | high | Chase/flee A* goal uses eye | **closed** |
| CMA-002 | high | Dual motor player vs NPC | **closed** |
| CMA-003 | medium | Wander probe/stuck | **closed** |
| CMA-004 | medium | Habitat climb vs jumpHeight | **closed** (jumpHeight + soft gate) |
| CMA-005 | medium | Aerial all-or-nothing | **closed** |
| CMA-006 | low | Stuck XZ-only | **closed** |
| CMA-007 | low | Intent sticky | note |
| CMA-008 | info | Docs stale | **closed** |
| CMA-009 | info | No per-mob telemetry | **closed** |
| CMA-010 | high | Walk/Run at zero speed | **closed** |
| CMA-011 | medium | Chase path_fail wall bash / idle | **closed** (direct/slide/soft) |
| CMA-012 | medium | Spider bone gait invisible | **closed** (arachnid Z/Y + fan) |
| CMA-013 | medium | Idle full motor | **closed** (early-out) |
| CMA-014 | medium | Diag every-frame storm | **closed** (event/throttle/batch) |
| CMA-015 | high | Post-motor `IsTerrestrialStandNode` veto | **closed** (soft actual-body gate) |
| CMA-016 | medium | Path 0 ok; no typeId on path_fail | **closed** (snap + fail reasons + ids) |
| CMA-017 | medium | Cross-backend gait confusion risk | **closed** (docs + separate packets) |
| CMA-018 | low | Chicken archetype aerial | **closed** (terrestrial_biped + profile chicken) |
| CMA-019 | low | Amphibious can_fly / aquatic arch on land | **closed** (land arch + inFluid→aquatic facts) |

## Backend × archetype matrix (fix packets)

| Backend | Species examples | Shared travel | Visual packet |
|---------|------------------|---------------|---------------|
| bone_skeleton | zombie, skeleton, spider, pig, sheep, cow, wolf, bunny, fox, chicken, bee, dolphin, squid, trout, tortoise, human | P0/P2 | BoneSkeletonPoseEngine profiles |
| gltf_skeleton | dirt/sand/stone/mese_monster, dungeon_master, warthog, rat, owl, shark, lava_flan, … | P0/P2 | clips/`state_map`/speeds vs Luanti |
| rigid_voxels | rigid_demo_walker/flyer/swimmer | P0/P2 | PosePresenter only |
| sprite override | fire_spirit | P0 aerial | no gait |

**Rule:** never apply bone profiles to glTF, presenters to bone, or glTF clips to bone spider.

## Habitat / behavior smoke

| Case | Species | Backend | Expect | Notes |
|------|---------|---------|--------|-------|
| Chase | zombie | bone | travel>0 flat; less habitat_reject | re-test after CMA-015 |
| Chase path | zombie | bone | path_ok or soft direct + travel | CMA-016 |
| Spider gait | spider | bone | A/B legs + rest fan | |
| Wander | dirt_monster | glTF | walk clip while moving | |
| Demo | rigid_demo_walker | rigid | presenter swing iff speed>0 | |
| Amphibious | seal/penguin | glTF | land walk; swim facts in fluid | |
| Aerial | bee / wasp | bone/glTF | fly without mass reject | |
| Lava | lava_flan | glTF | stay in lava | |
| Sprite | fire_spirit | sprite | billboard; aerial arch | |

## Regression guards

Do **not** weaken `IsTerrestrialStandNode` continuous clearance for **player** pathfinding. Soft habitat gate is for NPC post-motor / wander probe only. Nav goals remain feet/body.

## Next

1. Manual re-run: zombie chase + spider + dirt_monster + rigid_demo_walker
2. If `do_movement` still high → LOD motor packet
3. Spider wall-climb (MC) — separate bone-only follow-up
