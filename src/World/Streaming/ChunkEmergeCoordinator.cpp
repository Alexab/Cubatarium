#include "World/Streaming/ChunkEmergeCoordinator.h"
#include "World/Streaming/ColumnFlowScheduler.h"
#include "World/Streaming/ColumnFlowExecutor.h"
#include "World/Streaming/ColumnRenderablePolicy.h"
#include "World/Streaming/FocusIngressPolicy.h"
#include "World/Streaming/IdleRecoveryPolicy.h"
#include "World/Streaming/InputFirstPolicy.h"
#include "World/Streaming/MeshLitGate.h"
#include "World/Streaming/MeshWorkAdmission.h"
#include "World/Streaming/SoftDeferEmptyPolicy.h"
#include "World/Streaming/AntiFlickerPolicy.h"
#include "World/Streaming/VisualStagePolicy.h"
#include "World/Streaming/WorldBorderPolicy.h"
#include "World/Streaming/OceanFrontierPolicy.h"
#include "World/Streaming/OceanCruisePolicy.h"
#include "World/Streaming/RelightFifoPolicy.h"
#include "World/Streaming/CyOrderPolicy.h"
#include "World/Streaming/EnterVisualWarmupPolicy.h"
#include "World/Streaming/NearFovWorkPriority.h"
#include "Blocks/BlockRegistry.h"
#include "Render/Camera/Camera.h"
#include "Render/Mesh/GpuMeshPipeline.h"
#include "World/Chunks/ChunkManager.h"
#include "World/Core/BlockWorld.h"
#include "World/Core/RuntimeTuning.h"
#include "World/Streaming/FrameStreamingBudget.h"
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
#include <unordered_set>
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
  // Era20: shrink vs Era19×8/128 (enter app_update≈2s), but keep enough to
  // seed r≤2 before InGame so land miss doesn't stick from empty spawn ring.
  const int mesh =
      std::min(64, std::max(coop_budget * 4, 16));
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
  GetColumnFlowExecutor().BeginFrame();
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
  // Cruise SOTA: early ColumnFlow sites only Enqueue; one DrainBudget at end.
  int column_flow_drain_n = 0;
  int column_flow_admit_batch = 1;
  int idle_seam_budget_this_frame = 0;
  auto note_column_flow_drain = [&](int drain_n, int admit_batch)
  {
    column_flow_drain_n = std::max(column_flow_drain_n, drain_n);
    column_flow_admit_batch = std::max(column_flow_admit_batch, admit_batch);
  };
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
  // Era22 I-M8: track miss witness age (~120 frames ≈ 1 period ≈2s).
  if (missing_visible_mesh)
  {
    MissWitnessAgeFrames =
        std::min(MissWitnessAgeFrames + 1, 1000000);
  }
  else
  {
    MissWitnessAgeFrames = 0;
    MissStuckSelfHealPeriod = 0;
    MissStuckForcePinPeriod = 0;
  }
  glm::ivec3 nearest_missing_hole{};
  const bool have_nearest_missing =
      missing_visible_mesh &&
      mesh_service.FindNearestMissingGreedyMesh(
          world.GetBlockWorld(), focus_ground_horiz, focus_radius,
          nearest_missing_hole);
  prep_missing_ms = prep_ms_since(prep_t);
  prep_t = std::chrono::high_resolution_clock::now();
  double prep_pending_light_ms = 0.0;
  double prep_softdefer_setup_ms = 0.0;
  double prep_dirty_count_ms = 0.0;
  double prep_black_sticky_ms = 0.0;
  const auto &ring_sample = world.GetFocusRingVisualSample();
  const bool ring_sample_ok =
      ring_sample.valid &&
      ring_sample.frame_epoch == world.GetStreamingFrameEpoch();
  bool pending_near_light = false;
  int pending_focus_count = 0;
  if (ring_sample_ok)
  {
    pending_focus_count = ring_sample.pending_light;
    pending_near_light = pending_focus_count > 0;
    prep_pending_light_ms = prep_ms_since(prep_t);
  }
  else
  {
    pending_near_light =
        world.HasPendingLightBeforeMeshNear(focus_ground_horiz, focus_radius);
    pending_focus_count =
        world.CountPendingLightBeforeMeshNear(focus_ground_horiz, focus_radius);
  prep_pending_light_ms = prep_ms_since(prep_t);
  }
  // Resolve enter-warmup outside SoftDefer setup timer — IsCreateSpawnWarmupSettled
  // can O(FOV) scan until latched (was ~33–55ms mislabeled as softdefer_setup).
  const bool enter_warmup_active = !world.IsCreateSpawnWarmupSettled();
  prep_t = std::chrono::high_resolution_clock::now();
  const int unlit_near_count = world.GetPhysicsTelemetry().FocusDarkMesh;
  // Stable SoftDefer policy: POD update each frame; Set*Fn once (not every frame).
  SoftDeferPolicy.world = &world;
  SoftDeferPolicy.focus_ground = focus_ground_horiz;
  SoftDeferPolicy.focus_radius = focus_radius;
  SoftDeferPolicy.have_nearest_missing = have_nearest_missing;
  SoftDeferPolicy.nearest_missing_hole = nearest_missing_hole;
  SoftDeferPolicy.missing_visible_mesh = missing_visible_mesh;
  SoftDeferPolicy.pending_focus_count = pending_focus_count;
  SoftDeferPolicy.unlit_near_count = unlit_near_count;
  if (!SoftDeferCallbacksInstalled)
  {
    mesh_service.SetDeferMeshUntilLitFn(
        [this](glm::ivec3 chunk_coord)
        {
          UWorld *world_ptr = SoftDeferPolicy.world;
          if (!world_ptr)
          {
            return false;
          }
          UWorld &world_ref = *world_ptr;
          UWorldMeshService &mesh_svc = world_ref.GetMeshService();
          const SoftDeferFramePolicy &pol = SoftDeferPolicy;
          const int horiz =
              std::max(std::abs(chunk_coord.x - pol.focus_ground.x),
                       std::abs(chunk_coord.z - pol.focus_ground.z));
          if (ShouldSkipSpawnMeshWhileRelightDeferred(
                  world_ref.IsLightingRelightDeferred(), horiz))
          {
            return true;
          }
          if (EnterLitQuiesceLiftSpawnSoftDefer(
                  world_ref.IsEnterLitQuiesceLatched(), horiz))
          {
            return false;
          }
          const bool underfeet = horiz <= 1;
          const bool pending = world_ref.IsPendingLightBeforeMesh(
              glm::ivec2(chunk_coord.x, chunk_coord.z));
          const bool is_nearest_hole =
              pol.have_nearest_missing &&
              chunk_coord.x == pol.nearest_missing_hole.x &&
              chunk_coord.z == pol.nearest_missing_hole.z;
          const bool has_mesh = mesh_svc.HasDrawableGreedyMesh(chunk_coord);
          const bool has_greedy = mesh_svc.HasGreedyMesh(chunk_coord);
          const bool in_focus = horiz <= pol.focus_radius;
          const glm::ivec3 ground(chunk_coord.x, 0, chunk_coord.z);
          const bool may_mesh =
              world_ref.MayMeshColumn(ground, /*underfeet_preview=*/false);
          const bool fully_dark =
              has_greedy &&
              mesh_svc.GetCache().ChunkHasFullyDarkFace(chunk_coord);
          const bool starve_hinterland = StarveHinterlandUnlit(
              world_ref.GetPhysicsTelemetry().SoftDeferEmptyNearN,
              pol.pending_focus_count);
          const bool allow_unlit =
              !starve_hinterland &&
              AllowUnlitFirstMesh(has_mesh, horiz, is_nearest_hole, in_focus,
                                  kVisualStageLitDrawableHoriz);
          const bool allow_unlit_hole = AllowUnlitDrawableUnderLightDebt(
              pol.pending_focus_count, pol.unlit_near_count, horiz, fully_dark,
              has_greedy, underfeet);
          (void)pol.missing_visible_mesh;
          return SoftDeferMeshUntilLitPolicy(
              underfeet, has_mesh || has_greedy,
              world_ref.RequiresLightingLitGate() && pending, in_focus, may_mesh,
              allow_unlit, allow_unlit_hole);
        });
    mesh_service.SetOnLitPendingNeededFn(
        [this](glm::ivec3 chunk_coord)
        {
          UWorld *world_ptr = SoftDeferPolicy.world;
          if (!world_ptr)
          {
            return;
          }
          UWorld &world_ref = *world_ptr;
          const glm::ivec2 key(chunk_coord.x, chunk_coord.z);
          // ColPipe P5: drawable remesh is MarkRelit/Dirty — not RemeshSeam+sticky.
          if (world_ref.GetMeshService().HasDrawableGreedyMesh(chunk_coord) ||
              world_ref.GetMeshService().IsChunkMeshDirty(chunk_coord) ||
              world_ref.GetMeshService().IsRemeshAfterApplyPending(chunk_coord) ||
              world_ref.GetMeshService().IsPendingGpuApply(chunk_coord) ||
              world_ref.GetMeshService().HasInflightMeshBuild(chunk_coord))
          {
            return;
          }
          if (!world_ref.IsPendingLightBeforeMesh(key))
          {
            world_ref.NotePendingLightBeforeMesh(
                glm::ivec3(key.x, 0, key.y), 0,
                world_ref.GetProceduralSettings().MaxHeight);
          }
          GetColumnFlowExecutor().Enqueue(key, ColumnWorkKind::RelightThenMesh,
                                          /*priority=*/70);
        });
    mesh_service.SetOnSoftDeferHeldFn(
        [this](glm::ivec3 chunk_coord)
        {
          UWorld *world_ptr = SoftDeferPolicy.world;
          if (!world_ptr)
          {
            return;
          }
          UWorld &world_ref = *world_ptr;
          const SoftDeferFramePolicy &pol = SoftDeferPolicy;
          const int horiz =
              std::max(std::abs(chunk_coord.x - pol.focus_ground.x),
                       std::abs(chunk_coord.z - pol.focus_ground.z));
          if (ShouldSkipSpawnMeshWhileRelightDeferred(
                  world_ref.IsLightingRelightDeferred(), horiz))
          {
            return;
          }
          ColumnWorkItem item{};
          item.column = glm::ivec2(chunk_coord.x, chunk_coord.z);
          item.kind = ColumnWorkKind::FirstMesh;
          item.priority = 100;
          item.cy = chunk_coord.y;
          GetColumnFlowExecutor().Enqueue(item);
        });
    mesh_service.SetOnLitDrawableCommittedFn(
        [this](glm::ivec3 chunk_coord)
        {
          UWorld *world_ptr = SoftDeferPolicy.world;
          if (!world_ptr)
          {
            return;
          }
          UWorld &world_ref = *world_ptr;
          const glm::ivec2 col(chunk_coord.x, chunk_coord.z);
          world_ref.ClearStickyRemeshAfterLightColumn(col);
          world_ref.NoteUnfinishedColumnDirty(col);
          if (!world_ref.IsPendingLightBeforeMesh(col))
          {
            const glm::ivec3 ground(col.x, 0, col.y);
            if (world_ref.IsColumnLitReady(ground) ||
                world_ref.GetColumnEmergeState(ground) ==
                    ColumnEmergeState::Meshing ||
                world_ref.GetColumnEmergeState(ground) ==
                    ColumnEmergeState::LitReady)
            {
              world_ref.SetColumnEmergeState(ground,
                                             ColumnEmergeState::RenderReady);
            }
          }
        });
    mesh_service.SetOnMeshColumnDirtyFn(
        [this](glm::ivec3 chunk_coord)
        {
          UWorld *world_ptr = SoftDeferPolicy.world;
          if (!world_ptr)
          {
            return;
          }
          world_ptr->NoteUnfinishedColumnDirty(
              glm::ivec2(chunk_coord.x, chunk_coord.z));
        });
    mesh_service.SetColumnFlowContainsFn(
        [this](glm::ivec2 col)
        { return GetColumnFlowExecutor().HasRepairTicket(col); });
    SoftDeferCallbacksInstalled = true;
  }
  prep_softdefer_setup_ms = prep_ms_since(prep_t);
  prep_t = std::chrono::high_resolution_clock::now();

  const size_t pending_dirty_early = mesh_service.GetDirtyCount();
  const int pending_async_early = mesh_service.GetAsyncInFlightCount();
  // Phase 1a: never second O(R²) CountUnfinished here — reuse Streaming sample.
  bool sample_valid = false;
  const int not_ready_early =
      world.GetLastUnfinishedVisualSample(&sample_valid);
  int focus_dirty_early = 0;
  if (ring_sample_ok)
  {
    focus_dirty_early = ring_sample.dirty_n;
    prep_dirty_count_ms = prep_ms_since(prep_t);
  }
  else
  {
    focus_dirty_early =
        mesh_service.CountDirtyWithinHorizontalRadius(focus_ground_horiz,
                                                      focus_radius);
    prep_dirty_count_ms = prep_ms_since(prep_t);
  }
  prep_t = std::chrono::high_resolution_clock::now();
  const bool near_mesh_backlog =
      focus_dirty_early > 0 || missing_visible_mesh;
  (void)pending_near_light;
  int black_sticky = 0;
  if (ring_sample_ok)
  {
    black_sticky = ring_sample.black_sticky;
    prep_black_sticky_ms = prep_ms_since(prep_t);
  }
  else
  {
    black_sticky =
        world.CountBlackStickyFocusMeshes(focus_ground, focus_radius);
    prep_black_sticky_ms = prep_ms_since(prep_t);
  }
  prep_unfinished_ms = prep_pending_light_ms + prep_softdefer_setup_ms +
                       prep_dirty_count_ms + prep_black_sticky_ms;
  prep_t = std::chrono::high_resolution_clock::now();
  // Lit-but-dirty catch-up: only when focus still has *missing* mesh pressure.
  // Remesh-of-existing (fd high, nr from Dirty/Active) must not latch forever —
  // IsColumnRenderReady no longer counts Dirty; keep debt off for remesh-only.
  const bool idle_remesh_debt =
      !moving && pending_focus_count == 0 && black_sticky == 0 &&
      !missing_visible_mesh && sample_valid && not_ready_early > 32;
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
        mesh_service.CancelInFlightOutsideHorizontalRadius(
            focus_ground_horiz, focus_radius, /*keep_horiz_lease=*/1);
      }
      // Never CancelAsyncInFlightKeepDirty during lit-but-dirty catch-up —
      // that reset async every ~30f and froze focus_dirty≈420 / nr≈52.
      // Underfeet lease: keep Active band while canceling hinterland thrash.
      if (async_saturated_idle && pending_async_early >= 40 &&
          !idle_remesh_debt && !idle_focus_dirty_debt &&
          pending_focus_count > 0)
      {
        mesh_service.CancelAsyncInFlightKeepDirty(focus_ground_horiz,
                                                  /*keep_horiz_lease=*/1);
      }
      {
        auto &exec = GetColumnFlowExecutor();
        exec.RequestPromoteRelight(
            glm::ivec2(focus_ground_horiz.x, focus_ground_horiz.z), 40);
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
        note_column_flow_drain(4, admit_n);
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
    static int sticky_remesh_drain_cd = 0;
    if (sticky_remesh_drain_cd > 0)
    {
      --sticky_remesh_drain_cd;
    }
    const auto sticky_drain = EvaluateStickyRemeshDrain(StickyRemeshDrainInput{
        black_sticky, last_frame_ms, moving,
        /*frames_since_last_drain=*/
        sticky_remesh_drain_cd > 0
            ? 0
            : 999});
    if (sticky_drain.run_drain)
    {
      // ColPipe P1: no RemeshSeam focus proxy — DrainRemeshSeamBudget/SyncIdle
      // drains the sticky set; FirstMesh only if feet column has no mesh owner.
      note_column_flow_drain(std::max(1, sticky_drain.budget), 1);
      sticky_remesh_drain_cd = moving ? 0 : 30;
      if (!moving)
      {
        idle_seam_budget_this_frame =
            std::max(idle_seam_budget_this_frame, sticky_drain.budget);
      }
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
  // Era14: also drop pin sticky when stale-near already 0 (gate black_sticky=1
  // with stop_dark_face_stale_near_end=0 — remesh ticket drained, set leftover).
  if (!moving && !missing_visible_mesh && black_sticky > 0)
  {
    const int stale_near = world.GetPhysicsTelemetry().DarkFaceStaleNearN;
    const double since_motion = world.GetTimeSinceMotionSec();
    if ((pending_focus_count == 0 && since_motion > 1.5) ||
        (stale_near == 0 && since_motion > 1.0 && black_sticky <= 2))
    {
      world.PruneStickyRemeshOutside(focus_ground_horiz, /*radius=*/0);
      world.ClearPendingLightAfterMeshCommitted(32);
    }
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
      have_nearest_missing &&
      std::max(std::abs(nearest_missing_hole.x - focus_ground_horiz.x),
               std::abs(nearest_missing_hole.z - focus_ground_horiz.z)) <= 1;
  const int nearest_miss_h =
      have_nearest_missing
          ? std::max(std::abs(nearest_missing_hole.x - focus_ground_horiz.x),
                     std::abs(nearest_missing_hole.z - focus_ground_horiz.z))
          : 99;
  const bool near_miss_urgent = missing_underfeet || nearest_miss_h <= 2;
  // B3: do not re-scan HasMissing(r=1) — nearest witness already covers underfeet.
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
    else if (!procedural.FillWater)
    {
      // Era33 P1: SoftDefer/empty scan prefers ground band over canopy cy.
      prefer_lower_cy = true;
      preferred_cy = std::min(preferred_cy, focus_ground.y);
    }
  }

  // SoftDefer empty / Hide⇒Ticket: FirstMesh-until-Drawable ownership + age SLA.
  // Era38 A1: collect → NearFovWorkScore sort → near reserve → rim-only offset.
  bool underfeet_undrawn = false;
  auto &phys_telem = world.GetPhysicsTelemetryMutable();
  const int prev_softdefer_empty = phys_telem.SoftDeferEmptyPlaceholderN;
  phys_telem.SoftDeferEmptyPlaceholderN = 0;
  phys_telem.SoftDeferEmptyStuckN = 0;
  phys_telem.SoftDeferEmptyStuckDefer = 0;
  phys_telem.SoftDeferEmptyAgeMaxFrames = 0;
  phys_telem.SoftDeferEmptyOwnedN = 0;
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
    // Rim-only miss: scan ground band, not cy=0..top (152933: scan spikes 41ms).
    int cy0 = (missing_visible_mesh && near_miss_urgent)
                  ? 0
                  : std::max(0, preferred_cy - 1);
    const int cy1_base = std::min(max_cy, preferred_cy + 2);
    int cy1 = cy1_base;
    if (procedural.FillWater)
    {
      const int sea = procedural.SeaLevel;
      const int sea_cy0 =
          FloorDiv(std::max(0, sea - CHUNK_SIZE), CHUNK_SIZE);
      const int sea_cy1 = FloorDiv(
          std::min(procedural.MaxHeight, sea + CHUNK_SIZE), CHUNK_SIZE);
      cy0 = std::min(cy0, sea_cy0);
      cy1 = std::max(cy1, std::min(max_cy, sea_cy1));
    }
    const int kEmptyOwnershipCap = EnterWarmupSoftDeferOwnershipCap(
        CruiseCatchUpOwnershipCap(
            SoftDeferOwnershipCap(prev_softdefer_empty), prev_softdefer_empty,
            moving),
        prev_softdefer_empty, enter_warmup_active);
    glm::vec2 view_fwd = world.GetLastMovementDirXz();
    if (!moving || glm::length(view_fwd) < 0.01f)
    {
      if (const auto camera = world.GetCurrentUserCamera())
      {
        const glm::vec3 front = camera->GetFront();
        view_fwd = glm::vec2(front.x, front.z);
      }
    }
    const float view_flen = glm::length(view_fwd);
    auto view_dot_at = [&](int dx, int dz) -> float
    {
      if (view_flen < 0.01f)
      {
        return 0.0f;
      }
      const float clen =
          std::sqrt(static_cast<float>(dx * dx + dz * dz));
      if (clen < 0.01f)
      {
        return 1.0f;
      }
      return (static_cast<float>(dx) / clen) * (view_fwd.x / view_flen) +
             (static_cast<float>(dz) / clen) * (view_fwd.y / view_flen);
    };
    struct SoftDeferEmptyCand
    {
      glm::ivec3 coord{};
      int horiz{0};
      float view_dot{0.0f};
      bool empty_placeholder{false};
    };
    // Era39: always recount SoftDefer empty (full focus disk); ownership when Cd ready.
    std::vector<SoftDeferEmptyCand> cands;
    cands.reserve(64);
    std::unordered_set<glm::ivec3, IVec3Hash> seen_empty;
    auto &exec = GetColumnFlowExecutor();
    const int heal_r = std::max(1, focus_radius);
    const int diam = 2 * heal_r + 1;
    const int cells = std::max(1, diam * diam);
    int empty_near_n = 0;
    static int SoftDeferRimScanCd = 0;
    const bool skip_softdefer_disk_scan =
        !near_miss_urgent && prev_softdefer_empty == 0 &&
        SoftDeferEmptyOwned.empty() && SoftDeferRimScanCd > 0;
    if (skip_softdefer_disk_scan)
    {
      --SoftDeferRimScanCd;
    }
    else if (!near_miss_urgent && prev_softdefer_empty == 0 &&
             SoftDeferEmptyOwned.empty())
    {
      SoftDeferRimScanCd = 6;
    }
    else
    {
      SoftDeferRimScanCd = 0;
    }
    const auto softdefer_scan_t0 = std::chrono::high_resolution_clock::now();
    if (!skip_softdefer_disk_scan)
    {
    for (int idx = 0; idx < cells; ++idx)
    {
      const int dx = (idx % diam) - heal_r;
      const int dz = (idx / diam) - heal_r;
      const int horiz = std::max(std::abs(dx), std::abs(dz));
      const int col_cy1 =
          SoftDeferCyWindowNearTop(max_cy, preferred_cy, horiz);
      const int eff_cy1 = std::max(cy1, col_cy1);
      for (int cy = cy0; cy <= eff_cy1; ++cy)
      {
        const glm::ivec3 coord(focus_ground_horiz.x + dx, cy,
                               focus_ground_horiz.z + dz);
        const bool has_drawable = mesh_service.HasDrawableGreedyMesh(coord);
        const bool has_greedy = mesh_service.HasGreedyMesh(coord);
        const bool is_dirty = mesh_service.IsChunkMeshDirty(coord);
        const bool pending_gpu = mesh_service.IsPendingGpuApply(coord);
        const bool inflight = mesh_service.HasInflightMeshBuild(coord);
        const bool soft_held = mesh_service.IsSoftDeferHeld(coord);
        if (has_drawable || pending_gpu || inflight || is_dirty)
        {
          continue;
        }
        if (!has_greedy && !soft_held)
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
        const int probe_stride = (horiz <= 2) ? 2 : 4;
        for (int z = 0; z < CHUNK_SIZE && !any_solid; z += probe_stride)
        {
          for (int x = 0; x < CHUNK_SIZE && !any_solid; x += probe_stride)
          {
            for (int y = 0; y < CHUNK_SIZE && !any_solid; y += probe_stride)
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
        const bool empty_placeholder = IsSoftDeferEmptyPlaceholder(
            has_greedy, has_drawable, is_dirty, pending_gpu, inflight,
            any_solid);
        const bool hide_held =
            !has_greedy && soft_held && any_solid && !has_drawable;
        if (!empty_placeholder && !hide_held)
        {
          continue;
        }
        if (empty_placeholder)
        {
          ++phys_telem.SoftDeferEmptyPlaceholderN;
        }
        if (!SoftDeferEmptyNeedsFirstMeshOwnership(
                empty_placeholder || hide_held, /*miss_or_in_focus=*/true))
        {
          continue;
        }
        seen_empty.insert(coord);
        {
          const auto age_it = SoftDeferEmptyAgeFrames.find(coord);
          if (age_it == SoftDeferEmptyAgeFrames.end())
          {
            SoftDeferEmptyAgeFrames.emplace(coord, 0);
          }
          else
          {
            ++age_it->second;
          }
        }
        const int age_frames = SoftDeferEmptyAgeFrames[coord];
        if (age_frames > phys_telem.SoftDeferEmptyAgeMaxFrames)
        {
          phys_telem.SoftDeferEmptyAgeMaxFrames = age_frames;
        }
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
        if (horiz <= 2)
        {
          ++empty_near_n;
        }
        SoftDeferEmptyCand cand{};
        cand.coord = coord;
        cand.horiz = horiz;
        cand.view_dot = view_dot_at(dx, dz);
        cand.empty_placeholder = empty_placeholder;
        cands.push_back(cand);
      }
    }
    }
    phys_telem.SoftdeferEmptyScanMs =
        std::chrono::duration<double, std::milli>(
            std::chrono::high_resolution_clock::now() - softdefer_scan_t0)
            .count();
    phys_telem.SoftDeferEmptyNearN = empty_near_n;

    // Era39 P2: remesh drawable face-neighbors when SoftDefer-hidden enters/leaves.
    // Lit drawable seam → RemeshQ (MarkDirty), not FirstMeshQ.
    const auto softdefer_own_t0 = std::chrono::high_resolution_clock::now();
    {
      int seamed = 0;
      auto remesh_drawable_faces = [&](glm::ivec3 hidden, bool now_hidden,
                                       bool prev_hidden)
      {
        static const glm::ivec3 kFace[4] = {
            {1, 0, 0}, {-1, 0, 0}, {0, 0, 1}, {0, 0, -1}};
        for (const glm::ivec3 &d : kFace)
        {
          if (seamed >= 4)
          {
            break;
          }
          const glm::ivec3 nb = hidden + d;
          const bool has_draw = mesh_service.HasDrawableGreedyMesh(nb);
          if (!ShouldRemeshDrawableForHiddenNeighborSeam(has_draw, now_hidden,
                                                         prev_hidden))
          {
            continue;
          }
          if (!mesh_service.TryConsumeDirtyAdmit())
          {
            continue;
          }
          mesh_service.GetCache().InvalidateMeshCapture(nb);
          mesh_service.MarkDirty(nb);
          ++seamed;
        }
      };
      for (const glm::ivec3 &coord : seen_empty)
      {
        const bool prev = SoftDeferEmptyPrevSeen.count(coord) > 0;
        remesh_drawable_faces(coord, /*now_hidden=*/true, prev);
      }
      for (const glm::ivec3 &coord : SoftDeferEmptyPrevSeen)
      {
        if (seen_empty.count(coord) != 0)
        {
          continue;
        }
        remesh_drawable_faces(coord, /*now_hidden=*/false,
                              /*prev_hidden=*/true);
      }
      SoftDeferEmptyPrevSeen = seen_empty;
    }

    if (SoftDeferEmptyShouldApplyOwnership(UndrawnForceCd <= 0))
    {
      std::stable_sort(cands.begin(), cands.end(),
                       [](const SoftDeferEmptyCand &a,
                          const SoftDeferEmptyCand &b)
                       {
                         const float sa =
                             NearFovWorkScore(a.horiz, a.view_dot);
                         const float sb =
                             NearFovWorkScore(b.horiz, b.view_dot);
                         if (sa != sb)
                         {
                           return sa < sb;
                         }
                         if (a.view_dot != b.view_dot)
                         {
                           return a.view_dot > b.view_dot;
                         }
                         return a.horiz < b.horiz;
                       });

      const int near_reserve =
          SoftDeferEmptyNearReserveSlots(kEmptyOwnershipCap);
      std::vector<SoftDeferEmptyCand> near_cands;
      std::vector<SoftDeferEmptyCand> rim_cands;
      near_cands.reserve(cands.size());
      rim_cands.reserve(cands.size());
      for (const SoftDeferEmptyCand &c : cands)
      {
        if (c.horiz <= 2)
        {
          near_cands.push_back(c);
        }
        else
        {
          rim_cands.push_back(c);
        }
      }
      // Era38: rim-only rotation so near ring is never offset-starved.
      if (!rim_cands.empty())
      {
        const int rim_n = static_cast<int>(rim_cands.size());
        const int rot = SoftDeferEmptyScanOffset % rim_n;
        if (rot > 0)
        {
          std::rotate(rim_cands.begin(), rim_cands.begin() + rot,
                      rim_cands.end());
        }
      }

      int marked_n = 0;
      int empty_fm_enqueue_n = 0;
      bool age_drain_done = false;
      auto apply_ownership = [&](const SoftDeferEmptyCand &cand,
                                 bool allow_own)
      {
        const glm::ivec3 coord = cand.coord;
        const int horiz = cand.horiz;
        const glm::ivec2 col(coord.x, coord.z);
        const bool has_fm =
            exec.Scheduler().Contains(col, ColumnWorkKind::FirstMesh);
        const bool inflight_or_pending =
            mesh_service.HasInflightMeshBuild(coord) ||
            mesh_service.IsPendingGpuApply(coord) ||
            mesh_service.IsPendingGpuQueued(coord) ||
            mesh_service.IsPendingGpuKickedOrDispatched(coord);
        const bool already_dirty = mesh_service.IsChunkMeshDirty(coord);
        const bool sticky_owned = SoftDeferEmptyOwned.count(coord) > 0;
        // CheapRemesh C4: FirstMesh enqueue XOR MarkDirty — not both same frame.
        const bool will_enqueue_fm =
            allow_own && !has_fm && empty_fm_enqueue_n < kEmptyOwnershipCap;
        // Do not re-MarkDirtyPriority every scan on sticky Owned / already Dirty.
        if (allow_own && !sticky_owned && !already_dirty && !will_enqueue_fm &&
            SoftDeferEmptyShouldMarkDirty(true, has_fm, inflight_or_pending))
        {
          mesh_service.MarkDirtyPriority(coord);
        }
        if (will_enqueue_fm)
        {
          ++empty_fm_enqueue_n;
          ColumnWorkItem item{};
          item.column = col;
          item.kind = ColumnWorkKind::FirstMesh;
          item.priority =
              ColumnFlowFirstMeshPriority(105, horiz, heal_r);
          item.scan_full_focus = missing_visible_mesh;
          item.cy = coord.y;
          exec.Enqueue(item);
        }
        if (allow_own)
        {
          SoftDeferEmptyOwned.insert(coord);
          bool fully_dark_or_void =
              mesh_service.HasDrawableGreedyMesh(coord) &&
              mesh_service.GetCache().ChunkHasFullyDarkFace(coord);
          if (!fully_dark_or_void && cand.empty_placeholder)
          {
            const int max_cy_v = std::max(
                0, FloorDiv(world.GetProceduralSettings().MaxHeight,
                            CHUNK_SIZE));
            for (int cy_v = 0; cy_v <= max_cy_v; ++cy_v)
            {
              const glm::ivec3 c(col.x, cy_v, col.y);
              if (mesh_service.HasDrawableGreedyMesh(c) &&
                  mesh_service.GetCache().ChunkHasFullyDarkFace(c))
              {
                fully_dark_or_void = true;
                break;
              }
            }
            if (!fully_dark_or_void &&
                world.IsPendingLightBeforeMesh(col))
            {
              fully_dark_or_void = true;
            }
          }
          if (SoftDeferEmptyNeedsParallelVoidRelight(cand.empty_placeholder,
                                                     fully_dark_or_void))
          {
            if (!exec.Scheduler().Contains(col,
                                           ColumnWorkKind::RelightThenMesh))
            {
              ColumnWorkItem relight{};
              relight.column = col;
              relight.kind = ColumnWorkKind::RelightThenMesh;
              relight.priority = ColumnFlowRelightPriorityUnderMiss(
                  95, horiz, heal_r, missing_visible_mesh);
              relight.scan_full_focus = missing_visible_mesh;
              relight.cy = coord.y;
              exec.Enqueue(relight);
            }
            if (phys_telem.DarkFaceVoidNearN > 200)
            {
              world.EnqueueVoidDarkColumnRelightNote(col);
            }
          }
        }
        if (exec.Scheduler().Contains(col, ColumnWorkKind::FirstMesh))
        {
          ++phys_telem.SoftDeferEmptyOwnedN;
        }
        const int age_frames = SoftDeferEmptyAgeFrames[coord];
        if (ShouldEscalateSoftDeferEmptyAge(age_frames))
        {
          const bool gpu_queued =
              mesh_service.IsPendingGpuQueued(coord) ||
              mesh_service.IsPendingGpuKickedOrDispatched(coord);
          if (SoftDeferEmptyPreferKickAfterAgeOnly(
                  true, true, missing_visible_mesh, gpu_queued))
          {
            mesh_service.PreferKickPendingGpuQueued(coord);
          }
          else if (!gpu_queued)
          {
            ColumnWorkItem item{};
            item.column = col;
            item.kind = ColumnWorkKind::FirstMesh;
            item.priority = 110;
            item.scan_full_focus = missing_visible_mesh;
            item.cy = coord.y;
            exec.Enqueue(item);
            if (!age_drain_done)
            {
              note_column_flow_drain(1, 1);
              age_drain_done = true;
            }
          }
        }
        underfeet_undrawn = true;
      };

      // Near first (sorted), then rim (sorted + rotated). near sticky owns
      // outside cap; cap only admits new ownership.
      (void)near_reserve;
      auto run_list = [&](const std::vector<SoftDeferEmptyCand> &list,
                          bool near_ring)
      {
        for (const SoftDeferEmptyCand &cand : list)
        {
          const bool had_own = SoftDeferEmptyOwned.count(cand.coord) > 0;
          const bool sticky = near_ring && SoftDeferEmptyShouldKeepOwnership(
                                               /*still_empty=*/true, had_own);
          bool allow_own = sticky;
          if (!allow_own && marked_n < kEmptyOwnershipCap)
          {
            allow_own = true;
            ++marked_n;
          }
          apply_ownership(cand, allow_own);
        }
      };
      run_list(near_cands, /*near_ring=*/true);
      run_list(rim_cands, /*near_ring=*/false);

      for (auto it = SoftDeferEmptyAgeFrames.begin();
           it != SoftDeferEmptyAgeFrames.end();)
      {
        if (seen_empty.count(it->first) != 0)
        {
          ++it;
          continue;
        }
        const glm::ivec3 &coord = it->first;
        const int dx = std::abs(coord.x - focus_ground_horiz.x);
        const int dz = std::abs(coord.z - focus_ground_horiz.z);
        const bool in_rim = std::max(dx, dz) <= heal_r && coord.y >= cy0 &&
                            coord.y <= cy1;
        const bool has_drawable = mesh_service.HasDrawableGreedyMesh(coord);
        const bool has_greedy = mesh_service.HasGreedyMesh(coord);
        const bool soft_held = mesh_service.IsSoftDeferHeld(coord);
        const bool still_empty =
            in_rim && !has_drawable && (has_greedy || soft_held);
        SoftDeferEmptyOwned.erase(coord);
        if (SoftDeferEmptyAgeShouldReset(still_empty, /*had_progress=*/false))
        {
          it = SoftDeferEmptyAgeFrames.erase(it);
        }
        else
        {
          ++it;
        }
      }
      if (marked_n > 0 || !cands.empty())
      {
        UndrawnForceCd =
            (missing_visible_mesh &&
             IsMissFirstMeshClass(true, phys_telem.MissCy, phys_telem.MissHoriz))
                ? 2
                : (phys_telem.SoftDeferEmptyPlaceholderN > 0 ? 2 : 8);
        if (!rim_cands.empty())
        {
          SoftDeferEmptyScanOffset =
              (SoftDeferEmptyScanOffset + kEmptyOwnershipCap) %
              std::max(1, static_cast<int>(rim_cands.size()));
        }
      }
    }
    else
    {
      // Cd cooling: keep sticky OwnedN telemetry honest without Dirty storm.
      for (const SoftDeferEmptyCand &cand : cands)
      {
        const glm::ivec2 col(cand.coord.x, cand.coord.z);
        if (exec.Scheduler().Contains(col, ColumnWorkKind::FirstMesh) ||
            SoftDeferEmptyOwned.count(cand.coord) > 0)
        {
          ++phys_telem.SoftDeferEmptyOwnedN;
        }
        underfeet_undrawn = true;
      }
    }
    phys_telem.SoftdeferEmptyOwnMs =
        std::chrono::duration<double, std::milli>(
            std::chrono::high_resolution_clock::now() - softdefer_own_t0)
            .count();
  }
  // B5: SoftDefer-empty stuck — ColPipe P4: FirstMesh only if not already owned
  // (no MarkDirtyPriority / full-column Dirty storm).
  if (!moving && missing_visible_mesh && phys_telem.SoftDeferEmptyStuckN > 0)
  {
    const bool stop_tail_stuck =
        MissWitnessAgeFrames > 240 && world.GetTimeSinceMotionSec() > 4.0 &&
        pending_focus_count <= 2;
    if (stop_tail_stuck || MissWitnessAgeFrames > 120)
    {
      const glm::ivec3 stuck(phys_telem.SoftDeferEmptyStuckCx,
                             phys_telem.SoftDeferEmptyStuckCy,
                             phys_telem.SoftDeferEmptyStuckCz);
      const glm::ivec2 stuck_col(stuck.x, stuck.z);
      auto &exec = GetColumnFlowExecutor();
      if (!exec.Scheduler().Contains(stuck_col, ColumnWorkKind::FirstMesh) &&
          SoftDeferEmptyOwned.count(stuck) == 0)
      {
        ColumnWorkItem pin{};
        pin.column = stuck_col;
        pin.kind = ColumnWorkKind::FirstMesh;
        pin.priority = stop_tail_stuck ? 126 : 115;
        pin.scan_full_focus = false;
        pin.cy = stuck.y;
        exec.Enqueue(pin);
        SoftDeferEmptyOwned.insert(stuck);
        note_column_flow_drain(2, 2);
      }
    }
  }

  phys_telem.SoftDeferHeldN =
      static_cast<int>(mesh_service.GetSoftDeferHeldCount());
  // Feet-column need only — missing/pending in r=1 neighbors must not boost
  // Immediate/schedule on the player column (manual 175310 two-chunk flicker).
  const bool missing_feet_column =
      have_nearest_missing && nearest_missing_hole.x == focus_ground_horiz.x &&
      nearest_missing_hole.z == focus_ground_horiz.z;
  const bool pending_feet = world.IsPendingLightBeforeMesh(
      glm::ivec2(focus_ground_horiz.x, focus_ground_horiz.z));
  const bool underfeet_need = UnderfeetNeedUrgent(
      missing_feet_column, pending_feet, underfeet_undrawn);
  if (underfeet_undrawn)
  {
    auto &exec = GetColumnFlowExecutor();
    exec.Enqueue(glm::ivec2(focus_ground_horiz.x, focus_ground_horiz.z),
                 ColumnWorkKind::FirstMesh, 110);
  }
  // Sky-only regress: holes SoT false while underfeet has solid without
  // drawable (0-quad fake-ready / SoftDefer empty pruned). Force FirstMesh
  // only when no Dirty/Held/inflight/GPU/RAA owner already holds the slice.
  {
    const glm::ivec2 uf(focus_ground_horiz.x, focus_ground_horiz.z);
    const int player_cy = FloorDiv(focus_block.y, CHUNK_SIZE);
    const int cy0 = std::max(0, player_cy - 2);
    const int cy1 = player_cy + 1;
    bool forced = false;
    for (int cy = cy0; cy <= cy1; ++cy)
    {
      const glm::ivec3 coord(uf.x, cy, uf.y);
      if (mesh_service.HasDrawableGreedyMesh(coord))
      {
        continue;
      }
      const UChunk *chunk =
          world.GetBlockWorld().GetChunkManager().GetChunk(coord);
      if (!chunk)
      {
        continue;
      }
      bool solid = false;
      for (int z = 0; z < CHUNK_SIZE && !solid; z += 4)
      {
        for (int x = 0; x < CHUNK_SIZE && !solid; x += 4)
        {
          for (int y = 0; y < CHUNK_SIZE && !solid; y += 4)
          {
            if (chunk->GetBlockLocal(glm::ivec3(x, y, z)) != BLOCK_AIR)
            {
              solid = true;
            }
          }
        }
      }
      if (!ShouldForceUnderfeetSolidFirstMeshDirty(
              /*has_drawable=*/false, solid,
              mesh_service.IsChunkMeshDirty(coord),
              mesh_service.IsSoftDeferHeld(coord),
              mesh_service.HasInflightMeshBuild(coord),
              mesh_service.IsPendingGpuApply(coord),
              mesh_service.IsRemeshAfterApplyPending(coord)))
      {
        if (solid && mesh_service.IsSoftDeferHeld(coord))
        {
          forced = true;
        }
        continue;
      }
      mesh_service.MarkDirtyPriority(coord);
      forced = true;
    }
    if (forced)
    {
      GetColumnFlowExecutor().Enqueue(uf, ColumnWorkKind::FirstMesh, 120);
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
      // Era38 A2: no rear Pass1b while near SoftDefer empty or pending debt.
      const bool starve_hinterland = StarveHinterlandUnlit(
          phys_telem.SoftDeferEmptyNearN, pending_focus_count);
      mesh_service.SetMaxRearFocusMeshPerFrame(
          starve_hinterland
              ? 0
              : (moving ? 3
                        : ((visual_holes || missing_underfeet) ? 2 : 0)));
    }
  }
  int mesh_drain = LastBudget.MaxMeshDrain;
  int mesh_schedule = LastBudget.MaxMeshSchedule;
  const size_t pending_dirty = mesh_service.GetDirtyCount();
  const int pending_async = mesh_service.GetAsyncInFlightCount();
  const int pending_gpu_queued_n =
      static_cast<int>(mesh_service.GetPendingGpuQueuedCount());
  const int fifo_n_imm = world.GetPendingTerrainRelightFifoCount();
  const int fifo_cap_imm = URuntimeTuning::Get().RelightFifoSoftCap;

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
    if (!world.IsEnterFovLitPassActive() && mtune.PendingLightSoftCap > 0 &&
        world.GetPendingLightBeforeMeshCount() >
            static_cast<size_t>(mtune.PendingLightSoftCap))
    {
      const int dropped = world.TrimPendingLightBeforeMesh(
          focus_ground_horiz, mtune.PendingLightSoftCap);
      phys.PendingLightDropped += static_cast<uint64_t>(std::max(0, dropped));
    }
    if (!world.IsEnterFovLitPassActive() && mtune.RelightFifoSoftCap > 0)
    {
      const int dropped = world.TrimFarRelightFifoFarthest(
          focus_ground_horiz, mtune.RelightFifoSoftCap);
      phys.RelightFifoDropped += static_cast<uint64_t>(std::max(0, dropped));
      phys.RelightTrimFarN += std::max(0, dropped);
      phys.RelightFifoDropN += std::max(0, dropped);
      // P4: under Red+holes+fifo pressure, trim again toward soft-cap aggressively.
      // Phase 1c: trim = truncate outer tickets alarm, not silent heal.
      const int fifo_live = world.GetPendingTerrainRelightFifoCount();
      if (ShouldCruiseRedFifoLightDrain(
              pressure, fifo_live, mtune.RelightFifoSoftCap,
              mtune.RelightFifoAdmitFrac,
              visual_holes || missing_visible_mesh || missing_underfeet,
              pending_focus_count))
      {
        const int drop2 = world.TrimFarRelightFifoFarthest(
            focus_ground_horiz, std::max(8, mtune.RelightFifoSoftCap * 3 / 4));
        phys.RelightFifoDropped += static_cast<uint64_t>(std::max(0, drop2));
        phys.RelightTrimFarN += std::max(0, drop2);
        phys.RelightFifoDropN += std::max(0, drop2);
      }
      // Phase 1c: after trim, drain light from MissReserved carve — not silent heal.
      if (phys.RelightTrimFarN > 0 &&
          (visual_holes || missing_visible_mesh || pending_focus_count > 0))
      {
        const int boost = std::min(8, 2 + phys.RelightTrimFarN / 8);
        GetColumnFlowExecutor().DrainIdlePendingLight(
            world, focus_ground_horiz, focus_radius, boost,
            /*allow_sync=*/false, last_frame_ms, pending_focus_count,
            missing_visible_mesh);
      }
    }
  }

  // Saturated async pool: drop far in-flight work so focus missing can schedule.
  static int async_relief_cooldown = 0;
  if ((visual_holes || missing_underfeet || pending_focus_count > 24) &&
      pending_async >= 28)
  {
    if (async_relief_cooldown <= 0)
    {
      mesh_service.CancelInFlightOutsideHorizontalRadius(
          focus_ground_horiz, focus_radius, /*keep_horiz_lease=*/1);
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

  // SOTA: FirstMesh > Relight > Remesh via queue priority — no starve forks.
  mesh_service.SetStarveOutsideFocusMesh(false);
  mesh_service.SetStarveRemeshForHoles(false);
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
      black_sticky == 0 && pending_dirty_early > 48;
  const bool base_suppress =
      idle_remesh_debt || idle_focus_dirty_debt ||
      suppress_seam_for_sticky_catchup || suppress_seam_standing_churn;
  world.SetSuppressRelightSeamDirty(
      ShouldSuppressRelightSeamDirtyForEnterGate(
          world.IsEnterLitGateActive(), world.IsSpawnMeshRingReady(),
          base_suppress));
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
  // ColdSupply S2: under Dirty soft pressure prefer focus±LitDrawable — do not
  // open outside 8–12 while PL debt / holes (manual 142000 emerge≈53 dirty≈149).
  if (idle_recovery || idle_remesh_debt)
  {
    mesh_service.SetMaxOutsideFocusMeshPerFrame(0);
  }
  else if (moving &&
           (visual_holes || missing_underfeet || underfeet_undrawn ||
            pending_dirty > 200 || pending_focus_count > 30))
  {
    if ((underfeet_undrawn || missing_underfeet) && pending_dirty <= 450)
    {
      mesh_service.SetMaxOutsideFocusMeshPerFrame(1);
    }
    else if (visual_holes && pending_dirty <= 200 && pending_focus_count <= 30)
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
  else if (!visual_holes && !missing_underfeet && pending_focus_count == 0 &&
           pending_dirty > 200 && last_frame_ms <= 28.0)
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
  // Era51 F1a: snapshot budget tracks emerge decay — was blanket 48ms.
  const double snapshot_budget =
      (visual_holes || missing_underfeet ||
       (idle_recovery && pending_focus_count > 0))
          ? std::min(StopIdleEmergeMs + 4.0, 48.0)
          : (idle_focus_dirty_debt ? 28.0 : 6.0);
  mesh_service.SetMeshSnapshotBudgetMs(snapshot_budget);
  // Healed idle (miss=0, no pending/holes): default idle emerge 60ms ate wall.
  // Keep 60 only while recovering holes/light. Sticky remesh is async — do not
  // hold the 60ms SyncRebuild band just because black_sticky>0 (I4e: sticky=8
  // kept emerge=60 and sync~35ms on classifier-calm stand).
  const bool healed_idle_emerge =
      !moving && !visual_holes && !missing_underfeet &&
      !missing_visible_mesh && pending_focus_count == 0;
  {
    constexpr double kWallEmaAlpha = 0.1;
    if (WallEmaMs <= 0.0)
    {
      WallEmaMs = last_frame_wall_ms;
    }
    else
    {
      WallEmaMs = WallEmaMs * (1.0 - kWallEmaAlpha) +
                  last_frame_wall_ms * kWallEmaAlpha;
    }
  }
  const double adaptive_moving_emerge =
      std::clamp(0.12 * WallEmaMs, 8.0, 20.0);
  // Era51 F1a: adaptive stop-phase emerge budget with decay.
  // Industry standard (Cubyz): 12ms/frame hard cap.  Previous blanket 60ms
  // when visual_holes=1 caused stop_wall_med ~110ms.  Now: start at 20ms on
  // stop, decay toward 8ms over ~5s.  Underfeet miss keeps 28ms floor.
  if (moving)
  {
    StopIdleEmergeMs = 20.0; // reset on next stop
  }
  else
  {
    constexpr double kDecay = 0.97; // ~5s to reach 8 from 20 at 30fps
    StopIdleEmergeMs = std::max(8.0, StopIdleEmergeMs * kDecay);
    // B6: persistent stop miss must not decay emerge budget to starvation.
    if (missing_visible_mesh && MissWitnessAgeFrames > 120)
    {
      StopIdleEmergeMs = std::max(StopIdleEmergeMs, 14.0);
    }
  }
  double stop_emerge =
      healed_idle_emerge ? (idle_focus_dirty_debt ? 28.0 : 14.0)
      : missing_underfeet ? std::max(StopIdleEmergeMs, 28.0)
                          : StopIdleEmergeMs;
  if (!moving && missing_visible_mesh && MissWitnessAgeFrames > 120)
  {
    stop_emerge = std::max(stop_emerge, pending_focus_count <= 2 ? 18.0 : 14.0);
  }
  mesh_service.SetMeshEmergeTotalBudgetMs(
      moving ? adaptive_moving_emerge : stop_emerge);
  auto clamp_emerge_to_phase = [&]()
  {
    const double cap = world.GetPhysicsTelemetry().EmergeBudgetCapMs;
    const bool protect_near = ShouldProtectNearEmergeFromPhaseClamp(
        missing_underfeet, nearest_miss_h,
        world.GetPhysicsTelemetry().UnderfeetHasMesh != 0);
    mesh_service.SetMeshEmergeTotalBudgetMs(static_cast<float>(
        ApplyPhaseEmergeClamp(
            static_cast<double>(mesh_service.GetMeshEmergeTotalBudgetMs()), cap,
            protect_near)));
  };
  clamp_emerge_to_phase();
  // Era31 I-T2: cap emerge + defer far stale remesh under ocean heal pressure.
  {
    auto &phys_ocean = world.GetPhysicsTelemetryMutable();
    const bool ocean_heal = IsOceanHealPressure(
        missing_visible_mesh, phys_ocean.DarkFaceVoidNearN,
        phys_ocean.VisibleBlackFocusN);
    if (ocean_heal)
    {
      if (moving)
      {
        mesh_service.SetMeshEmergeTotalBudgetMs(OceanHealMeshEmergeBudgetMs());
        clamp_emerge_to_phase();
      }
      if (phys_ocean.DarkFaceVoidNearN > 200)
      {
        mesh_service.SetStarveRemeshKeepHoriz(2);
        mesh_service.DropRemeshDirtyBeyondRadius(focus_ground_horiz,
                                                 /*keep_h=*/2, /*keep_cy=*/2,
                                                 /*remesh_only=*/true);
      }
    }
    // Era34 P2: SoftDefer empty / holes while moving — clamp emerge + bias FM.
    if (ShouldBiasFirstMeshOverRemesh(phys_ocean.SoftDeferEmptyPlaceholderN,
                                      visual_holes || missing_visible_mesh,
                                      moving) &&
        near_miss_urgent)
    {
      // Era35 P4: boost emerge budget when SoftDefer empty lag during cruise.
      const double cruise_budget = CruiseCatchUpEmergeBudgetMs(
          OceanHealMeshEmergeBudgetMs(), phys_ocean.SoftDeferEmptyPlaceholderN,
          moving);
      mesh_service.SetMeshEmergeTotalBudgetMs(std::min(
          mesh_service.GetMeshEmergeTotalBudgetMs(), cruise_budget));
      clamp_emerge_to_phase();
      mesh_service.SetStarveRemeshForHoles(true);
      mesh_drain = std::max(mesh_drain, 14);
      mesh_schedule = std::max(mesh_schedule, 12);
    }
  }

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
  // CheapRemesh C6: one coalesced DropRemesh pass (was keep_h=1/2/1 same tick).
  const glm::ivec3 focus_dirty_keep(focus_ground.x, preferred_cy,
                                    focus_ground.z);
  {
    int drop_keep_h = 0;
    bool do_drop = false;
    if (idle_focus_dirty_debt)
    {
      do_drop = true;
      drop_keep_h = std::max(drop_keep_h, 1);
    }
    if (!moving && !visual_holes && !pending_near_light &&
        focus_dirty_early > 160)
    {
      do_drop = true;
      drop_keep_h = std::max(drop_keep_h, 2);
    }
    // Idle opaque stability (manual 131234): far unlit↔lit remesh churns opaque.
    if (!moving && !visual_holes && !missing_visible_mesh)
    {
      do_drop = true;
      drop_keep_h = std::max(drop_keep_h, 1);
    }
    if (do_drop)
    {
      mesh_service.DropRemeshDirtyBeyondRadius(focus_dirty_keep, drop_keep_h,
                                              /*keep_cy=*/2);
    }
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
    // Era51 F1v: Stop-phase with visual_holes is a "persistent hole heal"
    // regime; don't blindly raise drain/schedule to 16 (it inflates wall).
    // Tie drain/schedule cap to the same stop-phase budget decay.
    const int stop_cap =
        std::clamp(static_cast<int>(StopIdleEmergeMs * 0.5), 6, 10);
    const int gpu_cap = moving ? 12 : stop_cap;
    if (moving)
    {
      mesh_schedule = std::max(mesh_schedule, gpu_cap);
      mesh_drain = std::max(mesh_drain, gpu_cap);
    }
    else
    {
      mesh_schedule = std::min(mesh_schedule, gpu_cap);
      mesh_drain = std::min(mesh_drain, gpu_cap);
    }
  }
  // F2(B): targeted drain/schedule boost only for proven stuck focus-miss.
  // Keep it wall-aware so we do not inflate already-hot stop frames.
  if (!moving && missing_visible_mesh && MissWitnessAgeFrames > 150)
  {
    const int stuck_drain = last_frame_ms <= 90.0 ? 16 : 12;
    const int stuck_schedule = last_frame_ms <= 90.0 ? 12 : 8;
    mesh_drain = std::max(mesh_drain, stuck_drain);
    mesh_schedule = std::max(mesh_schedule, stuck_schedule);
  }
  // F2(B2): stop-tail stuck miss escalation.
  // When miss persists deep into stop (not early settle window), boost
  // drain/schedule harder to finish missing-column emergence by tail.
  const bool stop_tail_stuck =
      !moving && missing_visible_mesh && MissWitnessAgeFrames > 240 &&
      world.GetTimeSinceMotionSec() > 4.0 && pending_focus_count <= 2;
  if (stop_tail_stuck)
  {
    mesh_service.SetStarveOutsideFocusMesh(true);
    const int tail_drain = last_frame_ms <= 90.0 ? 20 : 14;
    const int tail_schedule = last_frame_ms <= 90.0 ? 16 : 10;
    mesh_drain = std::max(mesh_drain, tail_drain);
    mesh_schedule = std::max(mesh_schedule, tail_schedule);
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
    // Era20: miss cy/horiz into early admit (was Finalize-only) so SoftDefer/
    // PreferKick see FirstMesh class for 214034 cy=3/mh=4 witnesses.
    ain.nearest_miss_horiz = world.GetPhysicsTelemetry().MissHoriz;
    ain.nearest_miss_cy = world.GetPhysicsTelemetry().MissCy;
    ain.enter_lit_gate = world.IsEnterLitGateActive();
    ain.remesh_queue_n = mesh_service.GetLastDirtyRemeshN();
    if (have_nearest_missing)
    {
      ain.nearest_miss_horiz = std::max(
          std::abs(nearest_missing_hole.x - focus_ground_horiz.x),
          std::abs(nearest_missing_hole.z - focus_ground_horiz.z));
      ain.nearest_miss_cy = nearest_missing_hole.y;
    }
    {
      MeshWorkAdmission early_adm = ComputeMeshWorkAdmission(ain);
      const auto &tune = URuntimeTuning::Get();
      const auto &pt = world.GetPhysicsTelemetry();
      RemeshAdmitBackpressureInput bin{};
      bin.stream_pressure = pt.StreamPressure;
      bin.fifo_n = pt.RelightFifoN;
      bin.dirty_n = static_cast<int>(pending_dirty_early);
      bin.relight_fifo_soft_cap = tune.RelightFifoSoftCap;
      bin.dirty_thrash_soft_cap = tune.DirtyThrashSoftCap;
      bin.fifo_admit_frac = tune.RelightFifoAdmitFrac;
      bin.admit_cap_red = tune.DirtyAdmitCapRed;
      bin.admit_cap_yellow = tune.DirtyAdmitCapYellow;
      bin.miss_active = visual_holes || missing_visible_mesh || missing_underfeet;
      bin.remesh_queue_n = mesh_service.GetLastDirtyRemeshN();
      ApplyRemeshAdmitBackpressure(early_adm, bin);
      mesh_service.SetMeshWorkAdmission(early_adm);
    }
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
  // Era31 I-T1: ocean void heal moving drain floor (even without miss).
  // Era36 B3: land cruise pending-light drain while moving (not ocean-only).
  {
    const int void_n = world.GetPhysicsTelemetry().DarkFaceVoidNearN;
    const int vb_n = world.GetPhysicsTelemetry().VisibleBlackFocusN;
    const bool ocean_heal =
        IsOceanHealPressure(missing_visible_mesh, void_n, vb_n);
    if (moving && ocean_heal)
    {
      static int ocean_void_drain_cd = 0;
      if (ocean_void_drain_cd <= 0)
      {
        const int floor =
            OceanHealMovingRelightDrainFloor(ocean_heal, moving, void_n);
        if (floor > 0)
        {
          GetColumnFlowExecutor().DrainIdlePendingLight(
              world, focus_ground_horiz, focus_radius, floor, false,
              last_frame_ms, pending_focus_count, missing_visible_mesh);
        }
        ocean_void_drain_cd = 1;
      }
      else
      {
        --ocean_void_drain_cd;
      }
    }
    else if (moving)
    {
      static int land_pending_drain_cd = 0;
      if (land_pending_drain_cd <= 0)
      {
        const int floor = LandMovingRelightDrainFloor(moving, pending_focus_count);
        if (floor > 0)
        {
          GetColumnFlowExecutor().DrainIdlePendingLight(
              world, focus_ground_horiz, focus_radius, floor, false,
              last_frame_ms, pending_focus_count, missing_visible_mesh);
        }
        land_pending_drain_cd = 1;
      }
      else
      {
        --land_pending_drain_cd;
      }
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
    note_column_flow_drain(4, 4);
    // ColPipe P1: no RemeshSeam on spawn catch-up (FirstMesh owns hole).
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
        (black_sticky > 0 && !moving &&
         recover_watchdog_frames >= RecoverWatchdogFramesForDarkNear(false)) ||
        (!moving && pending_focus_n > 15 && recover_watchdog_frames >= 2) ||
        (world.GetPhysicsTelemetry().DarkFaceNearN > 500 &&
         recover_watchdog_frames >=
             RecoverWatchdogFramesForDarkNear(moving)) ||
        // Era16: ticket orphans; Era17: also while VisibleBlackFocusN>0 (heal-until).
        // Idle: slower cadence — VB=81 stick forever otherwise (manual 160656).
        ((world.GetPhysicsTelemetry().VisibleBlackNoTicketN > 0 ||
          world.GetPhysicsTelemetry().VisibleBlackFocusN > 0) &&
         recover_watchdog_frames >= (moving ? 4 : 30));
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
                       admit_n, last_frame_ms, pending_async);
      if (!idle_remesh_debt && !idle_focus_dirty_debt)
      {
        const int pending_dark_preview =
            world.CountPendingDarkFocusMeshes(focus_ground, focus_radius);
        const bool urgent_dark_pending =
            pending_focus_n > 0 &&
            world.GetPhysicsTelemetry().DarkFaceNearN > 500;
        const glm::ivec2 focus_xz(focus_ground_horiz.x, focus_ground_horiz.z);
        const bool already_owns_light =
            world.IsPendingLightBeforeMesh(focus_xz) ||
            exec.Scheduler().Contains(focus_xz,
                                      ColumnWorkKind::RelightThenMesh) ||
            exec.Scheduler().Contains(focus_xz,
                                      ColumnWorkKind::PromoteRelight);
        if (ShouldEnqueueUrgentDarkRelight(pending_dark_preview > 0,
                                           urgent_dark_pending,
                                           already_owns_light))
        {
          // Must stay below FirstMesh (100+admit). recover_n+100 starved rim
          // admit (land-cruise miss_stuck 6–12s).
          const int relight_prio =
              missing_visible_mesh ? 55 : (recover_n + 100);
          exec.Enqueue(focus_xz, ColumnWorkKind::RelightThenMesh, relight_prio);
        }
      }
      // Era19: hitch drain via FrameStreamingBudget — FirstMesh/no_ticket only
      // under miss_first (no VB Capture/Relight storm on hot wall).
      const bool hitch_drain = last_frame_ms > 40.0;
      int drain_n = EvaluateMissFirstDrainN(
          recover_n, missing_visible_mesh,
          world.GetPhysicsTelemetry().VisibleBlackNoTicketN > 0,
          world.GetPhysicsTelemetry().VisibleBlackFocusN > 0, hitch_drain,
          moving, URuntimeTuning::Get().MissFirstFrameBudget);
      note_column_flow_drain(drain_n, 1);
      // ColPipe P1: kill recover RemeshSeam storm (DarkFaceNearN forever).
      // Dark debt → RelightThenMesh only when not already owned (above).
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
  // Era51 F1b: tighter Immediate cap on stop-phase (was 3/5ms → accum 44ms).
  // On stop: cap per-frame at 2ms so period total stays < 20ms at ~15fps.
  const double hard_immediate_ms =
      !moving ? 2.0
      : last_frame_ms > static_cast<double>(imm_tune.ImmediateHotWallMs)
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
            world.GetPhysicsTelemetry().DarkFaceStaleNearN,
            world.GetPhysicsTelemetry().SoftDeferEmptyPlaceholderN,
            world.GetPhysicsTelemetry().DarkFaceVoidNearN});
    if (cold.active && cold.promote_once &&
        (pending_focus_count > 0 || missing_visible_mesh))
    {
      auto &exec = GetColumnFlowExecutor();
      exec.RequestPromoteRelight(
          glm::ivec2(focus_ground_horiz.x, focus_ground_horiz.z), 40);
      if (cold.first_mesh_admit > 0)
      {
        exec.Enqueue(glm::ivec2(focus_ground_horiz.x, focus_ground_horiz.z),
                     ColumnWorkKind::FirstMesh, 80);
      }
    }
    else if (!moving && missing_visible_mesh && pending_focus_count > 0 &&
             last_frame_ms <= 20.0)
    {
      auto &exec = GetColumnFlowExecutor();
      exec.RequestPromoteRelight(
          glm::ivec2(focus_ground_horiz.x, focus_ground_horiz.z), 40);
      exec.Enqueue(glm::ivec2(focus_ground_horiz.x, focus_ground_horiz.z),
                   ColumnWorkKind::FirstMesh, 60);
    }
    else if (!moving &&
             (missing_visible_mesh || focus_not_render_ready > 0) &&
             last_frame_ms <= 28.0)
    {
      auto &exec = GetColumnFlowExecutor();
      exec.RequestPromoteRelight(
          glm::ivec2(focus_ground_horiz.x, focus_ground_horiz.z), 40);
      // Era22: do not RemeshSeam the player column for black_sticky ring debt —
      // that remeshed underfeet forever (manual 172208). Sticky drain + SyncIdle
      // own sticky columns.
      if (missing_visible_mesh)
      {
        const int sticky_sync =
            std::clamp(4 + (focus_not_render_ready > 16 ? 4 : 0), 4, 12);
        exec.Enqueue(glm::ivec2(focus_ground_horiz.x, focus_ground_horiz.z),
                     ColumnWorkKind::FirstMesh, 40);
        note_column_flow_drain(sticky_sync, 1);
      }
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
      // ColPipe P4: no underfeet Immediate — DirtyPriority + FirstMesh + lease.
      if (!mesh_service.HasMeshSatisfyingColumnReady(hole) &&
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
    // Era33 P1: land — ground/underfeet before canopy (miss_cy≈2–3 / tree tops
    // in void). Ocean — KEEP sea_cy early, then ± expand.
    int prefer_cy = focus_ground.y;
    if (procedural.FillWater && std::abs(focus_ground.y - sea_cy) <= 3)
    {
      prefer_cy = sea_cy;
    }
    else if (!procedural.FillWater)
    {
      // Prefer lowest loaded solid / cy0 as ground band, not camera canopy cy.
      prefer_cy = cy0;
      for (int cy = cy0; cy <= std::min(cy1, focus_ground.y); ++cy)
      {
        const glm::ivec3 probe(focus_ground.x, cy, focus_ground.z);
        const UChunk *probe_chunk =
            world.GetBlockWorld().GetChunkManager().GetChunk(probe);
        if (!probe_chunk)
        {
          continue;
        }
        bool solid = false;
        for (int z = 0; z < CHUNK_SIZE && !solid; z += 4)
        {
          for (int x = 0; x < CHUNK_SIZE && !solid; x += 4)
          {
            for (int y = 0; y < CHUNK_SIZE && !solid; y += 4)
            {
              if (probe_chunk->GetBlockLocal(glm::ivec3(x, y, z)) != BLOCK_AIR)
              {
                solid = true;
              }
            }
          }
        }
        if (solid)
        {
          prefer_cy = cy;
          break;
        }
      }
    }
    std::vector<int> cy_order =
        BuildMeshCyVisitOrder(cy0, cy1, prefer_cy, sea_cy,
                              procedural.FillWater);
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
            // ColPipe P4: no underfeet Immediate — DirtyPriority + FirstMesh.
            GetColumnFlowExecutor().Enqueue(
                glm::ivec2(coord.x, coord.z), ColumnWorkKind::FirstMesh, 110);
            mesh_service.MarkDirtyPriority(coord);
            ++immediate;
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
  // Era14 TD-ARCH-041: MarkDirty / promote always under miss — Imm only stays
  // behind budget/calm (DISCARD wall as Dirty enqueue gate).
  if (missing_visible_mesh)
  {
    static int force_hole_cd = 0;
    const FocusIngressDecision ingress = EvaluateFocusIngress(FocusIngressInput{
        moving, missing_visible_mesh, pending_focus_count, pending_async,
        last_frame_ms, world.GetPhysicsTelemetry().UnfinishedVisual,
        world.GetPhysicsTelemetry().DarkFaceStaleNearN,
        world.GetPhysicsTelemetry().SoftDeferEmptyPlaceholderN,
        world.GetPhysicsTelemetry().DarkFaceVoidNearN});
    if (force_hole_cd <= 0)
    {
      glm::ivec3 hole{};
      if (mesh_service.FindNearestMissingGreedyMesh(
              world.GetBlockWorld(), focus_ground_horiz, focus_radius, hole))
      {
        auto &exec = GetColumnFlowExecutor();
        exec.RequestPromoteRelight(
            glm::ivec2(focus_ground_horiz.x, focus_ground_horiz.z), 40);
        exec.RequestPromoteRelight(glm::ivec2(hole.x, hole.z), 90);
        if (moving && ingress.first_mesh_admit > 0)
        {
          exec.Enqueue(glm::ivec2(hole.x, hole.z), ColumnWorkKind::FirstMesh,
                       70 + ingress.first_mesh_admit * 10);
        }
        // Orphan Active (HasInflight) without builder flight — ColumnFlow only.
        if (mesh_service.HasInflightMeshBuild(hole))
        {
          const glm::ivec2 hole_xz(hole.x, hole.z);
          if (!exec.Scheduler().Contains(hole_xz, ColumnWorkKind::FirstMesh) &&
              !exec.Scheduler().Contains(hole_xz,
                                         ColumnWorkKind::RelightThenMesh))
          {
            exec.Enqueue(hole_xz, ColumnWorkKind::FirstMesh, 85);
          }
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
        const bool calm_enough_for_imm = last_frame_ms <= 50.0;
        // Era20 I-M2: cold-async Imm escape — ignore wall when async dead under
        // miss (manual 214034: wall~246 Imm=0 + async=0 sticky).
        const bool cold_async_escape =
            ShouldColdAsyncImmEscape(missing_visible_mesh, pending_async) &&
            is_nearest_hole && !hole_pending;
        // ColPipe P4: never RebuildChunkImmediate for FOV holes — FirstMesh only.
        {
          // ColumnFlow FirstMesh for nearest FOV hole — no MarkDirty bypass.
          if (!mesh_service.IsChunkMeshDirty(hole) &&
              !mesh_service.HasInflightMeshBuild(hole))
          {
            exec.Enqueue(glm::ivec2(hole.x, hole.z), ColumnWorkKind::FirstMesh,
                         75);
          }
          // Nudge only *missing* underfeet-ring slices via ColumnFlow FirstMesh.
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
                  exec.Enqueue(glm::ivec2(neighbor.x, neighbor.z),
                               ColumnWorkKind::FirstMesh, 65);
                }
              }
            }
          }
          // Q2 seam: already-drawable neighbors keep Unknown-culled side faces
          // until remeshed after this hole FirstMesh lands (manual 191432).
          // Cap 4; HoleDrain-safe (does not admit missing neighbors).
          // Q2 seam: already-drawable neighbors keep Unknown-culled side faces
          // until remeshed after this hole FirstMesh lands (manual 191432).
          // Cap 4; HoleDrain-safe. Always under miss — wall cost is async Dirty
          // (p2c holes=0.2 with hot-skip; p2 full seam holes≈0.045).
          // Era51 P3 skip: this path + CanSeedSkylightAtCommit already cover
          // InGame missing→loaded seam; no extra OpenSky/relight ring (enter-only).
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
                  // Closeout C: drawable hole-seam → RemeshQ (not FirstMesh).
                  mesh_service.MarkDirty(neighbor);
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
    pt.PrepPendingLightMs = prep_pending_light_ms;
    pt.PrepBlackStickyMs = prep_black_sticky_ms;
    pt.PrepDirtyCountMs = prep_dirty_count_ms;
    pt.PrepSoftdeferSetupMs = prep_softdefer_setup_ms;
    // SoftdeferEmptyScanMs / SoftdeferEmptyOwnMs written during SoftDefer block.
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
    // Near miss: drop hinterland FirstMesh Dirty only — never inside the
    // LitDrawable ring (183918 keep_h=2 starved cruise frontier / opaque).
    if (near_miss_urgent && pending_dirty_early > 96)
    {
      const int fm_keep_h = FirstMeshPruneKeepHoriz(focus_radius);
      world.GetPhysicsTelemetryMutable().DirtyDropped +=
          static_cast<uint64_t>(std::max(
              0, mesh_service.DropFarFirstMeshDirtyBeyondRadius(
                     focus_ground_horiz, fm_keep_h, /*keep_cy=*/-1)));
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
    if (StandWitnessColumnDirtyCd > 0)
    {
      --StandWitnessColumnDirtyCd;
    }
    const bool stop_tail_ownership_mode =
        !moving && missing_visible_mesh && MissWitnessAgeFrames > 240 &&
        world.GetTimeSinceMotionSec() > 4.0 && pending_focus_count <= 2;
    if (found_nearest_missing)
    {
      nearest_miss_nh = std::max(
          std::abs(isolated_hole.x - focus_ground_horiz.x),
          std::abs(isolated_hole.z - focus_ground_horiz.z));
      const glm::ivec2 hole_col(isolated_hole.x, isolated_hole.z);
      // Stand: Dirty full witness column cy0..band so low-cy rim slices heal
      // alongside exact FirstMesh (manual 131827 miss cy0–2). Cruise: exact cy.
      if (!moving && StandWitnessColumnDirtyCd <= 0)
      {
        const int max_y = procedural.MaxHeight;
        const int player_max =
            std::min(max_y, focus_ground.y * CHUNK_SIZE + CHUNK_SIZE * 3 - 1);
        const int hole_max =
            (isolated_hole.y + 1) * CHUNK_SIZE - 1;
        const int remesh_max =
            stop_tail_ownership_mode ? max_y : std::max(player_max, hole_max);
        const glm::ivec3 ground(isolated_hole.x, 0, isolated_hole.z);
        const int marked = mesh_service.MarkMissingSlicesDirtyPriority(
            world.GetBlockWorld(), ground, 0, remesh_max);
        if (marked > 0)
        {
          world.GetPhysicsTelemetryMutable().StandRimDirtyN += marked;
        }
        // B4: in stop-tail ownership mode re-mark more frequently so full-height
        // missing slices cannot fall out between FirstMesh admissions.
        StandWitnessColumnDirtyCd = stop_tail_ownership_mode ? 2 : 12;
      }
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
        hole.priority = stop_tail_ownership_mode ? 118 : 100;
        hole.scan_full_focus = false;
        hole.cy = stop_tail_ownership_mode ? -2 : isolated_hole.y;
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
    // (nh≤1). Force a full-focus every 4 skipped frames so other ring holes
    // cannot starve forever (Era33 P2 miss_end=0; was 8 → land miss_end).
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
      if (RimScanSkipStreak < 4)
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
    const bool stop_tail_ring_heal =
        !moving && missing_visible_mesh && MissWitnessAgeFrames > 180 &&
        world.GetTimeSinceMotionSec() > 3.0;
    if (stop_tail_ring_heal)
    {
      ColumnWorkItem tail_scan{};
      tail_scan.column =
          glm::ivec2(focus_ground_horiz.x, focus_ground_horiz.z);
      tail_scan.kind = ColumnWorkKind::FirstMesh;
      tail_scan.priority = 104;
      tail_scan.scan_full_focus = true;
      tail_scan.cy = -2;
      exec.Enqueue(tail_scan);
      note_column_flow_drain(5, 4);
    }
    else if (full_scan)
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
    note_column_flow_drain(drain_steps, admit_n);
    // ColPipe P4: one miss owner — Dirty only if not already Dirty/RAA/inflight;
    // FirstMesh ticket (FIFO pin stays in WorldStreaming miss path).
    auto pin_isolated_miss = [&](int first_mesh_prio) {
      if (!found_nearest_missing)
      {
        return;
      }
      const bool already_owned =
          mesh_service.IsChunkMeshDirty(isolated_hole) ||
          mesh_service.IsRemeshAfterApplyPending(isolated_hole) ||
          mesh_service.HasInflightMeshBuild(isolated_hole);
      if (!already_owned &&
          !mesh_service.HasMeshSatisfyingColumnReady(isolated_hole) &&
          !mesh_service.IsPendingGpuApply(isolated_hole))
      {
        mesh_service.MarkDirtyPriority(isolated_hole);
        ++world.GetPhysicsTelemetryMutable().StandRimDirtyN;
      }
      GetColumnFlowExecutor().Enqueue(
          glm::ivec2(isolated_hole.x, isolated_hole.z),
          ColumnWorkKind::FirstMesh, first_mesh_prio);
    };
    if (!moving && found_nearest_missing)
    {
      const int nh = std::max(
          std::abs(isolated_hole.x - focus_ground_horiz.x),
          std::abs(isolated_hole.z - focus_ground_horiz.z));
      if (nh <= 1)
      {
        pin_isolated_miss(110);
      }
    }
    // Era14 TD-ARCH-042 / Era14.1 A1: DesiredStage Dirty FirstMesh — no Imm
    // primary. Minecraft-style HP quota: nearest tops (cy≤3) PreferKick every
    // miss frame (was stand≥2 / cruise sticky≥5 — too late vs miss sticky).
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
      const bool tops_hp =
          missing_visible_mesh && isolated_hole.y <= 3 && no_drawable;
      // Era17 P2: cy≤1 miss always PreferKick (FirstMesh class), even mid FOV.
      const bool tops_firstmesh_class =
          missing_visible_mesh && isolated_hole.y <= 1 && no_drawable;

      // A1 HP quota: PreferKick when GPU stuck; Dirty only via pin_isolated_miss.
      if (tops_hp || tops_firstmesh_class)
      {
        pin_isolated_miss(112);
        if (queued_stuck || kicked_stuck ||
            mesh_service.IsPendingGpuApply(isolated_hole))
        {
          mesh_service.PreferKickPendingGpuQueued(isolated_hole);
        }
      }

      // Era23 I-M9: FirstMesh-class PreferKick every miss-frame (age SLA backup).
      {
        const bool miss_fm_class = IsMissFirstMeshClass(
            missing_visible_mesh, isolated_hole.y, nh);
        if (ShouldPreferKickMissWitnessEarly(missing_visible_mesh,
                                             miss_fm_class))
        {
          if (!(tops_hp || tops_firstmesh_class))
          {
            pin_isolated_miss(110);
          }
          if (queued_stuck || kicked_stuck ||
              mesh_service.IsPendingGpuApply(isolated_hole))
          {
            mesh_service.PreferKickPendingGpuQueued(isolated_hole);
          }
        }
        // Era24 P3 / I-E5: miss_cy>1 residual — pin FirstMesh on witness cy
        // (SoftDefer empty heal must not leave higher-cy hole orphaned).
        const int miss_age_periods_now = MissWitnessAgeFrames / 120;
        const bool stop_miss_heal_ok =
            !pending_near_light ||
            (pending_focus_count <= 2 && MissWitnessAgeFrames > 120);
        const bool force_full_column_pin =
            !moving && stop_miss_heal_ok && MissWitnessAgeFrames > 150 &&
            miss_age_periods_now > MissStuckForcePinPeriod;
        if (missing_visible_mesh && isolated_hole.y > 1 && miss_fm_class)
        {
          ColumnWorkItem pin{};
          pin.column = glm::ivec2(isolated_hole.x, isolated_hole.z);
          pin.kind = ColumnWorkKind::FirstMesh;
          pin.priority = stop_tail_ownership_mode ? 116 : 108;
          // B3: in stop-tail ownership mode we target only pinned column to
          // avoid losing FirstMesh ownership to full-ring admission churn.
          pin.scan_full_focus = !stop_tail_ownership_mode;
          pin.cy = isolated_hole.y;
          // Era22 F2c: when we're about to do a full-column pin, don't enqueue
          // the slice-pin first (scheduler is single-ticket-per-column).
          if (!force_full_column_pin)
          {
            exec.Enqueue(pin);
          }
          if (queued_stuck || kicked_stuck)
          {
            mesh_service.PreferKickPendingGpuQueued(isolated_hole);
          }
        }
      }

      // Era22 I-M8: miss witness age >T (~2 periods / ~4s) → PreferKick witness.
      // Admit bump applied after Finalize (so ComputeMeshWorkAdmission cannot
      // overwrite).
      {
        const int miss_age_periods = MissWitnessAgeFrames / 120;
        if (ShouldMissTimeSlaKick(missing_visible_mesh, miss_age_periods))
        {
          pin_isolated_miss(114);
          mesh_service.PreferKickPendingGpuQueued(isolated_hole);
          mesh_schedule = std::max(mesh_schedule, 12);
        }
      }

      // Era22 F2b/F2c: periodic re-enqueue to avoid stuck-hole starvation.
      // Every new 120f "miss period" → FirstMesh scan.
      // After age threshold → additionally pin FirstMesh on the isolated hole.
      const bool stop_miss_heal_ok =
          !pending_near_light ||
          (pending_focus_count <= 2 && MissWitnessAgeFrames > 120);
      if (!moving && stop_miss_heal_ok)
      {
        const int miss_age_periods = MissWitnessAgeFrames / 120;

        if (miss_age_periods > 0 &&
            miss_age_periods > MissStuckSelfHealPeriod)
        {
          MissStuckSelfHealPeriod = miss_age_periods;
          ColumnWorkItem heal_scan{};
          heal_scan.column = stop_tail_ownership_mode
                                 ? glm::ivec2(isolated_hole.x, isolated_hole.z)
                                 : glm::ivec2(focus_ground_horiz.x,
                                              focus_ground_horiz.z);
          heal_scan.kind = ColumnWorkKind::FirstMesh;
          heal_scan.priority = stop_tail_ownership_mode ? 104 : 90;
          // B3: during stop-tail stuck we switch from ring heal-scan to
          // column ownership so pin ticket is not starved by broad admission.
          heal_scan.scan_full_focus = !stop_tail_ownership_mode;
          // Era22 F2b: use full-height missing slice dirtying, not only
          // remesh-band. This targets stuck unfinished_visual that can span
          // above remesh_max.
          heal_scan.cy = -2;
          exec.Enqueue(heal_scan);
          note_column_flow_drain(3, 3);
        }

        if (MissWitnessAgeFrames > 150 &&
            miss_age_periods > MissStuckForcePinPeriod)
        {
          MissStuckForcePinPeriod = miss_age_periods;
          ColumnWorkItem pin{};
          pin.column = glm::ivec2(isolated_hole.x, isolated_hole.z);
          pin.kind = ColumnWorkKind::FirstMesh;
          pin.priority = stop_tail_ownership_mode ? 124 : 112;
          // B3: full-column pin must keep ownership on isolated hole in
          // stop-tail stuck mode instead of rescanning full focus ring.
          pin.scan_full_focus = !stop_tail_ownership_mode;
          // Full column pin: prevent "single-slice" rebuild that doesn't
          // eliminate focus_missing_mesh for stuck holes.
          pin.cy = -2; // Era22 F2c: whole-column missing slices (full height).
          exec.Enqueue(pin);
          if (stop_tail_ownership_mode)
          {
            note_column_flow_drain(4, 4);
          }
          if (queued_stuck || kicked_stuck)
          {
            mesh_service.PreferKickPendingGpuQueued(isolated_hole);
          }
        }
      }

      const bool stand_rim = !moving && nh <= 3;
      int stand_frames = 0;
      if (stand_rim && no_drawable)
      {
        const bool same_stand =
            isolated_hole.x == StandRimStickyCx &&
            isolated_hole.y == StandRimStickyCy &&
            isolated_hole.z == StandRimStickyCz;
        if (same_stand)
        {
          StandRimStickyFrames =
              std::min(StandRimStickyFrames + 1, 1000000);
        }
        else
        {
          StandRimStickyCx = isolated_hole.x;
          StandRimStickyCy = isolated_hole.y;
          StandRimStickyCz = isolated_hole.z;
          StandRimStickyFrames = 1;
        }
        stand_frames = StandRimStickyFrames;
        // ColPipe P4: PreferKick if GPU stuck; Dirty only once via pin (not every
        // stand frame).
        if (!tops_hp && stand_frames >= 1)
        {
          pin_isolated_miss(108);
        }
        if (!tops_hp && stand_frames >= 1 &&
            (queued_stuck || kicked_stuck))
        {
          mesh_service.PreferKickPendingGpuQueued(isolated_hole);
        }
      }
      else
      {
        StandRimStickyFrames = 0;
        StandRimStickyCx = StandRimStickyCy = StandRimStickyCz = 0;
      }

      // Cruise / far: Kick prefer only — no Immediate escape (F0 + DesiredStage).
      // Era14.1: sticky≥2 (was ≥5) when not already covered by tops_hp.
      if (!tops_hp && !stand_rim && nh <= 5 && sticky_frames >= 2 &&
          (queued_stuck || kicked_stuck))
      {
        mesh_service.PreferKickPendingGpuQueued(isolated_hole);
      }
      if (!tops_hp && !stand_rim && nh <= 3 && sticky_frames >= 2 &&
          pipeline_idle)
      {
        pin_isolated_miss(106);
      }
    }
    else
    {
      mesh_service.UpdateStickyNearestHole(glm::ivec3(0), false);
      StandRimStickyFrames = 0;
      StandRimStickyCx = StandRimStickyCy = StandRimStickyCz = 0;
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
    note_column_flow_drain(moving ? 2 : 3, moving ? 2 : 3);
  }
  // P3: soft cruise clamp — underfeet (or nh<=1 ahead under HoleDrain/Deep).
  // Applied next movement tick via PhysicsTelemetry.StreamSpeedClampScale.
  // Input-first: do not brake player when SLA is already broken; flight clamp
  // is softer than ground.
  {
    StreamSpeedClampInput cin{};
    cin.moving = moving;
    cin.missing_underfeet = missing_underfeet;
    cin.border_scale = 1.0f;
    cin.player_sla_broken = IsInputFirstPlayerSlaBroken(
        world.GetLastMovementFrameMs(),
        world.GetPhysicsTelemetry().PlayerLocomotionBlockMs);
    if (moving)
    {
      const glm::ivec3 fb = world.GetPreferredLoadFocusBlock();
      const glm::vec3 focus_pos(static_cast<float>(fb.x),
                                static_cast<float>(fb.y),
                                static_cast<float>(fb.z));
      cin.border_scale =
          SoftBorderSpeedScale(focus_pos, world.GetWorldBorder());
      const int void_near = world.GetPhysicsTelemetry().DarkFaceVoidNearN;
      const std::optional<int> terrain_top =
          world.FindHighestSolidY(fb.x, fb.z);
      const float terrain_y =
          terrain_top.has_value() ? static_cast<float>(*terrain_top) : 0.0f;
      cin.airborne = fb.y > terrain_y + 4.0f;
      cin.low_alt_frontier = void_near > 50 && fb.y < terrain_y + 24.0f;
      if (!missing_underfeet && found_nearest_missing)
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
            cin.hole_towards = true;
          }
        }
      }
    }
    world.GetPhysicsTelemetryMutable().StreamSpeedClampScale =
        ComputeStreamSpeedClampScale(cin);
  }
  const UnderfeetReservation uf_res = EvaluateUnderfeetReservation(
      underfeet_need, !(missing_underfeet || underfeet_undrawn),
      world.GetPhysicsTelemetry().UnderfeetPendingLight);
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
        clamp_emerge_to_phase();
      }
      sync_cap = std::min(sync_cap < 0 ? 0 : sync_cap, calm_cap.sync_cap);
      sync_budget_ms = std::min(sync_budget_ms, calm_cap.sync_budget_ms);
    }
  }
  ApplyUnderfeetReservationFloors(mesh_drain, mesh_schedule, uf_res);
  // F0: drain-first — ColumnFlow DrainBudget before GPU consume so PreferKick
  // from MarkRelit/tickets lands in the same frame (Sodium one-owner).
  // Single Seam MarkDirty window: DrainRemeshSeamBudget here only (mid-frame
  // sites Enqueue RemeshSeam without SyncIdle MarkDirty).
  {
    auto &exec = GetColumnFlowExecutor();
    exec.DrainBudget(world, std::max(1, column_flow_drain_n),
                     focus_ground_horiz, focus_radius,
                     column_flow_admit_batch);
    const int seam_budget =
        !moving
            ? idle_seam_budget_this_frame
            : std::clamp(2 + (black_sticky > 0 ? black_sticky : 0) +
                             (world.GetPhysicsTelemetry().DarkFaceNearN > 200
                                  ? 2
                                  : 0),
                         1, 8);
    if (seam_budget > 0)
    {
      exec.DrainRemeshSeamBudget(world, seam_budget);
    }
  }
  int gpu_consume_done = 0;
  {
    const MeshWorkAdmission &early_adm = mesh_service.GetMeshWorkAdmission();
    const int consume_drain = FinalizeDrain(mesh_drain, early_adm);
    const int relight_fifo_n = world.GetPhysicsTelemetry().RelightFifoN;
    const int consume_gpu_base =
        std::max(early_adm.gpu_apply_max,
                 std::max(3, std::max(mesh_drain, mesh_schedule)));
    const int underfeet_apply_nh = missing_underfeet ? 0 : 99;
    int consume_gpu = LandRelightGpuApplyFloor(
        relight_fifo_n, pending_focus_count, consume_gpu_base);
    consume_gpu = NearUnderfeetGpuApplyFloor(
        missing_underfeet, underfeet_apply_nh,
        world.GetPhysicsTelemetry().UnderfeetPendingLight, consume_gpu);
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
      ain.nearest_miss_cy = isolated_hole.y;
    }
    else
    {
      ain.nearest_miss_horiz = world.GetPhysicsTelemetry().MissHoriz;
      ain.nearest_miss_cy = world.GetPhysicsTelemetry().MissCy;
    }
    ain.enter_lit_gate = world.IsEnterLitGateActive();
    ain.remesh_queue_n = mesh_service.GetLastDirtyRemeshN();
    MeshWorkAdmission adm = ComputeMeshWorkAdmission(ain);
    {
      const auto &tune = URuntimeTuning::Get();
      const auto &pt = world.GetPhysicsTelemetry();
      RemeshAdmitBackpressureInput bin{};
      bin.stream_pressure = pt.StreamPressure;
      bin.fifo_n = pt.RelightFifoN;
      bin.dirty_n = static_cast<int>(pending_dirty);
      bin.relight_fifo_soft_cap = tune.RelightFifoSoftCap;
      bin.dirty_thrash_soft_cap = tune.DirtyThrashSoftCap;
      bin.fifo_admit_frac = tune.RelightFifoAdmitFrac;
      bin.admit_cap_red = tune.DirtyAdmitCapRed;
      bin.admit_cap_yellow = tune.DirtyAdmitCapYellow;
      bin.miss_active = visual_holes || missing_visible_mesh || missing_underfeet;
      bin.remesh_queue_n = mesh_service.GetLastDirtyRemeshN();
      ApplyRemeshAdmitBackpressure(adm, bin);
    }
    mesh_service.SetMeshWorkAdmission(adm);
    mesh_schedule = FinalizeSchedule(mesh_schedule, adm);
    mesh_drain = FinalizeDrain(mesh_drain, adm);
    // Era22 I-A1: post-Finalize async floor under miss|UV (TD-027 intent).
    {
      const int uv = world.GetPhysicsTelemetry().UnfinishedVisual;
      const int floor = AsyncScheduleFloorUnderMiss(
          missing_visible_mesh || fov_unfinished || uv > 0);
      if (floor > 0)
      {
        mesh_schedule = std::max(mesh_schedule, floor);
      }
    }
    // Era22 I-M8: FocusIngress admit bump after Finalize (age SLA).
    {
      const int miss_age_periods = MissWitnessAgeFrames / 120;
      if (ShouldMissTimeSlaKick(missing_visible_mesh, miss_age_periods))
      {
        MeshWorkAdmission bumped = adm;
        bumped.dirty_admit_budget = std::max(bumped.dirty_admit_budget, 2);
        bumped.softdefer_requeue = std::max(bumped.softdefer_requeue, 1);
        bumped.first_mesh_schedule =
            std::max(bumped.first_mesh_schedule, 4);
        {
          const auto &tune = URuntimeTuning::Get();
          const auto &pt = world.GetPhysicsTelemetry();
          RemeshAdmitBackpressureInput bin{};
          bin.stream_pressure = pt.StreamPressure;
          bin.fifo_n = pt.RelightFifoN;
          bin.dirty_n = static_cast<int>(pending_dirty);
          bin.relight_fifo_soft_cap = tune.RelightFifoSoftCap;
          bin.dirty_thrash_soft_cap = tune.DirtyThrashSoftCap;
          bin.fifo_admit_frac = tune.RelightFifoAdmitFrac;
          bin.admit_cap_red = tune.DirtyAdmitCapRed;
          bin.admit_cap_yellow = tune.DirtyAdmitCapYellow;
          bin.miss_active =
              visual_holes || missing_visible_mesh || missing_underfeet;
          bin.remesh_queue_n = mesh_service.GetLastDirtyRemeshN();
          ApplyRemeshAdmitBackpressure(bumped, bin);
        }
        mesh_service.SetMeshWorkAdmission(bumped);
        mesh_schedule = std::max(mesh_schedule, 12);
      }
    }
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
      // F3: prune remesh Dirty flood every HoleDrain frame (keep_h=1; 2 when deep RemeshQ).
      if (pending_dirty > 200 &&
          (visual_holes || missing_visible_mesh || missing_underfeet))
      {
        const int keep_h =
            mesh_service.GetLastDirtyRemeshN() > 40 ? 2 : 1;
        mesh_service.DropRemeshDirtyBeyondRadius(
            focus_ground_horiz, keep_h, /*keep_cy=*/-1,
            /*remesh_only=*/true);
      }
      // J2/K1 / Era14.1 A2: under miss never clamp SoftDeferHeld requeue below 1
      // (was unconditional min(...,1) which could zero via prior adm=0).
      if ((pending_async >= 12 || pending_gpu_n >= 12) &&
          gpu_consume_done > 0)
      {
        MeshWorkAdmission cut = adm;
        const int floor_rq = missing_visible_mesh ? 1 : 0;
        cut.softdefer_requeue =
            std::max(floor_rq, std::min(cut.softdefer_requeue, 1));
        mesh_service.SetMeshWorkAdmission(cut);
      }
    }
    LastBudget.MaxMeshSchedule = mesh_schedule;
    LastBudget.MaxMeshDrain = mesh_drain;
    LastBudget.AdmissionMode = static_cast<int>(adm.mode);
    LastBudget.DirtyAdmitBudget = adm.dirty_admit_budget;
    LastBudget.GpuApplyMax = adm.gpu_apply_max;
    {
      auto &pt = world.GetPhysicsTelemetryMutable();
      pt.DirtyAdmitBudgetEnd = adm.dirty_admit_budget;
      pt.FirstMeshScheduleCap = adm.first_mesh_schedule;
      pt.RemeshScheduleCap = adm.remesh_schedule;
    }
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
        clamp_emerge_to_phase();
      }
      sync_cap = std::min(sync_cap < 0 ? 0 : sync_cap, calm_cap.sync_cap);
      sync_budget_ms = std::min(sync_budget_ms, calm_cap.sync_budget_ms);
    }
  }
  ApplyUnderfeetReservationFloors(mesh_drain, mesh_schedule, uf_res);
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
