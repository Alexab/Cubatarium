#include "World/Streaming/WorldStreaming.h"
#include "World/Streaming/ColumnFlowExecutor.h"
#include "World/Streaming/FocusIngressPolicy.h"
#include "World/Streaming/IdleRecoveryPolicy.h"
#include "World/Streaming/SeedDecisionPolicy.h"
#include "World/Streaming/MemoryBudgetController.h"
#include "WorldGen/Pipelines/ComposableWorldGenerator.h"
#include "World/Math/GridMath.h"
#include "World/Streaming/ChunkEmergeCoordinator.h"
#include "World/Physics/ChunkPhysicsSeed.h"
#include "World/Diagnostics/FramePerfMonitor.h"
#include "World/Lighting/LightingSeedBackendFactory.h"
#include "Render/Backend/RenderBackendCaps.h"
#include "App/Settings/RenderSettings.h"
#include "Blocks/BlockRegistry.h"
#include "Creatures/Player/PlayerCapsule.h"
#include "Render/Camera/Camera.h"
#include "World/Chunks/Chunk.h"
#include "World/Chunks/ChunkManager.h"
#include "World/Chunks/StreamingAltitudePolicy.h"
#include "World/Chunks/TerrainColumnUtil.h"
#include "World/Core/BlockWorld.h"
#include "World/Core/World.h"
#include "World/Math/GridMath.h"
#include "World/Mesh/WorldMeshService.h"
#include "World/Objects/ObjectUtil.h"
#include "World/Persistence/WorldPersistence.h"
#include "WorldGen/Core/IUWorldGenPipeline.h"
#include "WorldGen/Core/ProceduralSettings.h"
#include "WorldGen/Pipelines/ComposableWorldGenerator.h"
#include "WorldGen/Stages/WorldGenStages.h"
#include "World/Core/RuntimeTuning.h"
#include "App/Platform/Log.h"
#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstdint>
#ifdef _WIN32
#ifndef NOMINMAX
#define NOMINMAX
#endif
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>
#include <psapi.h>
#endif

namespace cutum
{

namespace
{

constexpr float kBadFrameMs = 24.0f;
constexpr double kNearCompleteBudgetMs = 10.0;
constexpr double kFarStreamingBudgetMs = 5.0;
constexpr int kRelightBacklogStuckWindowMs = 1500;
constexpr int kRelightBgClampCooldownMs = 400;
constexpr int kAdaptiveRdMin = 3;
constexpr double kAdaptiveRdHysteresisSec = 2.5;

/// SoftDefer / rim FirstMesh repair anchor: nearest miss witness when present.
/// Manual 191432 exit stuck miss_horiz=2–3 while tickets stayed on focus_xz.
inline glm::ivec2 RepairColumnFromMissWitness(const PhysicsTelemetry &phys,
                                              glm::ivec2 focus_xz)
{
  if (phys.FocusMissingMesh != 0 && phys.MissHoriz > 0)
  {
    return glm::ivec2(phys.MissCx, phys.MissCz);
  }
  return focus_xz;
}

struct KeepPrewarmGate
{
  bool allow{false};
  int max_ops{0};
};

KeepPrewarmGate EvaluateKeepPrewarmGate(double frame_ms, int gen_backlog_total,
                                        int mesh_async, size_t mesh_dirty,
                                        bool near_mesh_backlog)
{
  KeepPrewarmGate gate;
  // Protect visual path only when near is actually starving AND gen is busy.
  // mesh_async saturation alone must not block keep prewarm (keep does not mesh).
  if (near_mesh_backlog && (gen_backlog_total > 6 || mesh_dirty > 96))
  {
    return gate;
  }
  if (gen_backlog_total > 16 || frame_ms > 32.0)
  {
    return gate;
  }
  // Soft prewarm: allow 1 op under moderate load so keep-shell actually warms.
  if (!near_mesh_backlog && gen_backlog_total < 12 && frame_ms < 24.0)
  {
    gate.allow = true;
    gate.max_ops = frame_ms < 14.0 && gen_backlog_total < 6 ? 2 : 1;
    return gate;
  }
  if (frame_ms < 18.0 && gen_backlog_total < 8 && mesh_dirty < 128)
  {
    gate.allow = true;
    gate.max_ops = 1;
    (void)mesh_async;
  }
  return gate;
}

int gLastBgRelightBacklog = 0;
std::chrono::steady_clock::time_point gLastBgRelightBacklogTs{};
std::chrono::steady_clock::time_point gBgRelightClampUntil{};

float QueryTerrainSurfaceWorldY(UWorld &world, const glm::vec3 &eye)
{
  UBlockRegistry &registry = world.GetBlockRegistry();
  const glm::ivec3 block = WorldPosToBlock(eye);
  const int surface_block_y = FindTopSolidSurfaceY(
      world.GetBlockWorld(), registry, block.x, block.z,
      world.GetProceduralSettings().MaxHeight);
  if (surface_block_y < 0)
  {
    return 0.0f;
  }
  return BlockTopY(surface_block_y);
}

} // namespace

UWorldStreaming::UWorldStreaming()
    : EmergeCoordinator(std::make_unique<UChunkEmergeCoordinator>())
{
}

UWorldStreaming::~UWorldStreaming() = default;

void UWorldStreaming::EnsureStreamer(UBlockWorld &blockWorld,
                                     UBlockRegistry &registry, uint32_t seed,
                                     const ProceduralSettings &settings)
{
  if (!Streamer)
  {
    Streamer = std::make_unique<UChunkStreamer>(blockWorld, registry, seed, 0,
                                                settings.MaxHeight);
    Streamer->SetRingGateEnabled(settings.RingGateEnabled);
  }
}

void UWorldStreaming::SetRenderDistance(int distance)
{
  if (Streamer)
  {
    Streamer->SetRenderDistance(distance);
  }
}

void UWorldStreaming::SetStreamerMaxLoadOpsPerFrame(int value)
{
  if (Streamer)
  {
    Streamer->SetMaxLoadOpsPerFrame(value);
  }
}

void UWorldStreaming::PrepareEnterGameSession(UWorld &world)
{
  if (!world.BlockRegistry)
  {
    return;
  }

  if (auto user = world.GetCurrentUser())
  {
    world.ApplyUserToCamera(user);
  }
  else
  {
    world.ApplySpawnToCamera();
  }
  world.ConsumeSpawnAreaPreparedByCooperativeLoad();
  world.BeginEnterGameMeshBurst(5);
}

void UWorldStreaming::WarmupSpawnAreaForEnterGame(UWorld &world)
{
  PrepareEnterGameSession(world);
  world.WarmupVisibleListAtCamera();
}

void UWorldStreaming::InitChunkScheduler(UWorld &world)
{
  if (!world.BlockRegistry)
  {
    // Destroy scheduler first so workers cannot touch a freed populator.
    ChunkScheduler.reset();
    ChunkPopulator.reset();
    return;
  }
  // Cancel old pool before replacing the populator it references. Never join
  // forever: in-flight populate/seal can outlive a short idle wait and used to
  // hang SaveMetadata ("Saving world...") via EnsureStreamingActiveAfter…
  if (ChunkScheduler)
  {
    ChunkScheduler->CancelAllPending(std::chrono::milliseconds(0));
    if (ChunkScheduler->WaitForWorkersIdle(std::chrono::milliseconds(500)))
    {
      ChunkScheduler.reset();
      ChunkPopulator.reset();
    }
    else
    {
      CubatariumLogInfo(
          "Streaming",
          "InitChunkScheduler: workers busy after cancel — abandon old pool");
      ChunkScheduler->ShutdownForProcessExit(std::chrono::milliseconds(0));
      (void)ChunkScheduler.release();
      (void)ChunkPopulator.release();
    }
  }
  ChunkPopulator = std::make_unique<UPipelineChunkPopulator>(
      *world.BlockRegistry, world.ObjectLibrary, world.WorldgenOwnerPackId);
  ChunkScheduler =
      std::make_unique<UChunkLoadScheduler>(*ChunkPopulator, ChunkGenTokens);
  ChunkScheduler->SetMarkDirtyFn(
      [this, &world](glm::ivec3 coord, int min_y, int max_y, bool fluid_sealed)
      {
        const glm::ivec3 ground(coord.x, 0, coord.z);
        if (Streamer)
        {
          Streamer->NotifyChunkCommitted(coord);
        }
        const ProceduralSettings &settings = world.GetProceduralSettings();
        const glm::ivec3 focus_ground =
            UChunkManager::WorldToChunk(world.GetPreferredLoadFocusBlock());
        const int focus_radius = world.GetStreamingFocusRadius();
        const bool near_focus =
            std::abs(coord.x - focus_ground.x) <= focus_radius &&
            std::abs(coord.z - focus_ground.z) <= focus_radius;
        DeferredPhysicsSeedQueue.push_back(coord);
        if (settings.FillWater)
        {
          // V_fluid: IntraChunkSeal once (populate XOR deferred drain). Never
          // sync IntraChunk on commit — single seal was 100–780ms of
          // commit_seal_ms and dominated hole-frame stream spikes (CB).
          if (!fluid_sealed)
          {
            DeferredIntraChunkSealNeeded.insert(coord);
          }
          DeferredShoreSealQueue.push_back(coord);
        }
        world.SetColumnEmergeState(ground, ColumnEmergeState::Lighting);
        // Mesh gate band = occupied ∪ sea (not 0..MaxHeight — that flooded Dirty
        // and remeshed unlit slices). MarkRelit remeshes lit ∪ pending ∪ sea.
        int dirty_min = std::max(0, min_y);
        int dirty_max = std::min(settings.MaxHeight, max_y);
        if (settings.FillWater)
        {
          dirty_min =
              std::min(dirty_min, std::max(0, settings.SeaLevel - CHUNK_SIZE));
          dirty_max = std::max(
              dirty_max,
              std::min(settings.MaxHeight, settings.SeaLevel + CHUNK_SIZE * 2));
        }
        if (near_focus)
        {
          const glm::ivec3 focus_block = world.GetPreferredLoadFocusBlock();
          dirty_min =
              std::min(dirty_min, std::max(0, focus_block.y - CHUNK_SIZE));
          dirty_max = std::max(
              dirty_max,
              std::min(settings.MaxHeight, focus_block.y + CHUNK_SIZE * 2));
        }
        if (dirty_max < dirty_min)
        {
          dirty_min = 0;
          dirty_max = settings.MaxHeight;
        }
        if (!world.IsLightingRelightDeferred())
        {
          // Relight Y: sea∪occupied∪player up to sky (not 0..floor). Full
          // 0..MaxHeight columns capped async throughput (~3/s) while flight
          // ingress piled pending_light_focus to 60–80.
          // Far columns: occupied band only — full MaxHeight doubled per-column
          // relight time and starved focus throughput.
          const int relight_min =
              settings.FillWater
                  ? std::max(0, std::min(dirty_min,
                                         settings.SeaLevel - CHUNK_SIZE * 2))
                  : dirty_min;
          // Near focus: mesh gate band + one slice pad for skylight ingress.
          const int relight_max =
              near_focus
                  ? std::min(settings.MaxHeight, dirty_max + CHUNK_SIZE * 2)
                  : std::min(settings.MaxHeight,
                             std::max(dirty_max, settings.SeaLevel +
                                                      CHUNK_SIZE * 2));
          const int horiz =
              std::max(std::abs(coord.x - focus_ground.x),
                       std::abs(coord.z - focus_ground.z));
          const bool underfeet = horiz <= 1;
          const bool admit_far_dirty =
              LastPressureCaps.level == StreamingPressureLevel::Green;
          // Flat / no lit-gate: mesh immediately (no PendingLight contract).
          if (!world.RequiresLightingLitGate())
          {
            world.SetColumnEmergeState(ground, ColumnEmergeState::LitReady);
            if (near_focus)
            {
              world.MeshService->MarkTerrainChunkMeshDirtySeamedPriority(
                  ground, dirty_min, dirty_max,
                  /*include_horizontal_neighbors=*/true);
            }
            else if (admit_far_dirty)
            {
              world.MeshService->MarkTerrainChunkMeshDirtySeamed(
                  ground, dirty_min, dirty_max, false);
            }
          }
          else
          {
            // Full lighting: SeedDecision (V3) — cruise near-focus may try
            // budgeted sync seed; fail → PendingLight (never silent LitReady).
            const bool neighborhood_ok = world.CanSeedSkylightAtCommit(ground);
            const double commit_frame_ms = world.GetLastMovementFrameMs();
            const bool moving_cruise =
                world.LastMovementSpeed >=
                settings.MovementPrefetchThreshold;
            const SeedDecision seed_decision =
                EvaluateSeedDecision(SeedDecisionInput{
                    underfeet, near_focus, neighborhood_ok, moving_cruise,
                    commit_frame_ms, world.PhysicsTelemetryData.VisualHoles,
                    LastPendingLightFocus});
            const bool relight_priority = seed_decision.priority_fifo;
            auto enqueue_pending_light = [&]() {
              // PendingLightBeforeMesh gate must match the async relight range.
              // If relight_min/max is wider than dirty_min/max, SoftDefer can
              // keep finalize_pending_gate=false for too long → pending stuck.
              const int enqueue_relight_min =
                  std::max(0, dirty_min - 1 /* air-neighbor pad */);
              const int enqueue_relight_max =
                  std::min(settings.MaxHeight, dirty_max + 1 /* air-neighbor pad */);
              world.Persistence->EnqueueTerrainColumnRelight(
                  ground.x * CHUNK_SIZE, ground.z * CHUNK_SIZE, relight_priority,
                  enqueue_relight_min, enqueue_relight_max);
              world.NotePendingLightBeforeMesh(ground, dirty_min, dirty_max);
              if (near_focus)
              {
                world.SetColumnEmergeState(ground, ColumnEmergeState::Lighting);
              }
              else if (admit_far_dirty)
              {
                world.MeshService->MarkTerrainChunkMeshDirtySeamed(
                    ground, dirty_min, dirty_max, false);
                world.SetColumnEmergeState(ground, ColumnEmergeState::Meshing);
              }
              else
              {
                world.SetColumnEmergeState(ground, ColumnEmergeState::Lighting);
              }
            };
            if (seed_decision.try_sync_seed)
            {
              const RenderBackendCaps &caps = GetActiveRenderBackendCaps();
              auto backend = SelectLightingSeedBackend(world, relight_min,
                                                       relight_max, caps);
              LightingSeedResult seed =
                  backend->TrySeedColumnAtCommit(ground,
                                                 seed_decision.budget_ms);
              if (seed.applied)
              {
                world.SetColumnEmergeState(ground, ColumnEmergeState::LitReady);
                world.MeshService->MarkTerrainChunkMeshDirtySeamedPriority(
                    ground, dirty_min, dirty_max,
                    /*include_horizontal_neighbors=*/true);
              }
              else
              {
                enqueue_pending_light();
              }
            }
            else
            {
              enqueue_pending_light();
            }
          }
        }
        else if (near_focus)
        {
          world.SetColumnEmergeState(ground, ColumnEmergeState::LitReady);
          world.MeshService->MarkTerrainChunkMeshDirtySeamedPriority(
              ground, dirty_min, dirty_max, true);
        }
        else if (LastPressureCaps.level == StreamingPressureLevel::Green)
        {
          world.SetColumnEmergeState(ground, ColumnEmergeState::LitReady);
          world.MeshService->MarkTerrainChunkMeshDirtySeamed(
              ground, dirty_min, dirty_max, false);
        }
        else
        {
          world.SetColumnEmergeState(ground, ColumnEmergeState::LitReady);
        }
      });
  world.Persistence->EnsureChunkIoInitialized();
}

void UWorldStreaming::RefreshStreamingPressure(UWorld &world)
{
  const glm::ivec3 focus_block = world.GetPreferredLoadFocusBlock();
  const glm::ivec3 focus_ground = UChunkManager::WorldToChunk(focus_block);
  const glm::ivec3 focus_horiz(focus_ground.x, 0, focus_ground.z);
  const int focus_radius = world.GetStreamingFocusRadius();
  const bool missing_near =
      world.GetMeshService().HasMissingGreedyMeshInHorizontalRadius(
          world.GetBlockWorld(), focus_horiz, focus_radius);
  if (missing_near)
  {
    glm::ivec3 miss_coord{0};
    if (world.GetMeshService().FindNearestMissingGreedyMesh(
            world.GetBlockWorld(), focus_horiz, focus_radius, miss_coord))
    {
      world.PhysicsTelemetryData.MissCx = miss_coord.x;
      world.PhysicsTelemetryData.MissCy = miss_coord.y;
      world.PhysicsTelemetryData.MissCz = miss_coord.z;
      world.PhysicsTelemetryData.MissHoriz =
          std::max(std::abs(miss_coord.x - focus_horiz.x),
                   std::abs(miss_coord.z - focus_horiz.z));
    }
  }
  else
  {
    world.PhysicsTelemetryData.MissCx = 0;
    world.PhysicsTelemetryData.MissCy = 0;
    world.PhysicsTelemetryData.MissCz = 0;
    world.PhysicsTelemetryData.MissHoriz = 0;
  }
  // Underfeet is a subset of focus — skip second full resident scan when focus
  // already reports no missing mesh (CB stream_ms on no-hole fly).
  const bool missing_underfeet =
      missing_near &&
      world.GetMeshService().HasMissingGreedyMeshInHorizontalRadius(
          world.GetBlockWorld(), focus_horiz, /*radius=*/1);
  const int pending_light_focus =
      world.CountPendingLightBeforeMeshNear(focus_horiz, focus_radius);
  const bool pending_underfeet =
      world.HasPendingLightBeforeMeshNear(focus_horiz, /*radius=*/1);

  StreamingPressureInput in;
  in.pending_light =
      static_cast<int>(world.GetPendingLightBeforeMeshCount());
  in.dirty = static_cast<int>(world.GetMeshService().GetDirtyCount());
  in.frame_ms = world.GetLastMovementFrameMs();
  // Pressure focus mode: visual holes only (pending light is light_debt).
  in.visual_holes = missing_near;
  in.underfeet_need = missing_underfeet || pending_underfeet;
  in.pending_light_focus = pending_light_focus;
  LastPendingLightFocus = pending_light_focus;
  LastPressureCaps = EvaluateStreamingPressure(in, PressureState);

  world.PhysicsTelemetryData.StreamPressure =
      static_cast<int>(LastPressureCaps.level);
  world.PhysicsTelemetryData.PendingLightFocus = pending_light_focus;
  const int sticky_remesh =
      world.CountBlackStickyFocusMeshes(focus_ground, focus_radius);
  const int pending_dark =
      world.CountPendingDarkFocusMeshes(focus_ground, focus_radius);
  const int dark_preview = sticky_remesh + pending_dark;
  // Cruise: CountUnfinishedVisualNear/ByFacing walk the whole focus ring with
  // IsTerrainChunkComplete + IsColumnRenderReady — ~5–9ms of stream_ms on
  // no-hole fly (CB wall_ms_no_holes). Idle/stop still needs the full count
  // for F2 fd_end / not_ready. Holes still use HasMissing above.
  const bool moving_for_telemetry =
      world.GetLastMovementSpeed() >
      world.GetProceduralSettings().MovementPrefetchThreshold;
  // Cruise: sample unfinished every N frames. Hold last SoT count between
  // samples for UnfinishedVisual / FocusNotRenderReady (ARCH gates).
  // FocusPressure = pending+dirty proxy for scheduler pressure only.
  const int focus_dirty_chunks =
      world.GetMeshService().CountDirtyWithinHorizontalRadius(focus_horiz,
                                                              focus_radius);
  static int unfinished_sample_cd = 0;
  static int last_unfinished_visual = 0;
  int unfinished_visual = 0;
  int focus_pressure = 0;
  if (!moving_for_telemetry)
  {
    last_unfinished_visual =
        world.CountUnfinishedVisualNear(focus_horiz, focus_radius);
    unfinished_visual = last_unfinished_visual;
    focus_pressure = unfinished_visual;
    unfinished_sample_cd = 0;
  }
  else if (--unfinished_sample_cd <= 0)
  {
    last_unfinished_visual =
        world.CountUnfinishedVisualNear(focus_horiz, focus_radius);
    unfinished_visual = last_unfinished_visual;
    focus_pressure = unfinished_visual;
    unfinished_sample_cd = 8;
  }
  else
  {
    unfinished_visual = last_unfinished_visual;
    if (missing_near && unfinished_visual == 0)
    {
      unfinished_visual = 1;
    }
    focus_pressure =
        pending_light_focus +
        (focus_dirty_chunks > 0 ? std::min(focus_dirty_chunks, 8) : 0);
    if (focus_pressure < unfinished_visual)
    {
      focus_pressure = unfinished_visual;
    }
  }
  world.PhysicsTelemetryData.FocusStickyRemesh = sticky_remesh;
  world.PhysicsTelemetryData.FocusPendingDark = pending_dark;
  world.PhysicsTelemetryData.FocusDarkMesh = dark_preview;
  world.PhysicsTelemetryData.FocusNotRenderReady = unfinished_visual;
  world.PhysicsTelemetryData.FocusPressure = focus_pressure;
  world.PhysicsTelemetryData.FocusDirtyChunks = focus_dirty_chunks;
  // Actual baked-dark vertices near camera (not PendingLight proxy).
  // Split stale (mesh dark, field lit) vs void-edge (both 0) for ARCH_D3.
  {
    world.PhysicsTelemetryData.DarkFaceNearN = 0;
    world.PhysicsTelemetryData.DarkFaceStaleNearN = 0;
    world.PhysicsTelemetryData.DarkFaceVoidNearN = 0;
    world.PhysicsTelemetryData.DarkFaceDist = 0.0;
    if (const auto camera = world.GetCurrentUserCamera())
    {
      UChunkMeshCache::DarkFaceHit hit{};
      int near_n = 0;
      int stale_n = 0;
      int void_n = 0;
      if (world.GetMeshService().GetCache().FindNearestDarkFaceNear(
              camera->GetPosition(), /*max_dist=*/24.0f, /*chunk_radius=*/2,
              hit, &near_n, &world.GetBlockWorld(), &stale_n, &void_n))
      {
        world.PhysicsTelemetryData.DarkFaceNearN = near_n;
        world.PhysicsTelemetryData.DarkFaceStaleNearN = stale_n;
        world.PhysicsTelemetryData.DarkFaceVoidNearN = void_n;
        world.PhysicsTelemetryData.DarkFaceBlockX = hit.block.x;
        world.PhysicsTelemetryData.DarkFaceBlockY = hit.block.y;
        world.PhysicsTelemetryData.DarkFaceBlockZ = hit.block.z;
        world.PhysicsTelemetryData.DarkFaceChunkX = hit.chunk.x;
        world.PhysicsTelemetryData.DarkFaceChunkY = hit.chunk.y;
        world.PhysicsTelemetryData.DarkFaceChunkZ = hit.chunk.z;
        world.PhysicsTelemetryData.DarkFaceBlockId =
            static_cast<int>(hit.blockId);
        world.PhysicsTelemetryData.DarkFaceIndex = hit.faceIndex;
        world.PhysicsTelemetryData.DarkFaceDist = hit.dist;
      }
    }
  }
  {
    int ahead = 0;
    int behind = 0;
    if (!moving_for_telemetry)
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
      world.CountUnfinishedVisualByFacing(focus_horiz, focus_radius, fwd, ahead,
                                          behind);
    }
    world.PhysicsTelemetryData.FocusUnfinishedAhead = ahead;
    world.PhysicsTelemetryData.FocusUnfinishedBehind = behind;
  }
  world.PhysicsTelemetryData.FocusMissingMesh = missing_near ? 1 : 0;
  world.PhysicsTelemetryData.VisualHoles = missing_near ? 1 : 0;
  // SoT unfinished (held sample while cruise); not pending-proxy.
  world.PhysicsTelemetryData.UnfinishedVisual = unfinished_visual;
  world.PhysicsTelemetryData.LightDebt = pending_light_focus > 0 ? 1 : 0;
  // NearFocusHoles telemetry = missing mesh only (same as VisualHoles).
  // Pending-light → LightDebt; sticky/pending_dark → FocusDarkMesh /
  // FocusStickyRemesh / FocusPendingDark. OR-ing dark_preview here was
  // overwritten after UpdateStreaming's mesh-only write, then re-applied in
  // TickAsync and inflated nh_no_miss_rate with miss=0 (SoftDefer remesh).
  world.PhysicsTelemetryData.NearFocusHoles = missing_near ? 1 : 0;

  // Underfeet column: catch draw_ok-but-invisible blind spot (manual 201621).
  {
    const glm::ivec2 under_xz(focus_horiz.x, focus_horiz.z);
    const ColumnRenderableState uf =
        world.GetColumnRenderableState(under_xz);
    bool has_mesh = false;
    const int max_cy = std::max(
        0, FloorDiv(world.GetProceduralSettings().MaxHeight, CHUNK_SIZE));
    for (int cy = 0; cy <= max_cy; ++cy)
    {
      if (world.GetMeshService().HasDrawableGreedyMesh(
              glm::ivec3(under_xz.x, cy, under_xz.y)))
      {
        has_mesh = true;
        break;
      }
    }
    world.PhysicsTelemetryData.UnderfeetDrawOk = uf.draw_ok ? 1 : 0;
    world.PhysicsTelemetryData.UnderfeetHasMesh = has_mesh ? 1 : 0;
    world.PhysicsTelemetryData.UnderfeetSticky =
        world.IsColumnStickyRemesh(under_xz) ? 1 : 0;
    world.PhysicsTelemetryData.UnderfeetPendingLight =
        world.IsPendingLightBeforeMesh(under_xz) ? 1 : 0;
    world.PhysicsTelemetryData.UnderfeetReason =
        static_cast<int>(uf.reason);
  }
}

void UWorldStreaming::TickAsyncChunkSystems(UWorld &world)
{
  const auto main_t0 = std::chrono::high_resolution_clock::now();
  auto elapsed_main_ms = [&]()
  {
    return std::chrono::duration<double, std::milli>(
               std::chrono::high_resolution_clock::now() - main_t0)
        .count();
  };

  world.PhysicsTelemetryData.CommitApplyMs = 0.0;
  world.PhysicsTelemetryData.CommitSealMs = 0.0;
  world.PhysicsTelemetryData.CommitPhysicsMs = 0.0;
  world.PhysicsTelemetryData.IdlePrefetchMs = 0.0;
  // StreamerUpdate / AsyncIo / RelightDrain accumulate for this tick; do not
  // clear RelightDrainMs here — DrainAsyncRelightResults already timed in World.

  // Authoritative pressure for this frame (UpdateStreaming earlier used the
  // previous frame's caps for Prefetch/MaxLoadOps — one-frame lag is fine).
  RefreshStreamingPressure(world);
  const StreamingPressureCaps &pressure = LastPressureCaps;

  const ProceduralSettings &procedural = world.GetProceduralSettings();
  EmergeCoordinator->BeginFrame(procedural, world.LastMovementSpeed,
                                world.MaxLoadOpsPerFrame,
                                world.GetLastMovementFrameMs());
  const UChunkEmergeCoordinator::FrameBudget budget =
      EmergeCoordinator->GetLastBudget();
  UChunkEmergeCoordinator::FrameBudget chunk_budget = budget;
  const double frame_ms = world.GetLastMovementFrameMs();
  const int pending_bg =
      world.Persistence ? world.Persistence->GetPendingTerrainColumnRelightCount()
                        : 0;

  const glm::ivec3 focus_block = world.GetPreferredLoadFocusBlock();
  const glm::ivec3 focus_ground = UChunkManager::WorldToChunk(focus_block);
  const glm::ivec3 focus_horiz(focus_ground.x, 0, focus_ground.z);
  const int focus_radius = world.GetStreamingFocusRadius();
  const size_t mesh_dirty = world.GetMeshService().GetDirtyCount();
  const bool near_mesh_backlog =
      world.GetMeshService().HasDirtyWithinHorizontalRadius(focus_horiz,
                                                           focus_radius) ||
      world.GetMeshService().HasMissingGreedyMeshInHorizontalRadius(
          world.GetBlockWorld(), focus_horiz, focus_radius);
  const int gen_backlog_total =
      ChunkScheduler ? ChunkScheduler->GetGenBacklogTotal() : 0;
  const int mesh_async = world.GetMeshService().GetAsyncInFlightCount();
  const bool keep_prewarm_gate =
      EvaluateKeepPrewarmGate(frame_ms, gen_backlog_total, mesh_async, mesh_dirty,
                              near_mesh_backlog)
          .allow;
  bool keep_prewarm_surplus = keep_prewarm_gate;

  auto is_near_column = [&](glm::ivec3 coord)
  {
    return std::max(std::abs(coord.x - focus_horiz.x),
                    std::abs(coord.z - focus_horiz.z)) <= focus_radius;
  };
  const double near_budget_ms =
      near_mesh_backlog ? 6.0 : kNearCompleteBudgetMs;
  auto near_exhausted = [&]()
  { return elapsed_main_ms() >= near_budget_ms; };
  auto far_exhausted = [&]()
  {
    if (!keep_prewarm_surplus)
    {
      return true;
    }
    return elapsed_main_ms() >= (near_budget_ms + kFarStreamingBudgetMs);
  };

  if (ChunkScheduler && procedural.AsyncChunkGeneration)
  {
    const bool moving_fast =
        world.LastMovementSpeed > procedural.MovementSpeedBoostThreshold;
    if (moving_fast &&
        (mesh_dirty > 16 || pending_bg > 8 || frame_ms > 20.0) &&
        !near_mesh_backlog)
    {
      chunk_budget.MaxChunkCommits = std::min(
          chunk_budget.MaxChunkCommits, procedural.MaxChunkCommitsPerFrame);
      chunk_budget.MaxLoadOps =
          std::min(chunk_budget.MaxLoadOps, procedural.MaxLoadOpsPerFrame);
    }
    // Near-complete first: do not starve commits when focus Dirty is high.
    const bool missing_near_mesh =
        world.GetMeshService().HasMissingGreedyMeshInHorizontalRadius(
            world.GetBlockWorld(), focus_horiz, focus_radius);
    const bool missing_underfeet =
        world.GetMeshService().HasMissingGreedyMeshInHorizontalRadius(
            world.GetBlockWorld(), focus_horiz, /*radius=*/1);
    const bool pending_underfeet =
        world.HasPendingLightBeforeMeshNear(focus_horiz, /*radius=*/1);
    const bool underfeet_need = missing_underfeet || pending_underfeet;
    bool incomplete_camera_column = false;
    {
      // Only the camera column completeness gates underfeet commit pressure.
      // Scanning ±1 kept MaxChunkCommits=1 forever while any neighbor was
      // still generating — empty underfeet at 100 FPS.
      const int max_y = procedural.MaxHeight;
      const glm::ivec3 camera_ground(focus_horiz.x, 0, focus_horiz.z);
      if (!IsTerrainChunkComplete(world.BlockWorld, camera_ground, max_y))
      {
        incomplete_camera_column = true;
      }
    }
    const bool near_focus_holes =
        missing_near_mesh ||
        world.HasPendingLightBeforeMeshNear(focus_horiz, focus_radius);
    // Commit pressure: missing/pending mesh underfeet OR camera column not
    // in RAM yet. Neighbor incompleteness must not starve commits.
    const bool underfeet_pressure =
        underfeet_need || incomplete_camera_column;
    if (near_focus_holes || underfeet_pressure)
    {
      keep_prewarm_surplus = false;
    }
    if (Streamer)
    {
      // Underfeet first when standing still. Any intentional travel (prefetch
      // threshold) must not clamp — radius=2 + near_skip carved holes mid-flight
      // when speed dipped below boost but player was still moving.
      const bool moving_any =
          world.LastMovementSpeed >= procedural.MovementPrefetchThreshold;
      if (moving_fast || moving_any)
      {
        Streamer->SetNearLoadRadius(-1);
      }
      else if (underfeet_pressure)
      {
        Streamer->SetNearLoadRadius(2);
      }
      else if (near_focus_holes || pressure.focus_pressure_mode)
      {
        // Hold focus NearLoad after holes clear (pressure hysteresis).
        Streamer->SetNearLoadRadius(focus_radius);
      }
      else
      {
        Streamer->SetNearLoadRadius(-1);
      }
    }
    if (underfeet_pressure)
    {
      // Healthy frames: feed the camera ring (commits 2–3). Hitch: 1 only.
      // Previous always-1 left empty underfeet while FPS sat at ~100.
      if (frame_ms > kBadFrameMs)
      {
        chunk_budget.MaxChunkCommits = 1;
        chunk_budget.MaxLoadOps = std::max(1, std::min(chunk_budget.MaxLoadOps, 2));
      }
      else
      {
        chunk_budget.MaxChunkCommits =
            std::max(chunk_budget.MaxChunkCommits, moving_fast ? 3 : 2);
        chunk_budget.MaxLoadOps =
            std::max(chunk_budget.MaxLoadOps, moving_fast ? 4 : 3);
      }
    }
    else if (near_mesh_backlog || near_focus_holes)
    {
      chunk_budget.MaxChunkCommits =
          std::min(chunk_budget.MaxChunkCommits, 2);
      chunk_budget.MaxLoadOps = std::max(1, std::min(chunk_budget.MaxLoadOps, 2));
    }
    else if (mesh_dirty > 32)
    {
      chunk_budget.MaxChunkCommits =
          std::max(1, chunk_budget.MaxChunkCommits / 2);
      chunk_budget.MaxLoadOps = std::max(1, chunk_budget.MaxLoadOps / 2);
    }
    // Standing with large dirty: stop feeding NEW far loads, but never starve
    // the focus ring while columns/meshes under the camera are incomplete.
    if (!moving_fast && mesh_dirty > 96 && !near_focus_holes &&
        !underfeet_pressure)
    {
      chunk_budget.MaxLoadOps = 0;
      chunk_budget.MaxChunkCommits = std::max(1, chunk_budget.MaxChunkCommits);
    }
    else if (!moving_fast && mesh_dirty > 48)
    {
      chunk_budget.MaxChunkCommits = std::max(1, chunk_budget.MaxChunkCommits);
    }
    if (!moving_fast && near_focus_holes && !underfeet_pressure)
    {
      chunk_budget.MaxLoadOps = std::max(chunk_budget.MaxLoadOps, 2);
      chunk_budget.MaxChunkCommits =
          std::min(std::max(chunk_budget.MaxChunkCommits, 1), 2);
    }
    // Hitch wins — except underfeet_pressure already capped above.
    if (frame_ms > kBadFrameMs && !underfeet_pressure)
    {
      chunk_budget.MaxChunkCommits = 1;
      chunk_budget.MaxLoadOps = std::max(1, std::min(chunk_budget.MaxLoadOps, 2));
    }
    const int completed_ready =
        ChunkScheduler ? ChunkScheduler->GetCompletedReadyCount() : 0;
    if (gen_backlog_total > 0 || completed_ready > 0)
    {
      chunk_budget.MaxChunkCommits = std::max(1, chunk_budget.MaxChunkCommits);
    }
    // Flying floor: near-hole caps above can leave MaxLoadOps=2 and the
    // player outruns the fill bubble. Keep boost while the frame is healthy
    // and streaming pressure allows admission boost.
    if (moving_fast && frame_ms <= kBadFrameMs && pressure.allow_fly_load_boost)
    {
      chunk_budget.MaxLoadOps =
          std::max(chunk_budget.MaxLoadOps, procedural.MaxLoadOpsPerFrameBoost);
      chunk_budget.MaxChunkCommits = std::max(
          chunk_budget.MaxChunkCommits,
          std::min(3, procedural.MaxChunkCommitsPerFrameBoost));
    }
    // FocusIngressBudget (GotBlocks analog): stall shell commit when focus missing
    // mesh but async pool idle.
    {
      static int ingress_stall_frames = 0;
      if (missing_near_mesh && mesh_async == 0)
      {
        ++ingress_stall_frames;
      }
      else
      {
        ingress_stall_frames = 0;
      }
      if (ingress_stall_frames > 8 && near_focus_holes && moving_fast)
      {
        chunk_budget.MaxChunkCommits = 0;
      }
      // V5 FOV visual SLA (TD-ARCH-007): after stop, do not expand shell while
      // focus unfinished — PendingLight is a mesh concern, not terrain ring.
      const double since_stop = world.GetTimeSinceMotionSec();
      if (since_stop > 0.0 && since_stop <= 8.0 &&
          world.PhysicsTelemetryData.UnfinishedVisual > 0)
      {
        chunk_budget.MaxChunkCommits =
            std::min(chunk_budget.MaxChunkCommits, 1);
        chunk_budget.MaxLoadOps = std::min(chunk_budget.MaxLoadOps, 2);
      }
    }
    chunk_budget.MaxLoadOps =
        ApplyPressureCap(chunk_budget.MaxLoadOps, pressure.max_load_ops_cap);
    chunk_budget.MaxChunkCommits = ApplyPressureCap(
        chunk_budget.MaxChunkCommits, pressure.max_commits_cap);
    ChunkScheduler->Tick(world.BlockWorld, chunk_budget.MaxChunkCommits,
                         chunk_budget.MaxLoadOps);
    world.PhysicsTelemetryData.CommitApplyMs =
        ChunkScheduler->GetLastTickApplyMs();
  }

  auto finish_telemetry = [&]()
  {
    world.PhysicsTelemetryData.PendingPlayerRelights = static_cast<uint64_t>(
        world.Persistence ? world.Persistence->GetPendingPlayerRelightCount()
                          : 0);
    world.PhysicsTelemetryData.PendingBackgroundRelights =
        static_cast<uint64_t>(pending_bg);
    world.PhysicsTelemetryData.AsyncRelightInflight =
        static_cast<uint64_t>(world.GetAsyncRelightInFlightCount());
    world.PhysicsTelemetryData.RelightDiscardedLate =
        world.GetRelightDiscardedLateCount();
    world.PhysicsTelemetryData.MeshDiscardedLate =
        world.GetMeshDiscardedLateCount();
    world.PhysicsTelemetryData.MeshApplyStale =
        world.GetMeshService().GetMeshApplyStaleCount();
    world.PhysicsTelemetryData.MeshReplaceHoleAvoided =
        world.GetMeshService().GetMeshReplaceHoleAvoidedCount();
    world.PhysicsTelemetryData.PendingGpuAppliesN = static_cast<int>(
        world.GetMeshService().GetPendingGpuAppliesCount());
    world.PhysicsTelemetryData.PendingGpuQueuedN = static_cast<int>(
        world.GetMeshService().GetPendingGpuQueuedCount());
    world.PhysicsTelemetryData.PendingGpuKickedN = static_cast<int>(
        world.GetMeshService().GetPendingGpuKickedCount());
    {
      const auto &budget = EmergeCoordinator->GetLastBudget();
      world.PhysicsTelemetryData.MeshScheduleFinal = budget.MaxMeshSchedule;
      world.PhysicsTelemetryData.MeshDrainFinal = budget.MaxMeshDrain;
      world.PhysicsTelemetryData.MeshAdmissionMode = budget.AdmissionMode;
    }
    world.PhysicsTelemetryData.GpuKickN =
        world.GetMeshService().GetLastGpuKickN();
    world.PhysicsTelemetryData.GpuFinishN =
        world.GetMeshService().GetLastGpuFinishN();
    world.PhysicsTelemetryData.GpuFinishNotReadyN =
        world.GetMeshService().GetLastGpuFinishNotReadyN();
    world.PhysicsTelemetryData.PostLoadRingNotReady =
        world.CountPostLoadRingNotReady();
    {
      const auto &tune = URuntimeTuning::Get();
      world.PhysicsTelemetryData.MeshCompletedN = static_cast<int>(
          world.GetMeshService().GetMeshCompletedSize());
      world.PhysicsTelemetryData.MeshCompletedCap = static_cast<int>(
          world.GetMeshService().GetMeshCompletedCapacity());
      world.PhysicsTelemetryData.MeshCompletedDiscarded =
          world.GetMeshService().GetMeshCompletedDiscardedOverflow();
      world.PhysicsTelemetryData.RelightCompletedN =
          static_cast<int>(world.GetRelightCompletedSize());
      world.PhysicsTelemetryData.RelightCompletedCap =
          static_cast<int>(world.GetRelightCompletedCapacity());
      world.PhysicsTelemetryData.RelightCompletedDiscarded =
          world.GetRelightCompletedDiscardedOverflow();
      world.PhysicsTelemetryData.DirtyN =
          static_cast<int>(world.GetMeshService().GetDirtyCount());
      world.PhysicsTelemetryData.PendingLightN =
          static_cast<int>(world.GetPendingLightBeforeMeshCount());
      world.PhysicsTelemetryData.RelightFifoN =
          world.Persistence
              ? world.Persistence->GetPendingTerrainColumnRelightCount()
              : 0;
      world.PhysicsTelemetryData.GpuPoolCapMb =
          static_cast<double>(tune.GpuVertexPoolMaxMb);
      world.PhysicsTelemetryData.BufferExpandEvents = tune.BufferExpandEvents;
      if (Streamer)
      {
        world.PhysicsTelemetryData.KeepMarginEff =
            Streamer->GetKeepRenderDistance() -
            Streamer->GetVisualRenderDistance();
      }
    }
    if (Streamer)
    {
      const int v = Streamer->GetVisualRenderDistance();
      const int k = Streamer->GetKeepRenderDistance();
      world.PhysicsTelemetryData.VisualCols = (2 * v + 1) * (2 * v + 1);
      world.PhysicsTelemetryData.KeepCols = (2 * k + 1) * (2 * k + 1);
    }
    world.PhysicsTelemetryData.GenBacklogTotal = gen_backlog_total;
  };

  // Physics seed: near first (two-pass), then far only when idle.
  {
    int near_done = 0;
    int far_done = 0;
    // Holes/backlog: skip physics seed (was 4) — ScanChunkFluidFrontier stacks
    // on hole frames; commits already enqueue light via FIFO.
    const int near_physics_budget = near_mesh_backlog ? 0 : 2;
    const int far_physics_budget =
        (keep_prewarm_surplus && !near_mesh_backlog) ? 1 : 0;
    const int pending_seed =
        static_cast<int>(DeferredPhysicsSeedQueue.size());
    auto make_seed_budgets = [&](bool near_column)
    {
      ChunkPhysicsSeedBudgets budgets;
      // Terrain commits can enqueue a large cold fluid frontier burst.
      // When the queue is long or the frame is already hot, seed only a tiny
      // subset and let later frames continue the scan instead of spiking phys_ms.
      const bool hot_frame = frame_ms > 16.0 || pending_seed > 32;
      const bool cold_backlog = pending_seed > 96 || gen_backlog_total > 0;
      if (hot_frame)
      {
        if (near_column)
        {
          budgets.MaxColumnsPerCommit = 1;
          budgets.MaxLiquidEnqueuePerCommit = 24;
        }
        else if (keep_prewarm_surplus && !near_mesh_backlog)
        {
          budgets.MaxColumnsPerCommit = 1;
          budgets.MaxLiquidEnqueuePerCommit = 8;
        }
        else
        {
          budgets.MaxColumnsPerCommit = 0;
          budgets.MaxLiquidEnqueuePerCommit = 0;
        }
      }
      if (cold_backlog)
      {
        budgets.MaxColumnsPerCommit =
            std::min(budgets.MaxColumnsPerCommit, near_column ? 1 : 0);
        budgets.MaxLiquidEnqueuePerCommit =
            std::min(budgets.MaxLiquidEnqueuePerCommit, near_column ? 8 : 0);
        if (!near_column && keep_prewarm_surplus && !near_mesh_backlog)
        {
          budgets.MaxColumnsPerCommit =
              std::max(budgets.MaxColumnsPerCommit, 1);
          budgets.MaxLiquidEnqueuePerCommit =
              std::max(budgets.MaxLiquidEnqueuePerCommit, 8);
        }
      }
      return budgets;
    };
    const auto physics_t0 = std::chrono::high_resolution_clock::now();
    for (auto it = DeferredPhysicsSeedQueue.begin();
         it != DeferredPhysicsSeedQueue.end() &&
         near_done < near_physics_budget && !near_exhausted();)
    {
      if (!is_near_column(*it))
      {
        ++it;
        continue;
      }
      const glm::ivec3 coord = *it;
      it = DeferredPhysicsSeedQueue.erase(it);
      ChunkPhysicsSeedBudgets seed_budgets = make_seed_budgets(true);
      SeedPhysicsOnChunkCommitted(world, coord, seed_budgets);
      ++near_done;
    }
    for (auto it = DeferredPhysicsSeedQueue.begin();
         it != DeferredPhysicsSeedQueue.end() &&
         far_done < far_physics_budget && !far_exhausted();)
    {
      if (is_near_column(*it))
      {
        ++it;
        continue;
      }
      const glm::ivec3 coord = *it;
      it = DeferredPhysicsSeedQueue.erase(it);
      ChunkPhysicsSeedBudgets seed_budgets = make_seed_budgets(false);
      if (seed_budgets.MaxColumnsPerCommit <= 0 &&
          seed_budgets.MaxLiquidEnqueuePerCommit <= 0)
      {
        it = DeferredPhysicsSeedQueue.insert(it, coord);
        break;
      }
      SeedPhysicsOnChunkCommitted(world, coord, seed_budgets);
      ++far_done;
    }
    world.PhysicsTelemetryData.CommitPhysicsMs =
        std::chrono::duration<double, std::milli>(
            std::chrono::high_resolution_clock::now() - physics_t0)
            .count();
  }

  // Shore seal: deferred IntraChunk (if populate skipped) + LiveShoreAir.
  // Cap count+wall time — multi×100–400ms seals/frame were CB hole spikes.
  // Never zero the budget on hot frames: that starved the queue entirely
  // (commit_seal_ms≡0) and deferred work into worse bursts.
  {
    const bool underfeet_pending =
        world.HasPendingLightBeforeMeshNear(focus_horiz, /*radius=*/1) ||
        world.GetMeshService().HasMissingGreedyMeshInHorizontalRadius(
            world.GetBlockWorld(), focus_horiz, /*radius=*/1);
    const bool shore_focus_holes =
        near_mesh_backlog ||
        world.HasPendingLightBeforeMeshNear(focus_horiz, focus_radius);
    const bool hole_pressure = shore_focus_holes || underfeet_pending;
    // Holes: do not start ShoreAir/IntraChunk on main — a single seal was
    // 200–470ms (CB spike_holes). Drain only on healthy frames; ShoreAir is
    // now 2-pass (was 8) so healthy drains stay cheaper.
    int near_shore_budget =
        hole_pressure ? 0 : (frame_ms > 16.0 ? 1 : 2);
    const double shore_wall_budget_ms = 10.0;
    const double seal_ms_before = world.PhysicsTelemetryData.CommitSealMs;
    const int far_shore_budget =
        (keep_prewarm_surplus && !underfeet_pending && !hole_pressure &&
         frame_ms <= 16.0)
            ? 1
            : 0;
    int near_done = 0;
    int far_done = 0;
    auto shore_exhausted = [&]()
    {
      return near_exhausted() ||
             (world.PhysicsTelemetryData.CommitSealMs - seal_ms_before) >=
                 shore_wall_budget_ms;
    };
    auto seal_one = [&](glm::ivec3 coord, bool near_column)
    {
      const ProceduralSettings &settings = world.GetProceduralSettings();
      bool changed = false;
      const auto seal_t0 = std::chrono::high_resolution_clock::now();
      if (DeferredIntraChunkSealNeeded.erase(coord) > 0)
      {
        changed = SealFluidIntraChunkOnCommitted(
                      world.BlockWorld, *world.BlockRegistry, settings,
                      world.WorldgenOwnerPackId, coord) ||
                  changed;
      }
      changed = SealFluidShoreAirOnChunkCommitted(
                    world.BlockWorld, *world.BlockRegistry, settings,
                    world.WorldgenOwnerPackId, coord) ||
                changed;
      world.PhysicsTelemetryData.CommitSealMs +=
          std::chrono::duration<double, std::milli>(
              std::chrono::high_resolution_clock::now() - seal_t0)
              .count();
      if (!changed)
      {
        return;
      }
      const glm::ivec3 ground(coord.x, 0, coord.z);
      // Shore seal mutates fluid after the first mesh — Dirty sea band.
      // Under Dirty backlog skip seamed fanout (ingress control).
      const int mesh_min_y = std::max(0, settings.SeaLevel - CHUNK_SIZE);
      const int mesh_max_y =
          std::min(settings.MaxHeight - 1, settings.SeaLevel + CHUNK_SIZE * 2);
      const bool seam =
          near_column && world.GetMeshService().GetDirtyCount() < 350;
      if (near_column)
      {
        world.MeshService->MarkTerrainChunkMeshDirtySeamedPriority(
            ground, mesh_min_y, mesh_max_y, seam);
      }
      else if (world.GetMeshService().GetDirtyCount() < 350)
      {
        world.MeshService->MarkTerrainChunkMeshDirtySeamed(
            ground, mesh_min_y, mesh_max_y, false);
      }
    };
    for (auto it = DeferredShoreSealQueue.begin();
         it != DeferredShoreSealQueue.end() &&
         near_done < near_shore_budget && !shore_exhausted();)
    {
      if (!is_near_column(*it))
      {
        ++it;
        continue;
      }
      const glm::ivec3 coord = *it;
      it = DeferredShoreSealQueue.erase(it);
      seal_one(coord, true);
      ++near_done;
    }
    for (auto it = DeferredShoreSealQueue.begin();
         it != DeferredShoreSealQueue.end() &&
         far_done < far_shore_budget && !shore_exhausted();)
    {
      if (is_near_column(*it))
      {
        ++it;
        continue;
      }
      const glm::ivec3 coord = *it;
      it = DeferredShoreSealQueue.erase(it);
      seal_one(coord, false);
      ++far_done;
    }
  }

  if (!near_exhausted())
  {
    const auto io_t0 = std::chrono::high_resolution_clock::now();
    world.Persistence->TickAsyncChunkIo(world);
    world.PhysicsTelemetryData.AsyncIoMs +=
        std::chrono::duration<double, std::milli>(
            std::chrono::high_resolution_clock::now() - io_t0)
            .count();
  }
  const int pending_player = world.Persistence->GetPendingPlayerRelightCount();
  const int player_budget = pending_player > 0 ? 2 : 0;
  const bool async_bg =
      procedural.AsyncRelight && !world.IsLightingRelightDeferred() &&
      world.AllowsAsyncLighting();
  int bg_budget =
      async_bg ? (pending_bg > 24 ? 3 : (pending_bg > 8 ? 2 : 1))
               : (pending_bg > 12 ? 2 : (pending_bg > 0 ? 1 : 0));

  const double flat_ms = world.GetMeshService().GetLastFlatRebuildMs();
  const auto now = std::chrono::steady_clock::now();
  if (gLastBgRelightBacklogTs == std::chrono::steady_clock::time_point{})
  {
    gLastBgRelightBacklogTs = now;
    gLastBgRelightBacklog = pending_bg;
  }
  else if (now - gLastBgRelightBacklogTs >=
           std::chrono::milliseconds(kRelightBacklogStuckWindowMs))
  {
    if (pending_bg >= gLastBgRelightBacklog)
    {
      gBgRelightClampUntil =
          now + std::chrono::milliseconds(kRelightBgClampCooldownMs);
    }
    gLastBgRelightBacklog = pending_bg;
    gLastBgRelightBacklogTs = now;
  }
  if ((frame_ms > kBadFrameMs || flat_ms > 16.0) && !near_mesh_backlog &&
      !world.HasPendingLightBeforeMeshNear(focus_horiz, focus_radius))
  {
    bg_budget = std::max(0, bg_budget / 2);
  }
  const bool near_pending_light =
      world.HasPendingLightBeforeMeshNear(focus_horiz, focus_radius);
  const bool underfeet_pending_light =
      world.HasPendingLightBeforeMeshNear(focus_horiz, /*radius=*/1);
  // Stuck clamp must not starve near first-light / visible holes / underfeet.
  if (now < gBgRelightClampUntil && !near_mesh_backlog && !near_pending_light &&
      !underfeet_pending_light)
  {
    bg_budget = pending_bg > 0 ? std::min(std::max(bg_budget, 1), 1) : 0;
  }
  // Far-budget gate may throttle prewarm, but must not cancel lighting for
  // already-committed columns: meshes built at light=0 stay black forever.
  // Also never zero budget when underfeet still awaits first light — Promote
  // below requeues work after pending_bg was snapshotted as 0.
  if (far_exhausted() && pending_player == 0 && !near_mesh_backlog &&
      pending_bg == 0 && !underfeet_pending_light)
  {
    bg_budget = 0;
  }
  if (pending_bg > 0)
  {
    bg_budget = std::max(bg_budget, 1);
    // Idle/FPS-recovered frames: catch up lighting instead of idling black.
    if (!near_mesh_backlog && frame_ms <= kBadFrameMs)
    {
      bg_budget = std::max(bg_budget, pending_bg > 16 ? 4 : 2);
    }
  }
  // Underfeet awaiting light: drain hard while FPS is healthy (logs showed
  // relight_drain≈0 with empty feet at ~100 FPS).
  if (underfeet_pending_light)
  {
    bg_budget = std::max(bg_budget, frame_ms > kBadFrameMs ? 1 : 2);
  }
  else if (pending_bg > 0 && (near_mesh_backlog || near_pending_light))
  {
    bg_budget = std::max(bg_budget, pending_bg > 8 ? 3 : 2);
  }
  // Standing in a dark focus pocket: pending_light stays ~30 while wall~14ms
  // because far remesh kept workers busy and bg drain stayed tiny.
  world.ReconcileAsyncRelightColumnInFlight();
  const int pending_light_n =
      static_cast<int>(world.GetPendingLightBeforeMeshCount());
  if (near_pending_light && frame_ms <= kBadFrameMs)
  {
    bg_budget = std::max(bg_budget, 2);
  }
  // Pressure valve: pending_light>~15 kept focus holes permanently open.
  // Capture is main-thread — keep floors tiny; hard_cap below is authoritative.
  if (pending_light_n > 15)
  {
    bg_budget = std::max(bg_budget, frame_ms > kBadFrameMs ? 2 : 3);
  }
  else if (pending_light_n > 10)
  {
    bg_budget = std::max(bg_budget, frame_ms > kBadFrameMs ? 1 : 2);
  }
  bg_budget = std::max(bg_budget, pressure.bg_budget_floor);
  // Focus pending stuck while wall healthy: enqueue more async relight jobs
  // (DrainRelightQueues is cheap; MarkRelit is what clears PendingLight).
  const int pending_light_focus_n =
      world.CountPendingLightBeforeMeshNear(focus_horiz, focus_radius);
  const int dark_face_near_n = world.GetPhysicsTelemetry().DarkFaceNearN;
  const int black_sticky_focus =
      world.CountBlackStickyFocusMeshes(focus_horiz, focus_radius);
  // Manual flight: pending_focus climbed while relight_drain≈0 on hitch —
  // keep a floor even when wall>24 so light debt cannot balloon forever.
  // Count floors are Capture-enqueue caps (main-thread). Keep them small —
  // wall time is enforced in DrainRelightQueues; large counts still burn
  // Capture before the first elapsed check (manual 220018: 15–52s).
  if (pending_light_focus_n > 40)
  {
    bg_budget =
        std::max(bg_budget, frame_ms > kBadFrameMs ? 2 : 4);
  }
  else if (pending_light_focus_n > 15)
  {
    bg_budget =
        std::max(bg_budget, frame_ms > kBadFrameMs ? 2 : 4);
  }
  else if (pending_light_focus_n > 8)
  {
    bg_budget = std::max(bg_budget, frame_ms > kBadFrameMs ? 1 : 3);
  }
  else if (pending_light_focus_n > 4)
  {
    bg_budget = std::max(bg_budget, frame_ms > kBadFrameMs ? 1 : 2);
  }
  else if (pending_light_focus_n > 0 && frame_ms <= kBadFrameMs)
  {
    bg_budget = std::max(bg_budget, 2);
  }
  if (pending_light_focus_n > 0 && dark_face_near_n > 500)
  {
    // Standing by black faces while focus pending exists: accelerate relight
    // capture so PendingLight columns do not keep dark meshes for many periods.
    bg_budget = std::max(bg_budget, frame_ms > kBadFrameMs ? 3 : 4);
  }
  const int mesh_async_n = world.GetMeshService().GetAsyncInFlightCount();
  const bool missing_focus_mesh =
      world.GetMeshService().HasMissingGreedyMeshInHorizontalRadius(
          world.GetBlockWorld(), focus_horiz, focus_radius);
  const bool idle_recovery =
      world.GetLastMovementSpeed() <=
          procedural.MovementPrefetchThreshold &&
      (pending_light_focus_n > 0 || black_sticky_focus > 0 ||
       mesh_async_n >= 36 || missing_focus_mesh);
  if (idle_recovery)
  {
    bg_budget = ComputeIdleRecoveryBgBudget(IdleRecoveryBgBudgetInput{
        idle_recovery, frame_ms, kBadFrameMs, pending_light_focus_n,
        black_sticky_focus, missing_focus_mesh, mesh_async_n, bg_budget});
  }
  else if (pending_light_focus_n > 0 && frame_ms > kBadFrameMs)
  {
    bg_budget = std::max(bg_budget, 4);
  }
  const bool moving_now =
      world.GetLastMovementSpeed() > procedural.MovementPrefetchThreshold;
  if (moving_now && pending_light_focus_n > 8 && frame_ms <= kBadFrameMs)
  {
    // Was 20–32: main-thread Capture per enqueue; keep walk drain paced.
    bg_budget = std::max(bg_budget, pending_light_focus_n > 24 ? 4 : 3);
  }
  // P0 frontier ingress via FocusIngressPolicy (dedicated floor, not F2 caps).
  // Rim FirstMesh SLA: missing mesh (pending optional) → prefer admit over
  // Capture/promote thrash (manual 131234 / land_fix miss_stuck).
  const bool rim_first_mesh_sla = missing_focus_mesh;
  const FocusIngressDecision ingress = EvaluateFocusIngress(FocusIngressInput{
      moving_now, missing_focus_mesh, pending_light_focus_n, mesh_async_n,
      frame_ms, world.PhysicsTelemetryData.UnfinishedVisual,
      world.PhysicsTelemetryData.DarkFaceStaleNearN});
  if (ingress.relight_floor > 0)
  {
    bg_budget = std::max(bg_budget, ingress.relight_floor);
  }
  // Priority SLA (Cubyz-style): while missing, do not let Capture steal FirstMesh.
  // Paced 1–2 (not 0 — land_fix_P1; not 4–6 — manual 170154 softd=4 thrash).
  if (rim_first_mesh_sla)
  {
    bg_budget = std::min(bg_budget, moving_now ? 1 : 2);
  }
  if (ingress.active && ingress.promote_once)
  {
    auto &exec = GetColumnFlowExecutor();
    const glm::ivec2 focus_xz(focus_horiz.x, focus_horiz.z);
    const glm::ivec2 repair_xz = RepairColumnFromMissWitness(
        world.PhysicsTelemetryData, focus_xz);
    // Rim SLA: FirstMesh Drain before any promote Capture.
    if (rim_first_mesh_sla && ingress.first_mesh_admit > 0)
    {
      if (repair_xz != focus_xz)
      {
        ++world.PhysicsTelemetryData.SoftDeferWitnessRetarget;
        world.PhysicsTelemetryData.SoftDeferWitnessHoriz =
            world.PhysicsTelemetryData.MissHoriz;
      }
      exec.Enqueue(repair_xz, ColumnWorkKind::FirstMesh, 100);
      exec.DrainBudget(world, moving_now ? 2 : 3, focus_horiz, focus_radius,
                       ingress.first_mesh_admit);
      // Underfeet promote only (r=1) — no full-focus promote while missing.
      exec.RunPromoteRelightNow(world, focus_horiz, /*focus_radius=*/1);
    }
    else
    {
      exec.RunPromoteRelightNow(world, focus_horiz, focus_radius);
      if (ingress.first_mesh_admit > 0)
      {
        if (repair_xz != focus_xz)
        {
          ++world.PhysicsTelemetryData.SoftDeferWitnessRetarget;
          world.PhysicsTelemetryData.SoftDeferWitnessHoriz =
              world.PhysicsTelemetryData.MissHoriz;
        }
        exec.Enqueue(repair_xz, ColumnWorkKind::FirstMesh, 80);
        exec.DrainBudget(world, 1, focus_horiz, focus_radius,
                         ingress.first_mesh_admit);
      }
    }
  }
  // TD-ARCH-030 / Phase 3: SoftDefer unfinished / missing → ColumnFlow ticket
  // (cap 1/tick). Do NOT inflate bg_budget Capture floor every period
  // (manual 130338 SoftDefer thrash +329; 171310 floor Δ+870).
  {
    const int unfinished = world.PhysicsTelemetryData.UnfinishedVisual;
    world.PhysicsTelemetryData.SoftDeferCaptureBudget = 0;
    if (unfinished > 0 || missing_focus_mesh)
    {
      // Era14.1 A3: miss Capture floor — keep move=1 (floor=2 raised wall on 1b);
      // idle stays 2 for SoftDefer light progress.
      int floor_budget =
          missing_focus_mesh
              ? (moving_now ? 1 : 2)
              : (pending_light_focus_n > 0
                     ? (frame_ms > kBadFrameMs
                            ? std::min(4, 1 + unfinished / 8)
                            : std::min(6, 2 + unfinished / 6))
                     : 0);
      world.PhysicsTelemetryData.SoftDeferCaptureBudget = floor_budget;
      if (floor_budget > 0)
      {
        auto &exec = GetColumnFlowExecutor();
        const glm::ivec2 focus_xz(focus_horiz.x, focus_horiz.z);
        // Anchor on miss witness (horiz 2–3 rim), not focus — else HasRepairTicket
        // on focus blocks floor while hole never gets FirstMesh (manual 191432).
        const glm::ivec2 repair_xz = RepairColumnFromMissWitness(
            world.PhysicsTelemetryData, focus_xz);
        // Rate-limit: skip if ColumnFlow already owns a repair ticket on target.
        if (!exec.HasRepairTicket(repair_xz))
        {
          ++world.PhysicsTelemetryData.SoftDeferCaptureFloorHits;
          if (repair_xz != focus_xz)
          {
            ++world.PhysicsTelemetryData.SoftDeferWitnessRetarget;
            world.PhysicsTelemetryData.SoftDeferWitnessHoriz =
                world.PhysicsTelemetryData.MissHoriz;
          }
          const ColumnWorkKind kind = missing_focus_mesh
                                          ? ColumnWorkKind::FirstMesh
                                          : ColumnWorkKind::RelightThenMesh;
          ColumnWorkItem item{};
          item.column = repair_xz;
          item.kind = kind;
          item.priority = 90;
          item.scan_full_focus = missing_focus_mesh;
          item.cy = -1;
          exec.Enqueue(item);
          exec.DrainBudget(world, 1, focus_horiz, focus_radius,
                           /*admit_batch=*/1);
        }
      }
      if (rim_first_mesh_sla)
      {
        bg_budget = std::min(bg_budget, moving_now ? 1 : 2);
      }
    }
  }
  // Two-tier promote via ColumnFlow only (underfeet then focus). Streaming
  // must not call Promote* directly (Era13 anti-zoo). RunPromoteRelightNow
  // Dispatches immediately so DrainBudget cannot steal FirstMesh/Remesh tickets.
  {
    auto &exec = GetColumnFlowExecutor();
    if (pending_bg > 0 || underfeet_pending_light)
    {
      exec.RunPromoteRelightNow(world, focus_horiz, /*focus_radius=*/1);
    }
    // While missing: underfeet-only promote (r=1). Full focus after miss clears.
    if (!rim_first_mesh_sla &&
        (pending_bg > 0 || near_pending_light || pending_light_focus_n > 0))
    {
      const int promo_r =
          (pending_light_focus_n > 0 && dark_face_near_n > 500)
              ? focus_radius + 1
              : focus_radius;
      exec.RunPromoteRelightNow(world, focus_horiz, promo_r);
    }
  }

  // Re-read queue depth after promote — snapshot pending_bg may have been 0.
  const int pending_bg_after =
      world.Persistence->GetPendingTerrainColumnRelightCount();
  if (pending_bg_after > 0 && underfeet_pending_light)
  {
    bg_budget = std::max(bg_budget, frame_ms > kBadFrameMs ? 1 : 2);
  }
  else if (pending_bg_after > 0 && bg_budget <= 0)
  {
    bg_budget = 1;
  }
  // Pending debt with empty relight FIFO: re-promote + clear lit-ready columns
  // (manual 155539: pend=2, relight_drain≈0). Frontier SoftDefer holes hitch
  // (manual 081734 wall 66–228) — do not require healthy wall when focus is
  // missing mesh, or FIFO stays empty and the hole never meshes.
  if (pending_light_focus_n > 0 && pending_bg_after == 0 &&
      (frame_ms <= kBadFrameMs || missing_focus_mesh) &&
      (idle_recovery || pending_light_focus_n > 8 || missing_focus_mesh ||
       world.GetAsyncRelightInFlightCount() == 0))
  {
    auto &exec = GetColumnFlowExecutor();
    exec.RunPromoteRelightNow(world, focus_horiz,
                              rim_first_mesh_sla ? 1 : focus_radius);
    world.ClearPendingLightAfterMeshCommitted(12);
    // Capture is main-thread: never burst 48–56 idle (manual 220018).
    // While missing: paced 1–2 (priority SLA / manual 170154 softd=4 thrash).
    const int hole_cap =
        rim_first_mesh_sla
            ? (moving_now ? 1 : 2)
            : (moving_now ? 2
                          : ((missing_focus_mesh && mesh_async_n < 8) ? 4 : 3));
    bg_budget = std::max(
        bg_budget,
        std::min(hole_cap, pending_light_focus_n +
                                (missing_focus_mesh ? 1 : 0)));
  }
  // Hard cap Capture enqueue every frame (move + idle). Sync path must stay 0
  // while walking; async Capture still costs hundreds of ms per column.
  if (!async_bg)
  {
    if (moving_now)
    {
      bg_budget = 0;
    }
    else
    {
      bg_budget = std::min(bg_budget, 1);
    }
  }
  else
  {
    // I7: calm stand with remesh debt but no holes — Capture inflates stream
    // (manual calm stream~46 with fd flat). Prefer remesh drain over Capture.
    // P1: lower fd threshold (24→8) once miss is clear so stand stream stays
    // ≤18 without starving rim FirstMesh (missing_focus_mesh excluded).
    const int unfinished_vis = world.PhysicsTelemetryData.UnfinishedVisual;
    const bool calm_fd_plateau =
        !moving_now && !missing_focus_mesh && unfinished_vis <= 0 &&
        pending_light_focus_n == 0 &&
        world.PhysicsTelemetryData.FocusDirtyChunks > 8;
    const int hard_cap =
        moving_now
            ? ((missing_focus_mesh && pending_light_focus_n > 0)
                   // SoftDefer FOV holes: keep Capture hot even if async already
                   // fed (pending plateaus at ~12 while holes stick).
                   ? (frame_ms > kBadFrameMs
                          ? 3
                          : (pending_light_focus_n > 8 ? 6 : 4))
                   : (frame_ms > kBadFrameMs ? 1 : 2))
            : (calm_fd_plateau
                   ? (frame_ms > kBadFrameMs ? 0 : 1)
                   : (frame_ms > kBadFrameMs ? 2 : 3));
    bg_budget = std::min(bg_budget, hard_cap);
    if (LastMemoryDecision.capture_hard_cap >= 0)
    {
      bg_budget = std::min(bg_budget, LastMemoryDecision.capture_hard_cap);
    }
  }

  {
    const auto relight_t0 = std::chrono::high_resolution_clock::now();
    world.Persistence->DrainRelightQueues(world, player_budget, bg_budget);
    world.PhysicsTelemetryData.RelightDrainMs +=
        std::chrono::duration<double, std::milli>(
            std::chrono::high_resolution_clock::now() - relight_t0)
            .count();
  }
  finish_telemetry();
}

void UWorldStreaming::QuiesceBackgroundWork(
    UWorld &world, const std::chrono::milliseconds async_io_timeout)
{
  if (Streamer)
  {
    Streamer->SetEnabled(false);
  }
  DeferredPhysicsSeedQueue.clear();
  DeferredShoreSealQueue.clear();
  DeferredIntraChunkSealNeeded.clear();
  PauseChunkGeneration(async_io_timeout);
  if (world.Persistence)
  {
    world.Persistence->ClearPendingRelights();
    for (int pass = 0; pass < 64; ++pass)
    {
      if (world.Persistence->TickDrainAsyncChunkIo(world, 8))
      {
        break;
      }
    }
    (void)world.Persistence->AbortAsyncChunkIoFor(async_io_timeout);
  }
}

void UWorldStreaming::CancelChunkGeneration()
{
  if (!ChunkScheduler)
  {
    return;
  }
  // Zero wait: clear queue + bump tokens so shouldCancel trips; do not join.
  ChunkScheduler->CancelAllPending(std::chrono::milliseconds(0));
}

void UWorldStreaming::AbandonWorkersForProcessExit(
    const std::chrono::milliseconds timeout)
{
  if (Streamer)
  {
    Streamer->SetEnabled(false);
  }
  SyncCoarseCacheGround = glm::ivec3(INT32_MAX, 0, INT32_MAX);
  CancelChunkGeneration();
  if (!ChunkScheduler)
  {
    ChunkPopulator.reset();
    return;
  }
  if (ChunkScheduler->WaitForWorkersIdle(timeout))
  {
    ChunkScheduler.reset();
    ChunkPopulator.reset();
    return;
  }
  // Workers still in carve/populate: detach threads, then leak scheduler +
  // populator so in-flight jobs do not use-after-free during process teardown.
  ChunkScheduler->ShutdownForProcessExit(std::chrono::milliseconds(0));
  (void)ChunkScheduler.release();
  (void)ChunkPopulator.release();
}

void UWorldStreaming::PauseChunkGeneration(
    const std::chrono::milliseconds worker_wait)
{
  if (!ChunkScheduler)
  {
    return;
  }
  ChunkScheduler->CancelAllPending(worker_wait);
  if (worker_wait.count() > 0)
  {
    (void)ChunkScheduler->WaitForWorkersIdle(worker_wait);
  }
}

void UWorldStreaming::ResumeStreamerAfterQuiesce()
{
  if (Streamer && StreamingEnabled)
  {
    Streamer->SetEnabled(true);
  }
}

void UWorldStreaming::TickMeshEmerge(UWorld &world)
{
  EmergeCoordinator->TickMeshEmerge(world, LastPressureCaps);
  // MeshWorkAdmission SoT lands in LastBudget at end of TickMeshEmerge.
  // finish_telemetry in TickAsyncChunkSystems runs *before* emerge — write
  // final schedule/drain/mode here so periods see HoleDrain under miss.
  {
    const auto &budget = EmergeCoordinator->GetLastBudget();
    world.PhysicsTelemetryData.MeshScheduleFinal = budget.MaxMeshSchedule;
    world.PhysicsTelemetryData.MeshDrainFinal = budget.MaxMeshDrain;
    world.PhysicsTelemetryData.MeshAdmissionMode = budget.AdmissionMode;
  }
  world.PhysicsTelemetryData.PendingGpuAppliesN = static_cast<int>(
      world.GetMeshService().GetPendingGpuAppliesCount());
  world.PhysicsTelemetryData.PendingGpuQueuedN = static_cast<int>(
      world.GetMeshService().GetPendingGpuQueuedCount());
  world.PhysicsTelemetryData.PendingGpuKickedN = static_cast<int>(
      world.GetMeshService().GetPendingGpuKickedCount());
  world.PhysicsTelemetryData.GpuKickN =
      world.GetMeshService().GetLastGpuKickN();
  world.PhysicsTelemetryData.GpuFinishN =
      world.GetMeshService().GetLastGpuFinishN();
  world.PhysicsTelemetryData.GpuFinishNotReadyN =
      world.GetMeshService().GetLastGpuFinishNotReadyN();
  world.PhysicsTelemetryData.MeshSyncMs =
      world.GetMeshService().GetLastMeshSyncMs();
  world.PhysicsTelemetryData.MeshSnapshotMs =
      world.GetMeshService().GetLastMeshSnapshotMs();
  world.PhysicsTelemetryData.MeshImmediateMs =
      world.GetMeshService().GetLastMeshImmediateMs();
  world.PhysicsTelemetryData.MeshImmediateCount =
      world.GetMeshService().GetLastMeshImmediateCount();
  world.PhysicsTelemetryData.EditImmediateN =
      world.GetMeshService().GetLastEditImmediateN();
  world.PhysicsTelemetryData.EditDirtyN =
      world.GetMeshService().GetLastEditDirtyN();
  world.PhysicsTelemetryData.EditNeighborPendingFrames =
      static_cast<uint64_t>(
          (std::max)(0, world.GetMeshService().GetAsyncInFlightCount()));
  world.PhysicsTelemetryData.ChunkNotReady =
      static_cast<uint64_t>(
          (std::max)(0, world.PhysicsTelemetryData.UnfinishedVisual));
  world.PhysicsTelemetryData.ChunkMeshedUnlit =
      static_cast<uint64_t>(
          (std::max)(0, world.PhysicsTelemetryData.FocusDarkMesh));
  world.PhysicsTelemetryData.MeshDirtyTickMs =
      world.GetMeshService().GetLastMeshDirtyTickMs();
}

void UWorldStreaming::InitStreamerCallbacks(UWorld &world)
{
  if (!Streamer || !world.BlockRegistry)
  {
    return;
  }
  InitChunkScheduler(world);
  const ProceduralSettings &procedural = world.GetProceduralSettings();

  Streamer->SetRenderDistance(world.RenderDistanceChunks);
  Streamer->SetMaxLoadOpsPerFrame(world.MaxLoadOpsPerFrame);
  Streamer->SetMaxUnloadOpsPerFrame(world.MaxUnloadOpsPerFrame);
  Streamer->SetMaxTerrainHeight(procedural.MaxHeight);
  Streamer->SetEnabled(StreamingEnabled);
  Streamer->SetWorldFolder(world.GetWorldFolderPath());
  Streamer->SetCallbacks(
      [this, &world](glm::ivec3 coord)
      {
        UWorldPersistence &persistence = *world.Persistence;
        if (persistence.GetChunkStorage().IsColumnSavePending(coord))
        {
          return false;
        }
        if (persistence.IsTerrainColumnDiskLoadPending(coord))
        {
          return false;
        }
        const ProceduralSettings &settings = world.GetProceduralSettings();
        if (settings.AsyncChunkIo)
        {
          persistence.RequestAsyncTerrainColumnLoad(world, coord);
          return false;
        }
        const auto t0 = std::chrono::high_resolution_clock::now();
        const bool loaded =
            persistence.LoadTerrainColumn(coord, world.BlockWorld,
                                          *world.BlockRegistry,
                                          settings.MaxHeight) > 0;
        if (loaded)
        {
          if (!IsTerrainChunkComplete(world.BlockWorld, coord,
                                      settings.MaxHeight))
          {
            persistence.PurgeIncompleteTerrainColumn(world.BlockWorld, coord,
                                                     settings.MaxHeight);
            FrameStreamingIoMs +=
                std::chrono::duration<double, std::milli>(
                    std::chrono::high_resolution_clock::now() - t0)
                    .count();
            return false;
          }
          persistence.EnqueueTerrainColumnRelight(coord.x * CHUNK_SIZE,
                                                  coord.z * CHUNK_SIZE);
        }
        FrameStreamingIoMs +=
            std::chrono::duration<double, std::milli>(
                std::chrono::high_resolution_clock::now() - t0)
                .count();
        return loaded;
      },
      [this, &world](glm::ivec3 coord)
      {
        UWorldPersistence &persistence = *world.Persistence;
        const glm::ivec3 ground(coord.x, 0, coord.z);
        persistence.CancelAsyncTerrainColumnLoad(ground);
        ChunkGenTokens.Bump(ground);
        if (ChunkScheduler)
        {
          ChunkScheduler->Invalidate(ground);
        }
        const auto t0 = std::chrono::high_resolution_clock::now();
        const ProceduralSettings &settings = world.GetProceduralSettings();
        if (settings.AsyncChunkIo)
        {
          persistence.RequestAsyncTerrainColumnSave(world, ground);
        }
        else
        {
          persistence.SaveTerrainColumn(ground, world.BlockWorld,
                                        *world.BlockRegistry,
                                        settings.MaxHeight);
          ChunkGenTokens.Bump(ground);
          if (ChunkScheduler)
          {
            ChunkScheduler->Invalidate(ground);
          }
        }
        FrameStreamingIoMs +=
            std::chrono::duration<double, std::milli>(
                std::chrono::high_resolution_clock::now() - t0)
                .count();
      },
      [&world](glm::ivec3 coord)
      {
        const glm::ivec3 ground(coord.x, 0, coord.z);
        // Sea-band neighbor remesh must run even under PendingLight — soft-defer
        // now allows sea cy, and skipping left blank water seams along flight.
        const ProceduralSettings &settings = world.GetProceduralSettings();
        const int remesh_min_y = std::max(0, settings.SeaLevel - CHUNK_SIZE);
        const int remesh_max_y = settings.SeaLevel + CHUNK_SIZE * 2;
        world.MarkTerrainChunkMeshDirtySeamed(ground, remesh_min_y, remesh_max_y,
                                              true);
      },
      [this, &world](int x, int z)
      {
        if (!world.AllowProceduralFill || !world.WorldGen)
        {
          return;
        }
        // Same CoarseHeightCache as async Populate (D: unify sampling entry).
        if (auto *composable =
                dynamic_cast<UComposableWorldGenerator *>(world.WorldGen.get()))
        {
          const glm::ivec3 ground(FloorDiv(x, CHUNK_SIZE), 0,
                                  FloorDiv(z, CHUNK_SIZE));
          if (ground != SyncCoarseCacheGround)
          {
            if (SyncCoarseCacheGround.x != INT32_MAX)
            {
              composable->EndChunkCoarseCache();
            }
            const int blend_pad = std::clamp(
                static_cast<int>(
                    std::lround(world.GetProceduralSettings().Tuning.biomeBlendRadius)),
                0, 16);
            composable->BeginChunkCoarseCache(ground.x * CHUNK_SIZE,
                                              ground.z * CHUNK_SIZE,
                                              blend_pad + 8);
            SyncCoarseCacheGround = ground;
          }
        }
        const auto t0 = std::chrono::high_resolution_clock::now();
        world.WorldGen->GenerateColumn(x, z);
        FrameStreamingGenMs +=
            std::chrono::duration<double, std::milli>(
                std::chrono::high_resolution_clock::now() - t0)
                .count();
      },
      [this, &world](glm::ivec3 coord)
      {
        world.Collision.RemoveChunkMovementSolidCache(coord);
        if (coord.y == 0 && ChunkScheduler)
        {
          ChunkScheduler->Invalidate(coord);
        }
      });
  Streamer->SetUnloadColumnCallback(
      [this, &world](glm::ivec3 ground, int max_cy)
      {
        world.ClearPendingLightBeforeMesh(glm::ivec2(ground.x, ground.z));
        world.ClearColumnEmergeState(glm::ivec2(ground.x, ground.z));
        world.GetMeshService().RemoveColumn(ground, max_cy);
        for (int cy = 0; cy <= max_cy; ++cy)
        {
          world.Collision.RemoveChunkMovementSolidCache(
              glm::ivec3(ground.x, cy, ground.z));
        }
        if (ChunkScheduler)
        {
          ChunkScheduler->Invalidate(ground);
        }
      });
  Streamer->SetAsyncGeneration(procedural.AsyncChunkGeneration);
  Streamer->SetAsyncCallbacks(
      [this, &world, &procedural](glm::ivec3 coord, int priority)
      {
        if (ChunkScheduler)
        {
          glm::ivec2 column_origin(0);
          bool has_origin = false;
          if (auto camera = world.GetCurrentUserCamera())
          {
            const PlayerCapsule cap = camera->GetPlayerCapsule();
            const glm::vec3 feet(camera->GetPosition().x,
                                 cap.feetY(camera->GetPosition()) + 0.01f,
                                 camera->GetPosition().z);
            const glm::ivec3 feet_block = WorldPosToBlock(feet);
            column_origin = glm::ivec2(feet_block.x, feet_block.z);
            has_origin = true;
          }
          ChunkScheduler->RequestLoad(coord, priority, procedural, column_origin,
                                      has_origin);
        }
      },
      [&world, this](glm::ivec3 coord)
      {
        if (ChunkScheduler && ChunkScheduler->IsPending(coord))
        {
          return false;
        }
        if (!world.BlockWorld.GetChunkManager().HasChunk(coord))
        {
          return false;
        }
        return IsTerrainChunkComplete(world.BlockWorld, coord,
                                      world.GetProceduralSettings().MaxHeight);
      });
  Streamer->SetColumnPendingCallback(
      [&world](glm::ivec3 coord)
      { return world.Persistence->IsTerrainColumnDiskLoadPending(coord); });
  Streamer->SetColumnPendingLightCallback(
      [&world](glm::ivec3 coord)
      {
        // Outer ring unlock requires LitReady+ (not awaiting first light).
        return !world.IsColumnVisualReadyForRing(coord);
      });
  Streamer->SetGenerationLightingHooks(
      [&world](bool deferred) { world.SetLightingRelightDeferred(deferred); },
      [this, &world](glm::ivec3 ground)
      {
        const ProceduralSettings &settings = world.GetProceduralSettings();
        if (settings.FillWater && world.BlockRegistry)
        {
          // Sync gen path: IntraChunk once, ShoreAir deferred (cheap).
          SealFluidShoreOnChunkCommitted(
              world.BlockWorld, *world.BlockRegistry, settings,
              world.WorldgenOwnerPackId, ground, /*include_shore_air=*/false);
          DeferredShoreSealQueue.push_back(ground);
        }
        world.SetColumnEmergeState(ground, ColumnEmergeState::Lighting);
        const glm::ivec3 focus_ground =
            UChunkManager::WorldToChunk(world.GetPreferredLoadFocusBlock());
        const int focus_radius = world.GetStreamingFocusRadius();
        const bool near_focus =
            std::max(std::abs(ground.x - focus_ground.x),
                     std::abs(ground.z - focus_ground.z)) <= focus_radius;
        world.Persistence->EnqueueTerrainColumnRelight(
            ground.x * CHUNK_SIZE, ground.z * CHUNK_SIZE, near_focus);
      });
}

void UWorldStreaming::RefreshStreamerSettings(const ProceduralSettings &settings,
                                              int maxLoadOpsPerFrame,
                                              int maxUnloadOpsPerFrame)
{
  if (!Streamer)
  {
    return;
  }
  Streamer->SetAsyncGeneration(settings.AsyncChunkGeneration);
  Streamer->SetMaxTerrainHeight(settings.MaxHeight);
  Streamer->SetMaxLoadOpsPerFrame(maxLoadOpsPerFrame);
  Streamer->SetMaxUnloadOpsPerFrame(maxUnloadOpsPerFrame);
  Streamer->SetRingGateEnabled(settings.RingGateEnabled);
}

void UWorldStreaming::UpdateStreaming(UWorld &world,
                                      UWorldMeshService &meshService,
                                      const RenderSettings &render,
                                      int renderDistanceChunks,
                                      int &effectiveRenderDistance,
                                      float &effectiveFogStartRatio,
                                      StreamingAltitudePolicyParams &altitudeParams,
                                      glm::vec3 &lastCameraPosition,
                                      float &lastMovementSpeed,
                                      glm::vec2 &lastMovementDirXz)
{
  if (!Streamer || !StreamingEnabled)
  {
    return;
  }
  URuntimeTuning::LoadStreamingTuneFile("streaming_tune.json");
  // Explicit Completed caps from tune (stress / low-mem). slots=0 keeps
  // constructor default and allows CompletedExpandEnabled growth.
  {
    const auto &tune = URuntimeTuning::Get();
    if (tune.MeshCompletedSlots > 0)
    {
      const size_t want =
          static_cast<size_t>(tune.MeshCompletedSlots);
      if (meshService.GetMeshCompletedCapacity() != want)
      {
        meshService.SetMeshCompletedCapacity(want);
      }
    }
    if (tune.RelightCompletedSlots > 0)
    {
      const size_t want =
          static_cast<size_t>(tune.RelightCompletedSlots);
      if (world.GetRelightCompletedCapacity() != want)
      {
        world.SetRelightCompletedCapacity(want);
      }
    }
  }
  if (auto camera = world.GetCurrentUserCamera())
  {
    const PlayerCapsule cap = camera->GetPlayerCapsule();
    const glm::vec3 eye = camera->GetPosition();
    glm::vec3 forward = camera->GetFront();
    forward.y = 0.0f;
    if (glm::length(forward) > 0.01f)
    {
      Streamer->SetViewForward(forward);
    }
    if (render.AltitudeAdaptiveFog)
    {
      altitudeParams.AltitudeThresholdBlocks = render.AltitudeFogThresholdBlocks;
      altitudeParams.RenderDistancePenaltyPerChunk = 1;
      altitudeParams.FogStartRatioBoost =
          std::max(0.15f, render.AltitudeFogPenaltyPer16Blocks * 4.0f);
      const float ground_y = render.AltitudeUseTerrainSurface
                                 ? QueryTerrainSurfaceWorldY(world, eye)
                                 : cap.feetY(eye);
      world.SetAltitudeAboveTerrain(std::max(0.0f, eye.y - ground_y));
      meshService.SetAltitudeCullState(world.GetAltitudeAboveTerrain(),
                                       render.AltitudeFogThresholdBlocks);
      const StreamingAltitudeSnapshot alt = ComputeStreamingAltitude(
          renderDistanceChunks, eye.y, ground_y,
          render.DistanceFogStartRatio, altitudeParams);
      effectiveRenderDistance = alt.EffectiveRenderDistance;
      effectiveFogStartRatio = alt.EffectiveFogStartRatio;
    }
    else
    {
      world.SetAltitudeAboveTerrain(0.0f);
      meshService.SetAltitudeCullState(0.0f, render.AltitudeFogThresholdBlocks);
      effectiveRenderDistance = renderDistanceChunks;
      effectiveFogStartRatio = render.DistanceFogStartRatio;
    }

    // Optional adaptive RD under streaming pressure (hysteresis).
    PhysMsEma = PhysMsEma <= 0.0
                    ? world.GetPhysicsTelemetry().PhysicsStepMs
                    : (0.85 * PhysMsEma +
                       0.15 * world.GetPhysicsTelemetry().PhysicsStepMs);
    if (render.AdaptiveRenderDistance)
    {
      if (AdaptiveEffectiveRd < 0)
      {
        AdaptiveEffectiveRd = effectiveRenderDistance;
      }
      // Memory Green may raise RD ceiling one step above altitude/base RD.
      int rd_ceiling = effectiveRenderDistance;
      if (LastMemoryDecision.memory_pressure == 0 &&
          LastMemoryDecision.max_effective_rd > rd_ceiling)
      {
        rd_ceiling = LastMemoryDecision.max_effective_rd;
      }
      const size_t dirty = meshService.GetDirtyCount();
      const int gen_backlog_total =
          ChunkScheduler ? ChunkScheduler->GetGenBacklogTotal() : 0;
      const auto now = std::chrono::steady_clock::now();
      const double since_adjust =
          AdaptiveRdLastAdjust.time_since_epoch().count() == 0
              ? kAdaptiveRdHysteresisSec
              : std::chrono::duration<double>(now - AdaptiveRdLastAdjust)
                    .count();
      if (since_adjust >= kAdaptiveRdHysteresisSec)
      {
        int next = AdaptiveEffectiveRd;
        if (dirty > 64 || gen_backlog_total > 12 || PhysMsEma > 40.0)
        {
          next = std::max(kAdaptiveRdMin, AdaptiveEffectiveRd - 1);
        }
        else if (dirty < 24 && PhysMsEma < 20.0)
        {
          next = std::min(rd_ceiling, AdaptiveEffectiveRd + 1);
        }
        if (next != AdaptiveEffectiveRd)
        {
          AdaptiveEffectiveRd = next;
          AdaptiveRdLastAdjust = now;
        }
      }
      AdaptiveEffectiveRd =
          std::clamp(AdaptiveEffectiveRd, kAdaptiveRdMin, rd_ceiling);
      effectiveRenderDistance = AdaptiveEffectiveRd;
    }
    else
    {
      AdaptiveEffectiveRd = -1;
    }

    Streamer->SetKeepPrefetchMargin(
        URuntimeTuning::Get().KeepPrefetchMargin);
    Streamer->SetMaxKeepPrefetchOpsPerFrame(
        URuntimeTuning::Get().MaxKeepPrefetchOpsPerFrame);
    {
      ++StreamingFrameCounter;
      MemoryBudgetSample sample;
      sample.stream_pressure = world.PhysicsTelemetryData.StreamPressure;
      sample.last_wall_ms = world.GetWallFrameDelta() * 1000.0;
      sample.visual_holes = world.PhysicsTelemetryData.VisualHoles;
      sample.pending_light_focus = world.PhysicsTelemetryData.PendingLightFocus;
      sample.dirty_chunks = world.PhysicsTelemetryData.FocusDirtyChunks;
      sample.baseline_keep_margin = URuntimeTuning::Get().KeepPrefetchMargin;
      sample.visual_rd = effectiveRenderDistance;
      // Prefer FramePerf cached sample (every ~30 frames) — avoid per-tick
      // GetProcessMemoryInfo on the streaming hot path (P0b).
      sample.private_mb = UFramePerfMonitor::GetLastPrivateMb();
#ifdef _WIN32
      if (sample.private_mb <= 0.0)
      {
        PROCESS_MEMORY_COUNTERS_EX pmc{};
        pmc.cb = sizeof(pmc);
        if (GetProcessMemoryInfo(
                GetCurrentProcess(),
                reinterpret_cast<PROCESS_MEMORY_COUNTERS *>(&pmc),
                sizeof(pmc)))
        {
          sample.private_mb =
              static_cast<double>(pmc.PrivateUsage) / (1024.0 * 1024.0);
        }
      }
#endif
      MemoryBudgetDecision decision;
      MemoryBudget.MaybeEvaluate(StreamingFrameCounter, sample,
                                 URuntimeTuning::Get(), decision);
      LastMemoryDecision = decision;
      world.PhysicsTelemetryData.MemoryPressure = decision.memory_pressure;
      Streamer->SetKeepPrefetchMargin(decision.keep_margin);
      if (!decision.allow_keep_prewarm)
      {
        Streamer->SetMaxKeepPrefetchOpsPerFrame(0);
      }
      if (decision.max_effective_rd < effectiveRenderDistance)
      {
        effectiveRenderDistance = decision.max_effective_rd;
        if (AdaptiveEffectiveRd >= 0)
        {
          AdaptiveEffectiveRd = effectiveRenderDistance;
        }
      }
      else if (decision.max_effective_rd > effectiveRenderDistance &&
               decision.memory_pressure == 0)
      {
        effectiveRenderDistance = decision.max_effective_rd;
        if (AdaptiveEffectiveRd >= 0)
        {
          AdaptiveEffectiveRd = effectiveRenderDistance;
        }
      }
      if (decision.emergency_cancel_outside)
      {
        meshService.CancelInFlightOutsideHorizontalRadius(
            glm::ivec3(world.PhysicsTelemetryData.FocusChunkX, 0,
                       world.PhysicsTelemetryData.FocusChunkZ),
            Streamer->GetVisualRenderDistance());
      }
      // TD-ARCH-009: soft-cap Dirty/Pending under MemoryBudget pressure.
      if (sample.dirty_chunks > 400 && sample.pending_light_focus > 8)
      {
        const int soft =
            std::max(16, URuntimeTuning::Get().PendingLightSoftCap);
        world.TrimPendingLightBeforeMesh(
            glm::ivec3(world.PhysicsTelemetryData.FocusChunkX, 0,
                       world.PhysicsTelemetryData.FocusChunkZ),
            soft);
      }
      // Free-list size tracks Keep footprint (MaxResidentChunks caps pool).
      {
        const auto &tune = URuntimeTuning::Get();
        const int keep_r =
            Streamer->GetVisualRenderDistance() + decision.keep_margin;
        const int diam = 2 * std::max(0, keep_r) + 1;
        const int max_h = world.GetProceduralSettings().MaxHeight;
        const int height_cy =
            std::max(1, (std::max(1, max_h) - 1) / CHUNK_SIZE + 1);
        const int footprint = diam * diam * height_cy;
        int free_cap = std::max(64, footprint / 4);
        if (tune.MaxResidentChunks > 0)
        {
          free_cap = std::min(free_cap, tune.MaxResidentChunks);
        }
        else
        {
          free_cap = std::min(free_cap, 512);
        }
        world.GetBlockWorld().GetChunkManager().SetMaxFreeListChunks(
            static_cast<size_t>(free_cap));
      }
      // Stepped Completed-slot expand when overflow discard rises under RAM
      // headroom (CompletedExpandEnabled). At most once per ~90 frames.
      {
        auto &tune = URuntimeTuning::Get();
        const uint64_t mesh_disc =
            meshService.GetMeshCompletedDiscardedOverflow();
        const uint64_t relight_disc =
            world.GetRelightCompletedDiscardedOverflow();
        const uint64_t mesh_delta =
            mesh_disc >= LastMeshCompletedDiscarded
                ? mesh_disc - LastMeshCompletedDiscarded
                : 0;
        const uint64_t relight_delta =
            relight_disc >= LastRelightCompletedDiscarded
                ? relight_disc - LastRelightCompletedDiscarded
                : 0;
        constexpr int kExpandCooldownFrames = 90;
        constexpr uint64_t kDiscardDeltaThreshold = 4;
        constexpr size_t kHardMaxSlots = 128;
        const bool expand_ok =
            tune.CompletedExpandEnabled &&
            sample.private_mb <
                static_cast<double>(tune.MemoryExpandKeepMb) &&
            decision.memory_pressure == 0 &&
            sample.stream_pressure <= 1 &&
            StreamingFrameCounter - LastCompletedExpandFrame >=
                kExpandCooldownFrames;
        if (expand_ok && mesh_delta >= kDiscardDeltaThreshold)
        {
          size_t cap = meshService.GetMeshCompletedCapacity();
          if (cap == 0)
          {
            cap = 24;
          }
          const size_t next =
              (std::min)(kHardMaxSlots, (std::max)(cap + 1, (cap * 3) / 2));
          if (next > cap)
          {
            meshService.SetMeshCompletedCapacity(next);
            ++tune.BufferExpandEvents;
            LastCompletedExpandFrame = StreamingFrameCounter;
          }
        }
        if (expand_ok && relight_delta >= kDiscardDeltaThreshold)
        {
          size_t cap = world.GetRelightCompletedCapacity();
          if (cap == 0)
          {
            cap = 32;
          }
          const size_t next =
              (std::min)(kHardMaxSlots, (std::max)(cap + 1, (cap * 3) / 2));
          if (next > cap)
          {
            world.SetRelightCompletedCapacity(next);
            ++tune.BufferExpandEvents;
            LastCompletedExpandFrame = StreamingFrameCounter;
          }
        }
        LastMeshCompletedDiscarded = mesh_disc;
        LastRelightCompletedDiscarded = relight_disc;
      }
    }
    Streamer->SetRenderDistance(effectiveRenderDistance);
    // Visual cull/mesh focus = visual RD; keep ring is Visual+margin.
    meshService.SetRenderDistanceChunks(
        Streamer->GetVisualRenderDistance());

    // Fog-only RD pull-in: mask unfinished gen / thrash without shrinking mesh RD.
    // Manual 161702: UnfinishedVisual max≈5 never hit threshold>8 — fog lagged
    // behind popping trees; config start_ratio=0.85 also left mid-range clear.
    // A+B water: unfinished near fluid/sea → stronger pull-in + wider sky horizon.
    // Manual 084551: do NOT pull from wall≈40 alone (cruise wall often >40 →
    // fog/trees flicker); latch hole_debt + rate-limit shrink/expand.
    // Phase B: expand/shrink/severe-wall from RuntimeTuning (not SoT predicates).
    {
      const URuntimeTuning &fog_tune = URuntimeTuning::Get();
      const double kFogPullInExpandSec =
          static_cast<double>(fog_tune.FogPullInExpandSec);
      const double kFogPullInShrinkSec =
          static_cast<double>(fog_tune.FogPullInShrinkSec);
      const double kFogPullInSevereWallMs =
          static_cast<double>(fog_tune.FogPullInSevereWallMs);
      // Shorter latch + fast decay when miss=0 (manual 130338: fog_debt=1
      // forever while unfinished refreshed 90-frame hold → opaque~279).
      constexpr int kFogHoleHoldFrames = 30;
      constexpr int kFogHoleDecayPerFrame = 3;
      int fog_rd = effectiveRenderDistance;
      int fog_margin = render.DistanceFogEndMarginBlocks;
      float fog_start_ratio = effectiveFogStartRatio;
      bool near_water_unfinished = false;
      if (render.FogPullInEnabled)
      {
        if (FogPullInRd < 0)
        {
          FogPullInRd = fog_rd;
        }
        if (FogPullInMarginHeld < 0)
        {
          FogPullInMarginHeld = fog_margin;
        }
        if (FogPullInStartRatioHeld < 0.0f)
        {
          FogPullInStartRatioHeld = fog_start_ratio;
        }
        const auto &phys = world.PhysicsTelemetryData;
        const double wall_ms = world.GetWallFrameDelta() * 1000.0;
        const int unfinished = phys.UnfinishedVisual;
        const int unfinished_ahead = phys.FocusUnfinishedAhead;
        const int gpu_pending = phys.PendingGpuAppliesN;
        // Latch on visual holes or focus missing mesh only — UnfinishedVisual
        // alone (dark/culled SoftDeferHeld) must not refresh hold (manual
        // 171310: miss=0 holes=0 still fog_debt≈94% / opaque~327).
        const bool hole_debt_now =
            phys.VisualHoles > 0 || phys.FocusMissingMesh > 0;
        if (hole_debt_now)
        {
          FogPullInHoleHoldFrames = kFogHoleHoldFrames;
        }
        else if (phys.FocusMissingMesh == 0 && phys.VisualHoles == 0)
        {
          // GO: fog_hole_debt→0 on stand when miss=0 (clear latch, do not
          // wait 30/decay while SoftDeferHeld keeps unfinished>0).
          FogPullInHoleHoldFrames = 0;
        }
        else if (FogPullInHoleHoldFrames > 0)
        {
          FogPullInHoleHoldFrames =
              std::max(0, FogPullInHoleHoldFrames - kFogHoleDecayPerFrame);
        }
        const bool hole_debt = FogPullInHoleHoldFrames > 0;
        world.PhysicsTelemetryData.FogHoleDebt = hole_debt ? 1 : 0;
        const int sea = world.GetProceduralSettings().SeaLevel;
        const glm::ivec3 camera_block = glm::ivec3(glm::floor(eye));
        const bool near_water_ctx =
            eye.y < static_cast<float>(sea) + 12.0f ||
            world.HasNearbyFluidSurface(camera_block, 24);
        near_water_unfinished = render.FogWaterUnfinishedBoost && hole_debt &&
                                near_water_ctx;
        int target = effectiveRenderDistance;
        int target_margin = render.DistanceFogEndMarginBlocks;
        float target_start = effectiveFogStartRatio;
        if (hole_debt)
        {
          // Cover incomplete outer ring before decor/trees pop in clear mid-range.
          const int unfinished_eff = std::max(unfinished, hole_debt_now ? unfinished : 1);
          int pull =
              1 + std::min(2, unfinished_eff / 3) +
              (phys.VisualHoles > 0 ? 1 : 0);
          if (near_water_unfinished)
          {
            pull += 1;
            target_margin += 24;
            target_start =
                std::min(target_start, render.FogWaterStartRatioCap);
          }
          else
          {
            target_start = std::min(target_start, 0.35f);
          }
          target = std::min(target, effectiveRenderDistance - pull);
          target_margin += 16 + std::min(32, unfinished_eff * 4);
          if (unfinished_ahead > 0)
          {
            target_margin += 8 + std::min(24, unfinished_ahead * 3);
          }
        }
        // Wall alone must NOT shrink fog (cruise wall med≈80 → flicker). Only
        // reinforce while already in hole latch with real missing mesh, or on
        // severe hitch (skip when miss=0 — manual 130338 opaque plateau).
        if (hole_debt && phys.FocusMissingMesh > 0 &&
            (phys.StreamPressure >= 2 || wall_ms > kFogPullInSevereWallMs))
        {
          target = std::min(target, target - 1);
          target_margin += 8;
          target_start = std::min(target_start, 0.40f);
        }
        const int fog_rd_max = std::max(1, effectiveRenderDistance);
        const int fog_rd_min =
            std::min(fog_rd_max, std::max(1, render.FogRdMin));
        target = std::clamp(target, fog_rd_min, fog_rd_max);
        const auto now = std::chrono::steady_clock::now();
        const double since =
            FogPullInLastAdjust.time_since_epoch().count() == 0
                ? kFogPullInExpandSec
                : std::chrono::duration<double>(now - FogPullInLastAdjust)
                      .count();
        const double since_shrink =
            FogPullInLastShrink.time_since_epoch().count() == 0
                ? kFogPullInShrinkSec
                : std::chrono::duration<double>(now - FogPullInLastShrink)
                      .count();
        if (target < FogPullInRd)
        {
          // Rate-limit shrink; severe missing/unfinished may step immediately.
          // gpu_pending alone is not urgent (213546: apply queue ≠ fog hole).
          const bool urgent =
              phys.VisualHoles > 0 || unfinished >= 3 ||
              (hole_debt_now && gpu_pending >= 8);
          if (urgent || since_shrink >= kFogPullInShrinkSec)
          {
            FogPullInRd = urgent ? target : std::max(target, FogPullInRd - 1);
            FogPullInLastAdjust = now;
            FogPullInLastShrink = now;
          }
        }
        else if (target > FogPullInRd && since >= kFogPullInExpandSec &&
                 !hole_debt)
        {
          const int step = (since >= kFogPullInExpandSec * 2.0) ? 2 : 1;
          FogPullInRd = std::min(FogPullInRd + step, target);
          FogPullInLastAdjust = now;
        }
        fog_rd = FogPullInRd;
        // Margin / start_ratio: snap tighter immediately, release only when
        // hole latch expired (matches RD expand gate).
        if (target_margin > FogPullInMarginHeld)
        {
          FogPullInMarginHeld = target_margin;
        }
        else if (!hole_debt && since >= kFogPullInExpandSec)
        {
          FogPullInMarginHeld =
              FogPullInMarginHeld -
              std::max(1, (FogPullInMarginHeld - target_margin + 1) / 2);
          FogPullInMarginHeld = std::max(target_margin, FogPullInMarginHeld);
        }
        if (target_start < FogPullInStartRatioHeld)
        {
          FogPullInStartRatioHeld = target_start;
        }
        else if (!hole_debt && since >= kFogPullInExpandSec)
        {
          FogPullInStartRatioHeld =
              FogPullInStartRatioHeld +
              0.5f * (target_start - FogPullInStartRatioHeld);
        }
        fog_margin = FogPullInMarginHeld;
        fog_start_ratio = FogPullInStartRatioHeld;
      }
      else
      {
        FogPullInRd = -1;
        FogPullInHoleHoldFrames = 0;
        FogPullInMarginHeld = -1;
        FogPullInStartRatioHeld = -1.0f;
        world.PhysicsTelemetryData.FogHoleDebt = 0;
        world.PhysicsTelemetryData.FogPullInRd = 0;
        world.PhysicsTelemetryData.FogPullInMargin = 0;
        world.PhysicsTelemetryData.FogPullInStartRatio = 0.0f;
      }
      world.SetEffectiveFogRenderDistance(fog_rd);
      world.SetEffectiveFogEndMarginBlocks(fog_margin);
      world.SetNearWaterUnfinishedFog(near_water_unfinished);
      effectiveFogStartRatio = fog_start_ratio;
      world.PhysicsTelemetryData.FogPullInRd = fog_rd;
      world.PhysicsTelemetryData.FogPullInMargin = fog_margin;
      world.PhysicsTelemetryData.FogPullInStartRatio = fog_start_ratio;
    }

    const float dt = std::max(0.0001f, camera->GetDeltaTime());
    const glm::vec3 delta = eye - lastCameraPosition;
    lastMovementSpeed = glm::length(glm::vec3(delta.x, 0.0f, delta.z)) / dt;
    world.UpdateMotionState(lastMovementSpeed, dt);
    {
      const ProceduralSettings &proc_for_dir = world.GetProceduralSettings();
      glm::vec2 move_xz(delta.x, delta.z);
      if (glm::length(move_xz) > 0.001f &&
          lastMovementSpeed >= proc_for_dir.MovementPrefetchThreshold)
      {
        lastMovementDirXz = glm::normalize(move_xz);
      }
      else
      {
        glm::vec2 view_xz(forward.x, forward.z);
        if (glm::length(view_xz) > 0.01f)
        {
          lastMovementDirXz = glm::normalize(view_xz);
        }
      }
    }
    lastCameraPosition = eye;

    const ProceduralSettings &procedural = world.GetProceduralSettings();
    const double frame_ms = world.GetLastMovementFrameMs();
    const size_t dirty_for_unload = meshService.GetDirtyCount();
    int unload_ops = world.MaxUnloadOpsPerFrame;
    // Moving / dirty / hitch: skip unload ForEach (CB wall_no_holes streamer).
    const bool moving_for_unload =
        lastMovementSpeed >= procedural.MovementPrefetchThreshold;
    if (moving_for_unload || frame_ms > 16.0 || dirty_for_unload > 280)
    {
      unload_ops = 0;
    }
    Streamer->SetEffectiveUnloadOpsPerFrame(unload_ops);

    // NearLoadRadius / MaxLoadOps must be set BEFORE Update — previously they
    // were applied only for Prefetch, so the load loop always saw TickAsync's
    // underfeet clamp (often 2) and stream_loads stayed ~0 while flying.
    const glm::ivec3 feet_chunk = UChunkManager::WorldToChunk(
        WorldPosToBlock(glm::vec3(eye.x, cap.feetY(eye) + 0.01f, eye.z)));
    const glm::ivec3 focus_horiz(feet_chunk.x, 0, feet_chunk.z);
    const int focus_radius = world.GetStreamingFocusRadius();
    const size_t dirty = meshService.GetDirtyCount();
    const int gen_backlog_total =
        ChunkScheduler ? ChunkScheduler->GetGenBacklogTotal() : 0;
    const int mesh_async = meshService.GetAsyncInFlightCount();
    const bool near_mesh_backlog =
        meshService.HasDirtyWithinHorizontalRadius(focus_horiz, focus_radius) ||
        meshService.HasMissingGreedyMeshInHorizontalRadius(world.GetBlockWorld(),
                                                         focus_horiz,
                                                         focus_radius);
    const bool visual_holes =
        meshService.HasMissingGreedyMeshInHorizontalRadius(
            world.GetBlockWorld(), focus_horiz, focus_radius);
    // Cached via HoleQuery memo when args match; underfeet subset of focus.
    const bool underfeet_need =
        (visual_holes &&
         meshService.HasMissingGreedyMeshInHorizontalRadius(
             world.GetBlockWorld(), focus_horiz, /*radius=*/1)) ||
        world.HasPendingLightBeforeMeshNear(focus_horiz, /*radius=*/1);
    const KeepPrewarmGate keep_gate = EvaluateKeepPrewarmGate(
        frame_ms, gen_backlog_total, mesh_async, dirty, near_mesh_backlog);
    const bool near_focus_holes =
        visual_holes ||
        world.HasPendingLightBeforeMeshNear(focus_horiz, focus_radius);
    const bool moving_fast =
        lastMovementSpeed >= procedural.MovementSpeedBoostThreshold;
    const bool moving_any =
        lastMovementSpeed >= procedural.MovementPrefetchThreshold;
    // Caps from previous TickAsync (hysteresis evaluated once per frame there).
    const StreamingPressureCaps &pressure = LastPressureCaps;
    if (Streamer)
    {
      if (moving_fast || moving_any)
      {
        // Moving used to force NearLoadRadius=-1 (full VisualRD scan). On hole
        // frames that alone was ~150ms streamer_update (CB spike_holes).
        if (underfeet_need)
        {
          Streamer->SetNearLoadRadius(2);
        }
        else if (visual_holes || frame_ms > kBadFrameMs)
        {
          Streamer->SetNearLoadRadius(std::min(focus_radius, 3));
        }
        else
        {
          Streamer->SetNearLoadRadius(-1);
        }
        // Hitch / Yellow+: keep fill alive but drop boost so load+mesh do not
        // stack. Red also clamps MaxLoadOps via pressure caps below.
        int load_ops = world.MaxLoadOpsPerFrame;
        if (frame_ms <= 20.0 && moving_fast && pressure.allow_fly_load_boost &&
            !visual_holes && !underfeet_need)
        {
          load_ops = procedural.MaxLoadOpsPerFrameBoost;
        }
        if (visual_holes || underfeet_need || frame_ms > kBadFrameMs)
        {
          load_ops = std::min(load_ops, 2);
        }
        load_ops = ApplyPressureCap(load_ops, pressure.max_load_ops_cap);
        Streamer->SetMaxLoadOpsPerFrame(std::max(1, load_ops));
      }
      else if (underfeet_need)
      {
        Streamer->SetNearLoadRadius(2);
        Streamer->SetMaxLoadOpsPerFrame(ApplyPressureCap(
            world.MaxLoadOpsPerFrame, pressure.max_load_ops_cap));
      }
      else if (visual_holes || pressure.focus_pressure_mode)
      {
        Streamer->SetNearLoadRadius(focus_radius);
        Streamer->SetMaxLoadOpsPerFrame(ApplyPressureCap(
            world.MaxLoadOpsPerFrame, pressure.max_load_ops_cap));
      }
      else
      {
        Streamer->SetNearLoadRadius(-1);
        Streamer->SetMaxLoadOpsPerFrame(ApplyPressureCap(
            world.MaxLoadOpsPerFrame, pressure.max_load_ops_cap));
      }
    }
    {
      const auto update_t0 = std::chrono::high_resolution_clock::now();
      Streamer->Update(WorldPosToBlock(eye), eye, cap);
      world.PhysicsTelemetryData.StreamerUpdateMs +=
          std::chrono::duration<double, std::milli>(
              std::chrono::high_resolution_clock::now() - update_t0)
              .count();
    }

    const auto prefetch_t0 = std::chrono::high_resolution_clock::now();
    int prefetch_visual_ops = 0;
    // Prefetch at cruise speed, but skip when hitch'd, holes, or pressure≠allow —
    // Update still loads; deep ahead would only pile GenQ/Dirty (CB stream spikes).
    if (frame_ms <= 20.0 && pressure.allow_prefetch && !visual_holes &&
        !underfeet_need)
    {
      Streamer->PrefetchAhead(feet_chunk, forward, lastMovementSpeed,
                              procedural.MovementPrefetchThreshold,
                              &prefetch_visual_ops);
    }
    int prefetch_keep_ops = 0;
    // Idle in a hole pocket: keep-shell used to wait until holes cleared, so
    // standing at 100 FPS never requested the missing ring.
    if (keep_gate.allow &&
        ((!near_focus_holes && !underfeet_need) ||
         (!moving_fast && frame_ms <= 16.0 && near_focus_holes)))
    {
      const int keep_budget = std::min(
          keep_gate.max_ops, URuntimeTuning::Get().MaxKeepPrefetchOpsPerFrame);
      const int idle_hole_budget =
          (near_focus_holes && frame_ms <= 16.0) ? std::max(keep_budget, 2)
                                                 : keep_budget;
      Streamer->PrefetchKeepShell(feet_chunk, idle_hole_budget,
                                  &prefetch_keep_ops);
    }
    if ((world.GetEnterGameMeshBurstFrames() > 0 ||
         world.NeedsSpawnRingCatchUp()) &&
        !moving_fast)
    {
      glm::ivec3 missing{};
      const int keep_rd = Streamer ? Streamer->GetKeepRenderDistance() : 0;
      if (keep_rd > 0 &&
          meshService.FindNearestMissingGreedyMesh(
              world.GetBlockWorld(), focus_horiz, keep_rd, missing))
      {
        meshService.MarkDirtyPriority(missing);
      }
    }
    world.PhysicsTelemetryData.IdlePrefetchMs =
        std::chrono::duration<double, std::milli>(
            std::chrono::high_resolution_clock::now() - prefetch_t0)
            .count();
    world.PhysicsTelemetryData.PrefetchVisualOps = prefetch_visual_ops;
    world.PhysicsTelemetryData.PrefetchKeepOps = prefetch_keep_ops;
    world.PhysicsTelemetryData.GenBacklogTotal = gen_backlog_total;
    world.PhysicsTelemetryData.VisualCols =
        (2 * Streamer->GetVisualRenderDistance() + 1) *
        (2 * Streamer->GetVisualRenderDistance() + 1);
    world.PhysicsTelemetryData.KeepCols =
        (2 * Streamer->GetKeepRenderDistance() + 1) *
        (2 * Streamer->GetKeepRenderDistance() + 1);
    if (const StreamingFrameStats *st = &Streamer->GetLastFrameStats())
    {
      world.PhysicsTelemetryData.StreamLoads = st->loadsThisFrame;
      world.PhysicsTelemetryData.StreamAsyncQueued = st->asyncQueuedThisFrame;
      world.PhysicsTelemetryData.StreamRingBlocked = st->ringGateBlocked;
      world.PhysicsTelemetryData.StreamNearSkipped = st->nearLoadSkipped;
      world.PhysicsTelemetryData.StreamLoadCandidates = st->loadCandidates;
    }
    world.PhysicsTelemetryData.PendingLightCount =
        static_cast<int>(world.GetPendingLightBeforeMeshCount());
    world.PhysicsTelemetryData.FocusChunkX = focus_horiz.x;
    world.PhysicsTelemetryData.FocusChunkZ = focus_horiz.z;
    world.PhysicsTelemetryData.UnderfeetNeed = underfeet_need ? 1 : 0;
    world.PhysicsTelemetryData.VisualHoles = visual_holes ? 1 : 0;
    world.PhysicsTelemetryData.LightDebt =
        world.HasPendingLightBeforeMeshNear(focus_horiz, focus_radius) ? 1 : 0;
    // Telemetry NearFocusHoles = mesh holes only (see RefreshStreamingPressure).
    world.PhysicsTelemetryData.NearFocusHoles = visual_holes ? 1 : 0;
    world.PhysicsTelemetryData.PendingFocusCols =
        world.FormatPendingLightFocusColumns(focus_horiz, focus_radius, 12);
    // StreamPressure / PendingLightFocus already set in RefreshStreamingPressure.
  }
}

void UWorldStreaming::EnsureCollisionChunks(const glm::ivec3 &feetBlock,
                                            const glm::vec3 &forward)
{
  if (!Streamer || !StreamingEnabled)
  {
    return;
  }
  if (glm::length(forward) > 0.01f)
  {
    Streamer->SetViewForward(forward);
  }
  Streamer->EnsureCollisionChunks(feetBlock);
}

void UWorldStreaming::ResetFrameTiming()
{
  FrameStreamingGenMs = 0.0;
  FrameStreamingIoMs = 0.0;
}

const StreamingFrameStats *UWorldStreaming::GetLastFrameStats() const
{
  return Streamer ? &Streamer->GetLastFrameStats() : nullptr;
}

void UWorldStreaming::MarkPersistedColumnsFromWorld()
{
  if (Streamer)
  {
    Streamer->MarkPersistedColumnsFromWorld();
  }
}

} // namespace cutum
