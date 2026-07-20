#include "World/Streaming/ChunkEmergeCoordinator.h"
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
  UBlockRegistry &registry = world.GetBlockRegistry();
  UWorldMeshService &mesh_service = world.GetMeshService();
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
  // Soft-defer: remesh of existing while PendingLight waits MarkRelit.
  // First missing mesh allowed anywhere in focus (prevents empty flight strips
  // while light lags). Outside focus first mesh waits MayMesh/LitReady.
  mesh_service.SetDeferMeshUntilLitFn(
      [&world, &mesh_service, focus_ground_horiz,
       focus_radius](glm::ivec3 chunk_coord)
      {
        const bool pending = world.IsPendingLightBeforeMesh(
            glm::ivec2(chunk_coord.x, chunk_coord.z));
        if (mesh_service.HasGreedyMesh(chunk_coord))
        {
          return pending; // defer remesh while unlit
        }
        const int horiz =
            std::max(std::abs(chunk_coord.x - focus_ground_horiz.x),
                     std::abs(chunk_coord.z - focus_ground_horiz.z));
        if (horiz <= focus_radius)
        {
          return false; // first mesh in focus always
        }
        const glm::ivec3 ground(chunk_coord.x, 0, chunk_coord.z);
        return !world.MayMeshColumn(ground, /*underfeet_preview=*/false);
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
  // visual_holes = missing mesh only; near_focus_holes kept for legacy paths
  // that still want light-debt urgency for relight (not starve).
  const bool visual_holes = missing_visible_mesh;
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

  {
    const URuntimeTuning &tune = URuntimeTuning::Get();
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
  }
  int mesh_drain = LastBudget.MaxMeshDrain;
  int mesh_schedule = LastBudget.MaxMeshSchedule;
  const size_t pending_dirty = mesh_service.GetDirtyCount();
  const int pending_async = mesh_service.GetAsyncInFlightCount();

  // Starve keep-shell remesh only when focus/underfeet still MISSING mesh.
  // Including pending_light / focus_pressure_mode latched starve for the whole
  // flight (pending stays 30–60) and permanently blocked trail/sea Dirty →
  // transverse blank and black strips with water already in RAM.
  mesh_service.SetStarveOutsideFocusMesh(visual_holes || missing_underfeet);
  mesh_service.SetStarveRemeshForHoles(visual_holes || missing_underfeet);
  // One-shot pipeline flush when holes appear with saturated async — not every
  // frame (Cancel+reschedule thrash hung flight-sim wall time).
  {
    static bool flushed_for_holes = false;
    if (!(visual_holes || missing_underfeet))
    {
      flushed_for_holes = false;
    }
    else if (!flushed_for_holes && pending_async >= 24)
    {
      mesh_service.CancelAsyncInFlightKeepDirty();
      flushed_for_holes = true;
    }
  }
  // Healthy Dirty flush: raise outside-focus schedule so keep-shell remesh
  // cannot plateau ~450 forever (kMaxOutsideFocusPerFrame=2 alone).
  if (!visual_holes && !missing_underfeet && pending_dirty > 200 &&
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
  if (missing_underfeet && !moving)
  {
    // Standing: camera feet first, no far overflow.
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

  // Healthy flight with no visual holes: flush Dirty so pressure can leave Red
  // (Dirty plateaus ~700 trapped Red when exit required dirty<=500).
  // Skip while focus relight debt is high — flush starved MarkRelit (pending~50).
  if (!visual_holes && !missing_underfeet && !pending_near_light &&
      pending_dirty > 200 && last_frame_ms <= 28.0)
  {
    mesh_drain = std::max(mesh_drain, moving ? 16 : 22);
    mesh_schedule = std::max(mesh_schedule, moving ? 16 : 22);
  }
  if (!visual_holes && !pending_near_light && pending_dirty > 400 &&
      last_frame_ms <= 20.0)
  {
    mesh_drain = std::max(mesh_drain, 24);
    mesh_schedule = std::max(mesh_schedule, 24);
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
  if (!moving && pending_dirty > 32)
  {
    mesh_drain = std::max(mesh_drain, 20);
    mesh_schedule = std::max(mesh_schedule, 12);
  }
  else if (!moving && pending_dirty > 8)
  {
    mesh_drain = std::max(mesh_drain, 16);
    mesh_schedule = std::max(mesh_schedule, 10);
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
    mesh_drain = std::max(mesh_drain, 24);
    mesh_schedule = std::max(mesh_schedule, 24);
  }

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
    if (pending_focus > 0)
    {
      flush_n = std::max(flush_n, pending_focus > 15 ? 48 : 32);
    }
    world.FlushPendingRelightMeshColumns(flush_n);
  }

  // Hitch: cap *schedule* (snapshot cost). Keep drain higher when holes so
  // completed async frees pipeline slots for reserved focus-missing work.
  if (last_frame_ms > 24.0)
  {
    mesh_schedule = std::min(mesh_schedule, visual_holes || missing_underfeet ? 10 : 8);
    mesh_drain =
        std::min(mesh_drain, visual_holes || missing_underfeet ? 20 : 10);
  }
  else if (last_frame_ms > 16.0)
  {
    mesh_schedule = std::min(mesh_schedule, 12);
    mesh_drain = std::min(mesh_drain, visual_holes || missing_underfeet ? 16 : 12);
  }

  // Flight FPS guard: clamp schedule while moving; drain may stay higher so
  // MeshAsync does not sit at pipeline depth while focus is missing mesh.
  // Exception: Dirty flush with no visual holes may exceed fly_cap.
  if (moving && (visual_holes || missing_underfeet || pending_dirty <= 400))
  {
    int fly_cap = last_frame_ms > 20.0 ? 10 : 12;
    fly_cap = ApplyPressureCap(fly_cap, pressure.mesh_fly_cap);
    mesh_schedule = std::min(mesh_schedule, fly_cap);
    if (visual_holes || missing_underfeet)
    {
      mesh_drain = std::min(mesh_drain, std::max(fly_cap, 16));
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
         recover_watchdog_frames >= 4);
    if (recover_now && recover_n > 0)
    {
      world.RecoverUnlitFocusMeshes(recover_n);
      if (visual_holes || pending_near_light)
      {
        world.AdmitFocusMeshIngress(std::max(2, recover_n));
      }
      recover_watchdog_frames = 0;
    }
  }

  // Sync-rebuild missing solid slices: underfeet always; idle focus holes too
  // (holes=1 + underfeet=0 used to wait forever on async while MeshAsync=42).
  // Fly-wide sync was tried but hitch wall~100ms; keep async+Recover for cruise.
  const bool idle_focus_sync =
      !moving && missing_visible_mesh && last_frame_ms <= 20.0;
  if (underfeet_need || idle_focus_sync)
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
    // Underfeet: camera ±1. Idle focus holes: whole focus ring.
    const int horiz_r = underfeet_need ? 1 : focus_radius;
    const int kMaxImmediate =
        underfeet_need
            ? (last_frame_ms > 20.0 ? 1 : (moving ? 2 : 4))
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
          const int ring = std::max(std::abs(dx), std::abs(dz));
          if (underfeet_need && ring > 0 &&
              (moving || immediate >= (last_frame_ms > 20.0 ? 1 : 2)))
          {
            // While moving, only sync the camera column for underfeet path.
            continue;
          }
          for (int cy : cy_order)
          {
            if (immediate >= kMaxImmediate)
            {
              break;
            }
            const glm::ivec3 coord(focus_ground.x + dx, cy,
                                   focus_ground.z + dz);
            // Never sync-bake PendingLight — RebuildChunkImmediate bypasses
            // soft-defer and ships light=0. First mesh goes through Dirty
            // (Recover / commit preview).
            if (world.IsPendingLightBeforeMesh(glm::ivec2(coord.x, coord.z)))
            {
              continue;
            }
            if (mesh_service.HasGreedyMesh(coord))
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
            mesh_service.RebuildChunkImmediate(world.GetBlockWorld(), registry,
                                               coord);
            ++immediate;
          }
        }
      }
    }
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
    // Idle/healthy: fill underfeet fast. Hitch: small sync only.
    if (last_frame_ms > 24.0)
    {
      sync_cap = std::max(sync_cap, 2);
    }
    else if (last_frame_ms > 16.0)
    {
      sync_cap = std::max(sync_cap, moving ? 4 : 6);
    }
    else
    {
      const int missing_cap = moving ? 8 : 10;
      sync_cap = std::max(sync_cap, missing_cap);
    }
    mesh_schedule = std::max(mesh_schedule, moving ? 12 : 16);
    mesh_drain = std::max(mesh_drain, moving ? 12 : 16);
  }
  else if (missing_visible_mesh || pending_near_light)
  {
    // Forward wedge sync (dist 2) without flooding schedule when async idle.
    sync_cap = std::max(sync_cap, moving ? 4 : 8);
    mesh_schedule = std::max(mesh_schedule, moving ? 12 : 20);
    mesh_drain = std::max(mesh_drain, moving ? 12 : 24);
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
  const double sync_budget_ms =
      (underfeet_need && last_frame_ms <= 16.0)             ? 10.0
      : (!moving && missing_visible_mesh && last_frame_ms <= 20.0) ? 8.0
      : (last_frame_ms > 24.0)                              ? 4.0
                                                            : 6.0;
  const MeshRebuildTickStats tick_stats = mesh_service.RebuildDirtyChunksWithStats(
      world.GetBlockWorld(), registry, mesh_drain, mesh_schedule,
      /*force_sync=*/false, sync_cap, sync_budget_ms);
  mesh_service.DrainAsyncMeshResults(world.GetBlockWorld(), registry, mesh_drain);

#ifndef NDEBUG
  ++gMeshTelemetryTick;
  if (gMeshTelemetryTick % 60 == 0)
  {
    std::cout << "[MeshEmerge] dirty=" << mesh_service.GetDirtyCount()
              << " inflight=" << mesh_service.GetAsyncInFlightCount()
              << " sync=" << tick_stats.SyncRebuilt
              << " completed=" << tick_stats.Completed
              << " scheduled=" << tick_stats.Scheduled << std::endl;
  }
#endif
}

} // namespace cutum
