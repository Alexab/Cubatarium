#include "World/Streaming/ChunkEmergeCoordinator.h"
#include "Blocks/BlockRegistry.h"
#include "Render/Camera/Camera.h"
#include "World/Chunks/ChunkManager.h"
#include "World/Core/BlockWorld.h"
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

void UChunkEmergeCoordinator::TickMeshEmerge(UWorld &world)
{
  UBlockRegistry &registry = world.GetBlockRegistry();
  UWorldMeshService &mesh_service = world.GetMeshService();
  const ProceduralSettings &procedural = world.GetProceduralSettings();
  const float movement_speed = world.GetLastMovementSpeed();
  const bool moving =
      movement_speed > procedural.MovementSpeedBoostThreshold;
  const double last_frame_ms = world.GetLastMovementFrameMs();

  const glm::ivec3 focus_block = world.GetPreferredLoadFocusBlock();
  const glm::ivec3 focus_ground =
      UChunkManager::WorldToChunk(focus_block);
  const glm::ivec3 focus_ground_horiz(focus_ground.x, 0, focus_ground.z);
  const int focus_radius = world.GetStreamingFocusRadius();
  mesh_service.SetMeshRebuildFocus(focus_ground_horiz, focus_radius);
  mesh_service.SetDeferMeshUntilLitFn(
      [&world](glm::ivec3 chunk_coord)
      {
        return world.IsPendingLightBeforeMesh(
            glm::ivec2(chunk_coord.x, chunk_coord.z));
      });

  {
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
        // Underwater / in fluid: pull mesh toward the water surface.
        preferred_cy = FloorDiv(column.surfaceBlockY, CHUNK_SIZE);
        prefer_lower_cy = true;
      }
      else if (procedural.FillWater &&
               std::abs(preferred_cy - sea_cy) <= 3)
      {
        // Near sea level (beach / low flight): water surface first.
        preferred_cy = sea_cy;
      }
    }
    mesh_service.SetMeshVerticalPriority(preferred_cy, prefer_lower_cy);
  }
  int mesh_drain = LastBudget.MaxMeshDrain;
  int mesh_schedule = LastBudget.MaxMeshSchedule;

  const size_t pending_dirty = mesh_service.GetDirtyCount();
  const int pending_async = mesh_service.GetAsyncInFlightCount();
  const bool missing_visible_mesh =
      mesh_service.HasMissingGreedyMeshInHorizontalRadius(
          world.GetBlockWorld(), focus_ground_horiz, focus_radius);
  const bool near_mesh_backlog =
      mesh_service.HasDirtyWithinHorizontalRadius(focus_ground_horiz,
                                                 focus_radius) ||
      missing_visible_mesh;
  const bool pending_near_light =
      world.HasPendingLightBeforeMeshNear(focus_ground_horiz, focus_radius);
  const bool near_focus_holes = missing_visible_mesh || pending_near_light;
  // Underfeet = camera column ±1 only. Do NOT key off global Dirty count —
  // Dirty>256 is true almost every frame and the old flood path burned
  // mesh_emerge (sync 10–12 + drain 28+) even when feet already had mesh.
  const bool missing_underfeet =
      mesh_service.HasMissingGreedyMeshInHorizontalRadius(
          world.GetBlockWorld(), focus_ground_horiz, /*radius=*/1);
  const bool pending_underfeet =
      world.HasPendingLightBeforeMeshNear(focus_ground_horiz, /*radius=*/1);
  const bool underfeet_need = missing_underfeet || pending_underfeet;
  mesh_service.SetStarveOutsideFocusMesh(near_focus_holes || underfeet_need);
  // Prefer near mesh work whenever the focus ring has holes — not only
  // underfeet. Otherwise standing in a lit-but-unmeshed / pending-light pocket
  // leaves MaxHorizontalDist unlimited and far Dirty starves the fill.
  if (missing_underfeet && !pending_underfeet)
  {
    mesh_service.SetMeshScheduleMaxHorizontalDist(1);
    mesh_service.SetMeshScheduleOverflowPerFrame(0);
  }
  else if (underfeet_need)
  {
    mesh_service.SetMeshScheduleMaxHorizontalDist(1);
    mesh_service.SetMeshScheduleOverflowPerFrame(moving ? 6 : 4);
  }
  else if (near_focus_holes)
  {
    mesh_service.SetMeshScheduleMaxHorizontalDist(focus_radius);
    mesh_service.SetMeshScheduleOverflowPerFrame(moving ? 4 : 2);
  }
  else
  {
    mesh_service.SetMeshScheduleMaxHorizontalDist(-1);
    mesh_service.SetMeshScheduleOverflowPerFrame(0);
  }

  if (moving && near_mesh_backlog)
  {
    if (pending_dirty > 48 || pending_async > 16)
    {
      mesh_drain = std::max(mesh_drain, 16);
      mesh_schedule = std::max(mesh_schedule, 16);
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
      mesh_schedule = std::max(mesh_schedule, dirty_floor);
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
    const int flush_n =
        pending_dirty < 16 ? 64 : (pending_dirty < 48 ? 32 : 24);
    world.FlushPendingRelightMeshColumns(flush_n);
  }
  // Already-meshed focus columns with sky=0 never remesh unless relight is
  // re-queued (stuck black after premature light=0 mesh). Also: pending+sky
  // (neighbor lit) and missing GreedyCache after gate clear.
  {
    int recover_n = moving ? 4 : 6;
    if (underfeet_need)
    {
      recover_n = moving ? 8 : 12;
    }
    else if (pending_near_light || missing_visible_mesh)
    {
      recover_n = moving ? 6 : 8;
    }
    world.RecoverUnlitFocusMeshes(recover_n);
  }

  // Place-equivalent: sync-rebuild only solid slices around the player
  // (1 chunk below, 2 above; horizontal ±1). Rest of the vertical stack
  // stays on Dirty/async — do not burn the frame on full columns.
  if (underfeet_need)
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
    // Hitch frames: at most 2 immediate rebuilds — logs showed emerge 270–414ms.
    // Healthy / mild frames: fill underfeet aggressively (empty feet at 100 FPS).
    const int kMaxImmediateUnderfeet =
        last_frame_ms > 24.0 ? 2 : (moving ? 4 : 6);
    for (int dz = -1; dz <= 1 && immediate < kMaxImmediateUnderfeet; ++dz)
    {
      for (int dx = -1; dx <= 1 && immediate < kMaxImmediateUnderfeet; ++dx)
      {
        // Camera column first (dx=dz=0), then ring.
        const int ring = std::max(std::abs(dx), std::abs(dz));
        if (ring > 0 && immediate >= (last_frame_ms > 24.0 ? 1 : 3))
        {
          // Keep most of the budget for under-camera slices.
          continue;
        }
        for (int cy : cy_order)
        {
          if (immediate >= kMaxImmediateUnderfeet)
          {
            break;
          }
          const glm::ivec3 coord(focus_ground.x + dx, cy, focus_ground.z + dz);
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
    sync_cap = std::max(sync_cap, moving ? 4 : 6);
    mesh_schedule = std::max(mesh_schedule, moving ? 12 : 16);
    mesh_drain = std::max(mesh_drain, moving ? 12 : 16);
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
      (underfeet_need && last_frame_ms <= 16.0) ? 10.0
      : (last_frame_ms > 24.0)                    ? 4.0
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
