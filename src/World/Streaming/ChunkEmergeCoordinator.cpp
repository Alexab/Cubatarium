#include "World/Streaming/ChunkEmergeCoordinator.h"
#include "World/Streaming/ColumnFlowScheduler.h"
#include "World/Streaming/ColumnFlowExecutor.h"
#include "World/Streaming/ColumnRenderablePolicy.h"
#include "World/Streaming/FocusIngressPolicy.h"
#include "World/Streaming/IdleRecoveryPolicy.h"
#include "World/Streaming/MeshLitGate.h"
#include "World/Streaming/MeshWorkAdmission.h"
#include "Blocks/BlockRegistry.h"
#include "Render/Camera/Camera.h"
#include "Render/Mesh/GpuMeshPipeline.h"
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
  const bool moving =
      movement_speed > procedural.MovementPrefetchThreshold;
  if (moving)
  {
    // Phase C: 8ms emerge contract — cap mesh work while cruising.
    budget.MaxMeshDrain = std::min(budget.MaxMeshDrain, 6);
    budget.MaxMeshSchedule = std::min(budget.MaxMeshSchedule, 6);
    budget.MaxChunkCommits = std::min(budget.MaxChunkCommits, 1);
  }
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
  auto prep_ms_since =
      [](std::chrono::high_resolution_clock::time_point t0) -> double
  {
    return std::chrono::duration<double, std::milli>(
               std::chrono::high_resolution_clock::now() - t0)
        .count();
  };
  auto prep_t = emerge_t0;
  double prep_missing_ms = 0.0;
  double prep_unfinished_ms = 0.0;
  double prep_sticky_ms = 0.0;
  double prep_drop_dirty_ms = 0.0;
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
  const double last_frame_wall_ms = world.GetLastMovementFrameMs();
  const double last_do_movement_ms =
      world.GetPhysicsTelemetry().DoMovementMs;
  // Use DoMovement time (actual CPU work) for emerge budgeting so GL swap
  // stalls do not starve the meshing pipeline.
  const double last_frame_ms =
      last_do_movement_ms > 0.5 ? last_do_movement_ms : last_frame_wall_ms;

  const glm::ivec3 focus_block = world.GetPreferredLoadFocusBlock();
  const glm::ivec3 focus_ground =
      UChunkManager::WorldToChunk(focus_block);
  const glm::ivec3 focus_ground_horiz(focus_ground.x, 0, focus_ground.z);
  const int focus_radius = world.GetStreamingFocusRadius();
  mesh_service.SetMeshRebuildFocus(focus_ground_horiz, focus_radius);
  // Soft-defer / V2a: no first-mesh while PendingLight except underfeet; remesh
  // of existing mesh is deferred even underfeet (player dig dark overwrite).
  // Cold SoftDefer hole: also allow first-mesh for the single nearest missing
  // chunk (dark preview) so visual_holes cannot plate cold_relight 4–6s while
  // Y-band Capture keeps finalize_gate=false (f2_cold_finalize).
  // HasMissing first — FindNearest walks all loaded chunks with solid probes
  // even when there is no hole (CB mesh_emerge_prep ~5ms on no-hole fly).
  const bool missing_visible_mesh =
      mesh_service.HasMissingGreedyMeshInHorizontalRadius(
          world.GetBlockWorld(), focus_ground_horiz, focus_radius);
  glm::ivec3 nearest_missing_hole{};
  const bool have_nearest_missing =
      missing_visible_mesh &&
      mesh_service.FindNearestMissingGreedyMesh(
          world.GetBlockWorld(), focus_ground_horiz, focus_radius,
          nearest_missing_hole);
  prep_missing_ms = prep_ms_since(prep_t);
  prep_t = std::chrono::high_resolution_clock::now();
  mesh_service.SetDeferMeshUntilLitFn(
      [&world, &mesh_service, focus_ground_horiz, focus_radius,
       have_nearest_missing, nearest_missing_hole](glm::ivec3 chunk_coord)
      {
        const int horiz =
            std::max(std::abs(chunk_coord.x - focus_ground_horiz.x),
                     std::abs(chunk_coord.z - focus_ground_horiz.z));
        const bool underfeet = horiz <= 1;
        const bool pending = world.IsPendingLightBeforeMesh(
            glm::ivec2(chunk_coord.x, chunk_coord.z));
        const bool is_nearest_hole =
            have_nearest_missing &&
            chunk_coord.x == nearest_missing_hole.x &&
            chunk_coord.z == nearest_missing_hole.z;
        const bool has_mesh = mesh_service.HasDrawableGreedyMesh(chunk_coord);
        const bool in_focus = horiz <= focus_radius;
        const glm::ivec3 ground(chunk_coord.x, 0, chunk_coord.z);
        const bool may_mesh =
            world.MayMeshColumn(ground, /*underfeet_preview=*/false);
        // SoftDefer: remesh while pending always deferred. UnlitFirstMesh is an
        // explicit SoT allow (AllowUnlitFirstMesh), not a pending-mask bypass.
        const bool allow_unlit = AllowUnlitFirstMesh(
            has_mesh, horiz, is_nearest_hole, in_focus);
        return SoftDeferMeshUntilLitPolicy(
            underfeet, has_mesh,
            world.RequiresLightingLitGate() && pending, in_focus, may_mesh,
            allow_unlit);
      });

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
  // Idle-only: CountUnfinishedVisualNear is O(focus²) complete+ready scans.
  // Moving paths never use not_ready_early (idle_remesh_debt requires !moving).
  const int not_ready_early =
      moving ? 0
             : world.CountUnfinishedVisualNear(focus_ground_horiz, focus_radius);
  const int focus_dirty_early =
      mesh_service.CountDirtyWithinHorizontalRadius(focus_ground_horiz,
                                                    focus_radius);
  prep_unfinished_ms += prep_ms_since(prep_t);
  prep_t = std::chrono::high_resolution_clock::now();
  // Lit-but-dirty catch-up: only when focus still has *missing* mesh pressure.
  // Remesh-of-existing (fd high, nr from Dirty/Active) must not latch forever —
  // IsColumnRenderReady no longer counts Dirty; keep debt off for remesh-only.
  const bool idle_remesh_debt =
      !moving && pending_focus_count == 0 && black_sticky == 0 &&
      !missing_visible_mesh && not_ready_early > 32;
  // Focus lit-but-dirty backlog with nr≈0 still fails F2 fd_end≤280. Use a
  // small persistence latch: activate debt only when dirty is high for several
  // consecutive idle frames and does not improve.
  static int idle_focus_dirty_prev = 0;
  static int idle_focus_dirty_high_frames = 0;
  const auto focus_dirty_debt = EvaluateIdleFocusDirtyDebt(
      IdleFocusDirtyDebtInput{moving, pending_focus_count, black_sticky,
                              missing_visible_mesh, focus_dirty_early,
                              idle_focus_dirty_prev,
                              idle_focus_dirty_high_frames});
  idle_focus_dirty_prev = focus_dirty_debt.prev_focus_dirty_next;
  idle_focus_dirty_high_frames = focus_dirty_debt.high_frames_next;
  const bool idle_focus_dirty_debt = focus_dirty_debt.active;
  // Pipeline full with no visual light debt → remesh thrash (manual 214430 /
  // 221846: async=42 with Dirty 58–224). Do not require Dirty>200.
  const bool remesh_thrash_only =
      pending_async_early >= 36 && pending_focus_count == 0 &&
      black_sticky == 0 && !missing_visible_mesh && !idle_remesh_debt;
  const bool idle_stop =
      !moving &&
      (pending_focus_count > 0 || black_sticky > 0 || missing_visible_mesh ||
       idle_remesh_debt || idle_focus_dirty_debt);
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
      if (!idle_remesh_debt && !idle_focus_dirty_debt)
      {
        mesh_service.CancelInFlightOutsideHorizontalRadius(focus_ground_horiz,
                                                           focus_radius);
      }
      // Never CancelAsyncInFlightKeepDirty during lit-but-dirty catch-up —
      // that reset async every ~30f and froze focus_dirty≈420 / nr≈52.
      if (async_saturated_idle && pending_async_early >= 40 &&
          !idle_remesh_debt && !idle_focus_dirty_debt &&
          pending_focus_count > 0)
      {
        mesh_service.CancelAsyncInFlightKeepDirty();
      }
      {
        auto &exec = GetColumnFlowExecutor();
        exec.RunPromoteRelightNow(world, focus_ground_horiz, focus_radius);
      }
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
    const auto visual_drain = EvaluateIdleVisualDrain(IdleVisualDrainInput{
        last_frame_ms, idle_visual_drain_cd, pending_focus_count,
        missing_visible_mesh, idle_pending_plateau_frames});
    if (visual_drain.run_drain)
    {
      GetColumnFlowExecutor().DrainIdlePendingLight(
          world, focus_ground_horiz, focus_radius, visual_drain.budget,
          visual_drain.allow_sync, last_frame_ms, pending_focus_count,
          missing_visible_mesh);
      if (visual_drain.allow_sync)
      {
        idle_pending_plateau_frames = 0;
      }
      if (pending_focus_count <= 8)
      {
        // E2 ownership: enqueue then drain (dispatch all kinds). Enqueue-only
        // starved Admit until recover_watchdog and left holes/pending high.
        // Scheduler dedupes same column+kind, so one FirstMesh carries admit_n.
        const int admit_n = std::min(2, visual_drain.budget);
        auto &exec = GetColumnFlowExecutor();
        exec.Enqueue(glm::ivec2(focus_ground_horiz.x, focus_ground_horiz.z),
                     ColumnWorkKind::FirstMesh, 40 + admit_n);
        exec.DrainBudget(world, 4, focus_ground_horiz, focus_radius, admit_n);
      }
      // F2: after stop, clear committed pending more aggressively.
      const int clear_n =
          world.GetTimeSinceMotionSec() > 0.0 &&
                  world.GetTimeSinceMotionSec() <= 8.0
              ? 24
              : 12;
      world.ClearPendingLightAfterMeshCommitted(clear_n);
      idle_visual_drain_cd = visual_drain.idle_visual_drain_cd_next;
    }
    else if (idle_visual_drain_cd > 0)
    {
      --idle_visual_drain_cd;
    }
    // Sticky remesh outside the wall≤28 visual-drain gate: stop-tail wall is
    // often 40–55ms (F2 sticky grew while SyncIdle never ran). ColumnFlow only.
    const auto sticky_drain = EvaluateStickyRemeshDrain(
        StickyRemeshDrainInput{black_sticky, last_frame_ms});
    if (sticky_drain.run_drain)
    {
      GetColumnFlowExecutor().DrainRemeshSeamBudget(world, sticky_drain.budget);
    }
    // I6: after sticky drain, drop lit remesh sticky that no longer has stale-dark
    // (Dirty-only leftover was pinning autofly sticky 2–6).
    if (pending_focus_count == 0 && !missing_visible_mesh && black_sticky > 0)
    {
      world.ClearPendingLightAfterMeshCommitted(
          std::max(8, black_sticky + 4));
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
  // I6: long calm stand — drop leftover sticky (void-edge stale can pin count=1).
  // P1 idle-clean: prune leftover sticky quickly once miss/pending clear.
  if (!moving && pending_focus_count == 0 && !missing_visible_mesh &&
      black_sticky > 0 && world.GetTimeSinceMotionSec() > 3.0)
  {
    world.PruneStickyRemeshOutside(focus_ground_horiz, /*radius=*/0);
    world.ClearPendingLightAfterMeshCommitted(32);
  }
  prep_sticky_ms = prep_ms_since(prep_t);
  prep_t = std::chrono::high_resolution_clock::now();
  // visual_holes = missing mesh only; near_focus_holes kept for legacy paths
  // that still want light-debt urgency for relight (not starve).
  const bool visual_holes = missing_visible_mesh;
  // Same cruise skip as not_ready_early — idle catch-up still counts fully.
  // I5: reuse not_ready_early — second CountUnfinishedVisualNear was duplicate.
  const int focus_not_render_ready = not_ready_early;
  const bool unfinished_visual =
      visual_holes || focus_not_render_ready > 0;
  const bool near_focus_holes = visual_holes || pending_near_light;
  const bool missing_underfeet =
      visual_holes &&
      mesh_service.HasMissingGreedyMeshInHorizontalRadius(
          world.GetBlockWorld(), focus_ground_horiz, /*radius=*/1);
  const bool pending_underfeet =
      world.HasPendingLightBeforeMeshNear(focus_ground_horiz, /*radius=*/1);
  prep_unfinished_ms += prep_ms_since(prep_t);
  prep_t = std::chrono::high_resolution_clock::now();

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

  // Empty SoftDefer placeholders: HasMissing now keys off !Drawable, but rim
  // still needs Force Dirty when SoftDefer dropped entries. Scan focus_radius
  // (was Chebyshev r<=1) with a per-frame MarkDirty cap (manual 101824 rim).
  bool underfeet_undrawn = false;
  auto &phys_telem = world.GetPhysicsTelemetryMutable();
  phys_telem.SoftDeferEmptyPlaceholderN = 0;
  phys_telem.SoftDeferEmptyStuckN = 0;
  phys_telem.SoftDeferEmptyStuckDefer = 0;
  {
    if (UndrawnForceCd > 0)
    {
      --UndrawnForceCd;
    }
    if (StuckSmokeCd > 0)
    {
      --StuckSmokeCd;
    }
    const int max_cy = std::max(
        0, FloorDiv(procedural.MaxHeight, CHUNK_SIZE));
    const int cy0 =
        missing_visible_mesh ? 0 : std::max(0, preferred_cy - 1);
    const int cy1 = std::min(max_cy, preferred_cy + 2);
    constexpr int kUndrawnMarkCap = 4;
    if (UndrawnForceCd <= 0)
    {
      int marked_n = 0;
      const int heal_r = std::max(1, focus_radius);
      for (int dx = -heal_r; dx <= heal_r && marked_n < kUndrawnMarkCap; ++dx)
      {
        for (int dz = -heal_r; dz <= heal_r && marked_n < kUndrawnMarkCap; ++dz)
        {
          for (int cy = cy0; cy <= cy1 && marked_n < kUndrawnMarkCap; ++cy)
          {
            const glm::ivec3 coord(focus_ground_horiz.x + dx, cy,
                                   focus_ground_horiz.z + dz);
            const int horiz = std::max(std::abs(dx), std::abs(dz));
            const bool has_drawable = mesh_service.HasDrawableGreedyMesh(coord);
            const bool has_greedy = mesh_service.HasGreedyMesh(coord);
            const bool is_dirty = mesh_service.IsChunkMeshDirty(coord);
            const bool pending_gpu = mesh_service.IsPendingGpuApply(coord);
            const bool inflight = mesh_service.HasInflightMeshBuild(coord);
            if (has_drawable || pending_gpu || inflight || is_dirty)
            {
              continue;
            }
            // Only wrong-empty SoftDefer placeholders (cache entry, no quads).
            if (!has_greedy)
            {
              continue;
            }
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
            ++phys_telem.SoftDeferEmptyPlaceholderN;
            // A2 smoke: stuck pattern HasGreedy && !Drawable && !Dirty && horiz>1
            if (horiz > 1)
            {
              ++phys_telem.SoftDeferEmptyStuckN;
              if (phys_telem.SoftDeferEmptyStuckN == 1)
              {
                phys_telem.SoftDeferEmptyStuckCx = coord.x;
                phys_telem.SoftDeferEmptyStuckCy = coord.y;
                phys_telem.SoftDeferEmptyStuckCz = coord.z;
                phys_telem.SoftDeferEmptyStuckHoriz = horiz;
                phys_telem.SoftDeferEmptyStuckDefer = 1;
              }
              if (StuckSmokeCd <= 0)
            {
#if defined(CUBATARIUM_SOFTDEFER_SMOKE)
                std::cerr << "[SoftDeferEmptySmoke] HasGreedy=!Drawable "
                          << "Dirty=0 SoftDefer~1 horiz=" << horiz << " at ("
                          << coord.x << "," << coord.y << "," << coord.z
                          << ")\n";
#endif
                StuckSmokeCd = 120;
              }
            }
            // FirstMesh placeholder — always Dirty (do not gate on DirtyAdmit;
            // HoleDrain admit=0 left post_stop miss sticky).
            mesh_service.MarkDirtyPriority(coord);
            underfeet_undrawn = true;
            ++marked_n;
          }
        }
      }
      if (marked_n > 0)
      {
        UndrawnForceCd = 8;
      }
    }
  }
  phys_telem.SoftDeferHeldN =
      static_cast<int>(mesh_service.GetSoftDeferHeldCount());
  const bool underfeet_need =
      missing_underfeet || pending_underfeet || underfeet_undrawn;
  if (underfeet_undrawn)
  {
    auto &exec = GetColumnFlowExecutor();
    exec.Enqueue(glm::ivec2(focus_ground_horiz.x, focus_ground_horiz.z),
                 ColumnWorkKind::FirstMesh, 110);
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
    // Idle lit-but-dirty: remesh stays isotropic (no view gate) — only when
    // there are no holes (idle_remesh_debt already implies !missing).
    // P1: with holes / cruise, idle uses camera-front + stronger bias (≥1.5).
    if (idle_remesh_debt)
    {
      mesh_service.SetMeshForwardBias(0.0f, glm::vec2(0.0f));
      mesh_service.SetMaxRearFocusMeshPerFrame(0);
    }
    else
    {
      glm::vec2 fwd = world.GetLastMovementDirXz();
      if (!moving || glm::length(fwd) < 0.01f)
      {
        if (const auto camera = world.GetCurrentUserCamera())
        {
          const glm::vec3 front = camera->GetFront();
          fwd = glm::vec2(front.x, front.z);
        }
      }
      const float bias_k =
          !moving ? std::max(tune.MeshForwardBiasK, 1.5f)
                  : tune.MeshForwardBiasK;
      mesh_service.SetMeshForwardBias(bias_k, fwd);
      // Cruise: rear slots for behind-camera unfinished. Idle+holes: keep a
      // few rear slots so FOV bias cannot starve stop recovery (P1 smoke).
      mesh_service.SetMaxRearFocusMeshPerFrame(
          moving ? 3
                 : ((visual_holes || missing_underfeet) ? 2 : 0));
    }
  }
  int mesh_drain = LastBudget.MaxMeshDrain;
  int mesh_schedule = LastBudget.MaxMeshSchedule;
  const size_t pending_dirty = mesh_service.GetDirtyCount();
  const int pending_async = mesh_service.GetAsyncInFlightCount();

  // Soft-cap Dirty under Yellow/Red: drop farthest remesh (not holes).
  // Manual 091724: Dirty~400–590 with SoftCap 1200 and async≤29 — thrash never
  // engaged. Use DirtyThrashSoftCap whenever stream Yellow/Red OR async thrash.
  // Anti-pattern: cruise keep_h=0 / eye-shell all-Dirty / remesh-only keep_h=1
  // beyond focus → wall_no_holes↑ dirty↑ / F2 pending (cb_nogui).
  {
    const auto &mtune = URuntimeTuning::Get();
    auto &phys = world.GetPhysicsTelemetryMutable();
    const int pressure = phys.StreamPressure;
    int dirty_cap = mtune.DirtySoftCap;
    const bool thrash_async =
        mtune.DirtyThrashAsyncMin > 0 &&
        pending_async >= mtune.DirtyThrashAsyncMin;
    if (mtune.DirtyThrashSoftCap > 0 &&
        (pressure >= 1 || thrash_async))
    {
      dirty_cap = std::min(dirty_cap, mtune.DirtyThrashSoftCap);
    }
    if (dirty_cap > 0)
    {
      mesh_service.ReserveDirtyCapacity(static_cast<size_t>(dirty_cap));
    }
    if (dirty_cap > 0 &&
        pending_dirty > static_cast<size_t>(dirty_cap) &&
        (pressure >= 1 || thrash_async))
    {
      const int dropped = mesh_service.MaybeDropFarthestDirty(
          focus_ground_horiz, static_cast<size_t>(dirty_cap), 1);
      phys.DirtyDropped += static_cast<uint64_t>(std::max(0, dropped));
    }
    if (pressure >= 1 && pending_dirty > 360 && !visual_holes &&
        !missing_underfeet)
    {
      const int dropped = mesh_service.MaybeDropFarthestDirty(
          focus_ground_horiz, 360, 1);
      phys.DirtyDropped += static_cast<uint64_t>(std::max(0, dropped));
    }
    if (mtune.PendingLightSoftCap > 0 &&
        world.GetPendingLightBeforeMeshCount() >
            static_cast<size_t>(mtune.PendingLightSoftCap))
    {
      const int dropped = world.TrimPendingLightBeforeMesh(
          focus_ground_horiz, mtune.PendingLightSoftCap);
      phys.PendingLightDropped += static_cast<uint64_t>(std::max(0, dropped));
    }
    if (mtune.RelightFifoSoftCap > 0)
    {
      const int dropped = world.TrimFarRelightFifoFarthest(
          focus_ground_horiz, mtune.RelightFifoSoftCap);
      phys.RelightFifoDropped += static_cast<uint64_t>(std::max(0, dropped));
    }
  }

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
                                         cruise_light_debt || idle_remesh_debt ||
                                         idle_focus_dirty_debt);
  // If hole pressure is high but async mesh is still near-zero, fully starving
  // remesh can deadlock focus in pending-light + missing-mesh state. Only relax
  // while moving — stop/idle must keep remesh starved to avoid post_stop_pending
  // and wall_ms spikes from aggressive hole-fill remesh.
  const bool relax_hole_starve =
      moving && (pending_focus_count > 16) && (pending_async <= 4);
  mesh_service.SetStarveRemeshForHoles((visual_holes || missing_underfeet) &&
                                       !relax_hole_starve);
  // Keep remesh within 2 (or 3 when stale-dark debt) so neighbor black faces
  // repair while FirstMesh fills the hole.
  {
    const int stale_n = world.GetPhysicsTelemetry().DarkFaceStaleNearN;
    mesh_service.SetStarveRemeshKeepHoriz(stale_n > 200 ? 3 : 2);
  }
  // Cruise Dirty flood: drop remesh beyond focus (keep first-mesh) so
  // dirty_med_no_holes clears CB (cb_starve: 652→369). Do not StarveRemesh
  // cruise-wide — that raised spike_holes (269).
  // Anti-pattern: throttle DropRemesh (cb_mid) → spike_max_wall ~4s.
  // Idle extension: stop often sits Dirty~650 / focus_dirty~170 with holes=0
  // (manual_post) — full Dirty sort then burns ~100ms/frame. Same remesh-only
  // prune as cruise; keep first-mesh so SoftDefer holes stay schedulable.
  // Isolated hole (manual 084551 near_h sticky): also prune remesh while
  // missing so Dirty≈300 does not starve the single FirstMesh column.
  // Keep remesh within 3 when stale-dark is high so neighbor black faces stay.
  if (pending_dirty > static_cast<size_t>(
                          std::max(280, URuntimeTuning::Get().DirtyThrashSoftCap)) &&
      (moving || (!moving && pending_focus_count <= 16) || visual_holes ||
       missing_underfeet))
  {
    const int stale_n = world.GetPhysicsTelemetry().DarkFaceStaleNearN;
    const int drop_keep_h =
        (visual_holes || missing_underfeet)
            ? std::min(focus_radius, stale_n > 200 ? 3 : 2)
            : focus_radius;
    const auto drop_t0 = std::chrono::high_resolution_clock::now();
    world.GetPhysicsTelemetryMutable().DirtyDropped += static_cast<uint64_t>(
        std::max(0, mesh_service.DropRemeshDirtyBeyondRadius(
                        focus_ground_horiz, drop_keep_h, /*keep_cy=*/-1,
                        /*remesh_only=*/true)));
    prep_drop_dirty_ms += prep_ms_since(drop_t0);
  }
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
  world.SetSuppressRelightSeamDirty(idle_remesh_debt || idle_focus_dirty_debt ||
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
  // C2: while focus has undrawn / visual holes, keep a trickle of outside
  // FirstMesh (MaxOutside=0 starved rim behind sticky miss).
  if (idle_recovery || idle_remesh_debt)
  {
    mesh_service.SetMaxOutsideFocusMeshPerFrame(0);
  }
  else if (moving &&
           (visual_holes || missing_underfeet || underfeet_undrawn ||
            pending_dirty > 450 || cruise_light_debt))
  {
    if (visual_holes || underfeet_undrawn || missing_underfeet)
    {
      mesh_service.SetMaxOutsideFocusMeshPerFrame(1);
    }
    else
    {
      mesh_service.SetMaxOutsideFocusMeshPerFrame(0);
    }
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
  // Focus lit-but-dirty catch-up also needs budget — 6ms left async≈4 and
  // fd flat ~415 for the whole stop (f2_fd_golden). Cap below hole 48ms.
  mesh_service.SetMeshSnapshotBudgetMs(
      (visual_holes || missing_underfeet ||
       (idle_recovery && pending_focus_count > 0))
          ? 48.0
          : (idle_focus_dirty_debt ? 28.0 : 6.0));
  // Healed idle (miss=0, no pending/holes): default idle emerge 60ms ate wall.
  // Keep 60 only while recovering holes/light. Sticky remesh is async — do not
  // hold the 60ms SyncRebuild band just because black_sticky>0 (I4e: sticky=8
  // kept emerge=60 and sync~35ms on classifier-calm stand).
  const bool healed_idle_emerge =
      !moving && !visual_holes && !missing_underfeet &&
      !missing_visible_mesh && pending_focus_count == 0;
  mesh_service.SetMeshEmergeTotalBudgetMs(
      moving ? 25.0
             : (healed_idle_emerge
                    ? (idle_focus_dirty_debt ? 28.0 : 14.0)
                    : 60.0));

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
  // Drop Dirty outside eye shell even when wall is hot (stop wall 80–200).
  // keep_h=1 + keep_cy=2 ≈ 45 max; also drops first-mesh/air Dirty that remesh-
  // only prune left behind (fd_end plateau ~410).
  const glm::ivec3 focus_dirty_keep(focus_ground.x, preferred_cy,
                                    focus_ground.z);
  if (idle_focus_dirty_debt)
  {
    mesh_service.DropRemeshDirtyBeyondRadius(focus_dirty_keep, /*keep_h=*/1,
                                            /*keep_cy=*/2);
  }
  // S3: stop Dirty plateau — drop remesh outside wider FOV shell.
  if (!moving && !visual_holes && !pending_near_light &&
      focus_dirty_early > 160)
  {
    mesh_service.DropRemeshDirtyBeyondRadius(focus_dirty_keep, /*keep_h=*/2,
                                            /*keep_cy=*/2);
  }
  // Idle opaque stability (manual 131234 p23: opaque_cmd_on 209→1021 while
  // nh/miss=0). Far unlit↔lit remesh churns the opaque draw list; fluid is a
  // separate pass so sea stays steady. keep_h=1 (was 2) — land_fix_P2 churn
  // 326 with focus+2 still too wide.
  if (!moving && !visual_holes && !missing_visible_mesh)
  {
    mesh_service.DropRemeshDirtyBeyondRadius(focus_dirty_keep, /*keep_h=*/1,
                                            /*keep_cy=*/2);
  }
  if ((idle_remesh_debt || idle_focus_dirty_debt) && last_frame_ms <= 55.0)
  {
    mesh_service.SetStarveOutsideFocusMesh(true);
    mesh_service.SetStarveRemeshForHoles(false);
    mesh_service.SetMaxOutsideFocusMeshPerFrame(0);
    if (idle_focus_dirty_debt)
    {
      mesh_drain = std::max(mesh_drain, last_frame_ms <= 36.0 ? 28 : 18);
      mesh_schedule = std::max(mesh_schedule, last_frame_ms <= 36.0 ? 18 : 12);
    }
    else
    {
      mesh_drain = std::max(mesh_drain, last_frame_ms <= 16.0 ? 16 : 12);
      mesh_schedule = std::max(mesh_schedule, last_frame_ms <= 16.0 ? 12 : 8);
    }
    world.ClearPendingLightAfterMeshCommitted(16);
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
  // Gpu packed defer: async telemetry stays low while GPU extract queues build.
  // Feed async harder so unfinished_visual / not_render_ready can clear.
  if ((visual_holes || missing_visible_mesh) && pending_async_early < 8)
  {
    const int gpu_cap = moving ? 12 : 16;
    mesh_schedule = std::max(mesh_schedule, gpu_cap);
    mesh_drain = std::max(mesh_drain, gpu_cap);
  }
  // Early pending_gpu read — MeshWorkAdmission for producers; Finalize after
  // drain-first consume so schedule sees post-Finish pending (F0).
  size_t pending_gpu_n = mesh_service.GetPendingGpuAppliesCount();
  {
    MeshWorkAdmissionInput ain{};
    ain.pending_gpu = pending_gpu_n;
    ain.pending_gpu_queued = mesh_service.GetPendingGpuQueuedCount();
    ain.pending_gpu_kicked = mesh_service.GetPendingGpuKickedCount();
    ain.visual_holes = visual_holes || missing_visible_mesh ||
                       world.GetPhysicsTelemetry().FocusMissingMesh > 0;
    ain.missing_underfeet = missing_underfeet;
    ain.moving = moving;
    ain.pending_light_near = pending_focus_count;
    ain.unfinished_visual = world.GetPhysicsTelemetry().UnfinishedVisual;
    ain.prev_mode = static_cast<uint8_t>(LastBudget.AdmissionMode);
    ain.ring_depth = UGpuMeshPipeline::kReadbackRing;
    mesh_service.SetMeshWorkAdmission(ComputeMeshWorkAdmission(ain));
  }
  if (focus_not_render_ready > 12 && pending_async_early < 10)
  {
    mesh_schedule = std::max(mesh_schedule, 14);
    mesh_drain = std::max(mesh_drain, 14);
  }
  // TD-ARCH-027: FOV unfinished → async throughput floor (not schedule cap).
  // Cap Immediate/sync elsewhere; workers need schedule headroom when Dirty high.
  if ((visual_holes || focus_not_render_ready > 0 ||
       world.GetPhysicsTelemetry().UnfinishedVisual > 0) &&
      pending_async_early < 8)
  {
    mesh_schedule = std::max(mesh_schedule, moving ? 12 : 16);
    mesh_drain = std::max(mesh_drain, moving ? 14 : 18);
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
      const bool stop_window =
          world.GetTimeSinceMotionSec() > 0.0 &&
          world.GetTimeSinceMotionSec() <= 8.0;
      const int budget =
          stop_window ? (pending_focus_count > 8 ? 10 : 8)
                      : (pending_focus_count > 20 ? 6 : 4);
      GetColumnFlowExecutor().DrainIdlePendingLight(
          world, focus_ground_horiz, focus_radius, budget, false,
          last_frame_ms, pending_focus_count, missing_visible_mesh);
      const int promoted = budget;
      idle_pending_promote_cd = promoted > 0 ? (stop_window ? 1 : 2) : 6;
    }
    else if (idle_pending_promote_cd > 0)
    {
      --idle_pending_promote_cd;
    }
    if (idle_pending_sync_cd <= 0 && pending_focus_count >= 2 &&
        pending_focus_count <= 5 && black_sticky == 0 &&
        last_frame_ms <= 16.0)
    {
      // Formerly sync RelightTerrainColumn; with AsyncRelight → FIFO only.
      GetColumnFlowExecutor().DrainIdlePendingLight(
          world, focus_ground_horiz, focus_radius, 1, true, last_frame_ms,
          pending_focus_count, missing_visible_mesh);
      const int synced = 1;
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
      GetColumnFlowExecutor().DrainIdlePendingLight(
          world, focus_ground_horiz, focus_radius, 5, false, last_frame_ms,
          pending_focus_count, missing_visible_mesh);
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
  if (world.GetEnterGameMeshBurstFrames() > 0)
  {
    mesh_drain = std::max(mesh_drain, 20);
    mesh_schedule = std::max(mesh_schedule, 16);
  }
  // TD-ARCH-021: keep catch-up while Visual ring unfinished (not only 5 frames).
  if (!moving && world.NeedsSpawnRingCatchUp())
  {
    mesh_drain = std::max(mesh_drain, 24);
    mesh_schedule = std::max(mesh_schedule, 20);
    // Hide⇒Ticket drain: sticky/stale-dark unfinished needs SyncIdle, not only
    // Dirty schedule (async can sit on remesh while draw_ok stays false).
    auto &exec = GetColumnFlowExecutor();
    exec.Enqueue(glm::ivec2(focus_ground_horiz.x, focus_ground_horiz.z),
                 ColumnWorkKind::FirstMesh, 80);
    exec.DrainBudget(world, 4, focus_ground_horiz, focus_radius, 4);
    exec.DrainRemeshSeamBudget(world, 8);
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
  // Frontier promote: WorldStreaming FocusIngress + ColumnFlowExecutor
  // PromoteRelight (not a second promote hook here — double-promote hung edge).

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
    if (!idle_remesh_debt && !idle_focus_dirty_debt)
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
    if (idle_remesh_debt || idle_focus_dirty_debt)
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
    if (idle_remesh_debt || idle_focus_dirty_debt)
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
  // Phase B: baselines from RuntimeTuning (then StreamingPressure fly_cap).
  if (moving && (visual_holes || missing_underfeet || pending_dirty <= 400))
  {
    const URuntimeTuning &fly_tune = URuntimeTuning::Get();
    int fly_cap =
        last_frame_ms > static_cast<double>(fly_tune.MeshFlyWallHotMs)
            ? fly_tune.MeshFlyCapWallHot
            : (last_frame_ms > static_cast<double>(fly_tune.MeshFlyWallMidMs)
                   ? fly_tune.MeshFlyCapWallMid
                   : fly_tune.MeshFlyCapWallOk);
    if (visual_holes || missing_underfeet)
    {
      fly_cap = std::max(
          fly_cap,
          last_frame_ms > static_cast<double>(fly_tune.MeshFlyWallHotMs)
              ? fly_tune.MeshFlyCapHolesHot
              : fly_tune.MeshFlyCapHolesOk);
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
        (!moving && pending_focus_n > 15 && recover_watchdog_frames >= 2) ||
        (world.GetPhysicsTelemetry().DarkFaceNearN > 500 &&
         recover_watchdog_frames >= 2);
    if (recover_now && recover_n > 0)
    {
      auto &exec = GetColumnFlowExecutor();
      int admit_n =
          (!moving && missing_visible_mesh && !idle_remesh_debt)
              ? 1
              : (!moving && pending_near_light && !idle_remesh_debt)
                    ? 1
                    : ((moving && (visual_holes || missing_visible_mesh)) ? 1
                                                                         : 0);
      if (moving && pending_dirty > 100 && pending_async < 6)
      {
        admit_n = std::max(admit_n, 4);
      }
      // FOV unfinished: always admit FirstMesh (manual_1940 underfeet lag).
      if (visual_holes || missing_underfeet || focus_not_render_ready > 0)
      {
        admit_n = std::max(admit_n, moving ? 3 : 2);
      }
      exec.TickDerived(world, focus_ground_horiz, focus_radius, moving,
                       missing_visible_mesh, visual_holes, idle_remesh_debt,
                       idle_focus_dirty_debt, pending_focus_n, recover_n,
                       admit_n);
      if (!idle_remesh_debt && !idle_focus_dirty_debt)
      {
        const int pending_dark_preview =
            world.CountPendingDarkFocusMeshes(focus_ground, focus_radius);
        const bool urgent_dark_pending =
            pending_focus_n > 0 &&
            world.GetPhysicsTelemetry().DarkFaceNearN > 500;
        if (pending_dark_preview > 0 || urgent_dark_pending)
        {
          // Must stay below FirstMesh (100+admit). recover_n+100 starved rim
          // admit (land-cruise miss_stuck 6–12s).
          const int relight_prio =
              missing_visible_mesh ? 55 : (recover_n + 100);
          exec.Enqueue(glm::ivec2(focus_ground_horiz.x, focus_ground_horiz.z),
                       ColumnWorkKind::RelightThenMesh, relight_prio);
        }
      }
      exec.DrainBudget(world, recover_n, focus_ground_horiz, focus_radius, 1);
      // Edge stale-dark / post-miss sticky: raise seam drain near sticky
      // (land_fix P3 — keep_h already 2–3 via StarveRemeshKeepHoriz).
      const int dark_n = world.GetPhysicsTelemetry().DarkFaceNearN;
      if (dark_n > 200 || black_sticky > 0)
      {
        const int seam_n = std::clamp(
            3 + (dark_n > 800 ? 4 : 0) + black_sticky * 2, 3, 10);
        exec.DrainRemeshSeamBudget(world, seam_n);
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
  // Phase B: budgets from RuntimeTuning (streaming_tune.json overlay).
  const auto immediate_budget_t0 = std::chrono::high_resolution_clock::now();
  const URuntimeTuning &imm_tune = URuntimeTuning::Get();
  const double hard_immediate_ms =
      last_frame_ms > static_cast<double>(imm_tune.ImmediateHotWallMs)
          ? static_cast<double>(imm_tune.ImmediateBudgetHotMs)
          : static_cast<double>(imm_tune.ImmediateBudgetOkMs);
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
            last_frame_ms, world.GetPhysicsTelemetry().UnfinishedVisual,
            world.GetPhysicsTelemetry().DarkFaceStaleNearN});
    if (cold.active && cold.promote_once &&
        (pending_focus_count > 0 || missing_visible_mesh))
    {
      auto &exec = GetColumnFlowExecutor();
      exec.RunPromoteRelightNow(world, focus_ground_horiz, focus_radius);
      if (cold.first_mesh_admit > 0)
      {
        exec.Enqueue(glm::ivec2(focus_ground_horiz.x, focus_ground_horiz.z),
                     ColumnWorkKind::FirstMesh, 80);
        exec.DrainBudget(world, 1, focus_ground_horiz, focus_radius,
                         cold.first_mesh_admit);
      }
    }
    else if (!moving && missing_visible_mesh && pending_focus_count > 0 &&
             last_frame_ms <= 20.0)
    {
      auto &exec = GetColumnFlowExecutor();
      exec.RunPromoteRelightNow(world, focus_ground_horiz, focus_radius);
      exec.Enqueue(glm::ivec2(focus_ground_horiz.x, focus_ground_horiz.z),
                   ColumnWorkKind::FirstMesh, 60);
      exec.DrainBudget(world, 2, focus_ground_horiz, focus_radius, 2);
    }
    else if (!moving &&
             (missing_visible_mesh || black_sticky > 0 ||
              focus_not_render_ready > 0) &&
             last_frame_ms <= 28.0)
    {
      auto &exec = GetColumnFlowExecutor();
      exec.RunPromoteRelightNow(world, focus_ground_horiz, focus_radius);
      exec.DrainBudget(world, 2, focus_ground_horiz, focus_radius, 2);
      // TD-ARCH-026/027: scale sticky remesh via ColumnFlow (no direct SyncIdle).
      const int sticky_sync =
          std::clamp(4 + black_sticky * 2 + (focus_not_render_ready > 16 ? 4 : 0),
                     4, 12);
      exec.DrainRemeshSeamBudget(world, sticky_sync);
    }
  }

  const bool idle_focus_sync =
      !moving &&
      (missing_visible_mesh || black_sticky > 0 || focus_not_render_ready > 0 ||
       (last_frame_ms <= 20.0 && pending_focus_count > 8));
  // Moving: skip full Immediate ring (MarkDirty flood). Underfeet r≤1 may
  // still Immediate when async is cold (Phase B sticky-miss fix).
  if (moving && (underfeet_need || missing_visible_mesh))
  {
    glm::ivec3 hole{};
    const int mark_r = underfeet_need ? 1 : focus_radius;
    bool have_hole = false;
    if (have_nearest_missing)
    {
      const int nh =
          std::max(std::abs(nearest_missing_hole.x - focus_ground_horiz.x),
                   std::abs(nearest_missing_hole.z - focus_ground_horiz.z));
      if (nh <= mark_r)
      {
        hole = nearest_missing_hole;
        have_hole = true;
      }
    }
    if (!have_hole)
    {
      have_hole = mesh_service.FindNearestMissingGreedyMesh(
          world.GetBlockWorld(), focus_ground_horiz, mark_r, hole);
    }
    if (have_hole && !mesh_service.HasInflightMeshBuild(hole))
    {
      const int hole_horiz =
          std::max(std::abs(hole.x - focus_ground_horiz.x),
                   std::abs(hole.z - focus_ground_horiz.z));
      const bool allow_uf_imm =
          underfeet_need && hole_horiz <= 1 &&
          underfeet_immediate_cd <= 0 &&
          underfeet_immediate_this_frame < kMaxUnderfeetImmediate &&
          pending_async < 2 && immediate_ms_used() < 8.0;
      if (allow_uf_imm)
      {
        mesh_service.RebuildChunkImmediate(world.GetBlockWorld(), registry,
                                           hole);
        ++underfeet_immediate_this_frame;
        underfeet_immediate_cd = 1;
        GetColumnFlowExecutor().Enqueue(glm::ivec2(hole.x, hole.z),
                                        ColumnWorkKind::FirstMesh, 110);
      }
      else if (!mesh_service.HasMeshSatisfyingColumnReady(hole) &&
               !mesh_service.IsPendingGpuApply(hole) &&
               !mesh_service.HasInflightMeshBuild(hole))
      {
        mesh_service.MarkDirtyPriority(hole);
        GetColumnFlowExecutor().Enqueue(glm::ivec2(hole.x, hole.z),
                                        ColumnWorkKind::FirstMesh, 110);
      }
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
            // SoftDefer empty placeholders still HasGreedyMesh but !ready —
            // skipping them left miss sticky while Immediate never ran.
            if (mesh_service.HasMeshSatisfyingColumnReady(coord) ||
                mesh_service.IsPendingGpuApply(coord) ||
                mesh_service.HasInflightMeshBuild(coord))
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
            // UnlitFirstMesh SoT: PendingLight must not block Immediate on a
            // missing slice — only remesh stays SoftDefer-gated.
            if (world.IsPendingLightBeforeMesh(glm::ivec2(coord.x, coord.z)))
            {
              GetColumnFlowExecutor().Enqueue(
                  glm::ivec2(coord.x, coord.z),
                  ColumnWorkKind::PromoteRelight, 100);
            }
            // Phase B (174511): narrow underfeet Immediate r≤1 only — breaks
            // sticky miss without reopening focus-ring Immediate zoo.
            GetColumnFlowExecutor().Enqueue(
                glm::ivec2(coord.x, coord.z), ColumnWorkKind::FirstMesh, 110);
            const bool allow_underfeet_immediate =
                underfeet_immediate_cd <= 0 &&
                underfeet_immediate_this_frame < kMaxUnderfeetImmediate &&
                immediate_ms_used() < 8.0 &&
                ((!moving && immediate_budget_ok()) ||
                 (moving && pending_async < 2 && immediate_ms_used() < 8.0));
            if (allow_underfeet_immediate &&
                std::max(std::abs(dx), std::abs(dz)) <= 1)
            {
              mesh_service.RebuildChunkImmediate(world.GetBlockWorld(), registry,
                                                 coord);
              ++immediate;
              ++underfeet_immediate_this_frame;
              underfeet_immediate_cd = last_frame_ms > 20.0 ? 1 : 0;
            }
            else
            {
              mesh_service.MarkDirtyPriority(coord);
              ++immediate;
            }
            continue;
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
  else if (idle_recovery && black_sticky > 0 && last_frame_ms <= 40.0)
  {
    sync_cap = std::max(sync_cap, black_sticky > 4 ? 2 : 1);
    mesh_drain = std::max(mesh_drain, black_sticky > 4 ? 18 : 12);
    mesh_schedule = std::max(mesh_schedule, black_sticky > 4 ? 14 : 10);
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
        last_frame_ms, world.GetPhysicsTelemetry().UnfinishedVisual,
        world.GetPhysicsTelemetry().DarkFaceStaleNearN});
    if (force_hole_cd <= 0)
    {
      {
        auto &exec = GetColumnFlowExecutor();
        exec.RunPromoteRelightNow(world, focus_ground_horiz, focus_radius);
      }
      glm::ivec3 hole{};
      if (mesh_service.FindNearestMissingGreedyMesh(
              world.GetBlockWorld(), focus_ground_horiz, focus_radius, hole))
      {
        // Orphan Active (HasInflight) without builder flight: still MarkDirty —
        // skipping left miss=1 sticky while FindNearest kept returning the hole.
        if (mesh_service.HasInflightMeshBuild(hole))
        {
          mesh_service.MarkDirtyPriority(hole);
        }
        else
      {
        const bool hole_pending = world.IsPendingLightBeforeMesh(
            glm::ivec2(hole.x, hole.z));
        const int hole_horiz = std::max(std::abs(hole.x - focus_ground_horiz.x),
                                        std::abs(hole.z - focus_ground_horiz.z));
        const bool hole_underfeet = hole_horiz <= 1;
        const bool is_nearest_hole =
            have_nearest_missing && hole.x == nearest_missing_hole.x &&
            hole.z == nearest_missing_hole.z;
        // Never Immediate while PendingLight — dark bake sticks until finalize.
        // Allow sync/Dirty for the nearest missing column after light clears.
        // (Tried Unlit Immediate while pending — land_fix_P1d miss_stuck 48s /
        // wall_med 162; keep MarkDirty + SoftDefer AllowUnlit instead.)
        const bool sync_ok =
            AllowSyncHoleFillForColumn(ingress, hole_underfeet) ||
            (is_nearest_hole && pending_async < 4);
        const double force_frame_cap = 40.0;
        const bool want_immediate =
            sync_ok && !hole_pending &&
            (!hole_underfeet ||
             (underfeet_immediate_cd <= 0 &&
              underfeet_immediate_this_frame < kMaxUnderfeetImmediate)) &&
            (moving
                 ? ((hole_underfeet ||
                     (is_nearest_hole && pending_async < 2)) &&
                    pending_async < 4 && immediate_ms_used() < 8.0)
                 : (last_frame_ms <= force_frame_cap && immediate_budget_ok()));
        if (want_immediate)
        {
          mesh_service.RebuildChunkImmediate(world.GetBlockWorld(), registry,
                                             hole);
          if (hole_underfeet)
          {
            ++underfeet_immediate_this_frame;
            underfeet_immediate_cd = 1;
          }
          // One Immediate per ~2 frames while moving — enough to break cold
          // 2s periods without mesh_emerge hitch storms.
          if (moving)
          {
            force_hole_cd = 1;
          }
        }
        else
        {
          // Always Dirty-queue the nearest FOV hole — SoftDefer FOV first-mesh
          // bypass covers pending; skipping MarkDirty left async=0 for periods.
          mesh_service.MarkDirtyPriority(hole);
          // Nudge only *missing* underfeet-ring slices. Remesh-dirtying already
          // meshed neighbors caused RemeshAfterApply storms + discarded_late
          // (manual 213546 discards 0→111, opaque churn 925).
          // HoleDrain: ColumnFlow FirstMesh only — no neighbor Dirty bypass.
          if (missing_visible_mesh &&
              mesh_service.GetMeshWorkAdmission().allow_neighbor_dirty)
          {
            for (int dz = -1; dz <= 1; ++dz)
            {
              for (int dx = -1; dx <= 1; ++dx)
              {
                if (dx == 0 && dz == 0)
                {
                  continue;
                }
                const glm::ivec3 neighbor(hole.x + dx, hole.y, hole.z + dz);
                if (!mesh_service.HasDrawableGreedyMesh(neighbor) &&
                    !mesh_service.IsPendingGpuApply(neighbor) &&
                    mesh_service.TryConsumeDirtyAdmit())
                {
                  mesh_service.MarkDirtyPriority(neighbor);
                }
              }
            }
          }
          // Q2 seam: already-drawable neighbors keep Unknown-culled side faces
          // until remeshed after this hole FirstMesh lands (manual 191432).
          // Cap 4; HoleDrain-safe (does not admit missing neighbors).
          if (missing_visible_mesh)
          {
            int seamed = 0;
            for (int dz = -1; dz <= 1 && seamed < 4; ++dz)
            {
              for (int dx = -1; dx <= 1 && seamed < 4; ++dx)
              {
                if (dx == 0 && dz == 0)
                {
                  continue;
                }
                const glm::ivec3 neighbor(hole.x + dx, hole.y, hole.z + dz);
                if (mesh_service.HasDrawableGreedyMesh(neighbor) &&
                    mesh_service.TryConsumeDirtyAdmit())
                {
                  mesh_service.MarkDirtyPriority(neighbor);
                  ++seamed;
                }
              }
            }
          }
        }
      }
      }
      if (force_hole_cd <= 0)
      {
        force_hole_cd =
            (pending_async < 4 && pending_focus_count > 0)
                ? 0
                : (last_frame_ms > 40.0 ? 4 : (moving ? 2 : 1));
      }
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
  // Re-assert moving no-hole dirty clamp after later schedule boosts (CB
  // wall_ms_no_holes). snap≤1 raised spike_holes (cb_wall2); keep snap2.
  if (moving && !visual_holes && !missing_underfeet && pending_dirty > 280)
  {
    mesh_schedule = std::min(mesh_schedule, 3);
    mesh_drain = std::min(mesh_drain, 10);
    mesh_service.SetMeshSnapshotBudgetMs(2.0);
  }
  // Saturated async on lit cruise: ease snapshot so phys catch-up stays down.
  if (moving && !visual_holes && !missing_underfeet && pending_async >= 28)
  {
    mesh_schedule = std::min(mesh_schedule, 2);
    mesh_drain = std::min(mesh_drain, 12);
    mesh_service.SetMeshSnapshotBudgetMs(1.5);
  }
  // Standing remesh thrash only when pipeline is saturated (manual 214430).
  // Do NOT clamp schedule whenever Dirty>100 — that froze Dirty≈270 with
  // async≈4 and left focus remesh starved (manual 220018).
  if (!moving && remesh_thrash_only && !world.NeedsSpawnRingCatchUp() &&
      focus_not_render_ready == 0)
  {
    mesh_schedule = std::min(mesh_schedule, 4);
    mesh_drain = std::max(mesh_drain, 24);
  }
  // Lit remesh-only idle (SoT unfinished=0, no holes): cap schedule so spawn
  // Dirty≈250 remesh does not pin wall≈85 for ~40s and dominate ARCH wall_med.
  if (!moving && pending_focus_count == 0 && !missing_visible_mesh &&
      black_sticky == 0 && not_ready_early == 0 && pending_dirty > 128 &&
      !world.NeedsSpawnRingCatchUp())
  {
    mesh_schedule = std::min(mesh_schedule, 6);
    mesh_drain = std::min(mesh_drain, 14);
    mesh_service.SetMeshSnapshotBudgetMs(2.0);
  }
  // Remaining Immediate/SyncRebuild budget shares the hard Immediate ceiling.
  // Moving underfeet: keep SyncRebuild ≤~3ms wall so one chunk cannot hitch.
  const double immediate_spent = immediate_ms_used();
  double sync_budget_ms =
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
  {
    auto &pt = world.GetPhysicsTelemetryMutable();
    pt.MeshEmergePrepMissingMs = prep_missing_ms;
    pt.MeshEmergePrepUnfinishedMs = prep_unfinished_ms;
    pt.MeshEmergePrepStickyMs = prep_sticky_ms;
    pt.MeshEmergePrepDropDirtyMs = prep_drop_dirty_ms;
    const double accounted = prep_missing_ms + prep_unfinished_ms +
                             prep_sticky_ms + prep_drop_dirty_ms;
    pt.MeshEmergePrepOtherMs =
        std::max(0.0, pt.MeshEmergePrepMs - accounted);
  }
  // Moving + dirty backlog: ensure minimum async feed rate so dirty queue
  // drains steadily instead of starving.
  if (moving && pending_dirty > 100 && pending_async < 8)
  {
    mesh_schedule = std::max(mesh_schedule, 6);
    mesh_drain = std::max(mesh_drain, 8);
  }
  // Targeted C/CB: mesh_dirty_tick_ms was ~1.0–1.2s median on edge — hard-cap
  // drain so emerge cannot burn the whole frame while holes stuck. Do NOT clamp
  // mesh_schedule here when holes (manual_1752 / arch_d2: async≈0 under Dirty).
  // FOV unfinished for schedule floors: live missing mesh only (not held
  // UnfinishedVisual telemetry — missing bump sticks 8 cruise frames and kept
  // schedule floors hot → mid wall≈40).
  const bool fov_unfinished =
      visual_holes || missing_underfeet || missing_visible_mesh;
  if (last_frame_ms > 100.0)
  {
    mesh_drain = (pending_dirty > 200) ? std::max(mesh_drain, 6) : 1;
    // Hitch: keep FirstMesh feed if FOV unfinished (manual_1940 async=0+holes).
    mesh_schedule =
        fov_unfinished ? std::max(mesh_schedule, 8)
                       : std::max(1, std::min(mesh_schedule, 4));
    sync_cap = 0;
  }
  else if (last_frame_ms > 40.0)
  {
    mesh_drain = std::min(mesh_drain, fov_unfinished ? 4 : 2);
    mesh_schedule =
        std::min(mesh_schedule, fov_unfinished ? (moving ? 10 : 8)
                                               : (moving ? 6 : 4));
    if (moving && !fov_unfinished)
    {
      sync_cap = 0;
    }
  }
  // Player dig/place: restore a tiny moving Immediate after cruise/hitch clamps
  // so side-wall edits are not SoftDefer-stuck invisible (manual 000459).
  if (world.GetPlayerRelightMeshBurstFrames() > 0)
  {
    sync_cap = std::max(sync_cap, moving ? 1 : 2);
  }
  // F0: SyncRebuild off unless dig/edit burst. Hot-frame gating still left a
  // cool→hot hitch (prev do_move<20 opens sync_cap, SyncRebuild burns 100–300ms).
  if (world.GetPlayerRelightMeshBurstFrames() <= 0)
  {
    sync_cap = 0;
  }
  // TD-ARCH-027 final floor AFTER wall clamps — FOV unfinished never async-starve.
  if (fov_unfinished && pending_async < 8)
  {
    mesh_schedule = std::max(mesh_schedule, moving ? 12 : 16);
    mesh_drain = std::max(mesh_drain, moving ? 10 : 14);
  }
  else if (pending_dirty > 100 && pending_async < 8 && last_frame_ms <= 100.0)
  {
    mesh_schedule = std::max(mesh_schedule, moving ? 10 : 14);
    mesh_drain = std::max(mesh_drain, moving ? 8 : 12);
  }
  // Lit remesh wall clamp AFTER SoT floors (ARCH_D3): dirty≫0 && no missing/UV.
  // Soft enough to keep mesh_async_med_when_dirty≥4 — schedule=2 starved async
  // (era13_01 wall≈48 / async=2). Prefer snapshot clamp + mild schedule cap.
  if (!fov_unfinished && !missing_visible_mesh &&
      world.GetPhysicsTelemetry().UnfinishedVisual == 0 &&
      pending_dirty > 280 && last_frame_ms > 28.0)
  {
    mesh_schedule = std::min(mesh_schedule, moving ? 5 : 7);
    mesh_drain = std::min(mesh_drain, moving ? 10 : 14);
    mesh_service.SetMeshSnapshotBudgetMs(moving ? 1.5 : 2.0);
  }
  // Phase C snapshot budget when healed+backlog (schedule hard-cap → Admission).
  if (!fov_unfinished && !missing_visible_mesh &&
      world.GetPhysicsTelemetry().UnfinishedVisual == 0 &&
      pending_gpu_n >= 12 && last_frame_ms > 24.0)
  {
    mesh_service.SetMeshSnapshotBudgetMs(moving ? 1.0 : 1.5);
  }
  // Isolated missing column (manual 084551): FirstMesh every frame while any
  // focus missing — not only held UnfinishedVisual — and prefer admit over
  // remesh when Dirty is high on the rim.
  glm::ivec3 isolated_hole{};
  const bool found_nearest_missing = mesh_service.FindNearestMissingGreedyMesh(
      world.GetBlockWorld(), focus_ground_horiz, focus_radius, isolated_hole);
  const bool isolated_missing =
      visual_holes || missing_visible_mesh || missing_underfeet ||
      found_nearest_missing;
  if (isolated_missing)
  {
    if (pending_dirty > 200)
    {
      mesh_service.DropRemeshDirtyBeyondRadius(
          focus_ground_horiz, /*keep_h=*/2, /*keep_cy=*/-1,
          /*remesh_only=*/true);
    }
    // Admit floor before remesh — Finalize MeshWorkAdmission caps schedule.
    mesh_schedule = std::max(mesh_schedule, moving ? 12 : 16);
    auto &exec = GetColumnFlowExecutor();
    const MeshWorkAdmission &adm = mesh_service.GetMeshWorkAdmission();
    // J2/K1/L1: under HoleDrain backlog, rarer full-focus scan saves wall.
    // DrainBudget cut ONLY when pending_async≥12 (workers already feed ring).
    // Cutting Drain on pending_gpu alone drifted rim mh 2–3→4–5 (manual 192715).
    const bool hole_backlog_mode =
        adm.mode == MeshWorkAdmission::Mode::HoleDrain ||
        adm.mode == MeshWorkAdmission::Mode::DeepBacklog;
    const bool backlog_hole_drain =
        hole_backlog_mode &&
        (pending_async >= 12 || pending_gpu_n >= 12);
    int nearest_miss_nh = -1;
    if (found_nearest_missing)
    {
      nearest_miss_nh = std::max(
          std::abs(isolated_hole.x - focus_ground_horiz.x),
          std::abs(isolated_hole.z - focus_ground_horiz.z));
      const glm::ivec2 hole_col(isolated_hole.x, isolated_hole.z);
      // N0c: Admit skips Pending/InFlight — promote Relight instead of no-op
      // FirstMesh enqueue (far-rim sticky mh=5 under dual backlog, 215629).
      const bool nearest_in_pipeline =
          mesh_service.IsPendingGpuApply(isolated_hole) ||
          mesh_service.HasInflightMeshBuild(isolated_hole) ||
          mesh_service.IsGpuExtractInFlight(isolated_hole);
      if (nearest_in_pipeline)
      {
        exec.RequestPromoteRelight(hole_col, /*priority=*/55);
      }
      else
      {
        ColumnWorkItem hole{};
        hole.column = hole_col;
        hole.kind = ColumnWorkKind::FirstMesh;
        hole.priority = 100;
        hole.scan_full_focus = false;
        hole.cy = isolated_hole.y;
        exec.Enqueue(hole);
      }
    }
    // N0b: never cut Drain when known far miss (nh≥4); async≥12 alone still cuts
    // only for closer rim (K1 pending_gpu cut remains forbidden).
    const bool drain_cut = hole_backlog_mode && pending_async >= 12 &&
                           !(nearest_miss_nh >= 4);
    // Full-focus scan every frame only in Normal; under backlog every 3f
    // (every 5f when GPU/async backlog — scan-only cut).
    // M1: rim plateau (nh 2–3) under HoleDrain/Deep — skip most full-focus
    // scans (nearest FirstMesh + Drain still run). Never skip underfeet
    // (nh≤1). Force a full-focus every 8 skipped frames so other ring holes
    // cannot starve forever (land-south miss_end).
    // N0a: far rim nh≥4 — full-scan every frame (ignore FocusScanCd / skip).
    const bool far_rim_force_scan =
        hole_backlog_mode && nearest_miss_nh >= 4;
    const bool rim_plateau_close =
        !far_rim_force_scan && hole_backlog_mode && found_nearest_missing &&
        nearest_miss_nh >= 2 && nearest_miss_nh <= 3;
    bool skip_full_scan_rim_close = false;
    if (rim_plateau_close)
    {
      ++RimScanSkipStreak;
      if (RimScanSkipStreak < 8)
      {
        skip_full_scan_rim_close = true;
      }
      else
      {
        RimScanSkipStreak = 0;
      }
    }
    else
    {
      RimScanSkipStreak = 0;
    }
    const bool full_scan =
        far_rim_force_scan ||
        (!skip_full_scan_rim_close &&
         (adm.mode == MeshWorkAdmission::Mode::Normal || FocusScanCd <= 0));
    if (full_scan)
    {
      ColumnWorkItem focus_scan{};
      focus_scan.column =
          glm::ivec2(focus_ground_horiz.x, focus_ground_horiz.z);
      focus_scan.kind = ColumnWorkKind::FirstMesh;
      focus_scan.priority = 95;
      focus_scan.scan_full_focus = true;
      focus_scan.cy = -1;
      exec.Enqueue(focus_scan);
      FocusScanCd = (far_rim_force_scan ||
                       adm.mode == MeshWorkAdmission::Mode::Normal)
                          ? 0
                          : (backlog_hole_drain ? 4 : 2);
    }
    else if (FocusScanCd > 0)
    {
      --FocusScanCd;
    }
    const int admit_n = std::max(1, adm.admit_batch);
    int drain_steps =
        drain_cut ? (moving ? 2 : 3) : (moving ? 3 : 4);
    // L2: nearest already at rim≥3 — never starve ColumnFlow FirstMesh drain.
    if (hole_backlog_mode && nearest_miss_nh >= 3)
    {
      drain_steps = std::max(drain_steps, moving ? 3 : 4);
    }
    exec.DrainBudget(world, drain_steps, focus_ground_horiz, focus_radius,
                     /*admit_batch=*/admit_n);
    // Idle sticky: force Immediate on nearest hole within underfeet when it is
    // not already in the mesh pipeline (HasMissing skips Pending/InFlight).
    if (!moving && found_nearest_missing)
    {
      const int nh = std::max(
          std::abs(isolated_hole.x - focus_ground_horiz.x),
          std::abs(isolated_hole.z - focus_ground_horiz.z));
      if (nh <= 1 && underfeet_immediate_cd <= 0 &&
          underfeet_immediate_this_frame < kMaxUnderfeetImmediate &&
          !mesh_service.HasMeshSatisfyingColumnReady(isolated_hole) &&
          !mesh_service.IsPendingGpuApply(isolated_hole) &&
          !mesh_service.HasInflightMeshBuild(isolated_hole) &&
          immediate_ms_used() < 8.0)
      {
        mesh_service.RebuildChunkImmediate(world.GetBlockWorld(), registry,
                                           isolated_hole);
        ++underfeet_immediate_this_frame;
        underfeet_immediate_cd = 1;
      }
    }
    // G1/H/I/J3: sticky nearest-hole Immediate — nh≤5. Count while pipeline
    // idle, Queued, or Kicked/Dispatched (J3: track kicked rim without Kick
    // drop). Escape drop+Imm remains Queued-only; kicked gets Kick prefer.
    if (found_nearest_missing)
    {
      const bool no_drawable =
          !mesh_service.HasDrawableGreedyMesh(isolated_hole);
      const bool no_inflight =
          !mesh_service.HasInflightMeshBuild(isolated_hole);
      const bool queued_stuck =
          mesh_service.IsPendingGpuQueued(isolated_hole);
      const bool kicked_stuck =
          mesh_service.IsPendingGpuKickedOrDispatched(isolated_hole);
      const bool pipeline_idle =
          no_drawable && !mesh_service.IsPendingGpuApply(isolated_hole) &&
          no_inflight;
      const bool sticky_alive =
          no_drawable && no_inflight &&
          (pipeline_idle || queued_stuck || kicked_stuck);
      mesh_service.UpdateStickyNearestHole(isolated_hole, sticky_alive);
      const int nh = std::max(
          std::abs(isolated_hole.x - focus_ground_horiz.x),
          std::abs(isolated_hole.z - focus_ground_horiz.z));
      const int sticky_frames = mesh_service.GetStickyNearestHoleFrames();
      const bool ms_ok = immediate_ms_used() < (moving ? 6.0 : 8.0);
      const bool slot_ok =
          underfeet_immediate_this_frame < kMaxUnderfeetImmediate;
      if (nh <= 5 && slot_ok && ms_ok)
      {
        if (pipeline_idle && sticky_frames >= 3)
        {
          mesh_service.RebuildChunkImmediate(world.GetBlockWorld(), registry,
                                             isolated_hole);
          ++underfeet_immediate_this_frame;
          underfeet_immediate_cd = 1;
          mesh_service.UpdateStickyNearestHole(isolated_hole, false);
        }
        else if (queued_stuck && sticky_frames >= 5 &&
                 mesh_service.DropQueuedPendingGpuApply(isolated_hole))
        {
          mesh_service.RebuildChunkImmediate(world.GetBlockWorld(), registry,
                                             isolated_hole);
          ++underfeet_immediate_this_frame;
          underfeet_immediate_cd = 1;
          mesh_service.UpdateStickyNearestHole(isolated_hole, false);
        }
      }
      // J3: nh≤3 sticky≥5 in GPU pipeline — hoist Queued Kick priority only
      // (never drop/Immediate on Kicked/Dispatched).
      if (nh <= 3 && sticky_frames >= 5 && (queued_stuck || kicked_stuck))
      {
        mesh_service.PreferKickPendingGpuQueued(isolated_hole);
      }
    }
    else
    {
      mesh_service.UpdateStickyNearestHole(glm::ivec3(0), false);
    }
  }
  else if (fov_unfinished)
  {
    auto &exec = GetColumnFlowExecutor();
    ColumnWorkItem focus_scan{};
    focus_scan.column =
        glm::ivec2(focus_ground_horiz.x, focus_ground_horiz.z);
    focus_scan.kind = ColumnWorkKind::FirstMesh;
    focus_scan.priority = 90;
    focus_scan.scan_full_focus = true;
    focus_scan.cy = -1;
    exec.Enqueue(focus_scan);
    exec.DrainBudget(world, moving ? 2 : 3, focus_ground_horiz, focus_radius,
                     /*admit_batch=*/moving ? 2 : 3);
  }
  // P3: soft cruise clamp — underfeet (or nh<=1 ahead under HoleDrain/Deep).
  // Applied next movement tick via PhysicsTelemetry.StreamSpeedClampScale.
  // Scale 0.85: land-south keeps underfeet≈1 all cruise; ×0.6–0.7 starved
  // miss_end (chunks/miss_stuck regress vs P2). Soft integrity, not hard brake.
  {
    float clamp_scale = 1.0f;
    if (moving)
    {
      if (missing_underfeet)
      {
        clamp_scale = 0.85f;
      }
      else if (found_nearest_missing)
      {
        const MeshWorkAdmission &adm_now =
            mesh_service.GetMeshWorkAdmission();
        const bool hole_mode =
            adm_now.mode == MeshWorkAdmission::Mode::HoleDrain ||
            adm_now.mode == MeshWorkAdmission::Mode::DeepBacklog;
        const int nh = std::max(
            std::abs(isolated_hole.x - focus_ground_horiz.x),
            std::abs(isolated_hole.z - focus_ground_horiz.z));
        if (hole_mode && nh <= 1)
        {
          const glm::vec2 vel = world.GetLastMovementDirXz();
          const glm::vec2 to_hole(
              static_cast<float>(isolated_hole.x - focus_ground_horiz.x),
              static_cast<float>(isolated_hole.z - focus_ground_horiz.z));
          const float vel_len = glm::length(vel);
          const float hole_len = glm::length(to_hole);
          if (vel_len > 0.01f && hole_len > 0.01f &&
              glm::dot(vel / vel_len, to_hole / hole_len) > 0.5f)
          {
            clamp_scale = 0.85f;
          }
        }
      }
    }
    world.GetPhysicsTelemetryMutable().StreamSpeedClampScale = clamp_scale;
  }
  {
    const auto calm_cap = EvaluateIdleMeshDrainCap(IdleMeshDrainCapInput{
        moving, missing_visible_mesh, pending_focus_count, black_sticky,
        not_ready_early, last_frame_ms, mesh_drain, mesh_schedule});
    if (calm_cap.active)
    {
      mesh_drain = calm_cap.mesh_drain;
      mesh_schedule = calm_cap.mesh_schedule;
      if (calm_cap.snapshot_budget_ms > 0.0)
      {
        mesh_service.SetMeshSnapshotBudgetMs(calm_cap.snapshot_budget_ms);
      }
      if (calm_cap.emerge_total_budget_ms > 0.0)
      {
        mesh_service.SetMeshEmergeTotalBudgetMs(calm_cap.emerge_total_budget_ms);
      }
      sync_cap = std::min(sync_cap < 0 ? 0 : sync_cap, calm_cap.sync_cap);
      sync_budget_ms = std::min(sync_budget_ms, calm_cap.sync_budget_ms);
    }
  }
  // F0: drain-first — Finish/Kick before Finalize so admission sees post-consume
  // pending (avoids mode=0/sch=12 thrash while telem pending stays high).
  int gpu_consume_done = 0;
  {
    const MeshWorkAdmission &early_adm = mesh_service.GetMeshWorkAdmission();
    const int consume_drain = FinalizeDrain(mesh_drain, early_adm);
    const int consume_gpu =
        std::max(early_adm.gpu_apply_max,
                 std::max(3, std::max(mesh_drain, mesh_schedule)));
    const double consume_budget =
        std::max(6.0, mesh_service.GetMeshEmergeTotalBudgetMs() *
                          early_adm.gpu_budget_frac);
    gpu_consume_done = mesh_service.ConsumeGpuApplyBacklog(
        world.GetBlockWorld(), registry, consume_drain, consume_gpu,
        consume_budget);
  }
  // E1/F0: Finalize on post-drain pending; floors above only propose.
  {
    pending_gpu_n = mesh_service.GetPendingGpuAppliesCount();
    MeshWorkAdmissionInput ain{};
    ain.pending_gpu = pending_gpu_n;
    ain.pending_gpu_queued = mesh_service.GetPendingGpuQueuedCount();
    ain.pending_gpu_kicked = mesh_service.GetPendingGpuKickedCount();
    ain.visual_holes = visual_holes || missing_visible_mesh ||
                       world.GetPhysicsTelemetry().FocusMissingMesh > 0;
    ain.missing_underfeet = missing_underfeet;
    ain.moving = moving;
    ain.pending_light_near = pending_focus_count;
    ain.unfinished_visual = world.GetPhysicsTelemetry().UnfinishedVisual;
    ain.prev_mode = static_cast<uint8_t>(LastBudget.AdmissionMode);
    ain.ring_depth = UGpuMeshPipeline::kReadbackRing;
    // K3: rim mh for remesh band when pending cooled.
    if (found_nearest_missing)
    {
      ain.nearest_miss_horiz = std::max(
          std::abs(isolated_hole.x - focus_ground_horiz.x),
          std::abs(isolated_hole.z - focus_ground_horiz.z));
    }
    else
    {
      ain.nearest_miss_horiz = world.GetPhysicsTelemetry().MissHoriz;
    }
    const MeshWorkAdmission adm = ComputeMeshWorkAdmission(ain);
    mesh_service.SetMeshWorkAdmission(adm);
    mesh_schedule = FinalizeSchedule(mesh_schedule, adm);
    mesh_drain = FinalizeDrain(mesh_drain, adm);
    mesh_service.SetStarveRemeshKeepHoriz(adm.starve_remesh_horiz);
    if ((visual_holes || missing_underfeet || missing_visible_mesh) &&
        adm.mode != MeshWorkAdmission::Mode::Normal)
    {
      mesh_service.SetStarveRemeshForHoles(true);
    }
    if (adm.promote_relight > 0)
    {
      auto &exec = GetColumnFlowExecutor();
      exec.RequestPromoteRelight(
          glm::ivec2(focus_ground_horiz.x, focus_ground_horiz.z), 95);
      // Async FIFO only — never sync RelightTerrainColumn on moving hot path.
      exec.DrainIdlePendingLight(world, focus_ground_horiz, focus_radius,
                                 adm.promote_relight, /*allow_sync=*/false,
                                 last_frame_ms, pending_focus_count,
                                 missing_visible_mesh);
    }
    if (adm.mode == MeshWorkAdmission::Mode::HoleDrain ||
        adm.mode == MeshWorkAdmission::Mode::DeepBacklog)
    {
      mesh_service.SetMaxOutsideFocusMeshPerFrame(0);
      // F3: prune remesh Dirty flood every HoleDrain frame (keep_h=1).
      if (pending_dirty > 200 &&
          (visual_holes || missing_visible_mesh || missing_underfeet))
      {
        mesh_service.DropRemeshDirtyBeyondRadius(
            focus_ground_horiz, /*keep_h=*/1, /*keep_cy=*/-1,
            /*remesh_only=*/true);
      }
      // J2/K1: same frame as heavy GPU drain + healthy async/GPU backlog —
      // clamp SoftDeferHeld requeue so Rebuild does not fight Finish/Kick.
      if ((pending_async >= 12 || pending_gpu_n >= 12) &&
          gpu_consume_done > 0)
      {
        MeshWorkAdmission cut = adm;
        cut.softdefer_requeue = std::min(cut.softdefer_requeue, 1);
        mesh_service.SetMeshWorkAdmission(cut);
      }
    }
    LastBudget.MaxMeshSchedule = mesh_schedule;
    LastBudget.MaxMeshDrain = mesh_drain;
    LastBudget.AdmissionMode = static_cast<int>(adm.mode);
    LastBudget.DirtyAdmitBudget = adm.dirty_admit_budget;
    LastBudget.GpuApplyMax = adm.gpu_apply_max;
  }
  // I4b: re-assert calm dirty_tick caps after Finalize — admission floors must
  // not restore drain/schedule/emerge budget on clean stand.
  {
    const auto calm_cap = EvaluateIdleMeshDrainCap(IdleMeshDrainCapInput{
        moving, missing_visible_mesh, pending_focus_count, black_sticky,
        not_ready_early, last_frame_ms, mesh_drain, mesh_schedule});
    if (calm_cap.active)
    {
      mesh_drain = calm_cap.mesh_drain;
      mesh_schedule = calm_cap.mesh_schedule;
      LastBudget.MaxMeshSchedule = mesh_schedule;
      LastBudget.MaxMeshDrain = mesh_drain;
      if (calm_cap.snapshot_budget_ms > 0.0)
      {
        mesh_service.SetMeshSnapshotBudgetMs(calm_cap.snapshot_budget_ms);
      }
      if (calm_cap.emerge_total_budget_ms > 0.0)
      {
        mesh_service.SetMeshEmergeTotalBudgetMs(calm_cap.emerge_total_budget_ms);
      }
      sync_cap = std::min(sync_cap < 0 ? 0 : sync_cap, calm_cap.sync_cap);
      sync_budget_ms = std::min(sync_budget_ms, calm_cap.sync_budget_ms);
    }
  }
  // F0: SyncRebuild always off in TickMeshEmerge. Dig/edit uses
  // RebuildChunkImmediate (PlayerRelightMeshBurst); SyncRebuild was still
  // burning 100–200ms whenever burst frames were non-zero on cruise.
  sync_cap = 0;
  MeshRebuildTickStats tick_stats{};
  {
    const MeshWorkAdmission &adm = mesh_service.GetMeshWorkAdmission();
    const int post_drain = std::max(mesh_drain, adm.max_drain);
    tick_stats = mesh_service.RebuildDirtyChunksWithStats(
        world.GetBlockWorld(), registry, mesh_drain, mesh_schedule,
        /*force_sync=*/false, /*max_sync_rebuild=*/0, sync_budget_ms,
        /*skip_gpu_consume=*/true);
    tick_stats.Completed += gpu_consume_done;
    mesh_service.DrainAsyncMeshResults(world.GetBlockWorld(), registry,
                                       post_drain);
    if (tick_stats.Completed > 0 || tick_stats.SyncRebuilt > 0 ||
        (!moving && pending_focus_count > 0))
    {
      world.DrainFocusVisualWork(focus_ground_horiz, focus_radius,
                                 idle_recovery ? 24 : 12);
    }
  }
  // After Apply/RemeshAfterApply: prune again so next frame's FocusDirtyChunks
  // (counted at UpdateStreaming start) sees the eye-shell residual, not the
  // full-focus remesh stack.
  if (idle_focus_dirty_debt)
  {
    mesh_service.DropRemeshDirtyBeyondRadius(focus_dirty_keep, /*keep_h=*/1,
                                            /*keep_cy=*/2);
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
