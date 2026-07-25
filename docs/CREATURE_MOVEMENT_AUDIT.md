# Creature movement audit (baseline)

Date: 2026-07-24 (code + instrumentation). Manual session follow-up: **2026-07-25**.

## Pipeline (reference)

1. `TickAgents` → intent (`Wander` / `Flee` / `Melee` + `USimpleFsmBrain`)
2. `Creature::ExecuteIntent` (NPC) or Camera + `CreatureLocomotionController` (player)
3. Habitat / collision resolve
4. `RebuildLocomotionFacts` → pose

Enable telemetry: default-on `gameplay.creature_movement_diag` (event/throttle/batch); console `creature_diag focus nearest|verbose on|dump`.

## Manual run 2026-07-25 (`bin/logs`)

Session `perf_20260725-094046_512.jsonl` (~121 periods):

| Metric | p50 (approx) | Notes |
|--------|--------------|-------|
| `do_movement_ms` / `physics_movement_ms` | ~34 ms | Dominant frame cost after mob spawn |
| `scene_ms` | ~7 ms | Render not primary culprit |
| `dirty` | ~269–287 | Streaming rebuild backlog |
| `pending_light` | ~28–33 | Relight backlog |

- **`creature_movement_diag.jsonl` missing** — config had `creature_movement_diag: false`; no AI movement evidence that session.
- Symptoms observed: zombies “march in place”; spider legs static; FPS drop after spawn.
- Root causes addressed in follow-up commits:
  - Habitat full-step revert + Walk/Run hint at zero travel → march
  - Spider `quadruped` X-roll on horizontal `leg0`–`leg7` → no visible gait
  - Per-frame NPC motor × N (+ dirty/light) → `do_movement` cost; naive every-frame JSONL would worsen FPS

## Findings

| ID | Severity | Finding | Evidence | Status |
|----|----------|---------|----------|--------|
| CMA-001 | high | Chase/flee A* goal uses eye | SimpleFsmBrain + ControlledCreatureInfo | **closed** (body/feet goals) |
| CMA-002 | high | Dual motor player vs NPC | Creature / Camera | **closed** (shared CreatureMotor) |
| CMA-003 | medium | Wander probe/stuck | WanderActivityAgent | **closed** (3D stuck + probe ~ speed) |
| CMA-004 | medium | Habitat climb/drop 1.25 vs jumpHeight | CreatureEnvironment + 2026-07-25 march | **closed** (jumpHeight gate + partial XZ accept) |
| CMA-005 | medium | Aerial all-or-nothing | ExecuteIntent | **closed** (shared ResolveMovement slide) |
| CMA-006 | low | Stuck XZ-only | CreatureActivitySteering | **closed** (3D speed) |
| CMA-007 | low | Intent sticky between activity ticks | agents | note (by design) |
| CMA-008 | info | Docs stale on flee/melee | docs | **closed** (docs sync) |
| CMA-009 | info | No per-mob telemetry | — | **closed** (diagnostics) |
| CMA-010 | high | Walk/Run suggestedAnim at zero speed | FinalizeLocomotionFacts / derive; 2026-07-25 | **closed** (no fake walk phase / state) |
| CMA-011 | medium | Chase `path_fail` → direct XZ wall bash | SimpleFsmBrain | **closed** (idle / lateral slide) |
| CMA-012 | medium | Spider bone gait invisible | BoneSkeletonPoseEngine quadruped | **closed** (`arachnid` Z/Y on leg0–7) |
| CMA-013 | medium | Idle NPC full motor every frame | ExecuteIntent; perf p50 ~34 ms | **closed** (grounded zero-wish early-out) |
| CMA-014 | medium | Default diag off / every-frame JSONL risk | config 2026-07-25; Record mutex+disk | **closed** (default-on event/throttle/batch) |

## Habitat / behavior smoke checklist

| Case | Species | Expect | Result |
|------|---------|--------|--------|
| Terrestrial wander slope/wall | pig | move or stuck→repick; no habitat leave | pending in-game |
| Flee | sheep | flee away; prefer `path_ok` on flat | pending |
| Chase | zombie | approach; `path_ok` / travel>0 on flat; no march-in-place | re-test after CMA-010/011/004 |
| Aquatic | trout/shark | stay in water | pending |
| Amphibious | seal/penguin | land or water OK | pending |
| Aerial | wasp/owl | air move; no embed in blocks | pending |
| Lava | lava_flan | stay in lava | pending |
| Spider gait | spider | alternating legs while wander travels | re-test after CMA-012 |
| Possess vs NPC step | pig | travel/step-up match | pending |
| Perf after spawn | mixed | `do_movement_ms` p50 below ~34 ms baseline at similar mob count | re-test after CMA-013 |

## Regression guards (player passability)

Do **not** weaken `IsTerrestrialStandNode` continuous clearance or feet-anchored `ResolveMovement` to “fix” chase. Nav goals must use body/feet; player motor path unchanged except shared helper extraction.

## Next

1. Re-run checklist with JSONL (`creature_diag focus nearest`) and compare `do_movement_ms` to 2026-07-25 baseline
2. If `do_movement` still >15 ms at 20+ mobs → separate LOD motor packet
3. Streaming Dirty/pending_light remains a secondary FPS track (out of this movement packet)
