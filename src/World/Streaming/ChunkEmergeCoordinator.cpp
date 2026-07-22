#include "World/Streaming/ChunkEmergeCoordinator.h"
#include "World/Streaming/FocusIngressPolicy.h"
#include "World/Streaming/MeshLitGate.h"
#include "Blocks/BlockRegistry.h"
#include "Render/Camera/Camera.h"
#include "World/Chunks/ChunkManager.h"
#include "World/Core/BlockWorld.h"
#include "World/Core/RuntimeTuning.h"
#include "World/Core/World.h"
#include "World/Math/BlockTypes.h"
#include "World/Math/GridMath.h"
#include "World/Mesh/WorldMeshService.h"
#include "WorldGen/Core/ProceduralSettings.h"
#include <algorithm>
#include <chrono>
#include <cmath>
#include <iostream>
#include <thread>
#include <vector>

namespace cutum
{

namespace
{
#ifndef NDEBUG
int gMeshTelemetryTick{0};
#endif

// Phase 4/P0: Streaming owns pre-drain promote. This hook no-ops when
// FocusIngressPolicy.promote_once so we do not enqueue a second promote
// after DrainRelightQueues (too late for this frame's cold hole).
void PromoteFrontierHoleIngress(UWorld &world, glm::ivec3 focus_ground_horiz,
                                int focus_radius, bool moving,
                                bool missing_visible_mesh,
                                int pending_focus_count, int pending_async,
                                double last_frame_ms)
{
  (void)world;
  (void)focus_ground_horiz;
  (void)focus_radius;
  const FocusIngressDecision d = EvaluateFocusIngress(FocusIngressInput{
      moving, missing_visible_mesh, pending_focus_count, pending_async,
      last_frame_ms});
  if (d.promote_once)
  {
    return;
  }
}

} // namespace

UChunkEmergeCoordinator::FrameBudget
UChunkEmergeCoordinator::ComputeBudget(const ProceduralSettings &procedural,
                                       float movement_speed,
                                       int default_load_ops,
                                       double last_frame_ms) const
{
  FrameBudget budget;
  const bool boost =
      movement_speed > procedural.MovementSpeedBoostThreshold;
  budget.MaxChunkCommits =
      boost ? procedural.MaxChunkCommitsPerFrameBoost
            : procedural.MaxChunkCommitsPerFrame;
  budget.MaxLoadOps = boost ? procedural.MaxLoadOpsPerFrameBoost
                            : default_load_ops;
  budget.MaxMeshDrain = kDefaultMeshDrain;
  budget.MaxMeshSchedule = kDefaultMeshSchedule;
  if (last_frame_ms > 24.0)
  {
    // Hitch: cut commits/load, keep mesh drain so Dirty does not spiral.
    budget.MaxChunkCommits = 1;
    budget.MaxLoadOps = std::max(1, budget.MaxLoadOps / 2);
  }
  else if (last_frame_ms > 20.0 && boost)
  {
    budget.MaxChunkCommits = std::max(1, budget.MaxChunkCommits - 1);
    budget.MaxLoadOps = std::max(1, budget.MaxLoadOps - 1);
  }
  if (boost && last_frame_ms <= 20.0)
  {
    budget.MaxMeshDrain =
        std::max(budget.MaxMeshDrain, budget.MaxChunkCommits * 2);
    budget.MaxMeshSchedule = budget.MaxMeshDrain;
  }
  return budget;
}

UChunkEmergeCoordinator::FrameBudget
UChunkEmergeCoordinator::WarmupBudget(int mesh_flush)
{
  FrameBudget budget;
  budget.MaxChunkCommits = mesh_flush;
  budget.MaxLoadOps = mesh_flush;
  budget.MaxMeshDrain = mesh_flush;
  budget.MaxMeshSchedule = mesh_flush;
  return budget;
}

UChunkEmergeCoordinator::FrameBudget
UChunkEmergeCoordinator::CooperativeWarmupBudget(int coop_budget)
{
  const int mesh =
      std::min(128, std::max(coop_budget * 8, 32));
  FrameBudget budget;
  budget.MaxMeshDrain = mesh;
  budget.MaxMeshSchedule = mesh;
  return budget;
}

UChunkEmergeCoordinator::FrameBudget
UChunkEmergeCoordinator::CreateMeshWarmupBudget(int coop_budget)
{
  const int threads = std::max(
      2, static_cast<int>(std::thread::hardware_concurrency() > 0
                              ? std::thread::hardware_concurrency()
                              : 4));
  const int mesh =
      std::min(512, std::max(coop_budget * 24, threads * 32));
  FrameBudget budget;
  budget.MaxMeshDrain = mesh;
  budget.MaxMeshSchedule = mesh;
  return budget;
}

void UChunkEmergeCoordinator::BeginFrame(const ProceduralSettings &procedural,
                                         float movement_speed,
                                         int default_load_ops,
                                         double last_frame_ms)
{
  LastBudget =
      ComputeBudget(procedural, movement_speed, default_load_ops, last_frame_ms);
}

void UChunkEmergeCoordinator::TickMeshEmerge(
    UWorld &world, const StreamingPressureCaps &pressure)
{
  const auto emerge_t0 = std::chrono::high_resolution_clock::now();
  UBlockRegistry &registry = world.GetBlockRegistry();
  UWorldMeshService &mesh_service = world.GetMeshService();
  // Count any Immediate this tick (including early idle paths).
  mesh_service.ResetImmediateMeshStats();
  const ProceduralSettings &procedural = world.GetProceduralSettings();
  const float movement_speed = world.GetLastMovementSpeed();
  // Mesh-while-moving uses prefetch threshold so cruise flight drains Dirty.
  const bool moving =
      movement_speed > procedural.MovementPrefetchThreshold;
  const bool moving_fast =
      movement_speed > procedural.MovementSpeedBoostThreshold;
  const double last_frame_ms = world.GetLastMovementFrameMs();

  const glm::ivec3 focus_block = world.GetPreferredLoadFocusBlock();
  const glm::ivec3 focus_ground =
      UChunkManager::WorldToChunk(focus_block);
  const glm::ivec3 focus_ground_horiz(focus_ground.x, 0, focus_ground.z);
  const int focus_radius = world.GetStreamingFocusRadius();
  mesh_service.SetMeshRebuildFocus(focus_ground_horiz, focus_radius);
  // Soft-defer / V2a: no first-mesh or remesh while PendingLight except underfeet.
  mesh_service.SetDeferMeshUntilLitFn(
      [&world, &mesh_service, focus_ground_horiz,
       focus_radius](glm::ivec3 chunk_coord)
      {
        const int horiz =
            std::max(std::abs(chunk_coord.x - focus_ground_horiz.x),
                     std::abs(chunk_coord.z - focus_ground_horiz.z));
        const bool underfeet = horiz <= 1;
        const bool pending = world.IsPendingLightBeforeMesh(
            glm::ivec2(chunk_coord.x, chunk_coord.z));
        const bool has_mesh = mesh_service.HasGreedyMesh(chunk_coord);
        const bool in_focus = horiz <= focus_radius;
        const glm::ivec3 ground(chunk_coord.x, 0, chunk_coord.z);
        const bool may_mesh =
            world.MayMeshColumn(ground, /*underfeet_preview=*/false);
        return SoftDeferMeshUntilLitPolicy(underfeet, has_mesh, pending,
                                           in_focus, may_mesh);
      });

  const bool missing_visible_mesh =
      mesh_service.HasMissingGreedyMeshInHorizontalRadius(
          world.GetBlockWorld(), focus_ground_horiz, focus_radius);
  const bool near_mesh_backlog =
      mesh_service.HasDirtyWithinHorizontalRadius(focus_ground_horiz,
                                                 focus_radius) ||
      missing_visible_mesh;
  const bool pending_near_light =
      world.HasPendingLightBeforeMeshNear(focus_ground_horiz, focus_radius);
  const int pending_focus_count =
      world.CountPendingLightBeforeMeshNear(focus_ground_horiz, focus_radius);
  const int black_sticky = world.CountBlackStickyFocusMeshes(focus_ground,
                                                             focus_radius);
  const size_t pending_dirty_early = mesh_service.GetDirtyCount();
  const int pending_async_early = mesh_service.GetAsyncInFlightCount();
  // Early not_ready for idle catch-up (full count filled later; cheap enough).
  const int not_ready_early =
      world.CountUnfinishedVisualNear(focus_ground_horiz, focus_radius);
  const int focus_dirty_early =
      mesh_service.CountDirtyWithinHorizontalRadius(focus_ground_horiz,
                                                    focus_radius);
  // Lit-but-dirty catch-up: only when focus still has *missing* mesh pressure.
  // Remesh-of-existing (fd high, nr from Dirty/Active) must not latch forever —
  // IsColumnRenderReady no longer counts Dirty; keep debt off for remesh-only.
  const bool idle_remesh_debt =
      !moving && pending_focus_count == 0 && black_sticky == 0 &&
      !missing_visible_mesh && not_ready_early > 32;
  // Pipeline full with no visual light debt → remesh thrash (manual 214430 /
  // 221846: async=42 with Dirty 58–224). Do not require Dirty>200.
  const bool remesh_thrash_only =
      pending_async_early >= 36 && pending_focus_count == 0 &&
      black_sticky == 0 && !missing_visible_mesh && !idle_remesh_debt;
  const bool idle_stop =
      !moving &&
      (pending_focus_count > 0 || black_sticky > 0 || missing_visible_mesh ||
       idle_remesh_debt);
  const bool idle_recovery = idle_stop;
  const bool async_saturated_idle = pending_async_early >= 36;
  static int idle_cancel_cooldown = 0;
  if (idle_recovery)
  {
    // Light debt with clean visuals (sticky=0, missing=0): promote often so
    // PendingLight cannot sit at ~30+ while relight_drain≈0 after Keys ghosts.
    const bool pending_debt_only =
        pending_focus_count > 0 && black_sticky == 0 && !missing_visible_mesh;
    if (idle_cancel_cooldown <= 0 || async_saturated_idle || pending_debt_only)
    {
      // Never CancelOutside during lit-but-dirty remesh catch-up: Active drop +
      // re-Dirty pinned async≈42 and Dirty≈535 while standing (manual 202805).
      if (!idle_remesh_debt)
      {
        mesh_service.CancelInFlightOutsideHorizontalRadius(focus_ground_horiz,
                                                           focus_radius);
      }
      // Never CancelAsyncInFlightKeepDirty during lit-but-dirty catch-up —
      // that reset async every ~30f and froze focus_dirty≈420 / nr≈52.
      if (async_saturated_idle && pending_async_early >= 40 &&
          !idle_remesh_debt && pending_focus_count > 0)
      {
        mesh_service.CancelAsyncInFlightKeepDirty();
      }
      world.PromotePendingLightRelightsNear(focus_ground_horiz, focus_radius);
      // Holes (V2a normal): still promote often — 120-frame gap left FIFO cold
      // while pending sat with relight_drain≈0.
      idle_cancel_cooldown =
          pending_debt_only
              ? 4
              : (idle_remesh_debt
                     ? 20
                     : (pending_focus_count > 0
                            ? 6
                            : (async_saturated_idle ? 30 : 60)));
    }
    else
    {
      --idle_cancel_cooldown;
    }
    // V2b: single bounded idle drain — no SyncIdle flood, no Admit×8, no Refresh.
    // High pending: drain every frame so FIFO stays hot after V2a no-mesh enqueue;
    // do not Admit more columns while light debt is heavy.
    static int idle_visual_drain_cd = 0;
    static int idle_pending_plateau_frames = 0;
    static int idle_pending_plateau_last = -1;
    if (pending_focus_count > 0 &&
        pending_focus_count == idle_pending_plateau_last)
    {
      ++idle_pending_plateau_frames;
    }
    else
    {
      idle_pending_plateau_frames = 0;
      idle_pending_plateau_last = pending_focus_count;
    }
    if (idle_visual_drain_cd <= 0 && last_frame_ms <= 28.0)
    {
      const int budget =
          pending_focus_count > 30
              ? 8
              : (pending_focus_count > 15 ? 6
                                         : (pending_focus_count > 0 ? 4 : 3));
      world.DrainIdleFocusPendingLight(focus_ground_horiz, focus_radius, budget);
      // Break-glass: pending frozen while async applies 0 (relight_drain≈0) —
      // sync underfeet only, never a focus flood.
      // Break-glass: pending frozen (west-strip edge often) — sync 1 farthest
      // focus column (underfeet-only never touched dist=4 ingress).
      // Frontier hole while standing: wall often 40–120; old gate (45f @
      // wall≤18) never fired (manual 102559: miss=1 ~10s @ wall 41–210).
      const int plateau_frames =
          missing_visible_mesh ? 12 : 45;
      const double plateau_wall_ms = missing_visible_mesh ? 120.0 : 18.0;
      if (idle_pending_plateau_frames >= plateau_frames &&
          last_frame_ms <= plateau_wall_ms && pending_focus_count > 0 &&
          last_frame_ms <= 40.0)
      {
        const auto sync_relight_t0 =
            std::chrono::high_resolution_clock::now();
        world.DrainIdleFocusPendingLightSync(focus_ground_horiz, focus_radius,
                                             1);
        world.GetPhysicsTelemetryMutable().RelightDrainMs +=
            std::chrono::duration<double, std::milli>(
                std::chrono::high_resolution_clock::now() - sync_relight_t0)
                .count();
        idle_pending_plateau_frames = 0;
      }
      if (pending_focus_count <= 8)
      {
        world.AdmitFocusVisibleMissing(std::min(2, budget));
        if (pending_focus_count > 0)
        {
          world.AdmitFocusMeshIngress(1);
        }
      }
      world.ClearPendingLightAfterMeshCommitted(12);
      // Sticky remesh: Dirty→async only (Immediate was mesh_emerge hitch).
      if (black_sticky > 0 && last_frame_ms <= 16.0)
      {
        world.SyncIdleFocusGreedyRemesh(1);
      }
      idle_visual_drain_cd =
          pending_focus_count > 8 ? 1 : (pending_focus_count > 0 ? 2 : 8);
    }
    else if (idle_visual_drain_cd > 0)
    {
      --idle_visual_drain_cd;
    }
  }
  else
  {
    idle_cancel_cooldown = 0;
  }
  // Cruise: keep sticky tracking to underfeet only so telemetry/effective_holes
  // are not flooded by far MarkRelit remesh debt.
  if (moving)
  {
    world.PruneStickyRemeshOutside(focus_ground_horiz, /*radius=*/1);
  }
  else if (idle_remesh_debt || black_sticky > 4)
  {
    // Idle catch-up: sticky full-focus flood (manual 210341: 7→13) blocked
    // ClearPending and inflated not_ready — keep underfeet only.
    world.PruneStickyRemeshOutside(focus_ground_horiz, /*radius=*/1);
  }
  // visual_holes = missing mesh only; near_focus_holes kept for legacy paths
  // that still want light-debt urgency for relight (not starve).
  const bool visual_holes = missing_visible_mesh;
  const int focus_not_render_ready =
      world.CountUnfinishedVisualNear(focus_ground_horiz, focus_radius);
  const bool unfinished_visual =
      visual_holes || focus_not_render_ready > 0;
  const bool near_focus_holes = visual_holes || pending_near_light;
  const bool missing_underfeet =
      mesh_service.HasMissingGreedyMeshInHorizontalRadius(
          world.GetBlockWorld(), focus_ground_horiz, /*radius=*/1);
  const bool pending_underfeet =
      world.HasPendingLightBeforeMeshNear(focus_ground_horiz, /*radius=*/1);
  const bool underfeet_need = missing_underfeet || pending_underfeet;

  int preferred_cy = focus_ground.y;
  bool prefer_lower_cy = false;
  const int sea_cy = FloorDiv(procedural.SeaLevel, CHUNK_SIZE);
  if (const auto camera = world.GetCurrentUserCamera())
  {
    const glm::vec3 eye = camera->GetPosition();
    preferred_cy = FloorDiv(static_cast<int>(std::floor(eye.y)), CHUNK_SIZE);
    const FluidColumnSurface column = world.FindFluidColumnSurface(eye);
    if (column.valid && eye.y < column.surfaceY)
    {
      preferred_cy = FloorDiv(column.surfaceBlockY, CHUNK_SIZE);
      prefer_lower_cy = true;
    }
    else if (procedural.FillWater &&
             std::abs(preferred_cy - sea_cy) <= 3)
    {
      // Near sea only — do not force sea_cy when flying with holes
      // (that starved player-altitude approach meshes).
      preferred_cy = sea_cy;
    }
  }
  mesh_service.SetMeshVerticalPriority(preferred_cy, prefer_lower_cy);
  // Lit-but-dirty catch-up: vertical priority left deep Dirty cy forever, so
  // IsColumnRenderReady (full 0..MaxHeight) never cleared (nr≈50 plateau).
  if (idle_remesh_debt)
  {
    mesh_service.ClearMeshVerticalPriority();
  }

  {
    const URuntimeTuning &tune = URuntimeTuning::Get();
    // Idle lit-but-dirty: no view bias — camera turn must not gate remesh
    // (manual 210341: progress only after yaw, then plateau again).
    if (idle_remesh_debt)
    {
      mesh_service.SetMeshForwardBias(0.0f, glm::vec2(0.0f));
      mesh_service.SetMaxRearFocusMeshPerFrame(0);
    }
    else
    {
      glm::vec2 fwd = world.GetLastMovementDirXz();
      if (glm::length(fwd) < 0.01f)
      {
        if (const auto camera = world.GetCurrentUserCamera())
        {
          const glm::vec3 front = camera->GetFront();
          fwd = glm::vec2(front.x, front.z);
        }
      }
      mesh_service.SetMeshForwardBias(tune.MeshForwardBiasK, fwd);
      // Cruise: keep a few focus slots for behind-camera unfinished columns.
      mesh_service.SetMaxRearFocusMeshPerFrame(moving ? 3 : 0);
    }
  }
  int mesh_drain = LastBudget.MaxMeshDrain;
  int mesh_schedule = LastBudget.MaxMeshSchedule;
  const size_t pending_dirty = mesh_service.GetDirtyCount();
  const int pending_async = mesh_service.GetAsyncInFlightCount();

  // Saturated async pool: drop far in-flight work so focus missing can schedule.
  static int async_relief_cooldown = 0;
  if ((visual_holes || missing_underfeet || pending_focus_count > 24) &&
      pending_async >= 28)
  {
    if (async_relief_cooldown <= 0)
    {
      mesh_service.CancelInFlightOutsideHorizontalRadius(focus_ground_horiz,
                                                         focus_radius);
      async_relief_cooldown = (visual_holes || missing_underfeet) ? 30 : 60;
    }
    else
    {
      --async_relief_cooldown;
    }
  }
  else
  {
    async_relief_cooldown = 0;
  }

  // Starve keep-shell remesh when focus MISSING mesh, or light debt on outer ring
  // (dark preview strip while dirty~500 starves async relight — manual 161327).
  const bool cruise_light_debt =
      moving && pending_near_light && !visual_holes &&
      pending_focus_count > 4;
  mesh_service.SetStarveOutsideFocusMesh(visual_holes || missing_underfeet ||
                                         cruise_light_debt || idle_remesh_debt);
  mesh_service.SetStarveRemeshForHoles(visual_holes || missing_underfeet);
  // While sticky remesh drains after pending→0, suppress seam MarkDirty even
  // before sticky hits 0 — otherwise remesh thrash pins async≈42 and nr climbs
  // (P0_hole_promote stop). Full idle_remesh_debt with sticky raised wall/sticky
  // (F2_sticky_remesh); only suppress seam here.
  const bool suppress_seam_for_sticky_catchup =
      !moving && pending_focus_count == 0 && !missing_visible_mesh &&
      black_sticky > 0 &&
      (focus_not_render_ready > 15 || focus_dirty_early > 24);
  // Standing remesh storm (manual 202805: Dirty≈535 async=42 holes=0): seam
  // MarkDirty kept re-feeding Dirty while existing meshes remeshed.
  const bool suppress_seam_standing_churn =
      !moving && pending_focus_count == 0 && !missing_visible_mesh &&
      black_sticky == 0 && pending_dirty_early > 100;
  world.SetSuppressRelightSeamDirty(idle_remesh_debt ||
                                    suppress_seam_for_sticky_catchup ||
                                    suppress_seam_standing_churn);
  // Always scan full focus for sync hole-fill when holes exist. Cap rebuild
  // count via sync_cap (cruise tiny, idle larger) — radius=2 while "moving"
  // missed stop holes when residual speed kept moving=true.
  mesh_service.SetSyncHoleFillRadius(
      (visual_holes || missing_underfeet) ? focus_radius : 1);
  // One-shot pipeline flush when holes appear with saturated async — not every
  // frame (Cancel+reschedule thrash hung flight-sim wall time).
  {
    static bool flushed_for_holes = false;
    if (!(visual_holes || missing_underfeet))
    {
      flushed_for_holes = false;
    }
    else if (!flushed_for_holes && pending_async >= 20)
    {
      mesh_service.CancelAsyncInFlightKeepDirty();
      flushed_for_holes = true;
    }
  }
  // Healthy Dirty flush: raise outside-focus schedule so keep-shell remesh
  // cannot plateau ~450 forever (kMaxOutsideFocusPerFrame=2 alone).
  // Idle lit-but-dirty: never open outside cap — that regressed not_ready.
  if (idle_recovery || idle_remesh_debt)
  {
    mesh_service.SetMaxOutsideFocusMeshPerFrame(0);
  }
  else if (moving &&
           (visual_holes || missing_underfeet || pending_dirty > 450 ||
            cruise_light_debt))
  {
    mesh_service.SetMaxOutsideFocusMeshPerFrame(0);
  }
  else if (!moving && focus_not_render_ready > 15 && last_frame_ms <= 28.0)
  {
    mesh_service.SetMaxOutsideFocusMeshPerFrame(0);
  }
  else if (!visual_holes && !missing_underfeet && pending_dirty > 200 &&
      last_frame_ms <= 28.0)
  {
    mesh_service.SetMaxOutsideFocusMeshPerFrame(pending_dirty > 400 ? 12 : 8);
  }
  else if (visual_holes || missing_underfeet)
  {
    mesh_service.SetMaxOutsideFocusMeshPerFrame(2);
  }
  else
  {
    mesh_service.SetMaxOutsideFocusMeshPerFrame(4);
  }
  // Schedule ring: pending_underfeet alone must NOT clamp to r=1 — that latched
  // MaxHorizontalDist during flight while PendingLight stayed high and carved
  // transverse "roads" of missing GreedyCache (columns loaded, mesh starved).
  // Idle recovery must win over underfeet r=1 — otherwise neighbors admitted
  // after approach never get scheduled while any underfeet hole remains.
  if (idle_recovery || idle_remesh_debt)
  {
    mesh_service.SetMeshScheduleMaxHorizontalDist(focus_radius);
    mesh_service.SetMeshScheduleOverflowPerFrame(0);
  }
  else if (missing_underfeet && !moving && !visual_holes)
  {
    // Standing, only underfeet hole, no focus visual holes: feet first.
    mesh_service.SetMeshScheduleMaxHorizontalDist(1);
    mesh_service.SetMeshScheduleOverflowPerFrame(0);
  }
  else if (missing_underfeet || visual_holes || pressure.focus_pressure_mode)
  {
    // Flight / visual holes: whole focus so lateral columns mesh.
    mesh_service.SetMeshScheduleMaxHorizontalDist(focus_radius);
    const int overflow =
        missing_underfeet ? (moving_fast ? 2 : 1)
        : visual_holes ? (moving ? 3 : 1)
                       : (moving ? 2 : 1);
    mesh_service.SetMeshScheduleOverflowPerFrame(overflow);
  }
  else
  {
    mesh_service.SetMeshScheduleMaxHorizontalDist(-1);
    mesh_service.SetMeshScheduleOverflowPerFrame(0);
  }
  // Holes / underfeet / light-debt idle: allow more snapshot captures.
  // Do NOT open 48ms for lit-but-dirty-only idle_recovery (idle FPS tax).
  mesh_service.SetMeshSnapshotBudgetMs(
      (visual_holes || missing_underfeet ||
       (idle_recovery && pending_focus_count > 0))
          ? 48.0
          : 6.0);

  // Healthy flight with no visual holes: flush Dirty so pressure can leave Red
  // (Dirty plateaus ~700 trapped Red when exit required dirty<=500).
  // Skip while focus relight debt is high — flush starved MarkRelit (pending~50).
  if (idle_recovery)
  {
    mesh_drain = std::max(mesh_drain, 14);
    mesh_schedule = std::max(mesh_schedule, 12);
  }
  // Lit-but-dirty catch-up: after light gate clears, flush focus remesh hard
  // (outside flush previously ate the budget — dirty↑ while nr plateau ~40).
  if (idle_remesh_debt && last_frame_ms <= 28.0)
  {
    mesh_service.SetStarveOutsideFocusMesh(true);
    mesh_service.SetStarveRemeshForHoles(false);
    // Mild catch-up — old 22–32 kept async saturated and wall~25–35 at rest.
    mesh_drain = std::max(mesh_drain, last_frame_ms <= 16.0 ? 16 : 12);
    mesh_schedule = std::max(mesh_schedule, last_frame_ms <= 16.0 ? 12 : 8);
    world.ClearPendingLightAfterMeshCommitted(8);
  }
  // Standing, no holes/pending/sticky: prefer drain over schedule so Dirty
  // shrinks without feeding remesh thrash (async pinned at max pipeline).
  if (!moving && !visual_holes && !pending_near_light && black_sticky == 0 &&
      pending_dirty > 200 && last_frame_ms <= 24.0)
  {
    mesh_schedule = std::min(mesh_schedule, 8);
    mesh_drain = std::max(mesh_drain, 14);
  }
  if (!visual_holes && !missing_underfeet && !pending_near_light &&
      pending_dirty > 200 && last_frame_ms <= 28.0)
  {
    // Moving + high Dirty without holes (manual 134418: dirty~520, async~41,
    // wall~40): drain results, do not ramp schedule (feeds remesh thrash).
    if (moving && pending_dirty > 400)
    {
      mesh_drain = std::max(mesh_drain, 20);
      mesh_schedule = std::min(mesh_schedule, 8);
    }
    else if (moving)
    {
      mesh_drain = std::max(mesh_drain, 16);
      mesh_schedule = std::max(mesh_schedule, 16);
    }
    else
    {
      // Standing: drain Completed only — schedule=22 latched async=42 forever.
      mesh_drain = std::max(mesh_drain, 24);
      mesh_schedule = std::min(mesh_schedule, 4);
    }
  }
  if (!moving && !visual_holes && !pending_near_light && pending_dirty > 400 &&
      last_frame_ms <= 20.0)
  {
    mesh_drain = std::max(mesh_drain, 24);
    mesh_schedule = std::max(mesh_schedule, 24);
  }

  // Teleport / cruise landing: one-shot focus remesh boost when entering idle
  // with lit-but-dirty debt (F2), without Recover MarkDirty flood.
  {
    static bool was_moving = false;
    if (!moving && was_moving && focus_dirty_early > 200 &&
        pending_focus_count == 0 && !missing_visible_mesh &&
        last_frame_ms <= 28.0)
    {
      mesh_service.SetStarveOutsideFocusMesh(true);
      mesh_drain = std::max(mesh_drain, 28);
      mesh_schedule = std::max(mesh_schedule, 22);
    }
    was_moving = moving;
  }

  if (moving && near_mesh_backlog)
  {
    // Cap fly drain hard — 24–28 collapsed FPS (~3) while Dirty stayed >1000.
    // Prefer steady 12–16 so mesh/scene can breathe; idle still flushes harder.
    if (pending_dirty > 48 || pending_async > 16)
    {
      mesh_drain = std::max(mesh_drain, moving_fast ? 16 : 14);
      mesh_schedule = std::max(mesh_schedule, moving_fast ? 14 : 12);
    }
    else if (pending_dirty > 16 || pending_async > 8)
    {
      mesh_drain = std::max(mesh_drain, 12);
      mesh_schedule = std::max(mesh_schedule, 12);
    }
  }

  // Standing still with backlog: prioritize drain/complete over new commits so
  // FPS can recover (dirty outside focus used to never clear).
  const bool idle_light_debt_only =
      !moving && pending_near_light && !visual_holes && black_sticky == 0;
  if (!moving && pending_dirty > 32 && !idle_light_debt_only)
  {
    mesh_drain = std::max(mesh_drain, 20);
    mesh_schedule = std::max(mesh_schedule, 12);
  }
  else if (!moving && pending_dirty > 8 && !idle_light_debt_only)
  {
    mesh_drain = std::max(mesh_drain, 16);
    mesh_schedule = std::max(mesh_schedule, 10);
  }
  if (idle_light_debt_only)
  {
    mesh_service.SetStarveOutsideFocusMesh(true);
    mesh_service.SetStarveRemeshForHoles(false);
    mesh_drain = std::min(mesh_drain, last_frame_ms <= 16.0 ? 10 : 6);
    mesh_schedule = std::min(mesh_schedule, 8);
    static int idle_pending_promote_cd = 0;
    static int idle_pending_sync_cd = 0;
    if (idle_pending_promote_cd <= 0 && last_frame_ms <= 20.0 &&
        pending_focus_count > 0)
    {
      const int budget = pending_focus_count > 20 ? 6 : 4;
      const int promoted = world.DrainIdleFocusPendingLight(
          focus_ground_horiz, focus_radius, budget);
      idle_pending_promote_cd = promoted > 0 ? 2 : 6;
    }
    else if (idle_pending_promote_cd > 0)
    {
      --idle_pending_promote_cd;
    }
    if (idle_pending_sync_cd <= 0 && pending_focus_count >= 2 &&
        pending_focus_count <= 5 && black_sticky == 0 &&
        last_frame_ms <= 16.0)
    {
      const int synced = world.DrainIdleFocusPendingLightSync(
          focus_ground_horiz, focus_radius, 1);
      idle_pending_sync_cd = synced > 0 ? 60 : 90;
    }
    else if (idle_pending_sync_cd > 0)
    {
      --idle_pending_sync_cd;
    }
  }
  if (cruise_light_debt)
  {
    mesh_service.SetStarveOutsideFocusMesh(true);
    mesh_drain = std::min(mesh_drain, last_frame_ms <= 16.0 ? 14 : 10);
    mesh_schedule = std::min(mesh_schedule, 10);
    static int cruise_promote_cd = 0;
    if (cruise_promote_cd <= 0 && last_frame_ms <= 24.0)
    {
      world.DrainIdleFocusPendingLight(focus_ground_horiz, focus_radius, 5);
      cruise_promote_cd = 3;
    }
    else if (cruise_promote_cd > 0)
    {
      --cruise_promote_cd;
    }
  }

  if (world.GetPlayerRelightMeshBurstFrames() > 0)
  {
    mesh_drain = std::max(mesh_drain, 24);
    mesh_schedule = std::max(mesh_schedule, 24);
  }

  // Near dirty must keep MeshAsync draining even under hitch frames.
  if (near_mesh_backlog)
  {
    mesh_drain = std::max(mesh_drain, 12);
    mesh_schedule = std::max(mesh_schedule, 12);
  }

  // Awaiting first light / missing mesh under feet only — modest boost.
  if (underfeet_need)
  {
    mesh_drain = std::max(mesh_drain, 20);
    mesh_schedule = std::max(mesh_schedule, 16);
  }
  else if (pending_near_light || near_focus_holes)
  {
    mesh_drain = std::max(mesh_drain, 16);
    mesh_schedule = std::max(mesh_schedule, 16);
  }
  if (visual_holes)
  {
    // Holes with idle async (~2) means schedule was too timid — fill missing.
    mesh_drain = std::max(mesh_drain, moving ? 18 : 24);
    mesh_schedule = std::max(mesh_schedule, moving ? 16 : 24);
    // Cold SoftDefer pool: push schedule so underfeet/first-mesh can enter
    // Dirty after promote (manual 190126: async_med≈1, holes_rate≈0.68).
    if (moving && pending_async < 4)
    {
      mesh_schedule = std::max(mesh_schedule, 20);
      mesh_drain = std::max(mesh_drain, 22);
    }
  }
  // P0: moving frontier hole + pending + cold mesh pool — promote relight every
  // tick so SoftDefer gate can clear without dark preview (manual 102559 phase A).
  PromoteFrontierHoleIngress(world, focus_ground_horiz, focus_radius, moving,
                             missing_visible_mesh, pending_focus_count,
                             pending_async, last_frame_ms);

  // Floor drain by Dirty backlog so hitch frames do not starve MeshAsync.
  // Cap schedule aggressiveness when underfeet is already OK — flooding
  // schedule while Dirty is high only burns CPU on far-within-focus remesh.
  if (pending_dirty > 0)
  {
    const int dirty_floor =
        std::min(underfeet_need ? 24 : 16,
                 std::max(1, static_cast<int>(pending_dirty) / 4));
    mesh_drain = std::max(mesh_drain, dirty_floor);
    if (last_frame_ms > 24.0 && !underfeet_need && !missing_visible_mesh &&
        !pending_near_light)
    {
      mesh_drain = std::max(mesh_drain, 8);
      mesh_schedule = std::min(mesh_schedule, 4);
    }
    else if (underfeet_need)
    {
      // High drain to finish in-flight underfeet; do not ramp schedule off
      // global Dirty — that re-fed far overflow before feet were visible.
      mesh_schedule = std::max(mesh_schedule, missing_underfeet ? 8 : 12);
    }
    else
    {
      // No underfeet hole: drain results, but do not ramp schedule with Dirty.
      mesh_schedule = std::max(mesh_schedule, std::min(dirty_floor, 12));
    }
  }

  // Remesh after light before consuming other dirty work so black (light=0)
  // meshes do not stick for many frames under ocean stream backlog.
  {
    const int pending_focus =
        world.CountPendingLightBeforeMeshNear(focus_ground_horiz, focus_radius);
    int flush_n =
        pending_dirty < 16 ? 64 : (pending_dirty < 48 ? 32 : 24);
    if (pending_dirty > 600)
    {
      flush_n = std::min(flush_n, 8);
    }
    if (pending_focus > 0)
    {
      flush_n = std::max(flush_n, pending_focus > 15 ? 48 : 32);
    }
    // Idle remesh catch-up: FlushPendingRelight re-MarksDirty and bumps
    // revisions under saturated async → stale Apply → Dirty plateau.
    if (!idle_remesh_debt)
    {
      world.FlushPendingRelightMeshColumns(flush_n);
    }
  }

  // Hitch: cap *schedule* (snapshot cost). Keep drain higher when holes so
  // completed async frees pipeline slots for reserved focus-missing work.
  // Lit-but-dirty catch-up must not be killed by soft hitch (wall~50–60 from
  // mesh itself) — that left async≈4 and focus_dirty flat at ~420.
  if (last_frame_ms > 24.0)
  {
    if (idle_remesh_debt)
    {
      const bool heavy_dirty = focus_dirty_early > 280;
      mesh_schedule =
          std::min(mesh_schedule, last_frame_ms > 40.0 ? 14
                                                         : (heavy_dirty ? 24 : 20));
      mesh_drain = std::min(mesh_drain, last_frame_ms > 40.0 ? 18
                                                               : (heavy_dirty ? 36 : 28));
    }
    else
    {
      mesh_schedule =
          std::min(mesh_schedule, visual_holes || missing_underfeet ? 10 : 8);
      mesh_drain =
          std::min(mesh_drain, visual_holes || missing_underfeet ? 20 : 10);
    }
  }
  else if (last_frame_ms > 16.0)
  {
    if (idle_remesh_debt)
    {
      const bool heavy_dirty = focus_dirty_early > 280;
      mesh_schedule = std::min(mesh_schedule, heavy_dirty ? 26 : 22);
      mesh_drain = std::min(mesh_drain, heavy_dirty ? 36 : 28);
    }
    else
    {
      mesh_schedule = std::min(mesh_schedule, 12);
      mesh_drain =
          std::min(mesh_drain, visual_holes || missing_underfeet ? 16 : 12);
    }
  }

  // Flight FPS guard: clamp schedule while moving; drain may stay higher so
  // MeshAsync does not sit at pipeline depth while focus is missing mesh.
  // Exception: Dirty flush with no visual holes may exceed fly_cap.
  if (moving && (visual_holes || missing_underfeet || pending_dirty <= 400))
  {
    // Holes: allow slightly higher schedule so focus missing can drain; hitch
    // still clamps via last_frame_ms.
    int fly_cap = last_frame_ms > 22.0 ? 8 : (last_frame_ms > 16.0 ? 12 : 16);
    if (visual_holes || missing_underfeet)
    {
      fly_cap = std::max(fly_cap, last_frame_ms > 22.0 ? 10 : 14);
    }
    fly_cap = ApplyPressureCap(fly_cap, pressure.mesh_fly_cap);
    mesh_schedule = std::min(mesh_schedule, fly_cap);
    if (visual_holes || missing_underfeet)
    {
      mesh_drain = std::min(mesh_drain, std::max(fly_cap, 18));
    }
    else
    {
      mesh_drain = std::min(mesh_drain, fly_cap);
    }
  }
  // Idle healthy holes: drain/schedule hard so focus Dirty can clear without
  // Recover flooding (CancelAsync+recover16 caused Dirty~1400 / emerge spikes).
  else if ((near_focus_holes || underfeet_need) && last_frame_ms <= 20.0)
  {
    mesh_drain = std::max(mesh_drain, 24);
    mesh_schedule = std::max(mesh_schedule, 20);
  }

  // Already-meshed focus columns with sky=0 never remesh unless relight is
  // re-queued (stuck black after premature light=0 mesh). Also: pending+sky
  // (neighbor lit) and missing GreedyCache after gate clear.
  {
    const int pending_light_n =
        static_cast<int>(world.GetPendingLightBeforeMeshCount());
    int recover_n = moving ? 3 : 6;
    if (missing_underfeet || pending_underfeet)
    {
      recover_n = moving ? 6 : 10;
    }
    else if (pending_near_light || missing_visible_mesh)
    {
      recover_n = moving ? 4 : 6;
    }
    // pending_light>~15 kept holes=1 forever — flush the focus gate harder,
    // but not when Dirty/async already can't drain (Recover→Dirty spiral).
    if (pending_light_n > 15)
    {
      const int boost = (pending_dirty > 400 || pending_async > 24)
                            ? (moving ? 6 : 8)
                            : (moving ? 10 : 12);
      recover_n = std::max(recover_n, boost);
    }
    // Focus still awaiting first light: prefer enqueue/LitReady over mesh flood.
    const int pending_focus_n =
        world.CountPendingLightBeforeMeshNear(focus_ground_horiz, focus_radius);
    if (pending_focus_n > 8)
    {
      recover_n = std::max(recover_n, moving ? 8 : 12);
    }
    // Saturated mesh pool: prefer drain over new Recover Dirty — but never
    // starve focus pending-light enqueue when the gate is the hole signal.
    if (pending_async > 24 && pending_dirty > 400 && pending_focus_n <= 8)
    {
      recover_n = std::min(recover_n, moving ? 2 : 3);
    }
    recover_n = ApplyPressureCap(recover_n, pressure.recover_n_cap);
    if (idle_recovery)
    {
      recover_n = std::max(recover_n, async_saturated_idle ? 10 : 6);
    }
    recover_n += URuntimeTuning::Get().RecoverNBoost;
    recover_n = std::max(0, recover_n);
    // Event-driven remesh is MarkRelit; Recover is a low-frequency watchdog.
    // Do NOT run every frame on visual_holes — that flooded Dirty (~600+) and
    // starved async drain (mesh_async stuck at 42 with holes).
    static int recover_watchdog_frames = 0;
    ++recover_watchdog_frames;
    const bool recover_now =
        recover_watchdog_frames >= 8 ||
        ((visual_holes || missing_underfeet) && recover_watchdog_frames >= 4) ||
        (pending_near_light && pending_focus_n > 12 &&
         recover_watchdog_frames >= 4) ||
        (black_sticky > 0 && !moving && recover_watchdog_frames >= 4) ||
        (!moving && pending_focus_n > 15 && recover_watchdog_frames >= 2);
    if (recover_now && recover_n > 0)
    {
      // Lit-but-dirty catch-up: Recover MarkDirty floods focus_dirty while
      // async is already full — skip Recover, only drain/schedule remesh.
      if (!idle_remesh_debt)
      {
        world.RecoverUnlitFocusMeshes(recover_n);
      }
      // V2b: one Admit path — missing only; no LightingWithoutDirty flood.
      if (!moving && (missing_visible_mesh || pending_near_light) &&
          !idle_remesh_debt)
      {
        world.AdmitFocusVisibleMissing(std::min(2, recover_n));
        if (pending_focus_n > 0)
        {
          world.AdmitFocusMeshIngress(1);
        }
      }
      else if (moving && visual_holes)
      {
        world.AdmitFocusMeshIngress(std::min(2, recover_n));
      }
      recover_watchdog_frames = 0;
    }
  }

  // Sync-rebuild missing solid slices: underfeet always; idle focus holes too.
  // Hitch must NOT disable focus hole sync — last_frame_ms>20 used to skip the
  // whole ring while visual_holes=1 for the entire stop.
  // Hard Immediate budget (manual spike: mesh_emerge≈RebuildChunkImmediate).
  // One greedy column can still overrun the budget once started — while moving
  // never call Immediate (Dirty→async only). Idle keeps bounded Immediate.
  // Immediate stats already reset at TickMeshEmerge entry.
  const auto immediate_budget_t0 = std::chrono::high_resolution_clock::now();
  const double hard_immediate_ms =
      last_frame_ms > 24.0 ? 4.0 : 6.0;
  auto immediate_ms_used = [&]() -> double
  {
    return std::chrono::duration<double, std::milli>(
               std::chrono::high_resolution_clock::now() - immediate_budget_t0)
        .count();
  };
  auto immediate_budget_ok = [&]() -> bool
  {
    return !moving && immediate_ms_used() < hard_immediate_ms;
  };
  // Underfeet Immediate: idle only; ≤1–2/frame + cooldown.
  static int underfeet_immediate_cd = 0;
  if (underfeet_immediate_cd > 0)
  {
    --underfeet_immediate_cd;
  }
  int underfeet_immediate_this_frame = 0;
  const int kMaxUnderfeetImmediate = last_frame_ms > 20.0 ? 1 : 2;

  // Cold SoftDefer hole: promote into FIFO only. Never DrainRelightQueues from
  // MeshEmerge while moving — even 1× sync RelightTerrainColumn is 1–4s
  // (manual 194645: prep≈relight_drain 3–4s with drain_n=1). Streaming owns
  // paced DrainRelightQueues (async enqueue when AsyncRelight is on).
  {
    const FocusIngressDecision cold =
        EvaluateFocusIngress(FocusIngressInput{
            moving, missing_visible_mesh, pending_focus_count, pending_async,
            last_frame_ms});
    if (cold.active && cold.promote_once && pending_focus_count > 0)
    {
      world.PromotePendingLightRelightsNear(focus_ground_horiz, focus_radius);
    }
    else if (!moving && missing_visible_mesh && pending_focus_count > 0 &&
             last_frame_ms <= 20.0)
    {
      // Promote only — DrainRelightQueuesBudget here still Captures on main
      // (manual 202805: prep≈relight 1.8s). Streaming owns paced drain.
      world.PromotePendingLightRelightsNear(focus_ground_horiz, focus_radius);
    }
  }

  const bool idle_focus_sync =
      !moving &&
      (missing_visible_mesh || black_sticky > 0 || focus_not_render_ready > 0 ||
       (last_frame_ms <= 20.0 && pending_focus_count > 8));
  // Moving: never walk the Immediate ring (MarkDirty flood + solid scans).
  // Prefer nearest missing → Dirty/async only (Immediate banned while moving).
  if (moving && (underfeet_need || missing_visible_mesh))
  {
    glm::ivec3 hole{};
    const int mark_r = underfeet_need ? 1 : focus_radius;
    if (mesh_service.FindNearestMissingGreedyMesh(
            world.GetBlockWorld(), focus_ground_horiz, mark_r, hole) &&
        !mesh_service.HasInflightMeshBuild(hole))
    {
      mesh_service.MarkDirtyPriority(hole);
    }
  }
  else if (underfeet_need || idle_focus_sync)
  {
    const int max_y = procedural.MaxHeight;
    int band_min_y = std::max(0, focus_block.y - CHUNK_SIZE);
    int band_max_y = std::min(max_y, focus_block.y + CHUNK_SIZE * 2);
    if (procedural.FillWater)
    {
      band_min_y =
          std::min(band_min_y, std::max(0, procedural.SeaLevel - CHUNK_SIZE));
      band_max_y = std::max(
          band_max_y,
          std::min(max_y, procedural.SeaLevel + CHUNK_SIZE * 2));
    }
    const int cy0 = FloorDiv(band_min_y, CHUNK_SIZE);
    const int cy1 = FloorDiv(band_max_y, CHUNK_SIZE);
    const int sea_cy = FloorDiv(procedural.SeaLevel, CHUNK_SIZE);
    int prefer_cy = focus_ground.y;
    if (procedural.FillWater && std::abs(focus_ground.y - sea_cy) <= 3)
    {
      prefer_cy = sea_cy;
    }
    // Prefer prefer_cy, then sea (if distinct), then expand within [cy0, cy1].
    std::vector<int> cy_order;
    cy_order.reserve(static_cast<size_t>(cy1 - cy0 + 1));
    auto push_cy = [&](int cy)
    {
      if (cy < cy0 || cy > cy1)
      {
        return;
      }
      if (std::find(cy_order.begin(), cy_order.end(), cy) == cy_order.end())
      {
        cy_order.push_back(cy);
      }
    };
    push_cy(prefer_cy);
    if (procedural.FillWater)
    {
      push_cy(sea_cy);
    }
    for (int d = 1; d <= std::max(prefer_cy - cy0, cy1 - prefer_cy); ++d)
    {
      push_cy(prefer_cy - d);
      push_cy(prefer_cy + d);
    }
    int immediate = 0;
    // Underfeet alone: camera ±1. Focus visual holes / idle holes: whole ring
    // (mirrors schedule — underfeet r=1 must not starve neighbor first-mesh).
    const int horiz_r =
        (underfeet_need && !visual_holes && !idle_focus_sync) ? 1
                                                              : focus_radius;
    const int kMaxImmediate =
        underfeet_need && !visual_holes
            ? kMaxUnderfeetImmediate
            : (last_frame_ms > 16.0 ? 2 : 4);
    for (int r = 0; r <= horiz_r && immediate < kMaxImmediate; ++r)
    {
      for (int dz = -r; dz <= r && immediate < kMaxImmediate; ++dz)
      {
        for (int dx = -r; dx <= r && immediate < kMaxImmediate; ++dx)
        {
          if (r > 0 && std::max(std::abs(dx), std::abs(dz)) != r)
          {
            continue;
          }
          if (!immediate_budget_ok())
          {
            goto done_underfeet_immediate;
          }
          const int ring = std::max(std::abs(dx), std::abs(dz));
          if (underfeet_need && ring > 0 &&
              (moving || immediate >= (last_frame_ms > 20.0 ? 1 : 2)))
          {
            // While moving, only sync the camera column for underfeet path.
            continue;
          }
          for (int cy : cy_order)
          {
            if (immediate >= kMaxImmediate || !immediate_budget_ok())
            {
              break;
            }
            const glm::ivec3 coord(focus_ground.x + dx, cy,
                                   focus_ground.z + dz);
            if (mesh_service.HasGreedyMesh(coord))
            {
              continue;
            }
            // Strict visual contract: do not sync-build an unlit first mesh.
            const UChunk *chunk =
                world.GetBlockWorld().GetChunkManager().GetChunk(coord);
            if (!chunk)
            {
              continue;
            }
            bool any_solid = false;
            for (int z = 0; z < CHUNK_SIZE && !any_solid; z += 4)
            {
              for (int x = 0; x < CHUNK_SIZE && !any_solid; x += 4)
              {
                for (int y = 0; y < CHUNK_SIZE && !any_solid; y += 4)
                {
                  if (chunk->GetBlockLocal(glm::ivec3(x, y, z)) != BLOCK_AIR)
                  {
                    any_solid = true;
                  }
                }
              }
            }
            if (!any_solid)
            {
              continue;
            }
            // Moving: never Immediate (one greedy can be seconds). Prefer Dirty.
            const bool allow_underfeet_immediate =
                !moving && underfeet_immediate_cd <= 0 &&
                underfeet_immediate_this_frame < kMaxUnderfeetImmediate &&
                immediate_budget_ok();
            if (!allow_underfeet_immediate)
            {
              mesh_service.MarkDirtyPriority(coord);
              continue;
            }
            mesh_service.RebuildChunkImmediate(world.GetBlockWorld(), registry,
                                               coord);
            ++immediate;
            ++underfeet_immediate_this_frame;
            underfeet_immediate_cd = last_frame_ms > 20.0 ? 1 : 0;
          }
        }
      }
    }
  done_underfeet_immediate:;
  }

  // After Recover may have just cleared PendingLight underfeet — force sync
  // hole-fill this frame (same path place uses, without waiting Dirty drain).
  const bool underfeet_need_after =
      mesh_service.HasMissingGreedyMeshInHorizontalRadius(
          world.GetBlockWorld(), focus_ground_horiz, /*radius=*/1) ||
      world.HasPendingLightBeforeMeshNear(focus_ground_horiz, /*radius=*/1);

  // Sync-fill holes: aggressive only for underfeet, not global Dirty flood.
  // Hitch: keep sync tiny — emerge spikes were 270–414ms with sync_cap 8–10.
  int sync_cap = last_frame_ms > 16.0 ? 1 : -1;
  if (pending_async > 0 && last_frame_ms > 24.0)
  {
    sync_cap = sync_cap < 0 ? 2 : std::min(sync_cap, 2);
  }
  if (underfeet_need || underfeet_need_after)
  {
    // Idle: SyncRebuild fill. Moving: sync_cap=0 — SyncRebuild/Immediate both
    // can burn seconds on one greedy column; fill via Dirty→async only.
    if (moving)
    {
      sync_cap = 0;
    }
    else if (last_frame_ms > 24.0)
    {
      sync_cap = std::max(sync_cap, 2);
    }
    else if (last_frame_ms > 16.0)
    {
      sync_cap = std::max(sync_cap, 4);
    }
    else
    {
      sync_cap = std::max(sync_cap, 8);
    }
    mesh_schedule = std::max(mesh_schedule, moving ? 14 : 16);
    mesh_drain = std::max(mesh_drain, moving ? 16 : 16);
  }
  else if (missing_visible_mesh)
  {
    // Cruise: prefer async (sync_cap 0–1). Idle: enough to clear focus ring.
    if (moving)
    {
      sync_cap = 0;
    }
    else
    {
      const int sync_idle = pending_focus_count > 8 ? 2 : 6;
      sync_cap = std::max(sync_cap, sync_idle);
    }
    mesh_schedule = std::max(mesh_schedule, moving ? 12 : 20);
    mesh_drain = std::max(mesh_drain, moving ? 12 : 24);
  }
  else if (pending_near_light)
  {
    if (!moving)
    {
      // Idle light debt: starve remesh so relight/MarkRelit can finish.
      mesh_schedule = std::min(mesh_schedule, 8);
      mesh_drain = std::min(mesh_drain, last_frame_ms <= 16.0 ? 10 : 6);
    }
    else
    {
      mesh_schedule = std::max(mesh_schedule, 10);
      mesh_drain = std::max(mesh_drain, 10);
    }
  }
  else if (idle_recovery && async_saturated_idle)
  {
    sync_cap = std::max(sync_cap, last_frame_ms > 20.0 ? 2 : 4);
    mesh_drain = std::max(mesh_drain, 28);
    // Visual pressure still needs schedule; remesh-only saturation must drain.
    if (pending_focus_count > 0 || black_sticky > 0 || missing_visible_mesh ||
        visual_holes)
    {
      mesh_schedule = std::max(mesh_schedule, 22);
    }
    else
    {
      mesh_schedule = std::min(mesh_schedule, 4);
    }
  }
  else if (idle_recovery && black_sticky > 0 && last_frame_ms <= 16.0)
  {
    sync_cap = std::max(sync_cap, 1);
    mesh_drain = std::max(mesh_drain, 12);
    mesh_schedule = std::max(mesh_schedule, 10);
  }
  else if (near_mesh_backlog)
  {
    sync_cap = std::max(sync_cap, pending_async > 0 && last_frame_ms > 24.0 ? 1
                                                                             : 2);
  }
  else if (!moving && pending_dirty > 64 && last_frame_ms <= 20.0)
  {
    sync_cap = 2;
  }
  // SoftDefer frontier hole: never RebuildChunkImmediate while PendingLight —
  // that builds dark preview and leaves sticky (manual 091143: holes+pending
  // → sticky 2–5). Promote/async light must clear the gate; mesh fills after.
  // Spike guard (manual 134418): cold async=0 + hole → no non-underfeet sync
  // fill (mesh_emerge 0.7–3s hitch). Moving: non-underfeet always Dirty/async.
  if (missing_visible_mesh && last_frame_ms <= 50.0)
  {
    static int force_hole_cd = 0;
    const FocusIngressDecision ingress = EvaluateFocusIngress(FocusIngressInput{
        moving, missing_visible_mesh, pending_focus_count, pending_async,
        last_frame_ms});
    if (force_hole_cd <= 0)
    {
      world.PromotePendingLightRelightsNear(focus_ground_horiz, focus_radius);
      glm::ivec3 hole{};
      if (mesh_service.FindNearestMissingGreedyMesh(
              world.GetBlockWorld(), focus_ground_horiz, focus_radius, hole) &&
          !mesh_service.HasInflightMeshBuild(hole))
      {
        const bool hole_pending = world.IsPendingLightBeforeMesh(
            glm::ivec2(hole.x, hole.z));
        const int hole_horiz = std::max(std::abs(hole.x - focus_ground_horiz.x),
                                        std::abs(hole.z - focus_ground_horiz.z));
        const bool hole_underfeet = hole_horiz <= 1;
        const bool sync_ok =
            AllowSyncHoleFillForColumn(ingress, hole_underfeet);
        // Moving: never Immediate (async only). Idle underfeet may Immediate
        // under hard budget + cooldown.
        const double force_frame_cap = 40.0;
        const bool want_immediate =
            !moving && sync_ok && (!hole_pending || hole_underfeet) &&
            last_frame_ms <= force_frame_cap && immediate_budget_ok() &&
            (!hole_underfeet ||
             (underfeet_immediate_cd <= 0 &&
              underfeet_immediate_this_frame < kMaxUnderfeetImmediate));
        if (want_immediate)
        {
          mesh_service.RebuildChunkImmediate(world.GetBlockWorld(), registry,
                                             hole);
          if (hole_underfeet)
          {
            ++underfeet_immediate_this_frame;
            underfeet_immediate_cd = 1;
          }
        }
        else if (!hole_pending || hole_underfeet)
        {
          mesh_service.MarkDirtyPriority(hole);
        }
      }
      force_hole_cd =
          last_frame_ms > 40.0 ? 4 : (moving ? 2 : 1);
    }
    else
    {
      --force_hole_cd;
    }
  }
  // Relight debt without holes: ease mesh so sim_ms can breathe (manual flight
  // sim_ms~120 when pending~30 and mesh_emerge~70).
  if (!visual_holes && !underfeet_need && pending_focus_count > 28 && moving)
  {
    mesh_schedule = std::min(mesh_schedule, 10);
    mesh_drain = std::min(mesh_drain, 14);
  }
  // Re-assert moving no-hole dirty clamp after later schedule boosts (F2).
  // Manual 161304: dirty~566 / wall~49 — prefer drain over schedule thrash.
  if (moving && !visual_holes && !missing_underfeet && !pending_near_light &&
      pending_dirty > 400)
  {
    mesh_schedule = std::min(mesh_schedule, 6);
    mesh_drain = std::max(mesh_drain, 24);
  }
  // Standing remesh thrash only when pipeline is saturated (manual 214430).
  // Do NOT clamp schedule whenever Dirty>100 — that froze Dirty≈270 with
  // async≈4 and left focus remesh starved (manual 220018).
  if (!moving && remesh_thrash_only)
  {
    mesh_schedule = std::min(mesh_schedule, 4);
    mesh_drain = std::max(mesh_drain, 24);
  }
  // Remaining Immediate/SyncRebuild budget shares the hard Immediate ceiling.
  // Moving underfeet: keep SyncRebuild ≤~3ms wall so one chunk cannot hitch.
  const double immediate_spent = immediate_ms_used();
  const double sync_budget_ms =
      moving ? std::min(3.0, std::max(0.5, hard_immediate_ms - immediate_spent))
             : std::max(0.5,
                        std::min(hard_immediate_ms - immediate_spent,
                                 (idle_recovery && black_sticky > 0 &&
                                  last_frame_ms <= 16.0)
                                     ? 6.0
                                 : (underfeet_need && last_frame_ms <= 16.0)
                                     ? 10.0
                                 : (!moving && missing_visible_mesh &&
                                    last_frame_ms <= 20.0)
                                     ? 8.0
                                 : (last_frame_ms > 24.0) ? 4.0
                                                         : 6.0));
  world.GetPhysicsTelemetryMutable().MeshEmergePrepMs =
      std::chrono::duration<double, std::milli>(
          std::chrono::high_resolution_clock::now() - emerge_t0)
          .count();
  const MeshRebuildTickStats tick_stats = mesh_service.RebuildDirtyChunksWithStats(
      world.GetBlockWorld(), registry, mesh_drain, mesh_schedule,
      /*force_sync=*/false, sync_cap, sync_budget_ms);
  mesh_service.DrainAsyncMeshResults(world.GetBlockWorld(), registry, mesh_drain);
  if (tick_stats.Completed > 0 || tick_stats.SyncRebuilt > 0 ||
      (!moving && pending_focus_count > 0))
  {
    world.DrainFocusVisualWork(focus_ground_horiz, focus_radius,
                               idle_recovery ? 24 : 12);
  }

#ifndef NDEBUG
  ++gMeshTelemetryTick;
  if (gMeshTelemetryTick % 60 == 0)
  {
    std::cout << "[MeshEmerge] dirty=" << mesh_service.GetDirtyCount()
              << " inflight=" << mesh_service.GetAsyncInFlightCount()
              << " sync=" << tick_stats.SyncRebuilt
              << " completed=" << tick_stats.Completed
              << " scheduled=" << tick_stats.Scheduled
              << " immediate_ms=" << mesh_service.GetLastMeshImmediateMs()
              << " immediate_n=" << mesh_service.GetLastMeshImmediateCount()
              << std::endl;
  }
#endif
}

} // namespace cutum
