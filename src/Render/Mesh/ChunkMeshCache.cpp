#include "Render/Mesh/ChunkMeshCache.h"
#include "Blocks/BlockRegistry.h"
#include "Render/Camera/Frustum.h"
#include "Render/Engine/DistanceFog.h"
#include "Render/Mesh/AsyncMeshBuilder.h"
#include "Render/Mesh/ChunkMeshRevisionRegistry.h"
#include "Render/Mesh/ChunkMeshSnapshot.h"
#include "Render/Mesh/CrossInstanceCollector.h"
#include "Render/Mesh/CrossMeshEmitter.h"
#include "Render/Mesh/MeshApplyPolicy.h"
#include "World/Streaming/AntiFlickerPolicy.h"
#include "World/Streaming/EnterVisualWarmupPolicy.h"
#include "World/Streaming/MeshLitGate.h"
#include "World/Streaming/RelightFifoPolicy.h"
#include "World/Streaming/SoftDeferEmptyPolicy.h"
#include "World/Streaming/VisualStagePolicy.h"
#include "Render/Mesh/GreedyMeshEmitter.h"
#include "Render/Mesh/GreedyMesher.h"
#include "Render/Mesh/GpuGreedyFaceExtract.h"
#include "Render/Mesh/GpuMeshPipeline.h"
#include "Render/Mesh/IUChunkCull.h"
#include "Render/Mesh/IUChunkMesher.h"
#include "Render/Mesh/MeshLightSampling.h"
#include "Render/GlIncludes.h"
#include "World/Core/BlockWorld.h"
#include "World/Math/GridMath.h"
#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstdint>
#include <limits>
#include <glm/gtc/matrix_transform.hpp>
#include <string>
#include <unordered_map>

namespace cutum
{

namespace
{

constexpr int kRemeshDeferredRingMax = 64;

bool CacheNeighborVisuallyDrawable(void *ctx, glm::ivec3 neighbor_chunk)
{
  auto *cache = static_cast<UChunkMeshCache *>(ctx);
  if (!cache)
  {
    return true;
  }
  // Era39: SoftDefer empty / Held / undrawn ⇒ not an occluder for shell faces.
  return cache->HasDrawableGreedyMesh(neighbor_chunk);
}

struct CullViewKeyAngles
{
  int iyaw{0};
  int ipitch{0};
};

CullViewKeyAngles QuantizeCullViewKey(const Frustum &frustum)
{
  // Near plane normal ≈ view forward (Frustum::FromViewProjection).
  glm::vec3 look(frustum.planes[4]);
  const float len = glm::length(look);
  if (len > 1e-6f)
  {
    look /= len;
  }
  constexpr float kRad2Deg = 57.29577951308232f;
  const float yaw_deg = std::atan2(look.x, look.z) * kRad2Deg;
  const float pitch_deg =
      std::asin(std::clamp(look.y, -1.0f, 1.0f)) * kRad2Deg;
  return {static_cast<int>(std::floor(yaw_deg / 2.0f)),
          static_cast<int>(std::floor(pitch_deg / 2.0f))};
}

bool IsFullyEnclosed(const UBlockWorld &world, glm::ivec3 pos)
{
  for (const glm::ivec3 &offset : NEIGHBOR_OFFSETS)
  {
    if (world.IsAir(pos + offset))
    {
      return false;
    }
  }
  return true;
}
constexpr int kCrossScanBelow = 2;
constexpr int kCrossScanAbove = 4;

int MaxSolidLocalY(const UChunk &chunk, const UBlockRegistry &registry)
{
  int max_y = 0;
  for (int ly = 0; ly < CHUNK_SIZE; ++ly)
  {
    for (int lz = 0; lz < CHUNK_SIZE; ++lz)
    {
      for (int lx = 0; lx < CHUNK_SIZE; ++lx)
      {
        const BlockId id = chunk.GetBlockLocal(glm::ivec3(lx, ly, lz));
        if (id == BLOCK_AIR ||
            registry.GetRenderStyle(id) == BlockRenderStyle::Cross)
        {
          continue;
        }
        max_y = std::max(max_y, ly);
      }
    }
  }
  return max_y;
}

void CollectCrossInstancesInBand(
    const UChunk &chunk, glm::ivec3 chunk_coord, const UBlockRegistry &registry,
    int max_local_y,
    std::unordered_map<BlockId, std::vector<CrossInstanceGpu>> &cross_instances)
{
  (void)max_local_y;
  CollectCrossInstancesFromChunk(chunk, chunk_coord, registry, cross_instances);
}
void MergeGreedyBatch(GreedyMeshBatch &dst, const GreedyMeshBatch &src)
{
  if (src.vertices.empty())
  {
    return;
  }
  const uint32_t base = static_cast<uint32_t>(dst.vertices.size());
  dst.vertices.insert(dst.vertices.end(), src.vertices.begin(),
                      src.vertices.end());
  dst.indices.reserve(dst.indices.size() + src.indices.size());
  for (uint32_t index : src.indices)
  {
    dst.indices.push_back(base + index);
  }
}

bool ChunkPassesFrustum(const Frustum *frustum, const glm::vec3 *cameraPos,
                        float maxCullDistance, glm::ivec3 chunk_coord,
                        bool horizontal_distance)
{
  if (!frustum || !cameraPos)
  {
    return true;
  }
  return frustum->IntersectsChunkAABB(ChunkAABBMin(chunk_coord),
                                      ChunkAABBMax(chunk_coord), *cameraPos,
                                      maxCullDistance, horizontal_distance);
}

} // namespace

size_t UChunkMeshCache::TotalCrossCenterCount() const
{
  size_t count = 0;
  for (const auto &entry : GreedyCache)
  {
    for (const auto &pair : entry.second.crossCenters)
    {
      count += pair.second.size();
    }
  }
  return count;
}
float UChunkMeshCache::MaxCullDistance() const
{
  return RenderHorizonBlocks(RenderDistanceChunks);
}

void UChunkMeshCache::SetMeshRebuildFocus(glm::ivec3 ground_chunk_coord,
                                          int radius_chunks)
{
  MeshFocusGroundChunk = ground_chunk_coord;
  MeshFocusRadiusChunks = std::max(1, radius_chunks);
  MeshFocusValid = true;
}

int UChunkMeshCache::SyncRebuildVisibleMissing(UBlockWorld &world,
                                               UBlockRegistry &registry,
                                               int max_sync, double max_ms)
{
  if (!MeshFocusValid || max_sync <= 0 || !Render.GreedyMeshing)
  {
    return 0;
  }

  const auto budget_t0 = std::chrono::high_resolution_clock::now();
  auto sync_budget_exhausted = [&]() -> bool
  {
    if (max_ms <= 0.0)
    {
      return false;
    }
    return std::chrono::duration<double, std::milli>(
               std::chrono::high_resolution_clock::now() - budget_t0)
               .count() >= max_ms;
  };

  struct Candidate
  {
    glm::ivec3 coord;
    int dist;
    int vert_dist;
    float forward_score;
  };
  std::vector<Candidate> candidates;
  candidates.reserve(64);
  const int prefer_cy =
      MeshVerticalPriorityValid ? MeshVerticalPreferredCy : MeshFocusGroundChunk.y;
  const glm::vec2 fwd_norm =
      glm::length(MeshForwardXz) > 0.01f ? glm::normalize(MeshForwardXz)
                                         : glm::vec2(0.0f);
  world.GetChunkManager().ForEachChunk(
      [&](const UChunk &chunk)
      {
        const glm::ivec3 coord = chunk.GetCoord();
        const int dx = std::abs(coord.x - MeshFocusGroundChunk.x);
        const int dz = std::abs(coord.z - MeshFocusGroundChunk.z);
        const int dist = std::max(dx, dz);
        if (dist > MeshFocusRadiusChunks)
        {
          return;
        }
        // SoftDefer / empty GpuQuadCount placeholders still sit in GreedyCache —
        // FirstMesh must key off Drawable, not mere cache presence (213543).
        if (HasDrawableGreedyMesh(coord))
        {
          return;
        }
        if (AsyncBuilder && AsyncBuilder->IsInFlight(coord))
        {
          return;
        }
        if (DeferMeshUntilLit && DeferMeshUntilLit(coord))
        {
          // Still enqueue Dirty so Pass1/async can fill once MayMesh opens —
          // silent skip left sticky holes when soft-defer was wrong/stale.
          Dirty.MarkDirtyPriority(coord);
          return;
        }
        // Skip empty air slices — they would burn sync budget before solid
        // underfeet (and get an empty GreedyCache entry either way later).
        bool any_solid = false;
        for (int z = 0; z < CHUNK_SIZE && !any_solid; z += 4)
        {
          for (int x = 0; x < CHUNK_SIZE && !any_solid; x += 4)
          {
            for (int y = 0; y < CHUNK_SIZE && !any_solid; y += 4)
            {
              if (chunk.GetBlockLocal(glm::ivec3(x, y, z)) != BLOCK_AIR)
              {
                any_solid = true;
              }
            }
          }
        }
        if (!any_solid)
        {
          return;
        }
        float forward_score = 0.0f;
        if (fwd_norm.x != 0.0f || fwd_norm.y != 0.0f)
        {
          const glm::vec2 to_chunk(static_cast<float>(coord.x - MeshFocusGroundChunk.x),
                                   static_cast<float>(coord.z - MeshFocusGroundChunk.z));
          if (glm::length(to_chunk) > 0.01f)
          {
            forward_score = glm::dot(glm::normalize(to_chunk), fwd_norm);
          }
        }
        // Sync hole-fill radius set by emerge (idle=focus, cruise=2).
        if (dist > SyncHoleFillRadius)
        {
          Dirty.MarkDirtyPriority(coord);
          return;
        }
        candidates.push_back(
            {coord, dist, std::abs(coord.y - prefer_cy), forward_score});
      });
  if (candidates.empty())
  {
    return 0;
  }
  // Dist 0 first, ahead in movement direction, near player cy, then dist.
  std::sort(candidates.begin(), candidates.end(),
            [](const Candidate &a, const Candidate &b)
            {
              if (a.dist != b.dist)
              {
                return a.dist < b.dist;
              }
              if (a.forward_score != b.forward_score)
              {
                return a.forward_score > b.forward_score;
              }
              if (a.vert_dist != b.vert_dist)
              {
                return a.vert_dist < b.vert_dist;
              }
              return false;
            });

  int rebuilt = 0;
  for (const Candidate &candidate : candidates)
  {
    if (rebuilt >= max_sync || sync_budget_exhausted())
    {
      break;
    }
    if (!world.GetChunkManager().HasChunk(candidate.coord))
    {
      continue;
    }
    if (DeferMeshUntilLit && DeferMeshUntilLit(candidate.coord))
    {
      continue;
    }
    RebuildChunk(world, registry, candidate.coord);
    Dirty.Erase(candidate.coord);
    ActiveMeshSourceRevision.erase(candidate.coord);
    GreedyBatchesDirty = true;
    ++rebuilt;
  }
  if (rebuilt > 0)
  {
    CrossBatchesDirty = true;
    PendingMeshRevisionBump = true;
  }
  return rebuilt;
}

void UChunkMeshCache::BumpMeshRevisionIfNeeded()
{
  if (!PendingMeshRevisionBump)
  {
    return;
  }
  ++MeshRevision;
  PendingMeshRevisionBump = false;
  InvalidateVisibleList();
}
void UChunkMeshCache::InvalidateVisibleList()
{
  InstancesDirty = true;
  GreedyBatchesDirty = true;
  CrossBatchesDirty = true;
  CrossBatchesDirty = true;
  LastCullCameraChunk = glm::ivec3(INT32_MAX, INT32_MAX, INT32_MAX);
  LastCullMeshRevision = 0;
  HaveLastCullViewKey = false;
  LastCullIYaw = INT32_MIN;
  LastCullIPitch = INT32_MIN;
}
void UChunkMeshCache::SetRenderSettings(const RenderSettings &settings)
{
  const bool meshPathChanged = settings.GreedyMeshing != Render.GreedyMeshing;
  Render = settings;
  if (meshPathChanged)
  {
    for (const auto &entry : Cache)
    {
      MarkDirty(entry.first);
    }
    for (const auto &entry : GreedyCache)
    {
      MarkDirty(entry.first);
    }
    InvalidateVisibleList();
    ++MeshRevision;
  }
}
void UChunkMeshCache::MarkAllDirty()
{
  Dirty.Clear();
  Cache.clear();
  GreedyCache.clear();
  GreedyVertexCountByChunk.clear();
  GreedyVertexCountTotal = 0;
  FluidSurfaceCache.clear();
  FluidSurfaceDirty.clear();
  Instances.clear();
  GreedyOpaqueCutoutRefs.clear();
  GreedyTransparentRefs.clear();
  MeshRevisions.Clear();
  ActiveMeshSourceRevision.clear();
  CaptureStore.InvalidateAll();
  ++MeshRevision;
  InstancesDirty = true;
  GreedyBatchesDirty = true;
  CrossBatchesDirty = true;
  InvalidateVisibleList();
}
void UChunkMeshCache::MarkAllDirtyFromWorld(const UBlockWorld &world,
                                            bool clear_existing_caches)
{
  if (clear_existing_caches)
  {
    MarkAllDirty();
  }
  world.GetChunkManager().ForEachChunk(
      [this](const UChunk &chunk) { MarkDirty(chunk.GetCoord()); });
}
void UChunkMeshCache::RebuildAll(UBlockWorld &world, UBlockRegistry &registry)
{
  MarkAllDirtyFromWorld(world, true);
  RebuildDirtyChunks(world, registry, 10000, 10000);
  if (Render.AsyncMeshing && Render.GreedyMeshing)
  {
    EnsureAsyncBuilder();
    int guard = 0;
    while (HasPendingAsyncMeshWork() && guard++ < 64)
    {
      RebuildDirtyChunks(world, registry, 10000, 10000);
      if (!AsyncBuilder->WaitIdleFor(std::chrono::milliseconds(2000)))
      {
        CancelAsyncMeshWork();
        break;
      }
    }
  }
}

bool UChunkMeshCache::HasPendingAsyncMeshWork() const
{
  if (!Render.AsyncMeshing || !Render.GreedyMeshing || !AsyncBuilder)
  {
    return false;
  }
  return AsyncBuilder->HasPendingWork();
}

bool UChunkMeshCache::HasAsyncInflightInHorizontalRadius(
    glm::ivec3 center_ground_chunk, int radius_chunks) const
{
  if (!Render.AsyncMeshing || !Render.GreedyMeshing || !AsyncBuilder)
  {
    return false;
  }
  return AsyncBuilder->HasInflightInHorizontalRadius(center_ground_chunk,
                                                     radius_chunks);
}

void UChunkMeshCache::WaitForAsyncMeshIdle()
{
  if (Render.AsyncMeshing && Render.GreedyMeshing && AsyncBuilder)
  {
    AsyncBuilder->WaitIdle();
  }
}

bool UChunkMeshCache::WaitForAsyncMeshIdleFor(
    const std::chrono::milliseconds timeout)
{
  if (!Render.AsyncMeshing || !Render.GreedyMeshing || !AsyncBuilder)
  {
    return true;
  }
  return AsyncBuilder->WaitIdleFor(timeout);
}

void UChunkMeshCache::CancelAsyncMeshWork()
{
  Dirty.Clear();
  ActiveMeshSourceRevision.clear();
  RemeshAfterApply.clear();
  if (!Render.AsyncMeshing || !Render.GreedyMeshing || !AsyncBuilder)
  {
    return;
  }
  AsyncBuilder->CancelPending();
}

void UChunkMeshCache::CancelAsyncInFlightKeepDirty()
{
  CancelAsyncInFlightKeepDirty(glm::ivec3(0), /*keep_horiz_lease=*/-1);
}

void UChunkMeshCache::CancelAsyncInFlightKeepDirty(glm::ivec3 focus_ground_chunk,
                                                   int keep_horiz_lease)
{
  if (!Render.AsyncMeshing || !Render.GreedyMeshing || !AsyncBuilder)
  {
    return;
  }
  // Dirty is removed at schedule time — re-queue Active coords before drop so
  // "KeepDirty" is real (otherwise cancel orphans remesh debt).
  // Underfeet lease: keep Active tracking for horiz≤keep (no MarkDirty storm).
  std::vector<std::pair<glm::ivec3, uint64_t>> keep_active;
  if (keep_horiz_lease >= 0)
  {
    keep_active.reserve(8);
  }
  for (const auto &entry : ActiveMeshSourceRevision)
  {
    if (keep_horiz_lease >= 0)
    {
      const int horiz =
          std::max(std::abs(entry.first.x - focus_ground_chunk.x),
                   std::abs(entry.first.z - focus_ground_chunk.z));
      if (horiz <= keep_horiz_lease)
      {
        keep_active.push_back(entry);
        continue;
      }
    }
    Dirty.MarkDirtyPriority(entry.first);
  }
  AsyncBuilder->CancelPending();
  // Builder InFlight was cleared; Active/RemeshAfterApply must follow or
  // HasInflightMeshBuild stays true and RemeshAfterApply loops forever while
  // async count (builder-only) looks drained.
  ActiveMeshSourceRevision.clear();
  RemeshAfterApply.clear();
  for (const auto &entry : keep_active)
  {
    ActiveMeshSourceRevision.insert(entry);
  }
}

void UChunkMeshCache::CancelInFlightOutsideHorizontalRadius(
    glm::ivec3 focus_ground_chunk, int radius_chunks, int keep_horiz_lease)
{
  if (!Render.AsyncMeshing || !Render.GreedyMeshing || !AsyncBuilder ||
      radius_chunks < 0)
  {
    return;
  }
  std::vector<glm::ivec3> outside;
  outside.reserve(ActiveMeshSourceRevision.size());
  for (const auto &entry : ActiveMeshSourceRevision)
  {
    const glm::ivec3 &coord = entry.first;
    const int horiz = std::max(std::abs(coord.x - focus_ground_chunk.x),
                               std::abs(coord.z - focus_ground_chunk.z));
    if (horiz <= keep_horiz_lease)
    {
      continue;
    }
    if (horiz > radius_chunks)
    {
      outside.push_back(coord);
    }
  }
  for (const glm::ivec3 &coord : outside)
  {
    // Drop Active tracking only. Do not MarkDirty — that re-fed the remesh
    // storm while standing. InFlight stays until Drain; result applies or
    // discards without another Capture cycle.
    ActiveMeshSourceRevision.erase(coord);
    RemeshAfterApply.erase(coord);
  }
  // Phase 2: destroy kicked fences / free staging outside focus — do not
  // touch live ChunkToSlot (only staging via FreeSlotByIndex).
  EnsureGpuPipeline();
  for (auto it = PendingGpuApplies.begin(); it != PendingGpuApplies.end();)
  {
    const int horiz = std::max(std::abs(it->coord.x - focus_ground_chunk.x),
                               std::abs(it->coord.z - focus_ground_chunk.z));
    if (horiz <= keep_horiz_lease || horiz <= radius_chunks)
    {
      ++it;
      continue;
    }
    if (it->ticket.valid && it->ticket.slotIndex >= 0 && GpuPipeline)
    {
      GpuPipeline->GetAllocator().FreeSlotByIndex(it->ticket.slotIndex);
    }
    if (it->ticket.fence)
    {
#if !defined(__ANDROID__) && !defined(CUBATARIUM_GLES)
      glDeleteSync(it->ticket.fence);
#endif
      it->ticket.fence = nullptr;
    }
    if (GpuPipeline)
    {
      GpuPipeline->ReleaseReadbackSlot(it->ticket);
    }
    GpuExtractInFlight.erase(it->coord);
    it = PendingGpuApplies.erase(it);
    TouchPendingGpuIndex();
  }
}

bool UChunkMeshCache::HasPendingDirty() const
{
  return !Dirty.empty() || HasPendingAsyncMeshWork();
}

bool UChunkMeshCache::HasGreedyMesh(glm::ivec3 chunk_coord) const
{
  return GreedyCache.find(chunk_coord) != GreedyCache.end();
}

bool UChunkMeshCache::ChunkHasLiveGpuDraw(glm::ivec3 chunk_coord) const
{
  const auto it = GreedyCache.find(chunk_coord);
  if (it == GreedyCache.end() || !it->second.GpuResident ||
      it->second.GpuQuadCount == 0)
  {
    return false;
  }
  const UGpuMeshPipeline *pipe = GetGpuMeshPipeline();
  return pipe && pipe->HasGpuMesh(chunk_coord);
}

void UChunkMeshCache::ClearStaleGpuResidentFlags(glm::ivec3 chunk_coord)
{
  auto it = GreedyCache.find(chunk_coord);
  if (it == GreedyCache.end())
  {
    return;
  }
  if (!it->second.GpuResident || it->second.GpuQuadCount == 0)
  {
    return;
  }
  it->second.GpuResident = false;
  it->second.GpuSlotIndex = -1;
  it->second.GpuQuadCount = 0;
  it->second.GpuHasDarkFace = false;
  it->second.GpuBlockRanges.clear();
  it->second.GpuTransparent = false;
}

bool UChunkMeshCache::HasAnyValidatedDrawRefs() const
{
  for (const GreedyBatchRef &ref : GreedyOpaqueCutoutRefs)
  {
    const GreedyMeshBatch *batch = TryGetGreedyBatch(ref);
    if (batch && !batch->vertices.empty() && !batch->indices.empty())
    {
      return true;
    }
  }
  for (const GreedyBatchRef &ref : GreedyTransparentRefs)
  {
    const GreedyMeshBatch *batch = TryGetGreedyBatch(ref);
    if (batch && !batch->vertices.empty() && !batch->indices.empty())
    {
      return true;
    }
  }
  const UGpuMeshPipeline *pipe = GetGpuMeshPipeline();
  if (pipe)
  {
    for (const GpuPackedChunkRef &ref : GpuPackedOpaqueRefs)
    {
      if (pipe->HasGpuMesh(ref.chunkCoord))
      {
        return true;
      }
    }
    for (const GpuPackedChunkRef &ref : GpuPackedTransparentRefs)
    {
      if (pipe->HasGpuMesh(ref.chunkCoord))
      {
        return true;
      }
    }
  }
  return false;
}

bool UChunkMeshCache::HasDrawableGreedyMesh(glm::ivec3 chunk_coord) const
{
  const auto it = GreedyCache.find(chunk_coord);
  if (it == GreedyCache.end())
  {
    return false;
  }
  if (it->second.GpuResident && it->second.GpuQuadCount > 0)
  {
    // Quads on GPU are a mesh even if compact cull hid the draw this frame.
    // Requiring live instance count made underfeet_has_mesh=0 / reason=7
    // after enter warmup uploaded geometry (manual 191142 blue screen).
    return true;
  }
  for (const GreedyMeshBatch &batch : it->second.batches)
  {
    if (!batch.vertices.empty() && !batch.indices.empty())
    {
      return true;
    }
  }
  return false;
}

bool UChunkMeshCache::HasMeshSatisfyingColumnReady(glm::ivec3 chunk_coord) const
{
  // Drawable only. GpuResident 0-quad used to fake "ready" after SoftDefer empty
  // publish cleared SoftDeferHeld (manual 171636 sky-only: holes=0 while
  // underfeet_has_mesh=0). Occluded true-empty still remeshes once; FogPullIn
  // prefers a brief hole over permanent invisible underfeet.
  return HasDrawableGreedyMesh(chunk_coord);
}

bool UChunkMeshCache::IsGpuExtractInFlight(glm::ivec3 chunk_coord) const
{
  return GpuExtractInFlight.count(chunk_coord) > 0;
}

bool UChunkMeshCache::IsPendingGpuApply(glm::ivec3 chunk_coord) const
{
  EnsurePendingGpuIndex();
  return PendingGpuIndex.find(chunk_coord) != PendingGpuIndex.end();
}

bool UChunkMeshCache::IsPendingGpuQueued(glm::ivec3 chunk_coord) const
{
  EnsurePendingGpuIndex();
  const auto it = PendingGpuIndex.find(chunk_coord);
  return it != PendingGpuIndex.end() &&
         it->second == PendingGpuApply::Phase::Queued;
}

bool UChunkMeshCache::IsPendingGpuKickedOrDispatched(glm::ivec3 chunk_coord) const
{
  EnsurePendingGpuIndex();
  const auto it = PendingGpuIndex.find(chunk_coord);
  return it != PendingGpuIndex.end() &&
         (it->second == PendingGpuApply::Phase::Kicked ||
          it->second == PendingGpuApply::Phase::Dispatched);
}

void UChunkMeshCache::EnsurePendingGpuIndex() const
{
  if (PendingGpuIndexEpoch == PendingGpuMutationEpoch)
  {
    return;
  }
  PendingGpuIndex.clear();
  PendingGpuIndex.reserve(PendingGpuApplies.size());
  for (const PendingGpuApply &pending : PendingGpuApplies)
  {
    PendingGpuIndex[pending.coord] = pending.phase;
  }
  PendingGpuIndexEpoch = PendingGpuMutationEpoch;
}

bool UChunkMeshCache::PreferKickPendingGpuQueued(glm::ivec3 chunk_coord)
{
  // Closeout F: cancel-stale / reorder GPU apply only — not a remesh owner.
  // Era15 TD-051: also promote Kicked/Dispatched to front so Finish drains
  // nearest tops stall (API was Queued-only → PreferKick no-op under Kick).
  auto promote = [&](PendingGpuApply::Phase want) -> bool {
    for (auto it = PendingGpuApplies.begin(); it != PendingGpuApplies.end();
         ++it)
    {
      if (it->coord != chunk_coord || it->phase != want)
      {
        continue;
      }
      if (it == PendingGpuApplies.begin())
      {
        return true;
      }
      PendingGpuApply pending = std::move(*it);
      PendingGpuApplies.erase(it);
      TouchPendingGpuIndex();
      PendingGpuApplies.push_front(std::move(pending));
      TouchPendingGpuIndex();
      return true;
    }
    return false;
  };
  if (promote(PendingGpuApply::Phase::Queued))
  {
    return true;
  }
  if (promote(PendingGpuApply::Phase::Kicked))
  {
    return true;
  }
  return promote(PendingGpuApply::Phase::Dispatched);
}

bool UChunkMeshCache::DropQueuedPendingGpuApply(glm::ivec3 chunk_coord)
{
  for (auto it = PendingGpuApplies.begin(); it != PendingGpuApplies.end(); ++it)
  {
    if (it->coord != chunk_coord)
    {
      continue;
    }
    if (it->phase != PendingGpuApply::Phase::Queued)
    {
      return false;
    }
    // Queued has no fence/staging yet — erase + clear Active so Immediate can
    // Capture a fresh revision without fighting Kick/Finish.
    ActiveMeshSourceRevision.erase(chunk_coord);
    GpuExtractInFlight.erase(chunk_coord);
    PendingGpuApplies.erase(it);
    TouchPendingGpuIndex();
    return true;
  }
  return false;
}

void UChunkMeshCache::NoteGeometryDirty(glm::ivec3 chunk_coord)
{
  GeometryDirtyChunks.insert(chunk_coord);
}

void UChunkMeshCache::ConsumeGeometryDirtyChunks(
    std::unordered_set<glm::ivec3, IVec3Hash> &out) const
{
  out.swap(GeometryDirtyChunks);
  GeometryDirtyChunks.clear();
}

bool UChunkMeshCache::BatchesHaveFullyDarkFace(
    const std::vector<GreedyMeshBatch> &batches)
{
  // Bottom faces (−Y, faceIndex 5) are normally light=0 (air/solid below has
  // no skylight). Counting them made SoftDefer/reject/sticky treat every
  // outdoor mesh as "dark" and drowned sticky side/top bake in diagnostics.
  for (const GreedyMeshBatch &batch : batches)
  {
    for (const GreedyMeshVertex &v : batch.vertices)
    {
      if (v.faceIndex >= 4.5f && v.faceIndex < 5.5f)
      {
        continue;
      }
      if (v.skyLight <= 0.0f && v.blockLight <= 0.0f)
      {
        return true;
      }
    }
  }
  return false;
}

bool UChunkMeshCache::BatchesHaveLitDrawableFace(
    const std::vector<GreedyMeshBatch> &batches)
{
  for (const GreedyMeshBatch &batch : batches)
  {
    for (const GreedyMeshVertex &v : batch.vertices)
    {
      if (v.faceIndex >= 4.5f && v.faceIndex < 5.5f)
      {
        continue;
      }
      if (v.skyLight > 0.0f || v.blockLight > 0.0f)
      {
        return true;
      }
    }
  }
  return false;
}

bool UChunkMeshCache::ChunkHasFullyDarkFace(glm::ivec3 chunk_coord) const
{
  const auto it = GreedyCache.find(chunk_coord);
  if (it == GreedyCache.end())
  {
    return false;
  }
  if (it->second.GpuResident)
  {
    return it->second.GpuHasDarkFace;
  }
  return BatchesHaveFullyDarkFace(it->second.batches);
}

bool UChunkMeshCache::ChunkHasLitDrawableFace(glm::ivec3 chunk_coord) const
{
  const auto it = GreedyCache.find(chunk_coord);
  if (it == GreedyCache.end())
  {
    return false;
  }
  // Keep-until-replace: live lit GPU wins over dark CPU batches (PendingReplace).
  if (ChunkHasLiveGpuDraw(chunk_coord) && !it->second.GpuHasDarkFace)
  {
    return true;
  }
  if (!it->second.batches.empty())
  {
    return BatchesHaveLitDrawableFace(it->second.batches);
  }
  return false;
}

bool UChunkMeshCache::ChunkHasStaleDarkFaces(glm::ivec3 chunk_coord,
                                             const UBlockWorld &world) const
{
  const auto it = GreedyCache.find(chunk_coord);
  if (it == GreedyCache.end())
  {
    return false;
  }
  auto face_air_offset = [](int fi) -> glm::ivec3
  {
    switch (fi)
    {
    case 0:
      return {0, 0, 1};
    case 1:
      return {1, 0, 0};
    case 2:
      return {0, 0, -1};
    case 3:
      return {-1, 0, 0};
    case 4:
      return {0, 1, 0};
    default:
      return {0, -1, 0};
    }
  };
  // GPU-resident meshes clear CPU batches — probe sky light on solids / +Y air.
  // Any-light was too aggressive (caves + surface mixed → remesh forever).
  if (it->second.batches.empty())
  {
    if (!(it->second.GpuResident && it->second.GpuHasDarkFace))
    {
      return false;
    }
    const UChunk *chunk = world.GetChunkManager().GetChunk(chunk_coord);
    if (!chunk)
    {
      return false;
    }
    const glm::ivec3 base = chunk_coord * CHUNK_SIZE;
    for (int z = 0; z < CHUNK_SIZE; z += 2)
    {
      for (int y = 0; y < CHUNK_SIZE; y += 2)
      {
        for (int x = 0; x < CHUNK_SIZE; x += 2)
        {
          const glm::ivec3 local(x, y, z);
          if (chunk->GetBlockLocal(local) == BLOCK_AIR)
          {
            continue;
          }
          const glm::ivec3 solid = base + local;
          if (UnpackSky(SampleLightPacked(world, solid)) > 0)
          {
            return true;
          }
          const glm::ivec3 above = solid + glm::ivec3(0, 1, 0);
          if (UnpackSky(SampleLightPacked(world, above)) > 0)
          {
            return true;
          }
        }
      }
    }
    return false;
  }
  for (const GreedyMeshBatch &batch : it->second.batches)
  {
    for (const GreedyMeshVertex &v : batch.vertices)
    {
      if (v.skyLight > 0.0f || v.blockLight > 0.0f)
      {
        continue;
      }
      const int fi = static_cast<int>(v.faceIndex + 0.5f);
      if (fi == 5)
      {
        continue; // −Y bottoms often legitimately unlit
      }
      const glm::ivec3 off = face_air_offset(fi);
      const glm::ivec3 solid(
          WorldCoordToBlockIndex(v.px - 0.5f * static_cast<float>(off.x)),
          WorldCoordToBlockIndex(v.py - 0.5f * static_cast<float>(off.y)),
          WorldCoordToBlockIndex(v.pz - 0.5f * static_cast<float>(off.z)));
      const glm::ivec3 air = solid + off;
      if (SampleLightPacked(world, air) != 0 ||
          SampleLightPacked(world, solid) != 0)
      {
        return true;
      }
    }
  }
  return false;
}

bool UChunkMeshCache::FindNearestDarkFaceNear(const glm::vec3 &camera_pos,
                                              float max_dist, int chunk_radius,
                                              DarkFaceHit &out,
                                              int *out_count_near,
                                              const UBlockWorld *world,
                                              int *out_stale_dark,
                                              int *out_void_edge) const
{
  if (out_count_near)
  {
    *out_count_near = 0;
  }
  if (out_stale_dark)
  {
    *out_stale_dark = 0;
  }
  if (out_void_edge)
  {
    *out_void_edge = 0;
  }
  if (max_dist <= 0.0f || chunk_radius < 0 || GreedyCache.empty())
  {
    return false;
  }
  const float max_dist2 = max_dist * max_dist;
  const glm::ivec3 cam_chunk = UChunkManager::WorldToChunk(
      glm::ivec3(WorldCoordToBlockIndex(camera_pos.x),
                 WorldCoordToBlockIndex(camera_pos.y),
                 WorldCoordToBlockIndex(camera_pos.z)));
  bool found = false;
  float best_d2 = max_dist2;
  DarkFaceHit best{};
  int count = 0;
  int stale_n = 0;
  int void_n = 0;
  auto face_air_offset = [](int fi) -> glm::ivec3
  {
    switch (fi)
    {
    case 0:
      return {0, 0, 1};
    case 1:
      return {1, 0, 0};
    case 2:
      return {0, 0, -1};
    case 3:
      return {-1, 0, 0};
    case 4:
      return {0, 1, 0};
    default:
      return {0, -1, 0};
    }
  };
  for (const auto &entry : GreedyCache)
  {
    const glm::ivec3 &cc = entry.first;
    const glm::ivec2 col_xz(cc.x, cc.z);
    const bool chunk_terminal = SoftDeferHeld.count(cc) > 0 ||
                                EnterTerminalHeld.count(cc) > 0;
    const bool col_done = EnterGateDoneColumns.count(col_xz) > 0;
    const int dx = std::abs(cc.x - cam_chunk.x);
    const int dy = std::abs(cc.y - cam_chunk.y);
    const int dz = std::abs(cc.z - cam_chunk.z);
    if ((std::max)(dx, (std::max)(dy, dz)) > chunk_radius)
    {
      continue;
    }
    for (const GreedyMeshBatch &batch : entry.second.batches)
    {
      for (const GreedyMeshVertex &v : batch.vertices)
      {
        // Skip −Y bottoms — same as BatchesHaveFullyDarkFace.
        if (v.faceIndex >= 4.5f && v.faceIndex < 5.5f)
        {
          continue;
        }
        if (v.skyLight > 0.0f || v.blockLight > 0.0f)
        {
          continue;
        }
        const float ddx = v.px - camera_pos.x;
        const float ddy = v.py - camera_pos.y;
        const float ddz = v.pz - camera_pos.z;
        const float d2 = ddx * ddx + ddy * ddy + ddz * ddz;
        if (d2 > max_dist2)
        {
          continue;
        }
        bool is_void_edge = false;
        bool is_stale = false;
        if (world)
        {
          const int fi = static_cast<int>(v.faceIndex + 0.5f);
          const glm::ivec3 off = face_air_offset(fi);
          const glm::ivec3 solid(
              WorldCoordToBlockIndex(v.px - 0.5f * static_cast<float>(off.x)),
              WorldCoordToBlockIndex(v.py - 0.5f * static_cast<float>(off.y)),
              WorldCoordToBlockIndex(v.pz - 0.5f * static_cast<float>(off.z)));
          const glm::ivec3 air = solid + off;
          if (SampleLightPacked(*world, air) != 0 ||
              SampleLightPacked(*world, solid) != 0)
          {
            is_stale = true;
          }
          else
          {
            is_void_edge = true;
          }
        }
        // Era52: terminal / gate-Done / LitReady void-edge — not unfinished void.
        if (EnterVoidTelemFaceExcluded(chunk_terminal, col_done, is_void_edge,
                                       EnterGpuQuiesceDrain,
                                       EnterVoidTelemLitReadyFn &&
                                           EnterVoidTelemLitReadyFn(col_xz)))
        {
          continue;
        }
        ++count;
        if (is_stale)
        {
          ++stale_n;
        }
        else if (is_void_edge)
        {
          ++void_n;
        }
        if (d2 < best_d2)
        {
          best_d2 = d2;
          best.block = glm::ivec3(WorldCoordToBlockIndex(v.px),
                                  WorldCoordToBlockIndex(v.py),
                                  WorldCoordToBlockIndex(v.pz));
          best.chunk = cc;
          best.blockId = batch.blockId;
          best.faceIndex = static_cast<int>(v.faceIndex);
          best.dist = std::sqrt(d2);
          found = true;
        }
      }
    }
    // GPU-resident FullyDark (no CPU batches): count near dark chunks.
    if (entry.second.batches.empty() && entry.second.GpuResident &&
        entry.second.GpuHasDarkFace && entry.second.GpuQuadCount > 0)
    {
      const glm::vec3 center(static_cast<float>(cc.x * CHUNK_SIZE + CHUNK_SIZE / 2),
                             static_cast<float>(cc.y * CHUNK_SIZE + CHUNK_SIZE / 2),
                             static_cast<float>(cc.z * CHUNK_SIZE + CHUNK_SIZE / 2));
      const float ddx = center.x - camera_pos.x;
      const float ddy = center.y - camera_pos.y;
      const float ddz = center.z - camera_pos.z;
      const float d2 = ddx * ddx + ddy * ddy + ddz * ddz;
      if (d2 <= max_dist2)
      {
        const bool is_stale =
            world && ChunkHasStaleDarkFaces(cc, *world);
        const bool is_void_edge = !is_stale;
        if (!EnterVoidTelemFaceExcluded(chunk_terminal, col_done, is_void_edge,
                                        EnterGpuQuiesceDrain,
                                        EnterVoidTelemLitReadyFn &&
                                            EnterVoidTelemLitReadyFn(col_xz)))
        {
          ++count;
          if (is_stale)
          {
            ++stale_n;
          }
          else
          {
            ++void_n;
          }
          if (d2 < best_d2)
          {
            best_d2 = d2;
            best.chunk = cc;
            best.block = glm::ivec3(cc.x * CHUNK_SIZE, cc.y * CHUNK_SIZE,
                                    cc.z * CHUNK_SIZE);
            best.dist = std::sqrt(d2);
            found = true;
          }
        }
      }
    }
  }
  if (out_count_near)
  {
    *out_count_near = count;
  }
  if (out_stale_dark)
  {
    *out_stale_dark = stale_n;
  }
  if (out_void_edge)
  {
    *out_void_edge = void_n;
  }
  if (found)
  {
    out = best;
  }
  return found;
}

bool UChunkMeshCache::IsChunkMeshDirty(glm::ivec3 chunk_coord) const
{
  return Dirty.Contains(chunk_coord);
}

int UChunkMeshCache::MaybeDropFarthestDirty(glm::ivec3 focus_ground_chunk,
                                            size_t soft_cap,
                                            int min_keep_horiz)
{
  return Dirty.MaybeDropFarthest(
      focus_ground_chunk, soft_cap, min_keep_horiz,
      [this](glm::ivec3 c) { return !HasGreedyMesh(c); });
}

uint64_t UChunkMeshCache::GetChunkMeshRevision(glm::ivec3 chunk_coord) const
{
  return MeshRevisions.Current(chunk_coord);
}

bool UChunkMeshCache::HasInflightMeshBuild(glm::ivec3 chunk_coord) const
{
  return ActiveMeshSourceRevision.find(chunk_coord) !=
         ActiveMeshSourceRevision.end();
}

uint64_t UChunkMeshCache::GetInflightSourceRevision(
    glm::ivec3 chunk_coord) const
{
  const auto it = ActiveMeshSourceRevision.find(chunk_coord);
  if (it == ActiveMeshSourceRevision.end())
  {
    return 0;
  }
  return it->second;
}

bool UChunkMeshCache::HasMissingGreedyMeshInHorizontalRadius(
    const UBlockWorld &world, glm::ivec3 center_ground_chunk,
    int radius_chunks) const
{
  if (radius_chunks < 0)
  {
    return false;
  }
  if (MissingMemo.epoch == HoleQueryEpoch &&
      MissingMemo.center == center_ground_chunk &&
      MissingMemo.radius == radius_chunks)
  {
    return MissingMemo.result;
  }
  bool missing = false;
  world.GetChunkManager().ForEachChunk(
      [&](const UChunk &chunk)
      {
        if (missing)
        {
          return;
        }
        const glm::ivec3 coord = chunk.GetCoord();
        const int dx = std::abs(coord.x - center_ground_chunk.x);
        const int dz = std::abs(coord.z - center_ground_chunk.z);
        if (std::max(dx, dz) > radius_chunks)
        {
          return;
        }
        if (HasMeshSatisfyingColumnReady(coord))
        {
          return;
        }
        // Empty SoftDefer / undrawn placeholder (!ready, often !GpuResident) is
        // still a hole for solid chunks (manual 101824). Intentional 0-quad
        // GPU commits are ready via HasMeshSatisfyingColumnReady.
        if (IsPendingGpuApply(coord))
        {
          return;
        }
        if (AsyncBuilder && AsyncBuilder->IsInFlight(coord))
        {
          return; // pipeline already building — not a stuck hole
        }
        // Ignore empty air slices — they never get a mesh and must not keep
        // underfeet_need / near_focus_holes stuck true forever.
        for (int z = 0; z < CHUNK_SIZE; z += 4)
        {
          for (int x = 0; x < CHUNK_SIZE; x += 4)
          {
            for (int y = 0; y < CHUNK_SIZE; y += 4)
            {
              if (chunk.GetBlockLocal(glm::ivec3(x, y, z)) != BLOCK_AIR)
              {
                missing = true;
                return;
              }
            }
          }
        }
      });
  MissingMemo.epoch = HoleQueryEpoch;
  MissingMemo.center = center_ground_chunk;
  MissingMemo.radius = radius_chunks;
  MissingMemo.result = missing;
  return missing;
}

void UChunkMeshCache::BeginHoleQueryFrame(glm::ivec3 focus_ground_chunk)
{
  if (focus_ground_chunk != LastHoleQueryFocus_)
  {
    ++HoleQueryEpoch;
    LastHoleQueryFocus_ = focus_ground_chunk;
    RequeueDeferredRemesh(static_cast<int>(RemeshDeferredRing_.size()));
  }
}

void UChunkMeshCache::DeferRemeshCoord(const glm::ivec3 &coord)
{
  if (RemeshDeferredSet_.count(coord) > 0)
  {
    return;
  }
  while (static_cast<int>(RemeshDeferredRing_.size()) >= kRemeshDeferredRingMax &&
         !RemeshDeferredRing_.empty())
  {
    const glm::ivec3 evict = RemeshDeferredRing_.front();
    RemeshDeferredRing_.pop_front();
    RemeshDeferredSet_.erase(evict);
  }
  RemeshDeferredSet_.insert(coord);
  RemeshDeferredRing_.push_back(coord);
}

void UChunkMeshCache::RequeueDeferredRemesh(int max_n)
{
  if (max_n <= 0 || RemeshDeferredRing_.empty())
  {
    return;
  }
  const int capped = std::min(
      max_n, std::max(1, static_cast<int>(RemeshDeferredRing_.size()) / 4));
  for (int i = 0; i < capped && !RemeshDeferredRing_.empty(); ++i)
  {
    const glm::ivec3 c = RemeshDeferredRing_.front();
    RemeshDeferredRing_.pop_front();
    RemeshDeferredSet_.erase(c);
    if (!Dirty.Contains(c))
    {
      Dirty.MarkDirty(c);
    }
  }
}

bool UChunkMeshCache::FindNearestMissingGreedyMesh(
    const UBlockWorld &world, glm::ivec3 center_ground_chunk, int radius_chunks,
    glm::ivec3 &out_coord) const
{
  if (radius_chunks < 0)
  {
    return false;
  }
  if (NearestMemo.epoch == HoleQueryEpoch &&
      NearestMemo.center == center_ground_chunk &&
      NearestMemo.radius == radius_chunks)
  {
    if (NearestMemo.found)
    {
      out_coord = NearestMemo.coord;
    }
    return NearestMemo.found;
  }
  // Ring-order + early exit. ForEachChunk over the whole resident set used to
  // cost 70–120ms mesh_emerge_prep on hole frames (CB spike_holes) while
  // searching for a nearest xz that is usually underfeet (r≤1).
  // Callers pass focus_ground_horiz with y=0 — scan a tall cy band so altitude
  // slices stay visible without walking every keep-shell chunk.
  constexpr int kMissingScanMaxCy = 48;
  const UChunkManager &chunks = world.GetChunkManager();
  auto chunk_is_solid_missing = [&](glm::ivec3 coord) -> bool
  {
    if (HasMeshSatisfyingColumnReady(coord))
    {
      return false;
    }
    // Empty SoftDefer placeholders remain missing until drawable/ready mesh.
    if (IsPendingGpuApply(coord))
    {
      return false;
    }
    if (AsyncBuilder && AsyncBuilder->IsInFlight(coord))
    {
      return false;
    }
    const UChunk *chunk = chunks.GetChunk(coord);
    if (!chunk)
    {
      return false;
    }
    for (int z = 0; z < CHUNK_SIZE; z += 4)
    {
      for (int x = 0; x < CHUNK_SIZE; x += 4)
      {
        for (int y = 0; y < CHUNK_SIZE; y += 4)
        {
          if (chunk->GetBlockLocal(glm::ivec3(x, y, z)) != BLOCK_AIR)
          {
            return true;
          }
        }
      }
    }
    return false;
  };
  bool found = false;
  glm::ivec3 best{0};
  for (int r = 0; r <= radius_chunks; ++r)
  {
    glm::ivec3 best_ring{0};
    int best_vdist = std::numeric_limits<int>::max();
    bool found_ring = false;
    for (int dz = -r; dz <= r; ++dz)
    {
      for (int dx = -r; dx <= r; ++dx)
      {
        if (r > 0 && std::max(std::abs(dx), std::abs(dz)) != r)
        {
          continue;
        }
        for (int cy = 0; cy <= kMissingScanMaxCy; ++cy)
        {
          const glm::ivec3 coord(center_ground_chunk.x + dx, cy,
                                 center_ground_chunk.z + dz);
          if (!chunk_is_solid_missing(coord))
          {
            continue;
          }
          const int vdist = std::abs(cy - center_ground_chunk.y);
          if (!found_ring || vdist < best_vdist)
          {
            found_ring = true;
            best_vdist = vdist;
            best_ring = coord;
          }
        }
      }
    }
    if (found_ring)
    {
      found = true;
      best = best_ring;
      break;
    }
  }
  NearestMemo.epoch = HoleQueryEpoch;
  NearestMemo.center = center_ground_chunk;
  NearestMemo.radius = radius_chunks;
  NearestMemo.found = found;
  NearestMemo.coord = best;
  if (found)
  {
    out_coord = best;
  }
  return found;
}

void UChunkMeshCache::BumpChunkMeshRevision(glm::ivec3 chunk_coord)
{
  MeshRevisions.Bump(chunk_coord);
  CaptureStore.Invalidate(chunk_coord);
}

void UChunkMeshCache::PrefetchMeshCapture(const UBlockWorld &world,
                                          glm::ivec3 chunk_coord)
{
  CaptureStore.SetNeighborVisualDrawableFn(CacheNeighborVisuallyDrawable, this);
  const uint64_t rev = MeshRevisions.Current(chunk_coord);
  CaptureStore.CaptureAndStore(world, chunk_coord, rev);
}

void UChunkMeshCache::InvalidateMeshCapture(glm::ivec3 chunk_coord)
{
  CaptureStore.Invalidate(chunk_coord);
}

void UChunkMeshCache::InvalidateInFlightMeshBuild(glm::ivec3 chunk_coord)
{
  ActiveMeshSourceRevision.erase(chunk_coord);
  RemeshAfterApply.erase(chunk_coord);
  BumpChunkMeshRevision(chunk_coord);
}

bool UChunkMeshCache::HasDirtyWithinHorizontalRadius(
    glm::ivec3 center_chunk, int radius_chunks) const
{
  return CountDirtyWithinHorizontalRadius(center_chunk, radius_chunks) > 0;
}

int UChunkMeshCache::CountDirtyWithinHorizontalRadius(
    glm::ivec3 center_chunk, int radius_chunks) const
{
  if (radius_chunks < 0)
  {
    return 0;
  }
  int count = 0;
  for (const glm::ivec3 &coord : Dirty)
  {
    const int dx = std::abs(coord.x - center_chunk.x);
    const int dz = std::abs(coord.z - center_chunk.z);
    if (std::max(dx, dz) <= radius_chunks)
    {
      ++count;
    }
  }
  return count;
}

int UChunkMeshCache::ParkDirtyWithinHorizontalRadius(glm::ivec3 center_chunk,
                                                     int radius_chunks)
{
  if (radius_chunks < 0)
  {
    return 0;
  }
  int parked = 0;
  for (auto it = Dirty.begin(); it != Dirty.end();)
  {
    const int dx = std::abs(it->x - center_chunk.x);
    const int dz = std::abs(it->z - center_chunk.z);
    if (std::max(dx, dz) <= radius_chunks)
    {
      it = Dirty.RemoveAt(it);
      ++parked;
    }
    else
    {
      ++it;
    }
  }
  return parked;
}

bool UChunkMeshCache::FindFirstDirtyInHorizontalRadius(
    glm::ivec3 center_chunk, int radius_chunks, glm::ivec3 &out_coord) const
{
  if (radius_chunks < 0)
  {
    return false;
  }
  for (const glm::ivec3 &coord : Dirty)
  {
    const int dx = std::abs(coord.x - center_chunk.x);
    const int dz = std::abs(coord.z - center_chunk.z);
    if (std::max(dx, dz) <= radius_chunks)
    {
      out_coord = coord;
      return true;
    }
  }
  return false;
}

size_t UChunkMeshCache::GetPendingGpuQueuedCount() const
{
  size_t n = 0;
  for (const PendingGpuApply &p : PendingGpuApplies)
  {
    if (p.phase == PendingGpuApply::Phase::Queued)
    {
      ++n;
    }
  }
  return n;
}

size_t UChunkMeshCache::GetPendingGpuKickedCount() const
{
  size_t n = 0;
  for (const PendingGpuApply &p : PendingGpuApplies)
  {
    if (p.phase == PendingGpuApply::Phase::Kicked ||
        p.phase == PendingGpuApply::Phase::Dispatched)
    {
      ++n;
    }
  }
  return n;
}

int UChunkMeshCache::CountPendingGpuAppliesInHorizontalRadius(
    glm::ivec3 center_ground_chunk, int radius_chunks) const
{
  if (radius_chunks < 0)
  {
    return 0;
  }
  int count = 0;
  for (const PendingGpuApply &pending : PendingGpuApplies)
  {
    const int dx = std::abs(pending.coord.x - center_ground_chunk.x);
    const int dz = std::abs(pending.coord.z - center_ground_chunk.z);
    if (std::max(dx, dz) <= radius_chunks)
    {
      ++count;
    }
  }
  return count;
}

int UChunkMeshCache::DropRemeshDirtyBeyondRadius(glm::ivec3 center_chunk,
                                                int keep_radius, int keep_cy,
                                                bool remesh_only)
{
  if (keep_radius < 0)
  {
    return 0;
  }
  auto beyond_keep = [&](const glm::ivec3 &coord) {
    const int horiz = std::max(std::abs(coord.x - center_chunk.x),
                               std::abs(coord.z - center_chunk.z));
    if (horiz > keep_radius)
    {
      return true;
    }
    if (keep_cy >= 0 && std::abs(coord.y - center_chunk.y) > keep_cy)
    {
      return true;
    }
    return false;
  };
  int dropped = 0;
  for (auto it = Dirty.begin(); it != Dirty.end();)
  {
    if (!beyond_keep(*it))
    {
      ++it;
      continue;
    }
    // Cruise: never drop first-mesh Dirty (creates holes in the focus ring).
    // Empty SoftDefer placeholders are !Drawable — protect like !HasGreedy.
    if (remesh_only && !HasDrawableGreedyMesh(*it) &&
        RemeshAfterApply.find(*it) == RemeshAfterApply.end())
    {
      ++it;
      continue;
    }
    RemeshAfterApply.erase(*it);
    it = Dirty.RemoveAt(it);
    ++dropped;
  }
  for (auto it = RemeshAfterApply.begin(); it != RemeshAfterApply.end();)
  {
    if (beyond_keep(*it))
    {
      it = RemeshAfterApply.erase(it);
      ++dropped;
    }
    else
    {
      ++it;
    }
  }
  return dropped;
}

int UChunkMeshCache::DropFarFirstMeshDirtyBeyondRadius(
    glm::ivec3 center_chunk, int keep_radius, int keep_cy)
{
  if (keep_radius < 0)
  {
    return 0;
  }
  auto beyond_keep = [&](const glm::ivec3 &coord) {
    const int horiz = std::max(std::abs(coord.x - center_chunk.x),
                               std::abs(coord.z - center_chunk.z));
    if (horiz > keep_radius)
    {
      return true;
    }
    if (keep_cy >= 0 && std::abs(coord.y - center_chunk.y) > keep_cy)
    {
      return true;
    }
    return false;
  };
  int dropped = 0;
  for (auto it = Dirty.begin(); it != Dirty.end();)
  {
    if (!beyond_keep(*it) || !Dirty.IsFirstMesh(*it))
    {
      ++it;
      continue;
    }
    RemeshAfterApply.erase(*it);
    it = Dirty.RemoveAt(it);
    ++dropped;
  }
  return dropped;
}

bool UChunkMeshCache::HasDirtyInColumnBand(glm::ivec2 ground_xz, int min_y,
                                           int max_y) const
{
  const int cy0 = FloorDiv(min_y, CHUNK_SIZE);
  const int cy1 = FloorDiv(max_y, CHUNK_SIZE);
  for (const glm::ivec3 &coord : Dirty)
  {
    if (coord.x == ground_xz.x && coord.z == ground_xz.y &&
        coord.y >= cy0 && coord.y <= cy1)
    {
      return true;
    }
  }
  return false;
}

bool UChunkMeshCache::HasSoftDeferHeldInColumn(glm::ivec2 ground_xz) const
{
  for (const glm::ivec3 &coord : SoftDeferHeld)
  {
    if (coord.x == ground_xz.x && coord.z == ground_xz.y)
    {
      return true;
    }
  }
  return false;
}

void UChunkMeshCache::HoldSoftDeferFirstMesh(glm::ivec3 chunk_coord)
{
  const bool inserted = SoftDeferHeld.insert(chunk_coord).second;
  // Era15 TD-050 / Era22 I-S2: Held must not park FirstMesh outside ColumnFlow.
  // Re-fire on insert; Requeue refreshes Contains while Held stays empty.
  if (inserted && OnSoftDeferHeld)
  {
    OnSoftDeferHeld(chunk_coord);
  }
  constexpr size_t kSoftDeferHeldCap = 384;
  if (SoftDeferHeld.size() <= kSoftDeferHeldCap)
  {
    return;
  }
  // Drop an arbitrary far entry so the side-set cannot grow unboundedly.
  // Era51: never drop EnterTerminalHeld SoftDefer under enter gate.
  for (auto it = SoftDeferHeld.begin(); it != SoftDeferHeld.end(); ++it)
  {
    if (EnterTerminalHeld.count(*it) == 0)
    {
      SoftDeferHeld.erase(it);
      return;
    }
  }
}

void UChunkMeshCache::HoldEnterTerminal(glm::ivec3 chunk_coord)
{
  HoldSoftDeferFirstMesh(chunk_coord);
  EnterTerminalHeld.insert(chunk_coord);
}

void UChunkMeshCache::ClearEnterTerminalHeld()
{
  EnterTerminalHeld.clear();
  ClearEnterGateDoneColumns();
  ClearEnterVoidTelemLitReadyFn();
  EnterPhantomDirtyPrunedTotal = 0;
}

void UChunkMeshCache::SyncEnterGateDoneColumns(
    const std::vector<glm::ivec2> &done_cols)
{
  EnterGateDoneColumns.clear();
  EnterGateDoneColumns.insert(done_cols.begin(), done_cols.end());
}

void UChunkMeshCache::ClearEnterGateDoneColumns()
{
  EnterGateDoneColumns.clear();
}

int UChunkMeshCache::PruneEnterPhantomDirty(const UBlockWorld &world)
{
  if (!EnterGpuQuiesceDrain)
  {
    return 0;
  }
  int pruned = 0;
  for (auto it = Dirty.begin(); it != Dirty.end();)
  {
    const glm::ivec3 &c = *it;
    if (EnterTerminalHeld.count(c) > 0)
    {
      RemeshAfterApply.erase(c);
      it = Dirty.RemoveAt(it);
      ++pruned;
      continue;
    }
    if (!world.GetChunkManager().HasChunk(c))
    {
      RemeshAfterApply.erase(c);
      it = Dirty.RemoveAt(it);
      ++pruned;
      continue;
    }
    if (!HasDrawableGreedyMesh(c) && !HasGreedyMesh(c) &&
        !HasInflightMeshBuild(c) && !IsPendingGpuApply(c) &&
        SoftDeferHeld.count(c) == 0)
    {
      RemeshAfterApply.erase(c);
      it = Dirty.RemoveAt(it);
      ++pruned;
      continue;
    }
    ++it;
  }
  EnterPhantomDirtyPrunedTotal += static_cast<uint64_t>(pruned);
  return pruned;
}

void UChunkMeshCache::ClearDirtyAndRemeshAfterApply(glm::ivec3 chunk_coord)
{
  Dirty.Erase(chunk_coord);
  RemeshAfterApply.erase(chunk_coord);
}

void UChunkMeshCache::NoteSoftDeferEmptyPublishAvoided(glm::ivec3 coord)
{
  ++SoftDeferEmptyPublishAvoided;
  SoftDeferEmptyAvoidFrames[coord] = 0;
}

void UChunkMeshCache::AgeSoftDeferEmptyAvoidFrames()
{
  for (auto it = SoftDeferEmptyAvoidFrames.begin();
       it != SoftDeferEmptyAvoidFrames.end();)
  {
    ++it->second;
    if (it->second > 120)
    {
      it = SoftDeferEmptyAvoidFrames.erase(it);
    }
    else
    {
      ++it;
    }
  }
}

void UChunkMeshCache::MaybeMarkDirtyAfterSoftDeferEmptyAvoid(glm::ivec3 coord)
{
  const bool has_ticket =
      IsPendingGpuApply(coord) || HasInflightMeshBuild(coord) ||
      IsPendingGpuQueued(coord) || IsPendingGpuKickedOrDispatched(coord) ||
      Dirty.Contains(coord);
  int frames = 999;
  const auto it = SoftDeferEmptyAvoidFrames.find(coord);
  if (it != SoftDeferEmptyAvoidFrames.end())
  {
    frames = it->second;
  }
  if (SoftDeferEmptyShouldMarkDirtyAfterAvoid(has_ticket, frames))
  {
    MarkDirtyPriority(coord);
  }
}

void UChunkMeshCache::RequeueSoftDeferHeld()
{
  if (SoftDeferHeld.empty())
  {
    return;
  }
  const int kRequeueBudget = std::max(0, WorkAdmission.softdefer_requeue);
  int requeued = 0;
  int ticket_refresh = 0;
  for (auto it = SoftDeferHeld.begin(); it != SoftDeferHeld.end();)
  {
    const glm::ivec3 coord = *it;
    // Era51b: enter SoftDefer terminal keeps SoftDeferHeld even if drawable FullyDark
    // (Hide⇒Ticket — exclude from void telem / no Dirty requeue).
    if (EnterGpuQuiesceDrain && EnterTerminalHeld.count(coord) > 0)
    {
      ++it;
      continue;
    }
    if (HasDrawableGreedyMesh(coord) || IsPendingGpuApply(coord) ||
        HasInflightMeshBuild(coord) || Dirty.Contains(coord) ||
        HasMeshSatisfyingColumnReady(coord))
    {
      it = SoftDeferHeld.erase(it);
      continue;
    }
    const bool still_deferred =
        DeferMeshUntilLit && DeferMeshUntilLit(coord);
    bool in_focus = false;
    if (MeshFocusValid)
    {
      const int horiz =
          std::max(std::abs(coord.x - MeshFocusGroundChunk.x),
                   std::abs(coord.z - MeshFocusGroundChunk.z));
      in_focus = horiz <= MeshFocusRadiusChunks;
    }
    const bool miss_or_focus = StarveRemeshForHoles || in_focus;
    // Era22/Era24: refresh FirstMesh Contains while Held. Under miss/focus
    // SoftDefer empty ownership — rim soft cap (not global 2).
    const int ticket_cap = miss_or_focus ? 8 : 2;
    if (OnSoftDeferHeld && ticket_refresh < ticket_cap)
    {
      OnSoftDeferHeld(coord);
      ++ticket_refresh;
    }
    // ColPipe P5: while SoftDefer still holds publication, ticket-only —
    // MarkDirty every tick refeeds dirty_revisit + remesh thrash.
    if (still_deferred)
    {
      ++it;
      continue;
    }
    // SoftDefer lifted: one Dirty admit then drop Held (FirstMesh owns heal).
    if (requeued >= kRequeueBudget || !TryConsumeDirtyAdmit())
    {
      ++it;
      continue;
    }
    it = SoftDeferHeld.erase(it);
    MarkDirtyPriority(coord);
    ++requeued;
  }
}

void UChunkMeshCache::MarkDirty(glm::ivec3 chunkCoord)
{
  // Era51: EnterTerminalHeld SoftDefer survives MarkDirty under enter gate.
  const bool keep_terminal =
      EnterGpuQuiesceDrain && EnterTerminalHeld.count(chunkCoord) > 0;
  if (keep_terminal)
  {
    // PreferKick only — do not re-Dirty FullyDark terminal remesh churn.
    if (IsPendingGpuApply(chunkCoord))
    {
      PreferKickPendingGpuQueued(chunkCoord);
    }
    return;
  }
  // FZ2.3-O3: already scheduled this emerge tick — skip re-Dirty ping-pong.
  if (ScheduledThisFrame_.count(chunkCoord) > 0)
  {
    ++LastDirtyScheduleDedupN;
    return;
  }
  SoftDeferHeld.erase(chunkCoord);
  // Mid-flight MarkDirty used to re-insert Dirty while Active stayed set —
  // Apply then immediately rescheduled forever (standing Dirty≈535 async=42).
  // Defer one remesh after Apply instead of stacking Dirty.
  if (ActiveMeshSourceRevision.find(chunkCoord) !=
      ActiveMeshSourceRevision.end())
  {
    if (IsPendingGpuApply(chunkCoord))
    {
      PreferKickPendingGpuQueued(chunkCoord);
      return;
    }
    // Era50: FullyDark under enter drain → Dirty (not RAA PreferKick loop).
    if (ChunkHasFullyDarkFace(chunkCoord) && HasDrawableGreedyMesh(chunkCoord) &&
        !EnterLitQuiesce)
    {
      // fall through to Dirty.MarkDirty below (erase Active gate via Dirty)
    }
    else
    {
      // Era47 P3 / Era50: EnterLitQuiesce only when worklist remaining==0.
      if (EnterLitQuiesce && HasDrawableGreedyMesh(chunkCoord))
      {
        return;
      }
      if (RemeshAfterApply.count(chunkCoord) > 0 || Dirty.Contains(chunkCoord))
      {
        return;
      }
      RemeshAfterApply.insert(chunkCoord);
      ++MarkDirtyToRaaN;
      return;
    }
  }
  SoftDeferHeld.erase(chunkCoord);
  const size_t before = Dirty.GetCount();
  Dirty.MarkDirty(chunkCoord);
  if (Dirty.GetCount() == before)
  {
    return;
  }
  BumpChunkMeshRevision(chunkCoord);
  // Do not InvalidateFluidSurface here: full-column remesh calls MarkDirty for
  // every cy×seam and kept fluid_map_dirty permanently high (100+), burning
  // 100–500ms/frame. Fluid map is invalidated once per column on gen/light.
  InstancesDirty = true;
  GreedyBatchesDirty = true;
  CrossBatchesDirty = true;
}
void UChunkMeshCache::MarkDirtyPriority(glm::ivec3 chunkCoord)
{
  const bool keep_terminal =
      EnterGpuQuiesceDrain && EnterTerminalHeld.count(chunkCoord) > 0;
  if (keep_terminal)
  {
    if (IsPendingGpuApply(chunkCoord))
    {
      PreferKickPendingGpuQueued(chunkCoord);
    }
    return;
  }
  if (ScheduledThisFrame_.count(chunkCoord) > 0)
  {
    ++LastDirtyScheduleDedupN;
    return;
  }
  const bool was_soft_held = SoftDeferHeld.count(chunkCoord) > 0;
  SoftDeferHeld.erase(chunkCoord);
  if (ActiveMeshSourceRevision.find(chunkCoord) !=
      ActiveMeshSourceRevision.end())
  {
    // Hole (!Drawable): Active+RemeshAfterApply-only left miss sticky after
    // CancelOutside / epoch DiscardedLate (manual 213543). Invalidate only
    // orphan Active (no builder / GPU pending) — invalidating live flight
    // every SoftDefer tick caused DiscardedLate storms (phase2 autofly).
    if (!HasDrawableGreedyMesh(chunkCoord))
    {
      const bool inflight =
          AsyncBuilder && AsyncBuilder->IsInFlight(chunkCoord);
      const bool gpu_pending =
          GpuExtractInFlight.find(chunkCoord) != GpuExtractInFlight.end();
      const bool pending_gpu_apply = IsPendingGpuApply(chunkCoord);
      // Era27 I-A4: SoftDefer empty / Hide⇒Ticket undrawn with live Active or
      // PendingReplace — hold supersede (no Forget → discarded_late hole).
      const bool soft_undrawn =
          was_soft_held || HasGreedyMesh(chunkCoord);
      if (pending_gpu_apply)
      {
        PreferKickPendingGpuQueued(chunkCoord);
        return;
      }
      // Convergence (manual 180247): SoftDefer empty under EnterLitQuiesce must
      // enter Dirty for FirstMesh. Hardcoded has_inflight=true + RAA park left
      // dirty=0 missing=1 forever (RAA↔SoftDeferHeld, never schedule).
      const bool live_flight = inflight || gpu_pending;
      if (EnterLitQuiesce && soft_undrawn)
      {
        if (live_flight)
        {
          InvalidateInFlightMeshBuild(chunkCoord);
          if (AsyncBuilder)
          {
            AsyncBuilder->ForgetInflight(chunkCoord);
          }
        }
        RemeshAfterApply.erase(chunkCoord);
        // fall through to Dirty.MarkDirtyPriority below
      }
      else if (live_flight ||
               ShouldHoldInflightSupersedeUnderMissUndrawn(
                   soft_undrawn, live_flight, /*has_drawable=*/false))
      {
        if (RemeshAfterApply.count(chunkCoord) > 0 ||
            Dirty.Contains(chunkCoord))
        {
          return;
        }
        RemeshAfterApply.insert(chunkCoord);
        ++MarkDirtyToRaaN;
        return;
      }
      else
      {
        InvalidateInFlightMeshBuild(chunkCoord);
        if (AsyncBuilder)
        {
          AsyncBuilder->ForgetInflight(chunkCoord);
        }
      }
    }
    else
    {
      if (IsPendingGpuApply(chunkCoord))
      {
        PreferKickPendingGpuQueued(chunkCoord);
        return;
      }
      // Era50: FullyDark remesh under enter drain must enter Dirty (async→GPU).
      // RAA park + EnterGpuQuiesceDrain PreferKick-only = remesh no-op loop.
      if (ChunkHasFullyDarkFace(chunkCoord) && !EnterLitQuiesce)
      {
        const bool existed_dark = Dirty.Contains(chunkCoord);
        Dirty.MarkDirtyPriority(chunkCoord);
        if (!existed_dark)
        {
          BumpChunkMeshRevision(chunkCoord);
        }
        InstancesDirty = true;
        GreedyBatchesDirty = true;
        CrossBatchesDirty = true;
        return;
      }
      // Era47 P3 / Era50: EnterLitQuiesce only when worklist remaining==0.
      if (EnterLitQuiesce)
      {
        return;
      }
      if (RemeshAfterApply.count(chunkCoord) > 0 || Dirty.Contains(chunkCoord))
      {
        return;
      }
      RemeshAfterApply.insert(chunkCoord);
      ++MarkDirtyToRaaN;
      return;
    }
  }
  // Re-prioritize / re-queue: bump only when newly entering Dirty.
  const bool existed = Dirty.Contains(chunkCoord);
  Dirty.MarkDirtyPriority(chunkCoord);
  if (!existed)
  {
    BumpChunkMeshRevision(chunkCoord);
  }
  InstancesDirty = true;
  GreedyBatchesDirty = true;
  CrossBatchesDirty = true;
}
void UChunkMeshCache::EnsureGpuPipeline()
{
#if !defined(__ANDROID__) && !defined(CUBATARIUM_GLES)
  if (!GpuPipelineInitAttempted)
  {
    GpuPipelineInitAttempted = true;
    if (Render.GpuPackedMeshing)
    {
      GpuPipeline = std::make_unique<UGpuMeshPipeline>();
      if (!GpuPipeline->Init())
      {
        GpuPipeline.reset();
      }
    }
  }
#endif
}

UGpuMeshPipeline *UChunkMeshCache::GetGpuMeshPipeline()
{
  EnsureGpuPipeline();
  return GpuPipeline.get();
}

const UGpuMeshPipeline *UChunkMeshCache::GetGpuMeshPipeline() const
{
  return GpuPipeline.get();
}

void UChunkMeshCache::RemoveChunk(glm::ivec3 chunkCoord)
{
  if (GpuPipeline)
  {
    GpuPipeline->FreeChunk(chunkCoord);
  }
  GpuExtractInFlight.erase(chunkCoord);
  for (auto it = PendingGpuApplies.begin(); it != PendingGpuApplies.end();)
  {
    if (it->coord == chunkCoord)
    {
      ActiveMeshSourceRevision.erase(chunkCoord);
      it = PendingGpuApplies.erase(it);
      TouchPendingGpuIndex();
    }
    else
    {
      ++it;
    }
  }
  Cache.erase(chunkCoord);
  GreedyCache.erase(chunkCoord);
  NoteGeometryDirty(chunkCoord);
  const auto vIt = GreedyVertexCountByChunk.find(chunkCoord);
  if (vIt != GreedyVertexCountByChunk.end())
  {
    GreedyVertexCountTotal -= vIt->second;
    GreedyVertexCountByChunk.erase(vIt);
  }
  Dirty.Erase(chunkCoord);
  MeshRevisions.Erase(chunkCoord);
  ActiveMeshSourceRevision.erase(chunkCoord);
  CaptureStore.Invalidate(chunkCoord);
  SoftDeferHeld.erase(chunkCoord);
  EnterTerminalHeld.erase(chunkCoord);
  GreedyBatchesDirty = true;
  if (chunkCoord.y == 0)
  {
    const glm::ivec3 ground(chunkCoord.x, 0, chunkCoord.z);
    FluidSurfaceCache.erase(ground);
    FluidSurfaceDirty.erase(ground);
  }
  ++MeshRevision;
  InstancesDirty = true;
  CrossBatchesDirty = true;
  InvalidateVisibleList();
}

void UChunkMeshCache::RemoveColumn(glm::ivec3 ground_coord, int max_cy)
{
  if (ground_coord.y != 0)
  {
    ground_coord.y = 0;
  }
  max_cy = std::max(0, max_cy);
  for (int cy = 0; cy <= max_cy; ++cy)
  {
    const glm::ivec3 slice(ground_coord.x, cy, ground_coord.z);
    if (GpuPipeline)
    {
      GpuPipeline->FreeChunk(slice);
    }
    Cache.erase(slice);
    GreedyCache.erase(slice);
    NoteGeometryDirty(slice);
    const auto vIt = GreedyVertexCountByChunk.find(slice);
    if (vIt != GreedyVertexCountByChunk.end())
    {
      GreedyVertexCountTotal -= vIt->second;
      GreedyVertexCountByChunk.erase(vIt);
    }
    Dirty.Erase(slice);
    MeshRevisions.Erase(slice);
    ActiveMeshSourceRevision.erase(slice);
    CaptureStore.Invalidate(slice);
    SoftDeferHeld.erase(slice);
    if (AsyncBuilder)
    {
      AsyncBuilder->ForgetInflight(slice);
    }
  }
  FluidSurfaceCache.erase(ground_coord);
  FluidSurfaceDirty.erase(ground_coord);
  ++MeshRevision;
  GreedyBatchesDirty = true;
  InstancesDirty = true;
  CrossBatchesDirty = true;
  InvalidateVisibleList();
}
size_t UChunkMeshCache::GetGreedyVertexCount() const
{
  return GreedyVertexCountTotal;
}
void UChunkMeshCache::RebuildFlatInstanceList(const Frustum *frustum,
                                              const glm::vec3 *cameraPos,
                                              float maxCullDistance)
{
  const bool horizontal_cull = UseHorizontalCullDistance();
  Instances.clear();
  for (const auto &entry : Cache)
  {
    if (frustum && cameraPos)
    {
      if (!frustum->IntersectsChunkAABB(ChunkAABBMin(entry.first),
                                        ChunkAABBMax(entry.first), *cameraPos,
                                        maxCullDistance, horizontal_cull))
      {
        continue;
      }
    }
    Instances.insert(Instances.end(), entry.second.begin(), entry.second.end());
  }
  InstancesDirty = false;
  ++CullRevision;
}
bool UChunkMeshCache::TrySkipFlatRebuildForVisibleChunks(
    const Frustum *frustum, const glm::vec3 *cameraPos, float maxCullDistance)
{
  if (!frustum || !cameraPos)
  {
    return false;
  }
  if (PendingMeshRevisionBump)
  {
    return false;
  }
  const glm::ivec3 cam_chunk = UChunkManager::WorldToChunk(
      glm::ivec3(WorldCoordToBlockIndex(cameraPos->x),
                 WorldCoordToBlockIndex(cameraPos->y),
                 WorldCoordToBlockIndex(cameraPos->z)));
  // Same camera chunk + mesh + quantized look (2°) → reuse LastVisibleChunks
  // without rescanning GreedyCache (TD-CS-018). Raw plane eps was too tight.
  const CullViewKeyAngles look = QuantizeCullViewKey(*frustum);
  const bool view_same = HaveLastCullViewKey && look.iyaw == LastCullIYaw &&
                         look.ipitch == LastCullIPitch;
  if (cam_chunk == LastCullCameraChunk &&
      MeshRevision == LastVisibleMeshRevision && view_same &&
      !LastVisibleChunks.empty() && HasAnyValidatedDrawRefs())
  {
    return true;
  }
  const bool horizontal_cull = UseHorizontalCullDistance();
  std::vector<glm::ivec3> visible;
  visible.reserve(GreedyCache.size());
  for (const auto &entry : GreedyCache)
  {
    if (!frustum->IntersectsChunkAABB(ChunkAABBMin(entry.first),
                                      ChunkAABBMax(entry.first), *cameraPos,
                                      maxCullDistance, horizontal_cull))
    {
      continue;
    }
    visible.push_back(entry.first);
  }
  std::sort(visible.begin(), visible.end(),
            [](const glm::ivec3 &a, const glm::ivec3 &b)
            {
              if (a.x != b.x)
              {
                return a.x < b.x;
              }
              if (a.y != b.y)
              {
                return a.y < b.y;
              }
              return a.z < b.z;
            });
  const bool have_draw_refs = HasAnyValidatedDrawRefs();
  // Empty==empty + unchanged revision would lock a sky hole forever
  // (manual 083819: first frustum miss, then skip until look/revision moved).
  // Stale GpuPacked refs (cache GpuResident but slot freed) must not count.
  if (visible == LastVisibleChunks && MeshRevision == LastVisibleMeshRevision &&
      (have_draw_refs || !visible.empty()))
  {
    LastCullCameraChunk = cam_chunk;
    LastCullPlanes = frustum->planes;
    HaveLastCullPlanes = true;
    LastCullIYaw = look.iyaw;
    LastCullIPitch = look.ipitch;
    HaveLastCullViewKey = true;
    return true;
  }
  LastVisibleChunks = std::move(visible);
  LastVisibleMeshRevision = MeshRevision;
  LastCullCameraChunk = cam_chunk;
  LastCullPlanes = frustum->planes;
  HaveLastCullPlanes = true;
  LastCullIYaw = look.iyaw;
  LastCullIPitch = look.ipitch;
  HaveLastCullViewKey = true;
  return false;
}
void UChunkMeshCache::RebuildFlatGreedyBatches(const Frustum *frustum,
                                               const glm::vec3 *cameraPos,
                                               float maxCullDistance)
{
  // Rate-limit full greedy batch rebuilds: light edits can produce many mesh
  // results per second; rebuilding the full flat batch list every frame can
  // dominate CPU time. Era21 I-R2: never skip after residency demote / keep-GPU
  // (stale GpuPacked refs → sky hole while GetSlot null).
  if (GreedyBatchesDirty)
  {
    const auto now = std::chrono::steady_clock::now();
    const bool have_draw_refs = HasAnyValidatedDrawRefs();
    // Empty refs + live cache is a sky hole — do not keep the last empty
    // rebuild across the 50ms rate limit (manual 083819: 265k verts, opaque=0).
    // Stale GpuPacked list (non-empty but no live slots) is the same class.
    if (!ForceFlatRebuildNext && have_draw_refs &&
        LastFlatRebuildAt != std::chrono::steady_clock::time_point{} &&
        now - LastFlatRebuildAt < std::chrono::milliseconds(50))
    {
      return;
    }
    ForceFlatRebuildNext = false;
  }
  if (frustum && cameraPos &&
      !GreedyBatchesDirty &&
      TrySkipFlatRebuildForVisibleChunks(frustum, cameraPos, maxCullDistance))
  {
    GreedyBatchesDirty = false;
    return;
  }
  const auto t0 = std::chrono::high_resolution_clock::now();
  const bool horizontal_cull = UseHorizontalCullDistance();
  if (frustum && cameraPos)
  {
    // Frustum-culling yields a view-dependent ref list; no persistent index.
  }
  GreedyOpaqueCutoutRefs.clear();
  GreedyTransparentRefs.clear();
  GpuPackedOpaqueRefs.clear();
  GpuPackedTransparentRefs.clear();
  GreedyOpaqueCutoutRefs.reserve(GreedyCache.size() * 4);
  GreedyTransparentRefs.reserve(GreedyCache.size());
  GpuPackedOpaqueRefs.reserve(GreedyCache.size());
  GpuPackedTransparentRefs.reserve(GreedyCache.size());

  for (const auto &entry : GreedyCache)
  {
    if (!ChunkPassesFrustum(frustum, cameraPos, maxCullDistance, entry.first,
                            horizontal_cull))
    {
      continue;
    }
    if (entry.second.GpuResident && entry.second.GpuQuadCount > 0)
    {
      EnsureGpuPipeline();
      if (GpuPipeline && GpuPipeline->HasGpuMesh(entry.first))
      {
        GpuPackedChunkRef pref;
        pref.chunkCoord = entry.first;
        pref.slotIndex = entry.second.GpuSlotIndex;
        pref.blockRanges = entry.second.GpuBlockRanges;
        if (entry.second.GpuTransparent)
        {
          GpuPackedTransparentRefs.push_back(std::move(pref));
        }
        else
        {
          GpuPackedOpaqueRefs.push_back(std::move(pref));
        }
        continue;
      }
      // Stale residency: GreedyCache flags without live SSBO slot (manual
      // 105307: opaque_gpu_packed_n>0 but DrawPackedGpuMeshes no-op).
      ClearStaleGpuResidentFlags(entry.first);
      bool has_cpu_drawable = false;
      for (const GreedyMeshBatch &batch : entry.second.batches)
      {
        if (!batch.vertices.empty() && !batch.indices.empty())
        {
          has_cpu_drawable = true;
          break;
        }
      }
      if (!has_cpu_drawable)
      {
        MarkDirty(entry.first);
      }
    }
    const std::vector<GreedyMeshBatch> &batches = entry.second.batches;
    for (size_t i = 0; i < batches.size(); ++i)
    {
      const GreedyMeshBatch &chunk_batch = batches[i];
      if (chunk_batch.vertices.empty() || chunk_batch.indices.empty())
      {
        continue;
      }
      const GreedyBatchRef ref{entry.first, static_cast<uint16_t>(i)};
      if (chunk_batch.Transparent)
      {
        GreedyTransparentRefs.push_back(ref);
      }
      else
      {
        GreedyOpaqueCutoutRefs.push_back(ref);
      }
    }
  }

  // Same fallback as RebuildFlatCrossInstances: a bad/empty frustum must not
  // leave GreedyCache undrawn (sky-only while greedy_vertices stay high).
  if (frustum && cameraPos && GreedyOpaqueCutoutRefs.empty() &&
      GreedyTransparentRefs.empty() && GpuPackedOpaqueRefs.empty() &&
      GpuPackedTransparentRefs.empty() && !GreedyCache.empty())
  {
    RebuildFlatGreedyBatches(nullptr, nullptr, 0.0f);
    return;
  }

  GreedyBatchesDirty = false;
  ++CullRevision;
  LastFlatRebuildAt = std::chrono::steady_clock::now();
  LastFlatRebuildMs = std::chrono::duration<double, std::milli>(
                          std::chrono::high_resolution_clock::now() - t0)
                          .count();
}
void UChunkMeshCache::RebuildFlatCrossInstances(const Frustum *frustum,
                                                const glm::vec3 *cameraPos,
                                                float maxCullDistance)
{
  const bool horizontal_cull = UseHorizontalCullDistance();
  const auto merge_from_cache = [&](const Frustum *cull_frustum,
                                    const glm::vec3 *cull_camera,
                                    float cull_distance)
      -> std::unordered_map<BlockId, std::vector<CrossInstanceGpu>>
  {
    std::unordered_map<BlockId, std::vector<CrossInstanceGpu>> merged;
    for (const auto &entry : GreedyCache)
    {
      if (!ChunkPassesFrustum(cull_frustum, cull_camera, cull_distance,
                              entry.first, horizontal_cull))
      {
        continue;
      }
      for (const auto &pair : entry.second.crossCenters)
      {
        std::vector<CrossInstanceGpu> &dst = merged[pair.first];
        dst.insert(dst.end(), pair.second.begin(), pair.second.end());
      }
    }
    return merged;
  };

  std::unordered_map<BlockId, std::vector<CrossInstanceGpu>> merged =
      merge_from_cache(frustum, cameraPos, maxCullDistance);
  if (frustum && cameraPos && merged.empty() && !GreedyCache.empty() &&
      (CrossBatchesDirty || CrossBatches.empty()))
  {
    merged = merge_from_cache(nullptr, nullptr, 0.0f);
  }
  else if (frustum && cameraPos && merged.empty() && !GreedyCache.empty())
  {
    return;
  }
  CrossBatches.clear();
  CrossBatches.reserve(merged.size());
  for (auto &pair : merged)
  {
    CrossInstanceBatch batch;
    batch.blockId = pair.first;
    batch.instances = std::move(pair.second);
    CrossBatches.push_back(std::move(batch));
  }
  CrossBatchesDirty = false;
}
void UChunkMeshCache::RebuildGreedyVisibleForCull(
    const Frustum *frustum, const glm::vec3 *camera_pos,
    float max_cull_distance)
{
  RebuildFlatGreedyBatches(frustum, camera_pos, max_cull_distance);
}

void UChunkMeshCache::CollectGreedyCullSpheres(
    std::vector<CullSphereEntry> &out) const
{
  out.clear();
  out.reserve(GreedyCache.size());
  for (const auto &entry : GreedyCache)
  {
    const glm::vec3 bmin = ChunkAABBMin(entry.first);
    const glm::vec3 bmax = ChunkAABBMax(entry.first);
    const glm::vec3 center = (bmin + bmax) * 0.5f;
    const float radius = glm::length(bmax - center);
    CullSphereEntry e;
    e.sphere = glm::vec4(center, radius);
    e.coord = entry.first;
    out.push_back(e);
  }
}

void UChunkMeshCache::RebuildFlatGreedyFromVisibilityMask(
    const uint32_t *vis, size_t vis_count,
    const std::vector<CullSphereEntry> &entries)
{
  const auto t0 = std::chrono::high_resolution_clock::now();
  GreedyOpaqueCutoutRefs.clear();
  GreedyTransparentRefs.clear();
  GreedyOpaqueCutoutRefs.reserve(GreedyCache.size() * 4);
  GreedyTransparentRefs.reserve(GreedyCache.size());
  const size_t n = (std::min)(vis_count, entries.size());
  for (size_t i = 0; i < n; ++i)
  {
    if (!vis[i])
    {
      continue;
    }
    const auto it = GreedyCache.find(entries[i].coord);
    if (it == GreedyCache.end())
    {
      continue;
    }
    const std::vector<GreedyMeshBatch> &batches = it->second.batches;
    for (size_t bi = 0; bi < batches.size(); ++bi)
    {
      const GreedyMeshBatch &chunk_batch = batches[bi];
      if (chunk_batch.vertices.empty() || chunk_batch.indices.empty())
      {
        continue;
      }
      const GreedyBatchRef ref{entries[i].coord, static_cast<uint16_t>(bi)};
      if (chunk_batch.Transparent)
      {
        GreedyTransparentRefs.push_back(ref);
      }
      else
      {
        GreedyOpaqueCutoutRefs.push_back(ref);
      }
    }
  }
  GreedyBatchesDirty = false;
  ++CullRevision;
  LastFlatRebuildAt = std::chrono::steady_clock::now();
  LastFlatRebuildMs = std::chrono::duration<double, std::milli>(
                          std::chrono::high_resolution_clock::now() - t0)
                          .count();
}

void UChunkMeshCache::UpdateVisibleInstances(const Frustum &frustum,
                                             const glm::mat4 &viewProj,
                                             const glm::vec3 &cameraPos)
{
  (void)viewProj;
  const float maxCullDistance = MaxCullDistance();
  const glm::ivec3 camera_chunk =
      UChunkManager::WorldToChunk(WorldPosToBlock(cameraPos));
  const bool greedy_refs_empty =
      GreedyOpaqueCutoutRefs.empty() && GreedyTransparentRefs.empty() &&
      GpuPackedOpaqueRefs.empty() && GpuPackedTransparentRefs.empty();
  const bool needs_greedy_rebuild =
      GreedyBatchesDirty || (greedy_refs_empty && !GreedyCache.empty());
  const bool needs_cross_rebuild =
      CrossBatchesDirty ||
      (CrossBatches.empty() && TotalCrossCenterCount() > 0);
  const CullViewKeyAngles look = QuantizeCullViewKey(frustum);
  const bool view_same = HaveLastCullViewKey && look.iyaw == LastCullIYaw &&
                         look.ipitch == LastCullIPitch;
  // Skip only when camera chunk + mesh + quantized look are unchanged.
  if (!InstancesDirty && !needs_greedy_rebuild && !needs_cross_rebuild &&
      MeshRevision == LastCullMeshRevision &&
      camera_chunk == LastCullCameraChunk && view_same)
  {
    return;
  }
  if (Render.GreedyMeshing)
  {
    if (Render.FrustumCulling)
    {
      if (needs_greedy_rebuild)
      {
        const auto t0 = std::chrono::steady_clock::now();
        if (CullBackend)
        {
          CullBackend->RebuildVisible(*this, &frustum, &cameraPos,
                                      maxCullDistance);
        }
        else
        {
          RebuildFlatGreedyBatches(&frustum, &cameraPos, maxCullDistance);
        }
        LastGpuCullMs = std::chrono::duration<double, std::milli>(
                            std::chrono::steady_clock::now() - t0)
                            .count();
      }
      if (needs_cross_rebuild)
      {
        RebuildFlatCrossInstances(&frustum, &cameraPos, maxCullDistance);
      }
    }
    else
    {
      if (needs_greedy_rebuild)
      {
        RebuildFlatGreedyBatches(nullptr, nullptr, 0.0f);
        LastGpuCullMs = 0.0;
      }
      if (needs_cross_rebuild)
      {
        RebuildFlatCrossInstances(nullptr, nullptr, 0.0f);
      }
    }
  }
  else
  {
    if (Render.FrustumCulling)
    {
      RebuildFlatInstanceList(&frustum, &cameraPos, maxCullDistance);
    }
    else
    {
      RebuildFlatInstanceList(nullptr, nullptr, 0.0f);
    }
  }
  LastCullCameraChunk = camera_chunk;
  LastCullMeshRevision = MeshRevision;
  LastCullPlanes = frustum.planes;
  HaveLastCullPlanes = true;
  LastCullIYaw = look.iyaw;
  LastCullIPitch = look.ipitch;
  HaveLastCullViewKey = true;
}
void UChunkMeshCache::EnsureAsyncBuilder()
{
  if (!AsyncBuilder)
  {
    AsyncBuilder = std::make_unique<UAsyncMeshBuilder>();
  }
  if (AsyncBuilder && MesherBackend)
  {
    AsyncBuilder->SetMesher(MesherBackend);
  }
}

bool UChunkMeshCache::CommitGpuMeshResult(
    const UBlockWorld &world, UBlockRegistry &registry, glm::ivec3 coord,
    uint64_t source_revision, GpuMeshProcessResult &&gpu_result,
    std::unordered_map<BlockId, std::vector<CrossInstanceGpu>> cross_centers)
{
  (void)registry;
  if (!world.GetChunkManager().HasChunk(coord))
  {
    if (GpuPipeline && gpu_result.slotIndex >= 0)
    {
      GpuPipeline->GetAllocator().FreeSlotByIndex(gpu_result.slotIndex);
    }
    return false;
  }
  const bool defer_until_lit = DeferMeshUntilLit && DeferMeshUntilLit(coord);
  const bool had_mesh = HasDrawableGreedyMesh(coord);
  const bool had_lit_mesh = had_mesh && !ChunkHasFullyDarkFace(coord);
  const bool had_live_lit_gpu =
      ChunkHasLiveGpuDraw(coord) && !ChunkHasFullyDarkFace(coord);
  if (ShouldRejectDarkMeshCommit(gpu_result.hasFullyDarkFace,
                                 defer_until_lit && had_mesh, had_lit_mesh,
                                 had_live_lit_gpu))
  {
    // Free staging only — FreeChunk(coord) would drop the live lit mesh that
    // ProcessSnapshot used to overwrite in-place (opaque collapse 213543).
    if (GpuPipeline && gpu_result.slotIndex >= 0)
    {
      GpuPipeline->GetAllocator().FreeSlotByIndex(gpu_result.slotIndex);
    }
    const bool remesh_after = RemeshAfterApply.erase(coord) > 0;
    if (ShouldMarkDirtyAfterDarkSoftDeferReject(remesh_after, had_mesh))
    {
      MarkDirtyPriority(coord);
    }
    return false;
  }

  ChunkGreedyMesh &chunkMesh = GreedyCache[coord];
  if (GpuPipeline)
  {
    // Publish staging over any prior live slot (Bind frees the old index).
    GpuPipeline->GetAllocator().BindCommittedSlot(coord, gpu_result.slotIndex);
  }
  chunkMesh.GpuResident = true;
  chunkMesh.GpuSlotIndex = gpu_result.slotIndex;
  chunkMesh.GpuQuadCount = gpu_result.quadCount;
  chunkMesh.GpuTransparent = gpu_result.transparent;
  chunkMesh.GpuHasDarkFace = gpu_result.hasFullyDarkFace;
  chunkMesh.GpuBlockRanges = std::move(gpu_result.blockRanges);
  chunkMesh.batches.clear();
  chunkMesh.crossCenters = std::move(cross_centers);
  GreedyVertexCountByChunk[coord] = 0;
  NoteGeometryDirty(coord);
  PendingMeshRevisionBump = true;
  InstancesDirty = true;
  CrossBatchesDirty = true;
  GreedyBatchesDirty = true;
  // Era49: lit GPU outcome clears StickyRemesh work-set (ready ≠ schedule).
  if (!gpu_result.hasFullyDarkFace && OnLitDrawableCommitted)
  {
    OnLitDrawableCommitted(coord);
  }
  // While enter worklist still draining, FullyDark+stale gets one Dirty.
  // After lit quiesce (remaining==0) stop the commit→Dirty pump — cruise heals.
  if (EnterGpuQuiesceDrain && !EnterLitQuiesce &&
      gpu_result.hasFullyDarkFace && !Dirty.Contains(coord))
  {
    const bool stale_lit_field = ChunkHasStaleDarkFaces(coord, world);
    // One remesh after light under enter gate — further FullyDark+sky is
    // residual cave dark (no-op Dirty spin forbidden by Enter SoT).
    if (stale_lit_field &&
        EnterFullyDarkStaleRemeshOnce.insert(coord).second)
    {
      MarkDirtyPriority(coord);
    }
  }
  // Era15 TD-050: Unlit FirstMesh publish → LitPending (not every dark remesh).
  if (OnLitPendingNeeded && !had_mesh &&
      (defer_until_lit || gpu_result.hasFullyDarkFace))
  {
    OnLitPendingNeeded(coord);
  }
  if (RemeshAfterApply.erase(coord) > 0)
  {
    const bool gpu_pending = IsPendingGpuApply(coord);
    const bool enter_gate =
        EnterGateBlocksRaaMarkDirty(EnterLitQuiesce, EnterGpuQuiesceDrain);
    const bool needs_first_mesh = !HasDrawableGreedyMesh(coord);
    const bool fully_dark_drawable =
        HasDrawableGreedyMesh(coord) && ChunkHasFullyDarkFace(coord);
    if (gpu_pending)
    {
      PreferKickPendingGpuQueued(coord);
    }
    else if (ShouldMarkDirtyAfterRemeshAfterApplyCommit(
                 Dirty.Contains(coord), gpu_pending, enter_gate,
                 needs_first_mesh, fully_dark_drawable))
    {
      // Closeout C: lit remesh → RemeshQ; missing/FullyDark → FirstMeshQ.
      if (needs_first_mesh || fully_dark_drawable)
      {
        MarkDirtyPriority(coord);
      }
      else
      {
        MarkDirty(coord);
      }
      ++RaaCommitMarkDirtyN;
    }
  }
  (void)source_revision;
  return true;
}

int UChunkMeshCache::DrainPendingGpuMeshes(UBlockWorld &world,
                                           UBlockRegistry &registry,
                                           int max_count, double budget_ms)
{
  MeshRebuildTickStats stats{};
  return ProcessPendingGpuMeshes(world, registry, max_count, budget_ms, stats);
}

int UChunkMeshCache::ConsumeGpuApplyBacklog(UBlockWorld &world,
                                           UBlockRegistry &registry,
                                           int max_drain, int gpu_max,
                                           double gpu_budget_ms)
{
  LastGpuKickN = 0;
  LastGpuFinishN = 0;
  LastGpuFinishNotReadyN = 0;
  int done = 0;
  MeshRebuildTickStats stats{};
  if (AsyncBuilder)
  {
    const MeshWorkAdmission &adm = WorkAdmission;
    int drain_cap = max_drain;
    if (adm.mode != MeshWorkAdmission::Mode::Normal)
    {
      drain_cap = std::max(drain_cap, adm.max_drain);
    }
    else if (!EnterGpuQuiesceDrain && PendingGpuApplies.size() >= 16)
    {
      drain_cap = std::min(drain_cap, 4);
    }
    for (MeshBuildResult &result : AsyncBuilder->DrainCompleted(drain_cap))
    {
      ApplyMeshResult(world, registry, std::move(result));
      ++done;
      ++stats.Completed;
    }
  }
  if (Render.GpuPackedMeshing && !PendingGpuApplies.empty() && gpu_max > 0)
  {
    const MeshWorkAdmission &adm = WorkAdmission;
    gpu_max = std::max(gpu_max, adm.gpu_apply_max);
    double budget = gpu_budget_ms;
    if (budget <= 0.0)
    {
      budget = std::max(6.0, MeshEmergeTotalBudgetMs * adm.gpu_budget_frac);
    }
    if ((adm.mode == MeshWorkAdmission::Mode::Normal || EnterGpuQuiesceDrain) &&
        PendingGpuApplies.size() >= 24)
    {
      gpu_max = std::max(gpu_max, 24);
      budget = std::max(budget, MeshEmergeTotalBudgetMs * 0.85);
    }
    const int gpu_done =
        ProcessPendingGpuMeshes(world, registry, gpu_max, budget, stats);
    done += gpu_done;
    if (gpu_done > 0)
    {
      GreedyBatchesDirty = true;
      CrossBatchesDirty = true;
    }
  }
  return done;
}

int UChunkMeshCache::ProcessPendingGpuMeshes(UBlockWorld &world,
                                           UBlockRegistry &registry,
                                           int max_count, double budget_ms,
                                           MeshRebuildTickStats &stats)
{
  EnsureGpuPipeline();
  UGpuMeshPipeline *pipeline = GpuPipeline.get();
  if (!pipeline || !pipeline->IsReady() || !Render.GpuPackedMeshing ||
      PendingGpuApplies.empty() || max_count <= 0)
  {
    return 0;
  }
  // budget_ms < 0 → unlimited; == 0 → no work; > 0 → elapsed cap.
  if (budget_ms == 0.0)
  {
    return 0;
  }

  const auto t0 = std::chrono::high_resolution_clock::now();
  auto elapsed_ms = [&]() {
    return std::chrono::duration<double, std::milli>(
               std::chrono::high_resolution_clock::now() - t0)
        .count();
  };
  auto budget_left = [&]() {
    return budget_ms < 0.0 || elapsed_ms() < budget_ms;
  };

  // Underfeet lease: finish/kick horiz≤1 before hinterland GPU backlog.
  {
    const glm::ivec3 focus = MeshFocusGroundChunk;
    std::stable_partition(
        PendingGpuApplies.begin(), PendingGpuApplies.end(),
        [&](const PendingGpuApply &p)
        {
          const int horiz =
              std::max(std::abs(p.coord.x - focus.x),
                       std::abs(p.coord.z - focus.z));
          return horiz <= 1;
        });
  }

  // Phase A (174511): ring PBO → up to 4 Kick/Finish per tick; no shared-PBO
  // single-apply bottleneck. Prefer Finish over Kick when budget tight.
  // J1/K2: under HoleDrain/Deep miss backlog, bias Finish vs Kick so ring clears
  // (manual 170330/174559 finish_bl med=3 while pending~15–30).
  const bool hole_finish_bias =
      (WorkAdmission.mode == MeshWorkAdmission::Mode::HoleDrain ||
       WorkAdmission.mode == MeshWorkAdmission::Mode::DeepBacklog) &&
      PendingGpuApplies.size() >= 12;
  const bool hole_finish_deep =
      hole_finish_bias && PendingGpuApplies.size() >= 16;
  int finish_cap =
      std::min(UGpuMeshPipeline::kReadbackRing, std::max(0, max_count));
  int kick_cap =
      std::min(UGpuMeshPipeline::kReadbackRing, std::max(0, max_count));
  if (hole_finish_bias)
  {
    finish_cap = std::min(UGpuMeshPipeline::kReadbackRing,
                          std::max(finish_cap, (max_count * 3 + 3) / 4));
    kick_cap = std::min(kick_cap, std::max(2, max_count / 2));
  }
  int processed = 0;
  int finished = 0;
  int kicked = 0;
  int finish_attempts = 0;

  auto fail_ticket = [&](PendingGpuApply &pending) {
    if (pending.ticket.valid && pending.ticket.slotIndex >= 0)
    {
      pipeline->GetAllocator().FreeSlotByIndex(pending.ticket.slotIndex);
    }
    if (pending.ticket.fence)
    {
#if !defined(__ANDROID__) && !defined(CUBATARIUM_GLES)
      glDeleteSync(pending.ticket.fence);
#endif
      pending.ticket.fence = nullptr;
    }
    pipeline->ReleaseReadbackSlot(pending.ticket);
    pending.ticket = {};
    ActiveMeshSourceRevision.erase(pending.coord);
    Dirty.MarkDirtyPriority(pending.coord);
  };

  auto revision_ok = [&](PendingGpuApply &pending,
                         bool &out_drop) -> bool {
    out_drop = false;
    const uint64_t expected_revision = MeshRevisions.Current(pending.coord);
    const auto revisionIt = ActiveMeshSourceRevision.find(pending.coord);
    const bool has_active = revisionIt != ActiveMeshSourceRevision.end();
    const uint64_t active_rev = has_active ? revisionIt->second : 0;
    const MeshApplyRevDecision decision = ClassifyMeshApplyRevision(
        has_active, active_rev, pending.sourceRevision, expected_revision);
    if (decision == MeshApplyRevDecision::DropNoActive)
    {
      fail_ticket(pending);
      if (!HasDrawableGreedyMesh(pending.coord) &&
          !Dirty.Contains(pending.coord))
      {
        Dirty.MarkDirtyPriority(pending.coord);
      }
      out_drop = true;
      return false;
    }
    if (decision == MeshApplyRevDecision::DiscardOlderKeepActive)
    {
      fail_ticket(pending);
      ++MeshApplySupersededCount;
      out_drop = true;
      return false;
    }
    if (decision == MeshApplyRevDecision::RemeshObsoleteTracked)
    {
      fail_ticket(pending);
      ++MeshApplyStaleCount;
      Dirty.MarkDirty(pending.coord);
      out_drop = true;
      return false;
    }
    return true;
  };

  // Pass B0: advance Dispatched (counter poll + emit). RectsHold per PBO →
  // multiple Dispatched OK (≤ kReadbackRing). NotReady leaves in place.
  for (size_t i = 0; i < PendingGpuApplies.size() && budget_left();)
  {
    if (PendingGpuApplies[i].phase != PendingGpuApply::Phase::Dispatched)
    {
      ++i;
      continue;
    }
    PendingGpuApply &pending_ref = PendingGpuApplies[i];
    bool dropped = false;
    if (!revision_ok(pending_ref, dropped))
    {
      GpuExtractInFlight.erase(pending_ref.coord);
      PendingGpuApplies.erase(PendingGpuApplies.begin() +
                              static_cast<std::ptrdiff_t>(i));
      TouchPendingGpuIndex();
      continue;
    }
    const auto st = pipeline->TryCompleteCountersAndEmit(
        pending_ref.ticket, registry, /*timeout_ns=*/0);
    if (st == UGpuMeshPipeline::GpuFinishStatus::NotReady)
    {
      ++LastGpuFinishNotReadyN;
      ++i;
      continue;
    }
    if (st == UGpuMeshPipeline::GpuFinishStatus::Failed)
    {
      fail_ticket(pending_ref);
      GpuExtractInFlight.erase(pending_ref.coord);
      PendingGpuApplies.erase(PendingGpuApplies.begin() +
                              static_cast<std::ptrdiff_t>(i));
      TouchPendingGpuIndex();
      continue;
    }
    if (pending_ref.ticket.quadCount == 0)
    {
      GpuMeshProcessResult gpu_result;
      gpu_result.success = true;
      gpu_result.slotIndex = pending_ref.ticket.slotIndex;
      gpu_result.quadCount = 0;
      gpu_result.transparent = pending_ref.transparent;
      PendingGpuApply pending = std::move(pending_ref);
      PendingGpuApplies.erase(PendingGpuApplies.begin() +
                              static_cast<std::ptrdiff_t>(i));
      TouchPendingGpuIndex();
      GpuExtractInFlight.erase(pending.coord);
      ActiveMeshSourceRevision.erase(pending.coord);
      if (CommitGpuMeshResult(world, registry, pending.coord,
                              pending.sourceRevision, std::move(gpu_result),
                              std::move(pending.crossCenters)))
      {
        ++processed;
        ++finished;
        ++stats.Completed;
      }
      continue;
    }
    pending_ref.phase = PendingGpuApply::Phase::Kicked;
    TouchPendingGpuIndex();
    ++i;
  }

  // Pass B: poll Kicked fences (timeout=0). NotReady leaves ticket in place —
  // never erase+push_back (deque iterator UB / spin). attempts_cap = finish_cap.
  for (size_t i = 0; i < PendingGpuApplies.size() && finished < finish_cap &&
                     finish_attempts < finish_cap && processed < max_count &&
                     budget_left();)
  {
    if (PendingGpuApplies[i].phase != PendingGpuApply::Phase::Kicked)
    {
      ++i;
      continue;
    }
    ++finish_attempts;
    PendingGpuApply &pending_ref = PendingGpuApplies[i];

    bool dropped = false;
    if (!revision_ok(pending_ref, dropped))
    {
      GpuExtractInFlight.erase(pending_ref.coord);
      PendingGpuApplies.erase(PendingGpuApplies.begin() +
                              static_cast<std::ptrdiff_t>(i));
      TouchPendingGpuIndex();
      continue;
    }

    GpuMeshProcessResult gpu_result;
    gpu_result.success = true;
    gpu_result.slotIndex = pending_ref.ticket.slotIndex;
    uint32_t quad_count = 0;
    const auto st = pipeline->TryFinishComputePasses(
        pending_ref.ticket, registry, quad_count, &gpu_result.blockRanges,
        &gpu_result.hasFullyDarkFace, /*timeout_ns=*/0);
    if (st == UGpuMeshPipeline::GpuFinishStatus::NotReady)
    {
      ++LastGpuFinishNotReadyN;
      ++i;
      continue;
    }
    PendingGpuApply pending = std::move(pending_ref);
    PendingGpuApplies.erase(PendingGpuApplies.begin() +
                            static_cast<std::ptrdiff_t>(i));
    TouchPendingGpuIndex();
    GpuExtractInFlight.erase(pending.coord);
    if (st == UGpuMeshPipeline::GpuFinishStatus::Failed)
    {
      fail_ticket(pending);
      continue;
    }
    gpu_result.quadCount = quad_count;
    gpu_result.transparent = pending.transparent;
    ActiveMeshSourceRevision.erase(pending.coord);
    if (CommitGpuMeshResult(world, registry, pending.coord,
                            pending.sourceRevision, std::move(gpu_result),
                            std::move(pending.crossCenters)))
    {
      ++processed;
      ++finished;
      ++stats.Completed;
    }
  }

  // Pass A: Kick Queued while free ring PBO + staging; stop new kicks late so
  // remaining budget can Finish fences that became ready during Kick.
  // J1/K2 miss backlog: cut Kick earlier (not freeze 0.55) so Finish catches up.
  // K2 pending≥16: kick_cut 0.68; M2 pending≥12: 0.70 — above discarded 0.55.
  // N0d: after hole clear, Normal + async refill still needs Finish share
  // (manual 215629 i=17/23 fin=0 wall~113); never use 0.55 under HoleDrain.
  const int async_inflight = GetAsyncInFlightCount();
  const double kick_cut =
      (WorkAdmission.mode == MeshWorkAdmission::Mode::HoleDrain ||
       WorkAdmission.mode == MeshWorkAdmission::Mode::DeepBacklog)
          ? (hole_finish_deep ? 0.68 : (hole_finish_bias ? 0.70 : 0.90))
          : ((async_inflight >= 16 ||
              PendingGpuApplies.size() >= 12)
                 ? 0.75
                 : 0.55);
  auto find_prefer_queued = [&]() {
    auto missing_it = std::find_if(
        PendingGpuApplies.begin(), PendingGpuApplies.end(),
        [&](const PendingGpuApply &p) {
          return p.phase == PendingGpuApply::Phase::Queued &&
                 !HasDrawableGreedyMesh(p.coord);
        });
    if (missing_it != PendingGpuApplies.end())
    {
      return missing_it;
    }
    return std::find_if(PendingGpuApplies.begin(), PendingGpuApplies.end(),
                        [](const PendingGpuApply &p) {
                          return p.phase == PendingGpuApply::Phase::Queued;
                        });
  };
  while (kicked < kick_cap && processed < max_count && budget_left() &&
         pipeline->HasFreeReadbackSlot())
  {
    if (budget_ms > 0.0 && elapsed_ms() >= budget_ms * kick_cut)
    {
      break; // Finish-only remainder — avoid Kick counter-sync storm
    }
    auto queued_it = find_prefer_queued();
    if (queued_it == PendingGpuApplies.end())
    {
      break;
    }

    PendingGpuApply pending = std::move(*queued_it);
    PendingGpuApplies.erase(queued_it);
    TouchPendingGpuIndex();
    GpuExtractInFlight.erase(pending.coord);

    bool dropped = false;
    if (!revision_ok(pending, dropped))
    {
      continue;
    }

    bool has_transparent = false;
    for (BlockId id : pending.snapshot.blocks)
    {
      if (id != 0 && (registry.IsTransparent(id) ||
                      registry.GetRenderStyle(id) == BlockRenderStyle::Cutout))
      {
        has_transparent = true;
        break;
      }
    }
    const int slot_idx =
        pipeline->GetAllocator().AllocateStagingSlot(has_transparent);
    if (slot_idx < 0)
    {
      PendingGpuApplies.push_front(std::move(pending));
      TouchPendingGpuIndex();
      GpuExtractInFlight.insert(PendingGpuApplies.front().coord);
      break;
    }
    if (!pipeline->KickComputePasses(pending.snapshot, registry, pending.coord,
                                     slot_idx, pending.ticket))
    {
      pipeline->GetAllocator().FreeSlotByIndex(slot_idx);
      ActiveMeshSourceRevision.erase(pending.coord);
      Dirty.MarkDirtyPriority(pending.coord);
      continue;
    }
    pending.transparent = has_transparent;
    pending.phase = pending.ticket.awaitingCounters
                        ? PendingGpuApply::Phase::Dispatched
                        : PendingGpuApply::Phase::Kicked;
    const glm::ivec3 kicked_coord = pending.coord;
    PendingGpuApplies.push_back(std::move(pending));
    TouchPendingGpuIndex();
    GpuExtractInFlight.insert(kicked_coord);
    ++kicked;
  }

  // J1: second Finish pass with Kick-cut remainder (fences ready mid-Kick).
  if (hole_finish_bias && finished < finish_cap && processed < max_count &&
      budget_left())
  {
    for (size_t i = 0; i < PendingGpuApplies.size() && finished < finish_cap &&
                       finish_attempts < finish_cap * 2 &&
                       processed < max_count && budget_left();)
    {
      if (PendingGpuApplies[i].phase != PendingGpuApply::Phase::Kicked)
      {
        ++i;
        continue;
      }
      ++finish_attempts;
      PendingGpuApply &pending_ref = PendingGpuApplies[i];

      bool dropped = false;
      if (!revision_ok(pending_ref, dropped))
      {
        GpuExtractInFlight.erase(pending_ref.coord);
        PendingGpuApplies.erase(PendingGpuApplies.begin() +
                                static_cast<std::ptrdiff_t>(i));
        TouchPendingGpuIndex();
        continue;
      }

      GpuMeshProcessResult gpu_result;
      gpu_result.success = true;
      gpu_result.slotIndex = pending_ref.ticket.slotIndex;
      uint32_t quad_count = 0;
      const auto st = pipeline->TryFinishComputePasses(
          pending_ref.ticket, registry, quad_count, &gpu_result.blockRanges,
          &gpu_result.hasFullyDarkFace, /*timeout_ns=*/0);
      if (st == UGpuMeshPipeline::GpuFinishStatus::NotReady)
      {
        ++LastGpuFinishNotReadyN;
        ++i;
        continue;
      }
      PendingGpuApply pending = std::move(pending_ref);
      PendingGpuApplies.erase(PendingGpuApplies.begin() +
                              static_cast<std::ptrdiff_t>(i));
      TouchPendingGpuIndex();
      GpuExtractInFlight.erase(pending.coord);
      if (st == UGpuMeshPipeline::GpuFinishStatus::Failed)
      {
        fail_ticket(pending);
        continue;
      }
      gpu_result.quadCount = quad_count;
      gpu_result.transparent = pending.transparent;
      ActiveMeshSourceRevision.erase(pending.coord);
      if (CommitGpuMeshResult(world, registry, pending.coord,
                              pending.sourceRevision, std::move(gpu_result),
                              std::move(pending.crossCenters)))
      {
        ++processed;
        ++finished;
        ++stats.Completed;
      }
    }
  }

  LastGpuKickN += kicked;
  LastGpuFinishN += finished;
  return processed;
}

void UChunkMeshCache::ApplyMeshResult(const UBlockWorld &world,
                                      UBlockRegistry &registry,
                                      MeshBuildResult &&result)
{
  if (!world.GetChunkManager().HasChunk(result.coord))
  {
    return;
  }
  const uint64_t expected_revision = MeshRevisions.Current(result.coord);
  const auto revisionIt = ActiveMeshSourceRevision.find(result.coord);
  if (revisionIt == ActiveMeshSourceRevision.end())
  {
    // CancelOutside / Invalidate cleared Active before Drain. Silent drop of a
    // hole left miss sticky while Dirty plateaued on remesh (manual 213543).
    if (!HasDrawableGreedyMesh(result.coord) &&
        !Dirty.Contains(result.coord))
    {
      Dirty.MarkDirtyPriority(result.coord);
      InstancesDirty = true;
      GreedyBatchesDirty = true;
      CrossBatchesDirty = true;
    }
    return;
  }
  const MeshApplyRevDecision decision = ClassifyMeshApplyRevision(
      true, revisionIt->second, result.sourceRevision, expected_revision);
  if (decision == MeshApplyRevDecision::DiscardOlderKeepActive)
  {
    // Older async result — keep Active tracking for the newer in-flight rev.
    ++MeshApplySupersededCount;
    return;
  }
  if (decision == MeshApplyRevDecision::RemeshObsoleteTracked)
  {
    ActiveMeshSourceRevision.erase(revisionIt);
    GpuExtractInFlight.erase(result.coord);
    // Tracked rev is obsolete vs Current — remesh WITHOUT bumping revision.
    // Remesh class only (TD-ARCH-029); MarkDirtyPriority flooded FirstMesh.
    ++MeshApplyStaleCount;
    Dirty.MarkDirty(result.coord);
    InstancesDirty = true;
    GreedyBatchesDirty = true;
    CrossBatchesDirty = true;
    return;
  }

  // GPU packed-quad path: defer GL compute out of ApplyMeshResult so async
  // drain stays fast and MeshEmergeTotalBudgetMs is not blown on one chunk.
  if (result.GpuExtractPending && result.PendingSnapshot)
  {
    EnsureGpuPipeline();
    if (Render.GpuPackedMeshing && GpuPipeline && GpuPipeline->IsReady())
    {
      // F1: remesh Queued respects ring budget; FirstMesh holes always enqueue.
      const bool missing_first =
          !HasDrawableGreedyMesh(result.coord);
      if (!missing_first && !TryConsumeEnqueueGpu())
      {
        ActiveMeshSourceRevision.erase(revisionIt);
        GpuExtractInFlight.erase(result.coord);
        // Era47 P3: under enter lit-quiesce park RAA instead of Dirty refeed.
        if (EnterLitQuiesce)
        {
          RemeshAfterApply.insert(result.coord);
          ++MarkDirtyToRaaN;
          if (IsPendingGpuApply(result.coord))
          {
            PreferKickPendingGpuQueued(result.coord);
          }
        }
        else
        {
          Dirty.MarkDirty(result.coord);
        }
        InstancesDirty = true;
        GreedyBatchesDirty = true;
        CrossBatchesDirty = true;
        return;
      }
      if (missing_first)
      {
        (void)TryConsumeEnqueueGpu(); // best-effort; never deny FirstMesh
      }
      PendingGpuApply pending;
      pending.coord = result.coord;
      pending.sourceRevision = result.sourceRevision;
      pending.snapshot = std::move(*result.PendingSnapshot);
      pending.crossCenters = std::move(result.crossCenters);
      result.PendingSnapshot.reset();
      result.GpuExtractPending = false;
      if (MeshFocusValid)
      {
        const int horiz = std::max(
            std::abs(pending.coord.x - MeshFocusGroundChunk.x),
            std::abs(pending.coord.z - MeshFocusGroundChunk.z));
        const bool missing = !HasDrawableGreedyMesh(pending.coord);
        if (missing && horiz <= MeshFocusRadiusChunks)
        {
          PendingGpuApplies.push_front(std::move(pending));
          TouchPendingGpuIndex();
        }
        else
        {
          PendingGpuApplies.push_back(std::move(pending));
          TouchPendingGpuIndex();
        }
      }
      else
      {
        PendingGpuApplies.push_back(std::move(pending));
        TouchPendingGpuIndex();
      }
      GpuExtractInFlight.insert(result.coord);
      return;
    }
  }

  ActiveMeshSourceRevision.erase(revisionIt);

  // Legacy GPF1 readback path (fallback).
  if (result.GpuExtractPending && result.PendingSnapshot && MesherBackend)
  {
    bool extracted = MesherBackend->TryExtractOpaqueToBatches(
        *result.PendingSnapshot, registry, result.coord, result.batches,
        /*deferred_no_gpu_readback=*/false,
        /*greedy_merge_rects=*/false);
    if (!extracted)
    {
      std::unordered_map<BlockId, GreedyMeshBatch> byBlockId;
      const auto quads =
          MesherBackend->BuildChunkMesh(*result.PendingSnapshot, registry);
      for (const GreedyQuad &q : quads)
      {
        GreedyMeshBatch &batch = byBlockId[q.Id];
        batch.blockId = q.Id;
        batch.Transparent = registry.IsTransparent(q.Id);
        batch.AlphaCutout =
            registry.GetRenderStyle(q.Id) == BlockRenderStyle::Cutout;
        const size_t base_vertex = batch.vertices.size();
        AppendGreedyQuad(q, result.coord, batch.vertices, batch.indices);
        for (size_t i = base_vertex; i < batch.vertices.size(); ++i)
        {
          ApplyVertexLight(batch.vertices[i], q.LightPacked);
        }
      }
      result.batches.clear();
      result.batches.reserve(byBlockId.size());
      for (auto &entry : byBlockId)
      {
        entry.second.blockId = entry.first;
        result.batches.push_back(std::move(entry.second));
      }
    }
    result.PendingSnapshot.reset();
    result.GpuExtractPending = false;
  }

  const bool defer_until_lit =
      DeferMeshUntilLit && DeferMeshUntilLit(result.coord);
  // Empty SoftDefer placeholders must not count as had_lit (keep dark forever).
  const bool had_mesh = HasDrawableGreedyMesh(result.coord);
  const bool had_lit_mesh = had_mesh && !ChunkHasFullyDarkFace(result.coord);
  const bool had_live_lit_gpu =
      ChunkHasLiveGpuDraw(result.coord) && !ChunkHasFullyDarkFace(result.coord);
  const bool new_dark = BatchesHaveFullyDarkFace(result.batches);
  if (ShouldRejectDarkMeshCommit(new_dark, defer_until_lit && had_mesh,
                                 had_lit_mesh, had_live_lit_gpu))
  {
    // Keep prior lit mesh (or hole). MarkRelit owns requeue when SoftDefer+had_mesh.
    const bool remesh_after = RemeshAfterApply.erase(result.coord) > 0;
    if (ShouldMarkDirtyAfterDarkSoftDeferReject(remesh_after, had_mesh))
    {
      MarkDirtyPriority(result.coord);
    }
    return;
  }

  ChunkGreedyMesh &chunkMesh = GreedyCache[result.coord];
  // Era15 TD-ARCH-049 MeshResidency: publish CPU batches before FreeChunk so
  // HasDrawable never drops when GPU was the sole drawable source. Freeing
  // first left a one-frame sky hole (flicker / mesh_discarded_late thrash).
  // After CPU publish, FreeChunk is still required or MDI keeps drawing the
  // stale GPU SSBO while GpuResident is cleared (dark_face_stale spikes).
  const bool had_gpu_resident = chunkMesh.GpuResident && GpuPipeline;
  const bool had_gpu_drawable =
      had_gpu_resident && chunkMesh.GpuQuadCount > 0;
  size_t new_vertex_count = 0;
  bool new_cpu_drawable = false;
  for (const GreedyMeshBatch &b : result.batches)
  {
    new_vertex_count += b.vertices.size();
    if (!b.vertices.empty() && !b.indices.empty())
    {
      new_cpu_drawable = true;
    }
  }
  // Era20 I-M3: SoftDefer/empty CPU must not FreeChunk a live GPU drawable.
  // Intentional occluded empty (no SoftDefer) still FreeChunks → 0-quad ready.
  if (ShouldKeepPriorGpuOnEmptyCpuReplace(had_gpu_drawable, new_cpu_drawable) &&
      (defer_until_lit || SoftDeferHeld.count(result.coord) > 0))
  {
    ++MeshReplaceHoleAvoided;
    MarkDirtyPriority(result.coord);
    return;
  }
  // Era24 I-E1 / Era32 I-L3: SoftDefer empty — never erase live GPU resident.
  // Keep prior slot + ticket; Absent+erase only when !GpuResident.
  if ((defer_until_lit || SoftDeferHeld.count(result.coord) > 0) &&
      !new_cpu_drawable)
  {
    // EnterLitQuiesce: SoftDeferHeld empty avoid looped async forever while
    // SoftDefer still true on PendingLight (manual 181421 async=1 missing=1).
    // SoftDefer already lifted for spawn — drop Held and publish state.
    if (EnterLitQuiesce && !defer_until_lit)
    {
      SoftDeferHeld.erase(result.coord);
      RemeshAfterApply.erase(result.coord);
      // fall through to publish / FreeChunk path
    }
    else
    {
      NoteSoftDeferEmptyPublishAvoided(result.coord);
      HoldSoftDeferFirstMesh(result.coord);
      if (IsPendingGpuApply(result.coord))
      {
        PreferKickPendingGpuQueued(result.coord);
      }
      if (had_gpu_resident)
      {
        ++MeshReplaceHoleAvoided;
        MaybeMarkDirtyAfterSoftDeferEmptyAvoid(result.coord);
        return;
      }
      // Era39: keep HasGreedy sticky — do not erase GreedyCache (flash).
      MaybeMarkDirtyAfterSoftDeferEmptyAvoid(result.coord);
      return;
    }
  }
  const auto oldIt = GreedyVertexCountByChunk.find(result.coord);
  if (oldIt != GreedyVertexCountByChunk.end())
  {
    GreedyVertexCountTotal -= oldIt->second;
  }
  GreedyVertexCountByChunk[result.coord] = new_vertex_count;
  GreedyVertexCountTotal += new_vertex_count;
  // Write-first: CPU drawable before FreeChunk (ShouldPublishCpuBatchesBeforeFreeGpu).
  chunkMesh.batches = std::move(result.batches);
  chunkMesh.crossCenters = std::move(result.crossCenters);
  const bool intentional_empty =
      new_vertex_count == 0 && !defer_until_lit &&
      SoftDeferHeld.count(result.coord) == 0;
  const bool underfeet_lease =
      MeshFocusValid &&
      std::max(std::abs(result.coord.x - MeshFocusGroundChunk.x),
               std::abs(result.coord.z - MeshFocusGroundChunk.z)) <= 1;
  const int gpu_keep_horiz =
      MeshFocusValid
          ? std::max(std::abs(result.coord.x - MeshFocusGroundChunk.x),
                     std::abs(result.coord.z - MeshFocusGroundChunk.z))
          : 999;
  const int gpu_keep_ring =
      std::max(MeshFocusRadiusChunks, kVisualStageLitDrawableHoriz);
  // Era21 I-R1: keep live GPU SSBO until BindCommitted (PendingReplace).
  // Underfeet: also retain on intentional empty (no PreferKick storm).
  // P4: vis/keep ring also keeps until Bind (cruise pool 13.8→2 MB).
  // LitRing: lit GpuPacked keep-until-replace (never free into dark plug).
  if (ShouldDeferFreeChunkUntilPackedReplace(had_gpu_drawable,
                                             new_cpu_drawable) &&
      (!intentional_empty ||
       ShouldRetainUnderfeetGpuOnEmptyReplace(underfeet_lease, had_gpu_drawable,
                                              intentional_empty) ||
       ShouldKeepPackedDrawUntilBind(had_gpu_drawable, gpu_keep_horiz,
                                     gpu_keep_ring, false) ||
       ShouldKeepLitPackedUntilBind(had_lit_mesh && had_gpu_drawable,
                                    gpu_keep_horiz, gpu_keep_ring, false)))
  {
    ++MeshReplaceHoleAvoided;
    // Keep same live SSBO — do not mark GreedyBatchesDirty/ForceFlat (refs OK).
    // CPU batches still update for Satisfying / seam queries.
    PendingMeshRevisionBump = true;
    CrossBatchesDirty = true;
    // PreferKick only for lit (or non-dark) pending replacement — not dark-over-lit.
    if (IsPendingGpuApply(result.coord) &&
        ShouldPreferKickPendingGpuAfterLitKeep(
            had_lit_mesh && had_gpu_drawable, new_dark))
    {
      PreferKickPendingGpuQueued(result.coord);
    }
    if (OnLitPendingNeeded && !had_mesh && (defer_until_lit || new_dark))
    {
      OnLitPendingNeeded(result.coord);
    }
    if (RemeshAfterApply.erase(result.coord) > 0)
    {
      const bool gpu_pending = IsPendingGpuApply(result.coord);
      const bool enter_gate =
          EnterGateBlocksRaaMarkDirty(EnterLitQuiesce, EnterGpuQuiesceDrain);
      const bool needs_first_mesh = !HasDrawableGreedyMesh(result.coord);
      const bool fully_dark_drawable =
          HasDrawableGreedyMesh(result.coord) &&
          ChunkHasFullyDarkFace(result.coord);
      if (gpu_pending &&
          ShouldPreferKickPendingGpuAfterLitKeep(
              had_lit_mesh && had_gpu_drawable, new_dark))
      {
        PreferKickPendingGpuQueued(result.coord);
      }
      else if (!gpu_pending &&
               ShouldMarkDirtyAfterRemeshAfterApplyCommit(
                   Dirty.Contains(result.coord), gpu_pending, enter_gate,
                   needs_first_mesh, fully_dark_drawable))
      {
        // Closeout C: lit remesh → RemeshQ; missing/FullyDark → FirstMeshQ.
        if (needs_first_mesh || fully_dark_drawable)
        {
          MarkDirtyPriority(result.coord);
        }
        else
        {
          MarkDirty(result.coord);
        }
        ++RaaCommitMarkDirtyN;
      }
    }
    return;
  }
  if (had_gpu_resident)
  {
    // Regress counter: free-first would have holed GPU-only drawable columns.
    if (CpuReplaceFreeFirstWouldHole(had_gpu_drawable, new_cpu_drawable))
    {
      ++MeshReplaceHoleAvoided;
    }
    GpuPipeline->FreeChunk(result.coord);
    ForceFlatRebuildNext = true;
  }
  chunkMesh.GpuResident = false;
  chunkMesh.GpuSlotIndex = -1;
  chunkMesh.GpuQuadCount = 0;
  chunkMesh.GpuHasDarkFace = false;
  chunkMesh.GpuBlockRanges.clear();
  chunkMesh.GpuTransparent = false;
  // Intentional empty: match Immediate / GPU 0-quad ready (HasMeshSatisfying).
  // SoftDefer empty placeholders must stay !ready (I-M3) — do not fake
  // GpuResident 0-quad or miss/holes latch on undrawn SoftDefer.
  if (intentional_empty)
  {
    chunkMesh.GpuResident = true;
    chunkMesh.GpuSlotIndex = -1;
    chunkMesh.GpuQuadCount = 0;
  }
  NoteGeometryDirty(result.coord);
  PendingMeshRevisionBump = true;
  InstancesDirty = true;
  CrossBatchesDirty = true;
  GreedyBatchesDirty = true;
  // Era49: lit CPU mesh outcome clears StickyRemesh work-set.
  if (!new_dark && OnLitDrawableCommitted)
  {
    OnLitDrawableCommitted(result.coord);
  }
  // Era15 TD-050: Unlit FirstMesh CPU publish → LitPending.
  if (OnLitPendingNeeded && !had_mesh && (defer_until_lit || new_dark))
  {
    OnLitPendingNeeded(result.coord);
  }
  // Light/content changed while this build was Active — remesh once with a
  // fresh Capture (avoids MarkDirty mid-flight Dirty plateau).
  if (RemeshAfterApply.erase(result.coord) > 0)
  {
    const bool gpu_pending = IsPendingGpuApply(result.coord);
    const bool enter_gate =
        EnterGateBlocksRaaMarkDirty(EnterLitQuiesce, EnterGpuQuiesceDrain);
    const bool needs_first_mesh = !HasDrawableGreedyMesh(result.coord);
    const bool fully_dark_drawable =
        HasDrawableGreedyMesh(result.coord) &&
        ChunkHasFullyDarkFace(result.coord);
    if (gpu_pending)
    {
      PreferKickPendingGpuQueued(result.coord);
    }
    else if (ShouldMarkDirtyAfterRemeshAfterApplyCommit(
                 Dirty.Contains(result.coord), gpu_pending, enter_gate,
                 needs_first_mesh, fully_dark_drawable))
    {
      if (needs_first_mesh || fully_dark_drawable)
      {
        MarkDirtyPriority(result.coord);
      }
      else
      {
        MarkDirty(result.coord);
      }
      ++RaaCommitMarkDirtyN;
    }
  }
}

void UChunkMeshCache::RebuildDirtyChunks(UBlockWorld &world,
                                         UBlockRegistry &registry,
                                         int max_drain_per_frame,
                                         int max_schedule_per_frame)
{
  (void)RebuildDirtyChunksWithStats(world, registry, max_drain_per_frame,
                                    max_schedule_per_frame);
}

MeshRebuildTickStats UChunkMeshCache::RebuildDirtyChunksWithStats(
    UBlockWorld &world, UBlockRegistry &registry, int max_drain_per_frame,
    int max_schedule_per_frame, bool force_sync, int max_sync_rebuild,
    double max_sync_ms, bool skip_gpu_consume)
{
  const auto dirty_tick_t0 = std::chrono::high_resolution_clock::now();
  auto seg_t0 = dirty_tick_t0;
  auto take_seg_ms = [&seg_t0]() -> double
  {
    const auto now = std::chrono::high_resolution_clock::now();
    const double ms =
        std::chrono::duration<double, std::milli>(now - seg_t0).count();
    seg_t0 = now;
    return ms;
  };
  MeshRebuildTickStats stats;
  CaptureRefreshBudgetLeft =
      // Era14 TD-ARCH-046: prefer store hit; live Capture refresh is the hitch.
      // Cap refreshes from MeshSnapshotBudgetMs (already cruise-clamped in
      // emerge). Floor 1 so schedule can still progress on store miss.
      std::max(1, static_cast<int>(MeshSnapshotBudgetMs * 0.35));
  LastMeshSyncMs = 0.0;
  LastMeshSnapshotMs = 0.0;
  LastMeshDirtyTickMs = 0.0;
  LastMeshDirtyPruneMs = 0.0;
  LastMeshDirtyPruneN = 0;
  LastMeshDirtySortMs = 0.0;
  LastMeshDirtyDrainMs = 0.0;
  LastMeshDirtyDrainN = 0;
  LastMeshDirtyScheduleMs = 0.0;
  LastMeshDirtyScheduleOkN = 0;
  LastMeshDirtyScheduleSkipN = 0;
  LastMeshDirtyGpuMs = 0.0;
  LastMeshDirtyGpuN = 0;
  LastMeshDirtySyncMs = 0.0;
  LastMeshDirtySyncN = 0;
  LastDirtyTouchN = static_cast<int>(Dirty.GetCount());
  LastDirtyFmN = static_cast<int>(Dirty.GetFirstMeshCount());
  LastDirtyRemeshN = static_cast<int>(Dirty.GetRemeshCount());
  LastDirtyRevisitSameN = 0;
  LastDirtyScheduleDedupN = 0;
  ScheduledThisFrame_.clear();
  // Sky-only / enter: orphan RemeshAfterApply with no Dirty/Active/GPU owner must
  // become Dirty. FullyDark drawable is NOT "owned" — enter PreferKick-only left
  // remesh_after_apply=32 with raa_commit=0 (manual 123647).
  {
    constexpr int kOrphanRaaReconcile = 12;
    int promoted = 0;
    for (auto it = RemeshAfterApply.begin();
         it != RemeshAfterApply.end() && promoted < kOrphanRaaReconcile;)
    {
      const glm::ivec3 c = *it;
      const bool owned =
          Dirty.Contains(c) || IsPendingGpuApply(c) ||
          (AsyncBuilder && AsyncBuilder->IsInFlight(c)) ||
          ActiveMeshSourceRevision.count(c) > 0;
      if (owned)
      {
        ++it;
        continue;
      }
      it = RemeshAfterApply.erase(it);
      // ColPipe P2: !Drawable → FirstMeshQ; FullyDark only if stale field;
      // lit remesh → RemeshQ. True-dark FullyDark drops RAA without Dirty.
      if (!HasDrawableGreedyMesh(c))
      {
        Dirty.MarkDirtyPriority(c);
        ++RaaCommitMarkDirtyN;
        ++promoted;
      }
      else if (ChunkHasFullyDarkFace(c))
      {
        if (ChunkHasStaleDarkFaces(c, world))
        {
          Dirty.MarkDirtyPriority(c);
          ++RaaCommitMarkDirtyN;
          ++promoted;
        }
      }
      else
      {
        Dirty.MarkDirty(c);
        ++RaaCommitMarkDirtyN;
        ++promoted;
      }
    }
    if (promoted > 0)
    {
      InstancesDirty = true;
      GreedyBatchesDirty = true;
      CrossBatchesDirty = true;
    }
  }
  if (!PrevDirtyForRevisit.empty() && !Dirty.empty())
  {
    for (const glm::ivec3 &coord : Dirty)
    {
      if (PrevDirtyForRevisit.count(coord) > 0)
      {
        ++LastDirtyRevisitSameN;
      }
    }
  }
  // F0 drain-first: ConsumeGpuApplyBacklog owns Kick/Finish before Rebuild.
  // Do not wipe those counters when skip_gpu_consume (SoT gpu_kick_n telem).
  if (!skip_gpu_consume)
  {
    LastGpuKickN = 0;
    LastGpuFinishN = 0;
    LastGpuFinishNotReadyN = 0;
  }
  bool mesh_data_changed = false;

  CaptureStore.SetNeighborVisualDrawableFn(CacheNeighborVisuallyDrawable, this);
  AgeSoftDeferEmptyAvoidFrames();
  RequeueSoftDeferHeld();

  // Era47: lit-quiesce Dirty prune runs before sort/schedule — sticky
  // SoftDefer/orphan (!HasChunk) otherwise block IsSpawnMeshRingReady
  // forever while gpu/async already 0 (World_174 stuck_dirty y=4).
  // Era49: do NOT RemoveAt unfinished FullyDark drawable Dirty — remesh must
  // finish to VisualReady (PreferKick/RAA alone left void≈856).
  // Convergence (manual 173849/180247): SoftDefer empty outside spawn parks to
  // SoftDeferHeld; spawn r≤2 undrawn KEEPS Dirty so FirstMesh can schedule
  // (park+Requeue cancelled Dirty every frame → missing=1 forever).
  if (EnterLitQuiesce && !Dirty.empty())
  {
    for (auto it = Dirty.begin(); it != Dirty.end();)
    {
      if (!world.GetChunkManager().HasChunk(*it))
      {
        it = Dirty.RemoveAt(it);
        ++LastMeshDirtyPruneN;
        continue;
      }
      int horiz = 999;
      if (MeshFocusValid)
      {
        horiz = std::max(std::abs(it->x - MeshFocusGroundChunk.x),
                         std::abs(it->z - MeshFocusGroundChunk.z));
      }
      if (HasDrawableGreedyMesh(*it) && ChunkHasFullyDarkFace(*it))
      {
        const glm::ivec2 col(it->x, it->z);
        if (EnterGateDoneColumns.count(col) > 0)
        {
          RemeshAfterApply.erase(*it);
          it = Dirty.RemoveAt(it);
          ++LastMeshDirtyPruneN;
          continue;
        }
        ++it;
        continue;
      }
      const bool soft_empty =
          HasGreedyMesh(*it) && !HasDrawableGreedyMesh(*it);
      if (soft_empty)
      {
        if (EnterLitQuiesceKeepSpawnUndrawnDirty(true, /*has_drawable=*/false,
                                                 horiz))
        {
          ++it;
          continue;
        }
        HoldSoftDeferFirstMesh(*it);
        RemeshAfterApply.erase(*it);
        it = Dirty.RemoveAt(it);
        ++LastMeshDirtyPruneN;
        continue;
      }
      if (HasDrawableGreedyMesh(*it) || SoftDeferHeld.count(*it) > 0)
      {
        it = Dirty.RemoveAt(it);
        ++LastMeshDirtyPruneN;
        continue;
      }
      // Pending-light park: SoftDeferHeld owns FirstMesh; keep Dirty only when
      // we will schedule FirstMesh this frame — or spawn undrawn (180247).
      if (DeferMeshUntilLit && DeferMeshUntilLit(*it))
      {
        const bool has_drawable = HasDrawableGreedyMesh(*it);
        if (EnterLitQuiesceKeepSpawnUndrawnDirty(true, has_drawable, horiz))
        {
          ++it;
          continue;
        }
        bool in_focus = false;
        if (MeshFocusValid)
        {
          in_focus = horiz <= MeshFocusRadiusChunks;
        }
        const bool miss_or_focus = StarveRemeshForHoles || in_focus;
        if (!ShouldScheduleFirstMeshUnderSoftDefer(has_drawable,
                                                   miss_or_focus))
        {
          if (!has_drawable)
          {
            HoldSoftDeferFirstMesh(*it);
          }
          it = Dirty.RemoveAt(it);
          ++LastMeshDirtyPruneN;
          continue;
        }
      }
      ++it;
    }
  }

  if (!Dirty.empty())
  {
    // When focus holes/pending-light debt are active, huge Dirty sets were being
    // fully sorted on the main thread only to drop most remesh entries later in
    // try_schedule(). Prune obviously unschedulable remesh work before the sort
    // so mesh_dirty_tick_ms cannot dominate the frame at Dirty~400-900.
    // B1: align remesh SoftDefer prune with partial-sort threshold (was 256) so
    // Dirty~96–256 cannot burn sort before schedule under miss pressure.
    if (MeshFocusValid && Dirty.GetCount() > 96)
    {
      for (auto it = Dirty.begin(); it != Dirty.end();)
      {
        // SoftDefer remesh: drop. Era22 I-S1: under miss/focus !Drawable
        // FirstMesh keep Dirty for schedule (not RemoveAt / Held-park forever).
        // Outside !Drawable → SoftDeferHeld (requeue when MayMesh/focus).
        if (DeferMeshUntilLit && DeferMeshUntilLit(*it))
        {
          const bool has_drawable = HasDrawableGreedyMesh(*it);
          bool in_focus = false;
          if (MeshFocusValid)
          {
            const int horiz =
                std::max(std::abs(it->x - MeshFocusGroundChunk.x),
                         std::abs(it->z - MeshFocusGroundChunk.z));
            in_focus = horiz <= MeshFocusRadiusChunks;
          }
          const bool miss_or_focus = StarveRemeshForHoles || in_focus;
          if (ShouldScheduleFirstMeshUnderSoftDefer(has_drawable,
                                                    miss_or_focus))
          {
            ++it;
            continue;
          }
          if (!has_drawable)
          {
            HoldSoftDeferFirstMesh(*it);
          }
          it = Dirty.RemoveAt(it);
          ++LastMeshDirtyPruneN;
          continue;
        }
        const bool has_drawable = HasDrawableGreedyMesh(*it);
        if (!has_drawable)
        {
          ++it;
          continue;
        }
        if (StarveRemeshForHoles)
        {
          // Keep near-ring remesh so black faces on neighbors of a hole repair
          // while FirstMesh runs (manual 090713: miss=1 + dark_stale≈2700).
          if (MeshFocusValid)
          {
            const int horiz =
                std::max(std::abs(it->x - MeshFocusGroundChunk.x),
                         std::abs(it->z - MeshFocusGroundChunk.z));
            if (horiz <= StarveRemeshKeepHoriz)
            {
              ++it;
              continue;
            }
          }
          it = Dirty.RemoveAt(it);
          ++LastMeshDirtyPruneN;
          continue;
        }
        if (StarveOutsideFocusMesh)
        {
          const int horiz = std::max(std::abs(it->x - MeshFocusGroundChunk.x),
                                     std::abs(it->z - MeshFocusGroundChunk.z));
          if (horiz > MeshFocusRadiusChunks)
          {
            it = Dirty.RemoveAt(it);
            ++LastMeshDirtyPruneN;
            continue;
          }
        }
        ++it;
      }
    }
    LastMeshDirtyPruneMs += take_seg_ms();
    // B4: if prune already blew emerge budget and FOV has no missing mesh,
    // skip sort so schedule/GPU can still run under the remaining wall.
    const double tick_so_far =
        std::chrono::duration<double, std::milli>(
            std::chrono::high_resolution_clock::now() - dirty_tick_t0)
            .count();
    const bool memo_holes =
        MissingMemo.epoch == HoleQueryEpoch && MissingMemo.result;
    const bool skip_sort_for_budget =
        !memo_holes && !StarveRemeshForHoles &&
        MeshEmergeTotalBudgetMs > 0.0 &&
        tick_so_far >= MeshEmergeTotalBudgetMs;
    const bool skip_sort_high_revisit =
        PendingLightFocusPressure_ <= 30 && Dirty.GetCount() > 0 &&
        LastDirtyRevisitSameN > 0 &&
        static_cast<double>(LastDirtyRevisitSameN) >=
            0.92 * static_cast<double>(Dirty.GetCount());
    const bool skip_sort_vb_heal =
        (VisibleBlackNoTicketPressure_ > 0 && PendingLightFocusPressure_ <= 30) ||
        VbFocusBlinkDelta_ > 10;
    ++DirtySortFrameCounter_;
    const bool skip_sort_o4_throttle =
        PendingLightFocusPressure_ <= 30 && Dirty.GetCount() > 0 &&
        LastDirtyRevisitSameN > 0 &&
        static_cast<double>(LastDirtyRevisitSameN) >=
            0.80 * static_cast<double>(Dirty.GetCount()) &&
        (DirtySortFrameCounter_ % 3) != 0;
    if (!skip_sort_for_budget && !skip_sort_high_revisit &&
        !skip_sort_vb_heal && !skip_sort_o4_throttle)
    {
    // Precompute missing-mesh set once — SortByDistanceKey compares O(n log n)
    // times; per-compare GreedyCache.find was burning wall during flight.
    std::unordered_set<glm::ivec3, IVec3Hash> missing_set;
    missing_set.reserve(Dirty.GetCount());
    for (const glm::ivec3 &coord : Dirty)
    {
      // Empty SoftDefer placeholders must sort as FirstMesh (missing), not remesh.
      if (!HasDrawableGreedyMesh(coord))
      {
        missing_set.insert(coord);
      }
    }
    auto missing_mesh = [&missing_set](glm::ivec3 coord)
    { return missing_set.find(coord) != missing_set.end(); };
    if (MeshFocusValid)
    {
      // missing → effective horiz (forward bias) → preferred cy.
      // Full sort on Dirty~174–278 still dominated dirty_tick (manual 130338);
      // always partial_sort above 96 with a tighter front window.
      constexpr size_t kPartialSortThreshold = 96;
      constexpr size_t kMinSortFront = 48;
      const size_t sort_front = std::max(
          kMinSortFront,
          static_cast<size_t>(std::max(8, max_schedule_per_frame) * 4 + 16));
      if (Dirty.GetCount() > kPartialSortThreshold)
      {
        Dirty.PartialSortByDistanceKey(
            MeshFocusGroundChunk, MeshVerticalPreferredCy, MeshPreferLowerCy,
            MeshVerticalPriorityValid, missing_mesh, sort_front,
            MeshForwardBiasK, MeshForwardXz, MeshFocusRadiusChunks);
      }
      else
      {
        Dirty.SortByDistanceKey(MeshFocusGroundChunk, MeshVerticalPreferredCy,
                                MeshPreferLowerCy, MeshVerticalPriorityValid,
                                missing_mesh, MeshForwardBiasK, MeshForwardXz,
                                MeshFocusRadiusChunks);
      }
      if (JustRelitFirstMeshValid_)
      {
        Dirty.BoostJustRelitNear(MeshFocusGroundChunk, JustRelitFirstMeshColumn_,
                                 kVisualStageNearFovHoriz);
      }
    }
    else
    {
      Dirty.PrioritizeChunksWithoutMesh(missing_mesh);
    }
    }
    LastMeshDirtySortMs += take_seg_ms();
  }
  else
  {
    LastMeshDirtyPruneMs += take_seg_ms();
  }
  if (!force_sync && Render.AsyncMeshing && Render.GreedyMeshing)
  {
    const int sync_cap =
        max_sync_rebuild >= 0
            ? max_sync_rebuild
            : std::max(2, std::min(12, max_schedule_per_frame));
    if (sync_cap <= 0)
    {
      LastMeshSyncMs = 0.0;
    }
    else
    {
      const auto sync_t0 = std::chrono::high_resolution_clock::now();
      const int sync_filled =
          SyncRebuildVisibleMissing(world, registry, sync_cap, max_sync_ms);
      LastMeshSyncMs = std::chrono::duration<double, std::milli>(
                           std::chrono::high_resolution_clock::now() - sync_t0)
                           .count();
      LastMeshDirtySyncMs += LastMeshSyncMs;
      LastMeshDirtySyncN += sync_filled;
      stats.SyncRebuilt += sync_filled;
      stats.Completed += sync_filled;
      mesh_data_changed = sync_filled > 0;
    }
  }
  seg_t0 = std::chrono::high_resolution_clock::now();
  if (!force_sync && Render.AsyncMeshing && Render.GreedyMeshing)
  {
    EnsureAsyncBuilder();
    // Compact far remesh when backlog starves focus missing (Dirty~800 / async~42).
    // After stale-apply fix: also compact when remesh plateau (dirty≫0, no holes).
    if (MeshFocusValid && Dirty.GetCount() > 200)
    {
      const int in_flight = AsyncBuilder->GetInFlightCount();
      const int compact_horiz =
          (in_flight >= 16 || Dirty.GetCount() > 280) ? MeshFocusRadiusChunks
                                                      : MeshFocusRadiusChunks + 1;
      // Never compact underfeet (horiz==0); keep remesh only outside.
      if (StarveRemeshForHoles || in_flight >= 16 || Dirty.GetCount() > 280)
      {
        for (auto it = Dirty.begin(); it != Dirty.end();)
        {
          const int horiz = std::max(std::abs(it->x - MeshFocusGroundChunk.x),
                                     std::abs(it->z - MeshFocusGroundChunk.z));
          if (horiz > 0 && horiz > compact_horiz &&
              GreedyCache.find(*it) != GreedyCache.end() &&
              !Dirty.IsFirstMesh(*it))
          {
            it = Dirty.RemoveAt(it);
            ++LastMeshDirtyPruneN;
          }
          else
          {
            ++it;
          }
        }
      }
    }
    for (const glm::ivec3 &coord : AsyncBuilder->TakeOverflowCoords())
    {
      // Remesh overflow requeue gated; FirstMesh holes always re-enter Dirty.
      if (!HasDrawableGreedyMesh(coord) || TryConsumeDirtyAdmit())
      {
        MarkDirtyPriority(coord);
      }
    }
    for (const glm::ivec3 &coord : AsyncBuilder->TakeDiscardedCoords())
    {
      // Epoch / jobId DiscardedLate frees InFlight but leaves Active orphan —
      // FirstMesh always requeues; remesh discard respects DirtyAdmit.
      if (!HasDrawableGreedyMesh(coord) || TryConsumeDirtyAdmit())
      {
        MarkDirtyPriority(coord);
      }
    }
    if (!skip_gpu_consume)
    {
      const MeshWorkAdmission &adm = WorkAdmission;
      int drain_cap = max_drain_per_frame;
      if (adm.mode != MeshWorkAdmission::Mode::Normal)
      {
        drain_cap = std::max(drain_cap, adm.max_drain);
      }
      else if (!EnterGpuQuiesceDrain && PendingGpuApplies.size() >= 16)
      {
        // Healed Normal path: prefer Finish over flooding Queued.
        drain_cap = std::min(drain_cap, 4);
      }
      int drained = 0;
      for (MeshBuildResult &result : AsyncBuilder->DrainCompleted(drain_cap))
      {
        ApplyMeshResult(world, registry, std::move(result));
        mesh_data_changed = true;
        ++stats.Completed;
        ++drained;
      }
      LastMeshDirtyDrainN += drained;
    }
    LastMeshDirtyDrainMs += take_seg_ms();

    if (!skip_gpu_consume && Render.GpuPackedMeshing &&
        !PendingGpuApplies.empty())
    {
      const MeshWorkAdmission &adm = WorkAdmission;
      const size_t pending_n = PendingGpuApplies.size();
      double gpu_budget =
          std::max(6.0, MeshEmergeTotalBudgetMs * adm.gpu_budget_frac);
      int gpu_max =
          std::max(3, std::max(max_drain_per_frame, max_schedule_per_frame));
      gpu_max = std::max(gpu_max, adm.gpu_apply_max);
      if (adm.mode != MeshWorkAdmission::Mode::Normal)
      {
        max_schedule_per_frame =
            std::min(max_schedule_per_frame, std::max(1, adm.max_schedule));
      }
      else if (!EnterGpuQuiesceDrain && pending_n >= 24)
      {
        gpu_max = std::max(gpu_max, 24);
        gpu_budget = std::max(gpu_budget, MeshEmergeTotalBudgetMs * 0.85);
        max_schedule_per_frame = std::min(max_schedule_per_frame, 1);
      }
      else if (EnterGpuQuiesceDrain && pending_n >= 24)
      {
        gpu_max = std::max(gpu_max, 24);
        gpu_budget = std::max(gpu_budget, MeshEmergeTotalBudgetMs * 0.85);
      }
      (void)pending_n;
      const int gpu_done = ProcessPendingGpuMeshes(world, registry, gpu_max,
                                                 gpu_budget, stats);
      LastMeshDirtyGpuN += gpu_done;
      if (gpu_done > 0)
      {
        mesh_data_changed = true;
        GreedyBatchesDirty = true;
        CrossBatchesDirty = true;
      }
    }
    else if (skip_gpu_consume &&
             WorkAdmission.mode != MeshWorkAdmission::Mode::Normal)
    {
      max_schedule_per_frame = std::min(max_schedule_per_frame,
                                        std::max(1, WorkAdmission.max_schedule));
    }
    LastMeshDirtyGpuMs += take_seg_ms();

    // Era47: after lit-quiesce prune, remaining Dirty are FirstMesh holes —
    // never sit at max_schedule=0 (sticky ring forever).
    if (EnterLitQuiesce && max_schedule_per_frame <= 0 && !Dirty.empty())
    {
      max_schedule_per_frame = 1;
    }
    const int max_pipeline = std::max(
        max_schedule_per_frame, AsyncBuilder->GetMaxPipelineDepth());
    // Cap main-thread snapshot capture. Default 6ms; raise under visual holes /
    // idle so Dirty~500 can schedule (async was stuck at 1–5).
    const double kSnapshotBudgetMs = MeshSnapshotBudgetMs;
    int scheduled = 0;
    int outside_focus_scheduled = 0;
    int overflow_scheduled = 0;
    int reserved_focus_scheduled = 0;
    int remesh_scheduled = 0;
    const int outside_focus_cap = MaxOutsideFocusMeshPerFrame;
    constexpr int kReservedFocusMissingSlots = 16;
    const MeshWorkAdmission &sched_adm = WorkAdmission;
    // F2: under holes, Pass 1 uses first_mesh_schedule; remesh uses remesh_schedule.
    const int first_mesh_cap =
        sched_adm.first_mesh_schedule > 0
            ? sched_adm.first_mesh_schedule
            : kReservedFocusMissingSlots;
    int remesh_cap =
        sched_adm.remesh_schedule > 0 ? sched_adm.remesh_schedule
                                      : max_schedule_per_frame;
    const int rear_focus_cap = std::max(0, MaxRearFocusMeshPerFrame);
    int rear_focus_scheduled = 0;
    const auto leave_in_under_pl = [&](const glm::ivec3 &c) {
      if (!ShouldLeaveInRemeshUnderPlPressure())
        return false;
      int horiz = 999;
      if (MeshFocusValid)
      {
        horiz = std::max(std::abs(c.x - MeshFocusGroundChunk.x),
                         std::abs(c.z - MeshFocusGroundChunk.z));
      }
      if (!Fz2DeferGated_ &&
          ShouldSkipDeferRemeshForLitRingFullyDark(horiz,
                                                   ChunkHasFullyDarkFace(c)))
      {
        return false;
      }
      return !ShouldRemoveAtRemeshDespitePlPressure(
          horiz, ChunkHasFullyDarkFace(c), EnterFovLitPressure_,
          VisibleBlackNoTicketPressure_, kVisualStageLitDrawableHoriz, 12,
          VisibleBlackFocusPressure_, VbFocusStableFrames_);
    };

    auto try_schedule = [&](auto it, bool count_outside, bool count_overflow,
                            bool count_reserved) -> decltype(it)
    {
      if (AsyncBuilder->GetInFlightCount() >= max_pipeline)
      {
        ++LastMeshDirtyScheduleSkipN;
        return Dirty.end();
      }
      if (LastMeshSnapshotMs >= kSnapshotBudgetMs)
      {
        ++LastMeshDirtyScheduleSkipN;
        return Dirty.end();
      }
      {
        const double total_elapsed =
            std::chrono::duration<double, std::milli>(
                std::chrono::high_resolution_clock::now() - dirty_tick_t0)
                .count();
        if (total_elapsed > MeshEmergeTotalBudgetMs)
        {
          ++LastMeshDirtyScheduleSkipN;
          return Dirty.end();
        }
      }
      if (EnterGpuQuiesceDrain && EnterTerminalHeld.count(*it) > 0)
      {
        ++LastMeshDirtyScheduleSkipN;
        return Dirty.RemoveAt(it);
      }
      if (EnterLitQuiesce && HasDrawableGreedyMesh(*it))
      {
        // Era49: keep FullyDark remesh Dirty under enter quiesce.
        if (ChunkHasFullyDarkFace(*it))
        {
          // fall through to schedule / PreferKick path
        }
        else
        {
          ++LastMeshDirtyScheduleSkipN;
          return Dirty.RemoveAt(it);
        }
      }
      // Era47: SoftDeferHeld owned by ColumnFlow FirstMesh — Dirty remesh under
      // lit-quiesce only blocks IsSpawnMeshRingReady without helping holes.
      if (EnterLitQuiesce && SoftDeferHeld.count(*it) > 0 &&
          !ChunkHasFullyDarkFace(*it))
      {
        ++LastMeshDirtyScheduleSkipN;
        return Dirty.RemoveAt(it);
      }
      if (AsyncBuilder->IsInFlight(*it))
      {
        // CheapRemesh C0: Inflight owns the chunk — erase Dirty so schedule
        // does not revisit every frame (dirty_revisit thrash). Supersede
        // mid-flight only via Active→RAA latch, not via leave-in-Dirty.
        ++DirtyScheduleSkipInflightN;
        ++LastMeshDirtyScheduleSkipN;
        if (leave_in_under_pl(*it))
        {
          return std::next(it);
        }
        return Dirty.RemoveAt(it);
      }
      // ColdWall S0a: PendingGpu owns the chunk — mirror Inflight RemoveAt.
      if (IsPendingGpuApply(*it) || IsPendingGpuQueued(*it) ||
          IsPendingGpuKickedOrDispatched(*it))
      {
        ++DirtyScheduleSkipInflightN;
        ++LastMeshDirtyScheduleSkipN;
        if (leave_in_under_pl(*it))
        {
          return std::next(it);
        }
        return Dirty.RemoveAt(it);
      }
      if (!world.GetChunkManager().HasChunk(*it))
      {
        // Era47/Era52: orphan Dirty under streaming freeze must not sticky-block ring.
        if (EnterLitQuiesce || EnterGpuQuiesceDrain)
        {
          ++LastMeshDirtyScheduleSkipN;
          return Dirty.RemoveAt(it);
        }
        ++LastMeshDirtyScheduleSkipN;
        return std::next(it);
      }
      if (DeferMeshUntilLit && DeferMeshUntilLit(*it))
      {
        const bool has_drawable = HasDrawableGreedyMesh(*it);
        int horiz = 999;
        bool in_focus = false;
        if (MeshFocusValid)
        {
          horiz = std::max(std::abs(it->x - MeshFocusGroundChunk.x),
                           std::abs(it->z - MeshFocusGroundChunk.z));
          in_focus = horiz <= MeshFocusRadiusChunks;
        }
        // Phase 1b: ring≤2 FirstMesh is non-stealable under SoftDefer.
        const bool near_ring_first_mesh =
            !has_drawable && horiz <= kVisualStageNearFovHoriz;
        const bool miss_or_focus = StarveRemeshForHoles || in_focus;
        if (near_ring_first_mesh ||
            ShouldScheduleFirstMeshUnderSoftDefer(has_drawable, miss_or_focus))
        {
          // fall through to Capture/Enqueue
        }
        else
        {
          if (!has_drawable)
          {
            HoldSoftDeferFirstMesh(*it);
          }
          ++LastMeshDirtyScheduleSkipN;
          // Outer SoftDefer: leave coord in Dirty (std::next) so revisit can
          // rotate after near-ring drains — do not RemoveAt-steal slots.
          if (!has_drawable && horiz > kVisualStageNearFovHoriz)
          {
            return std::next(it);
          }
          return Dirty.RemoveAt(it);
        }
      }
      if (StarveRemeshForHoles && HasDrawableGreedyMesh(*it))
      {
        // Keep near-ring remesh for neighbor black-face repair beside holes.
        if (MeshFocusValid)
        {
          const int horiz =
              std::max(std::abs(it->x - MeshFocusGroundChunk.x),
                       std::abs(it->z - MeshFocusGroundChunk.z));
          if (horiz <= StarveRemeshKeepHoriz)
          {
            // fall through to schedule
          }
          else
          {
            ++LastMeshDirtyScheduleSkipN;
            return Dirty.RemoveAt(it);
          }
        }
        else
        {
          ++LastMeshDirtyScheduleSkipN;
          return Dirty.RemoveAt(it);
        }
      }
      const uint64_t source_revision = MeshRevisions.Current(*it);
      const auto snap_t0 = std::chrono::high_resolution_clock::now();
      ChunkMeshSnapshot snapshot = CaptureStore.TakeOrRefresh(
          world, *it, source_revision, CaptureRefreshBudgetLeft);
      LastMeshSnapshotMs += std::chrono::duration<double, std::milli>(
                                std::chrono::high_resolution_clock::now() -
                                snap_t0)
                                .count();
      ActiveMeshSourceRevision[*it] = snapshot.sourceRevision;
      AsyncBuilder->Enqueue(std::move(snapshot), registry);
      ScheduledThisFrame_.insert(*it);
      it = Dirty.RemoveAt(it);
      ++scheduled;
      ++stats.Scheduled;
      if (count_overflow)
      {
        ++overflow_scheduled;
      }
      if (count_outside)
      {
        ++outside_focus_scheduled;
      }
      if (count_reserved)
      {
        ++reserved_focus_scheduled;
      }
      return it;
    };

    // Pass 1: FirstMeshQ prefix only (dual-queue: remesh never scanned here).
    seg_t0 = std::chrono::high_resolution_clock::now();
    const bool focus_missing_for_schedule =
        MeshFocusValid &&
        HasMissingGreedyMeshInHorizontalRadius(world, MeshFocusGroundChunk,
                                               MeshFocusRadiusChunks);
    // P4: under holes, prefer GPU Finish for focus-missing but keep 1 remesh
    // slot so lit settle is not deferred indefinitely (manual 222059 flicker).
    if ((StarveRemeshForHoles || focus_missing_for_schedule) &&
        CountPendingGpuAppliesInHorizontalRadius(MeshFocusGroundChunk,
                                                 MeshFocusRadiusChunks) > 4)
    {
      remesh_cap = std::min(remesh_cap, 1);
    }
    if (MeshFocusValid && first_mesh_cap > 0)
    {
      int outer_soft_defer_skips = 0;
      constexpr int kMaxOuterSoftDeferSkips = 32;
      for (auto it = Dirty.begin();
           it != Dirty.end() && scheduled < max_schedule_per_frame &&
           reserved_focus_scheduled < first_mesh_cap;)
      {
        if (!Dirty.IsFirstMesh(*it))
        {
          break; // Dual-Q: remesh suffix — stop Pass1 walk (Phase 1b early-stop)
        }
        const int dx = std::abs(it->x - MeshFocusGroundChunk.x);
        const int dz = std::abs(it->z - MeshFocusGroundChunk.z);
        const int horiz = std::max(dx, dz);
        if (horiz > MeshFocusRadiusChunks || HasDrawableGreedyMesh(*it))
        {
          ++it;
          continue;
        }
        const int scheduled_before = scheduled;
        const int skip_before = LastMeshDirtyScheduleSkipN;
        auto next = try_schedule(it, false, false, true);
        if (next == Dirty.end())
        {
          break;
        }
        if (scheduled == scheduled_before &&
            LastMeshDirtyScheduleSkipN > skip_before &&
            horiz > kVisualStageNearFovHoriz)
        {
          ++outer_soft_defer_skips;
          if (outer_soft_defer_skips >= kMaxOuterSoftDeferSkips)
          {
            break; // avoid thrash-walking entire FirstMeshQ for skip telem
          }
        }
        it = next;
      }
    }

    // Pass 1b: reserved rear-hemisphere focus slots so MeshForwardBias cannot
    // leave unfinished columns behind the camera (manual 220707).
    if (MeshFocusValid && rear_focus_cap > 0 && MeshForwardBiasK > 0.0f)
    {
      const float flen = std::sqrt(MeshForwardXz.x * MeshForwardXz.x +
                                   MeshForwardXz.y * MeshForwardXz.y);
      if (flen >= 0.01f)
      {
        const float fx = MeshForwardXz.x / flen;
        const float fz = MeshForwardXz.y / flen;
        for (auto it = Dirty.begin();
             it != Dirty.end() && scheduled < max_schedule_per_frame &&
             rear_focus_scheduled < rear_focus_cap;)
        {
          const int dx = std::abs(it->x - MeshFocusGroundChunk.x);
          const int dz = std::abs(it->z - MeshFocusGroundChunk.z);
          const int horiz = std::max(dx, dz);
          if (horiz > MeshFocusRadiusChunks)
          {
            ++it;
            continue;
          }
          const float tdx =
              static_cast<float>(it->x - MeshFocusGroundChunk.x);
          const float tdz =
              static_cast<float>(it->z - MeshFocusGroundChunk.z);
          const float tlen = std::sqrt(tdx * tdx + tdz * tdz);
          if (tlen < 0.01f ||
              (tdx / tlen) * fx + (tdz / tlen) * fz >= -0.05f)
          {
            ++it;
            continue;
          }
          const int scheduled_before = scheduled;
          auto next = try_schedule(it, false, false, false);
          if (next == Dirty.end())
          {
            break;
          }
          if (scheduled > scheduled_before)
          {
            ++rear_focus_scheduled;
          }
          it = next;
        }
      }
    }

    RequeueDeferredRemesh(remesh_cap);
    const auto coord_horiz = [this](const glm::ivec3 &c) -> int
    {
      if (!MeshFocusValid)
      {
        return 999;
      }
      return std::max(std::abs(c.x - MeshFocusGroundChunk.x),
                      std::abs(c.z - MeshFocusGroundChunk.z));
    };
    const auto skip_defer_lit_ring = [this, &coord_horiz](const glm::ivec3 &c) -> bool
    {
      if (!Fz2DeferGated_)
      {
        return ShouldSkipDeferRemeshForLitRingFullyDark(
            coord_horiz(c), ChunkHasFullyDarkFace(c));
      }
      return ShouldSkipDeferRemeshUnderVbHealPressure(
          coord_horiz(c), ChunkHasFullyDarkFace(c), EnterFovLitPressure_,
          VisibleBlackNoTicketPressure_, kVisualStageLitDrawableHoriz, 12,
          VisibleBlackFocusPressure_, VbFocusStableFrames_);
    };
    for (auto it = Dirty.begin();
         it != Dirty.end() && scheduled < max_schedule_per_frame;)
    {
      if (AsyncBuilder->GetInFlightCount() >= max_pipeline)
      {
        break;
      }
      if (LastMeshSnapshotMs >= kSnapshotBudgetMs)
      {
        break;
      }
      // Era47: enter lit-quiesce — drop lit drawable remesh Dirty (gate blocker).
      // Era49: keep FullyDark drawable Dirty until lit GPU commit.
      if (EnterGpuQuiesceDrain && EnterTerminalHeld.count(*it) > 0)
      {
        it = Dirty.RemoveAt(it);
        continue;
      }
      if (EnterLitQuiesce && HasDrawableGreedyMesh(*it) &&
          !ChunkHasFullyDarkFace(*it))
      {
        it = Dirty.RemoveAt(it);
        continue;
      }
      if (EnterLitQuiesce && SoftDeferHeld.count(*it) > 0 &&
          !ChunkHasFullyDarkFace(*it))
      {
        it = Dirty.RemoveAt(it);
        continue;
      }
      if (AsyncBuilder->IsInFlight(*it))
      {
        // CheapRemesh C0: Inflight owns the chunk — erase Dirty (same as
        // FirstMesh schedule path). Leave-in-Dirty caused dirty_revisit thrash.
        ++DirtyScheduleSkipInflightN;
        if (leave_in_under_pl(*it))
        {
          ++it;
          continue;
        }
        it = Dirty.RemoveAt(it);
        continue;
      }
      // ColdWall S0a: PendingGpu owns the chunk — mirror Inflight RemoveAt.
      if (IsPendingGpuApply(*it) || IsPendingGpuQueued(*it) ||
          IsPendingGpuKickedOrDispatched(*it))
      {
        ++DirtyScheduleSkipInflightN;
        if (leave_in_under_pl(*it))
        {
          ++it;
          continue;
        }
        it = Dirty.RemoveAt(it);
        continue;
      }
      // D1c: when drain-first left schedule=1 under miss, never spend it on remesh.
      if (focus_missing_for_schedule && max_schedule_per_frame <= 1 &&
          HasDrawableGreedyMesh(*it))
      {
        // ColdWall S0c: remesh starve under miss — erase Dirty (not leave-in).
        it = Dirty.RemoveAt(it);
        continue;
      }
      // F2: remesh quota separate from FirstMesh under HoleDrain/Deep.
      const bool is_remesh = HasDrawableGreedyMesh(*it);
      if (is_remesh && remesh_scheduled >= remesh_cap)
      {
        // Dual-queue: remesh suffix is contiguous after FirstMesh — stop walk.
        // ColdPL-2A: defer over-cap remesh (not leave-in revisit churn).
        if (!Dirty.IsFirstMesh(*it))
        {
          if (!skip_defer_lit_ring(*it))
          {
            DeferRemeshCoord(*it);
          }
          it = Dirty.RemoveAt(it);
          continue;
        }
        ++it;
        continue;
      }
      bool outside_focus = false;
      bool schedule_overflow = false;
      if (MeshFocusValid)
      {
        const int dx = std::abs(it->x - MeshFocusGroundChunk.x);
        const int dz = std::abs(it->z - MeshFocusGroundChunk.z);
        const int horiz = std::max(dx, dz);
        if (MeshScheduleMaxHorizontalDist >= 0 &&
            horiz > MeshScheduleMaxHorizontalDist)
        {
          if (overflow_scheduled >= MeshScheduleOverflowPerFrame)
          {
            ++it;
            continue;
          }
          schedule_overflow = true;
        }
        if (horiz > MeshFocusRadiusChunks)
        {
          outside_focus = true;
          // Compaction: drop far remesh only (never missing) when Dirty flooded.
          const size_t dirty_n = Dirty.GetCount();
          const int in_flight =
              AsyncBuilder ? AsyncBuilder->GetInFlightCount() : 0;
          const int drop_horiz =
              (dirty_n > 400 || in_flight >= 32) ? MeshFocusRadiusChunks
                                                 : MeshFocusRadiusChunks + 1;
          if (dirty_n > 400 && GreedyCache.find(*it) != GreedyCache.end() &&
              horiz > drop_horiz)
          {
            it = Dirty.RemoveAt(it);
            continue;
          }
          int outside_cap = outside_focus_cap;
          if (StarveOutsideFocusMesh)
          {
            // Empty SoftDefer placeholders are FirstMesh (!Drawable), not remesh.
            const bool missing = !HasDrawableGreedyMesh(*it);
            // MaxOutside==0 must stay hard zero (idle remesh). Old remesh
            // fallback to 1 fed CancelOutside discard storms.
            outside_cap =
                missing ? outside_focus_cap
                        : (outside_focus_cap > 0 ? 1 : 0);
          }
          if (outside_focus_scheduled >= outside_cap)
          {
            if (is_remesh && HasDrawableGreedyMesh(*it))
            {
              if (!skip_defer_lit_ring(*it))
              {
                DeferRemeshCoord(*it);
                it = Dirty.RemoveAt(it);
              }
              else
              {
                ++it;
              }
            }
            else
            {
              ++it;
            }
            continue;
          }
        }
      }
      if (!world.GetChunkManager().HasChunk(*it))
      {
        // Keep briefly for in-flight commits; prune far ghosts so Dirty cannot
        // plateau forever on never-loaded seamed neighbors.
        if (MeshFocusValid)
        {
          const int dx = std::abs(it->x - MeshFocusGroundChunk.x);
          const int dz = std::abs(it->z - MeshFocusGroundChunk.z);
          const int horiz = std::max(dx, dz);
          if (horiz > MeshFocusRadiusChunks + 2)
          {
            it = Dirty.RemoveAt(it);
            continue;
          }
        }
        ++it;
        continue;
      }
      if (DeferMeshUntilLit && DeferMeshUntilLit(*it))
      {
        const bool has_drawable = HasDrawableGreedyMesh(*it);
        bool in_focus = false;
        if (MeshFocusValid)
        {
          const int horiz =
              std::max(std::abs(it->x - MeshFocusGroundChunk.x),
                       std::abs(it->z - MeshFocusGroundChunk.z));
          in_focus = horiz <= MeshFocusRadiusChunks;
        }
        const bool miss_or_focus = StarveRemeshForHoles || in_focus;
        if (ShouldScheduleFirstMeshUnderSoftDefer(has_drawable, miss_or_focus))
        {
          // Era22 I-S1: fall through to schedule FirstMesh.
        }
        else
        {
          if (!has_drawable)
          {
            HoldSoftDeferFirstMesh(*it);
          }
          it = Dirty.RemoveAt(it);
          continue;
        }
      }
      if (StarveRemeshForHoles && HasDrawableGreedyMesh(*it))
      {
        // Keep near-ring remesh for neighbor black-face repair beside holes.
        if (MeshFocusValid)
        {
          const int horiz =
              std::max(std::abs(it->x - MeshFocusGroundChunk.x),
                       std::abs(it->z - MeshFocusGroundChunk.z));
          if (horiz > StarveRemeshKeepHoriz)
          {
            it = Dirty.RemoveAt(it);
            continue;
          }
        }
        else
        {
          it = Dirty.RemoveAt(it);
          continue;
        }
      }
      const uint64_t source_revision = MeshRevisions.Current(*it);
      const bool count_as_remesh = HasDrawableGreedyMesh(*it);
      const auto snap_t0 = std::chrono::high_resolution_clock::now();
      ChunkMeshSnapshot snapshot = CaptureStore.TakeOrRefresh(
          world, *it, source_revision, CaptureRefreshBudgetLeft);
      LastMeshSnapshotMs += std::chrono::duration<double, std::milli>(
                                std::chrono::high_resolution_clock::now() -
                                snap_t0)
                                .count();
      ActiveMeshSourceRevision[*it] = snapshot.sourceRevision;
      AsyncBuilder->Enqueue(std::move(snapshot), registry);
      ScheduledThisFrame_.insert(*it);
      it = Dirty.RemoveAt(it);
      ++scheduled;
      ++stats.Scheduled;
      if (count_as_remesh)
      {
        ++remesh_scheduled;
      }
      if (schedule_overflow)
      {
        ++overflow_scheduled;
      }
      if (outside_focus)
      {
        ++outside_focus_scheduled;
      }
    }
    LastMeshDirtyScheduleOkN = scheduled;
    LastMeshDirtyScheduleMs += take_seg_ms();
    if (InstancesDirty)
    {
      InstancesDirty = false;
    }
    if (mesh_data_changed)
    {
      GreedyBatchesDirty = true;
      CrossBatchesDirty = true;
    }
    BumpMeshRevisionIfNeeded();
    if (Render.GpuPackedMeshing && !PendingGpuApplies.empty())
    {
      const double tick_elapsed =
          std::chrono::duration<double, std::milli>(
              std::chrono::high_resolution_clock::now() - dirty_tick_t0)
              .count();
      const double remain =
          std::max(0.0, MeshEmergeTotalBudgetMs - tick_elapsed);
      if (remain > 0.0)
      {
        const MeshWorkAdmission &adm = WorkAdmission;
        const size_t pending_n = PendingGpuApplies.size();
        double gpu_budget =
            std::min(remain, std::max(4.0, MeshEmergeTotalBudgetMs *
                                               adm.gpu_budget_frac * 0.7));
        int gpu_max =
            std::max(2, std::max(max_drain_per_frame, max_schedule_per_frame) / 2);
        if (adm.mode != MeshWorkAdmission::Mode::Normal && remain > 1.0)
        {
          gpu_max = std::max(gpu_max, std::min(adm.gpu_apply_max, 16));
          gpu_budget = std::max(
              gpu_budget, std::min(remain, MeshEmergeTotalBudgetMs * 0.5));
        }
        else if (pending_n >= 12 && remain > 1.0)
        {
          gpu_max = std::max(gpu_max, 12);
          gpu_budget = std::max(
              gpu_budget, std::min(remain, MeshEmergeTotalBudgetMs * 0.5));
        }
        const int gpu_done = ProcessPendingGpuMeshes(world, registry, gpu_max,
                                                   gpu_budget, stats);
        LastMeshDirtyGpuN += gpu_done;
        if (gpu_done > 0)
        {
          mesh_data_changed = true;
          GreedyBatchesDirty = true;
          CrossBatchesDirty = true;
        }
      }
    }
    LastMeshDirtyGpuMs += take_seg_ms();
    LastRebuildTickStats = stats;
    LastMeshDirtyTickMs = std::chrono::duration<double, std::milli>(
                              std::chrono::high_resolution_clock::now() -
                              dirty_tick_t0)
                              .count();
    PrevDirtyForRevisit.clear();
    constexpr size_t kRevisitCap = 512;
    for (const glm::ivec3 &coord : Dirty)
    {
      if (PrevDirtyForRevisit.size() >= kRevisitCap)
      {
        break;
      }
      PrevDirtyForRevisit.insert(coord);
    }
    return stats;
  }

  int rebuilt = 0;
  const int sync_budget = std::max(max_drain_per_frame, max_schedule_per_frame);
  for (auto it = Dirty.begin(); it != Dirty.end() && rebuilt < sync_budget;)
  {
    RebuildChunk(world, registry, *it);
    it = Dirty.RemoveAt(it);
    ++rebuilt;
    ++stats.SyncRebuilt;
    ++stats.Completed;
  }
  LastMeshDirtySyncN += rebuilt;
  LastMeshDirtySyncMs += take_seg_ms();
  if (InstancesDirty)
  {
    InstancesDirty = false;
  }
  if (rebuilt > 0)
  {
    GreedyBatchesDirty = true;
    CrossBatchesDirty = true;
  }
  BumpMeshRevisionIfNeeded();
  LastRebuildTickStats = stats;
  LastMeshDirtyTickMs = std::chrono::duration<double, std::milli>(
                            std::chrono::high_resolution_clock::now() -
                            dirty_tick_t0)
                            .count();
  PrevDirtyForRevisit.clear();
  constexpr size_t kRevisitCapSync = 512;
  for (const glm::ivec3 &coord : Dirty)
  {
    if (PrevDirtyForRevisit.size() >= kRevisitCapSync)
    {
      break;
    }
    PrevDirtyForRevisit.insert(coord);
  }
  return stats;
}

int UChunkMeshCache::GetAsyncInFlightCount() const
{
  const int async_n = AsyncBuilder ? AsyncBuilder->GetInFlightCount() : 0;
  return async_n + static_cast<int>(GpuExtractInFlight.size());
}

size_t UChunkMeshCache::GetMeshCompletedSize() const
{
  return AsyncBuilder ? AsyncBuilder->GetCompletedSize() : 0;
}

size_t UChunkMeshCache::GetMeshCompletedCapacity() const
{
  return AsyncBuilder ? AsyncBuilder->GetCompletedCapacity() : 0;
}

uint64_t UChunkMeshCache::GetMeshCompletedDiscardedOverflow() const
{
  return AsyncBuilder ? AsyncBuilder->GetCompletedDiscardedOverflow() : 0;
}

void UChunkMeshCache::SetMeshCompletedCapacity(size_t cap)
{
  EnsureAsyncBuilder();
  if (AsyncBuilder)
  {
    AsyncBuilder->SetCompletedCapacity(cap);
  }
}

uint64_t UChunkMeshCache::GetMeshDiscardedLateCount() const
{
  if (!AsyncBuilder)
  {
    return 0;
  }
  return AsyncBuilder->GetDiscardedLateCount();
}

void UChunkMeshCache::DrainAsyncMeshResults(UBlockWorld &world,
                                            UBlockRegistry &registry,
                                            int max_per_frame)
{
  if (!Render.AsyncMeshing || !Render.GreedyMeshing || !AsyncBuilder)
  {
    return;
  }
  for (const glm::ivec3 &coord : AsyncBuilder->TakeOverflowCoords())
  {
    MarkDirtyPriority(coord);
  }
  for (const glm::ivec3 &coord : AsyncBuilder->TakeDiscardedCoords())
  {
    MarkDirtyPriority(coord);
  }
  for (MeshBuildResult &result : AsyncBuilder->DrainCompleted(max_per_frame))
  {
    ApplyMeshResult(world, registry, std::move(result));
  }
}

void UChunkMeshCache::ResetImmediateMeshStats()
{
  LastMeshImmediateMs = 0.0;
  LastMeshImmediateCount = 0;
}

void UChunkMeshCache::RebuildChunkImmediate(const UBlockWorld &world,
                                            UBlockRegistry &registry,
                                            glm::ivec3 chunkCoord)
{
  const auto t0 = std::chrono::high_resolution_clock::now();
  // Break flicker: late ApplyMeshResult of a pre-edit async snapshot must not
  // overwrite this Immediate mesh. Same revision invalidate as MarkDirtyPriority.
  InvalidateInFlightMeshBuild(chunkCoord);
  RebuildChunk(world, registry, chunkCoord);
  Dirty.Erase(chunkCoord);
  InvalidateVisibleList();
  LastMeshImmediateMs += std::chrono::duration<double, std::milli>(
                             std::chrono::high_resolution_clock::now() - t0)
                             .count();
  ++LastMeshImmediateCount;
}
void UChunkMeshCache::RebuildChunkLegacy(
    const UBlockWorld &world, UBlockRegistry &registry, glm::ivec3 chunkCoord,
    std::vector<FaceInstance> &chunkInstances)
{
  const UChunk *chunk = world.GetChunkManager().GetChunk(chunkCoord);
  if (!chunk)
  {
    return;
  }
  for (int z = 0; z < CHUNK_SIZE; ++z)
  {
    for (int y = 0; y < CHUNK_SIZE; ++y)
    {
      for (int x = 0; x < CHUNK_SIZE; ++x)
      {
        const glm::ivec3 local(x, y, z);
        const BlockId Id = chunk->GetBlockLocal(local);
        if (!registry.IsSolid(Id))
        {
          continue;
        }
        const glm::ivec3 worldPos(chunkCoord.x * CHUNK_SIZE + x,
                                  chunkCoord.y * CHUNK_SIZE + y,
                                  chunkCoord.z * CHUNK_SIZE + z);
        if (IsFullyEnclosed(world, worldPos))
        {
          continue;
        }
        FaceInstance instance;
        instance.Id = Id;
        instance.model = glm::translate(glm::mat4(1.0f), BlockCenter(worldPos));
        chunkInstances.push_back(instance);
      }
    }
  }
}
void UChunkMeshCache::RebuildChunk(const UBlockWorld &world,
                                   UBlockRegistry &registry,
                                   glm::ivec3 chunkCoord)
{
  const UChunk *chunk = world.GetChunkManager().GetChunk(chunkCoord);
  if (!chunk)
  {
    Cache.erase(chunkCoord);
    GreedyCache.erase(chunkCoord);
    PendingMeshRevisionBump = true;
    InstancesDirty = true;
    GreedyBatchesDirty = true;
    CrossBatchesDirty = true;
    return;
  }
  if (Render.GreedyMeshing)
  {
    Cache.erase(chunkCoord);
    std::unordered_map<BlockId, GreedyMeshBatch> byBlockId;
    const auto quads =
        MesherBackend
            ? MesherBackend->BuildChunkMesh(world, chunkCoord, registry)
            : UGreedyMesher::BuildChunkMesh(world, chunkCoord, registry);
    for (const GreedyQuad &q : quads)
    {
      GreedyMeshBatch &batch = byBlockId[q.Id];
      batch.blockId = q.Id;
      batch.Transparent = registry.IsTransparent(q.Id);
      batch.AlphaCutout =
          registry.GetRenderStyle(q.Id) == BlockRenderStyle::Cutout;
      const size_t base_vertex = batch.vertices.size();
      AppendGreedyQuad(q, chunkCoord, batch.vertices, batch.indices);
      for (size_t i = base_vertex; i < batch.vertices.size(); ++i)
      {
        GreedyMeshVertex &vertex = batch.vertices[i];
        ApplyVertexLight(vertex, q.LightPacked);
        const bool top_face = q.axis == 1 && q.faceSign > 0;
        vertex.wetness = SurfaceWetness * (top_face ? 0.15f : 0.05f);
      }
    }
    std::vector<GreedyMeshBatch> new_batches;
    new_batches.reserve(byBlockId.size());
    for (auto &pair : byBlockId)
    {
      pair.second.blockId = pair.first;
      new_batches.push_back(std::move(pair.second));
    }
    const bool defer_until_lit =
        DeferMeshUntilLit && DeferMeshUntilLit(chunkCoord);
    // Match CommitGpuMeshResult: empty SoftDefer placeholders (HasGreedy,
    // !Drawable / GpuQuadCount=0) must NOT count as had_lit_mesh — otherwise
    // place Immediate rejects dark rebuild and forever keeps undrawn (184035).
    const bool had_mesh = HasDrawableGreedyMesh(chunkCoord);
    const bool had_lit_mesh = had_mesh && !ChunkHasFullyDarkFace(chunkCoord);
    const bool had_live_lit_gpu =
        ChunkHasLiveGpuDraw(chunkCoord) && !ChunkHasFullyDarkFace(chunkCoord);
    const bool new_dark = BatchesHaveFullyDarkFace(new_batches);
    // First mesh (!had_mesh): never SoftDefer-reject dark place — otherwise
    // side-wall / far-focus edits stay invisible until Capture clears the gate.
    if (ShouldRejectDarkMeshCommit(new_dark, defer_until_lit && had_mesh,
                                   had_lit_mesh, had_live_lit_gpu))
    {
      // SoftDefer+had_mesh: wait MarkRelit (no Dirty thrash — manual 195432).
      if (ShouldMarkDirtyAfterDarkSoftDeferReject(/*remesh_after_apply=*/false,
                                                 had_mesh))
      {
        MarkDirtyPriority(chunkCoord);
      }
      return;
    }
    const int max_local_y = MaxSolidLocalY(*chunk, registry);
    ChunkGreedyMesh &chunkMesh = GreedyCache[chunkCoord];
    // Era15 TD-ARCH-049: publish CPU batches before FreeChunk (MeshResidency).
    const bool had_gpu_resident = chunkMesh.GpuResident && GpuPipeline;
    const bool had_gpu_drawable =
        had_gpu_resident && chunkMesh.GpuQuadCount > 0;
    size_t new_vertex_count = 0;
    bool new_cpu_drawable = false;
    for (const GreedyMeshBatch &b : new_batches)
    {
      new_vertex_count += b.vertices.size();
      if (!b.vertices.empty() && !b.indices.empty())
      {
        new_cpu_drawable = true;
      }
    }
    // Era20 I-M3: SoftDefer/empty Immediate must not FreeChunk live GPU drawable.
    if (ShouldKeepPriorGpuOnEmptyCpuReplace(had_gpu_drawable, new_cpu_drawable) &&
        (defer_until_lit || SoftDeferHeld.count(chunkCoord) > 0))
    {
      ++MeshReplaceHoleAvoided;
      MarkDirtyPriority(chunkCoord);
      return;
    }
    // Era24 I-E1 / Era32 I-L3: SoftDefer empty Immediate — keep GpuResident.
    if ((defer_until_lit || SoftDeferHeld.count(chunkCoord) > 0) &&
        !new_cpu_drawable)
    {
      NoteSoftDeferEmptyPublishAvoided(chunkCoord);
      HoldSoftDeferFirstMesh(chunkCoord);
      if (IsPendingGpuApply(chunkCoord) &&
          ShouldPreferKickPendingGpuAfterLitKeep(
              had_lit_mesh && had_gpu_drawable, /*new_mesh_fully_dark=*/true))
      {
        PreferKickPendingGpuQueued(chunkCoord);
      }
      if (had_gpu_resident)
      {
        ++MeshReplaceHoleAvoided;
        MaybeMarkDirtyAfterSoftDeferEmptyAvoid(chunkCoord);
        return;
      }
      // Era39: keep HasGreedy sticky — do not erase GreedyCache (flash).
      MaybeMarkDirtyAfterSoftDeferEmptyAvoid(chunkCoord);
      return;
    }
    chunkMesh.batches = std::move(new_batches);
    const bool intentional_empty =
        new_vertex_count == 0 && !defer_until_lit &&
        SoftDeferHeld.count(chunkCoord) == 0;
    const bool underfeet_lease =
        MeshFocusValid &&
        std::max(std::abs(chunkCoord.x - MeshFocusGroundChunk.x),
                 std::abs(chunkCoord.z - MeshFocusGroundChunk.z)) <= 1;
    const int gpu_keep_horiz =
        MeshFocusValid
            ? std::max(std::abs(chunkCoord.x - MeshFocusGroundChunk.x),
                       std::abs(chunkCoord.z - MeshFocusGroundChunk.z))
            : 999;
    const int gpu_keep_ring =
        std::max(MeshFocusRadiusChunks, kVisualStageLitDrawableHoriz);
    // Era21 I-R1: keep live GPU until BindCommitted (PendingReplace).
    // Underfeet: retain on intentional empty (no PreferKick).
    // P4: vis/keep ring also keeps until Bind.
    // LitRing: lit GpuPacked keep-until-replace.
    if (ShouldDeferFreeChunkUntilPackedReplace(had_gpu_drawable,
                                               new_cpu_drawable) &&
        (!intentional_empty ||
         ShouldRetainUnderfeetGpuOnEmptyReplace(underfeet_lease, had_gpu_drawable,
                                                intentional_empty) ||
         ShouldKeepPackedDrawUntilBind(had_gpu_drawable, gpu_keep_horiz,
                                       gpu_keep_ring, false) ||
         ShouldKeepLitPackedUntilBind(had_lit_mesh && had_gpu_drawable,
                                      gpu_keep_horiz, gpu_keep_ring, false)))
    {
      ++MeshReplaceHoleAvoided;
      // Same live SSBO — do not ForceFlatRebuild / GreedyBatchesDirty (refs OK).
      const auto oldItKeep = GreedyVertexCountByChunk.find(chunkCoord);
      if (oldItKeep != GreedyVertexCountByChunk.end())
      {
        GreedyVertexCountTotal -= oldItKeep->second;
      }
      GreedyVertexCountByChunk[chunkCoord] = new_vertex_count;
      GreedyVertexCountTotal += new_vertex_count;
      chunkMesh.crossCenters.clear();
      CollectCrossInstancesInBand(*chunk, chunkCoord, registry, max_local_y,
                                  chunkMesh.crossCenters);
      PendingMeshRevisionBump = true;
      CrossBatchesDirty = true;
      if (IsPendingGpuApply(chunkCoord) &&
          ShouldPreferKickPendingGpuAfterLitKeep(
              had_lit_mesh && had_gpu_drawable, new_dark))
      {
        PreferKickPendingGpuQueued(chunkCoord);
      }
      if (OnLitPendingNeeded && !had_mesh && (defer_until_lit || new_dark))
      {
        OnLitPendingNeeded(chunkCoord);
      }
      return;
    }
    if (had_gpu_resident)
    {
      if (CpuReplaceFreeFirstWouldHole(had_gpu_drawable, new_cpu_drawable))
      {
        ++MeshReplaceHoleAvoided;
      }
      GpuPipeline->FreeChunk(chunkCoord);
      ForceFlatRebuildNext = true;
    }
    chunkMesh.GpuResident = false;
    chunkMesh.GpuSlotIndex = -1;
    chunkMesh.GpuQuadCount = 0;
    chunkMesh.GpuHasDarkFace = false;
    chunkMesh.GpuBlockRanges.clear();
    chunkMesh.GpuTransparent = false;
    // Intentional empty (fully occluded solid): match GPU 0-quad ready so
    // HasMissing cannot latch forever on CPU Immediate (miss_cy sticky).
    // SoftDefer empty stays !ready (Era20 I-M3).
    if (intentional_empty)
    {
      chunkMesh.GpuResident = true;
      chunkMesh.GpuSlotIndex = -1;
      chunkMesh.GpuQuadCount = 0;
    }
    const auto oldIt = GreedyVertexCountByChunk.find(chunkCoord);
    if (oldIt != GreedyVertexCountByChunk.end())
    {
      GreedyVertexCountTotal -= oldIt->second;
    }
    GreedyVertexCountByChunk[chunkCoord] = new_vertex_count;
    GreedyVertexCountTotal += new_vertex_count;
    chunkMesh.crossCenters.clear();
    CollectCrossInstancesInBand(*chunk, chunkCoord, registry, max_local_y,
                                chunkMesh.crossCenters);
    NoteGeometryDirty(chunkCoord);
    if (OnLitPendingNeeded && !had_mesh && (defer_until_lit || new_dark))
    {
      OnLitPendingNeeded(chunkCoord);
    }
  }
  else
  {
    GreedyCache.erase(chunkCoord);
    const auto oldIt = GreedyVertexCountByChunk.find(chunkCoord);
    if (oldIt != GreedyVertexCountByChunk.end())
    {
      GreedyVertexCountTotal -= oldIt->second;
      GreedyVertexCountByChunk.erase(oldIt);
    }
    std::vector<FaceInstance> chunkInstances;
    chunkInstances.reserve(512);
    RebuildChunkLegacy(world, registry, chunkCoord, chunkInstances);
    Cache[chunkCoord] = std::move(chunkInstances);
  }
  PendingMeshRevisionBump = true;
  InstancesDirty = true;
  CrossBatchesDirty = true;
  GreedyBatchesDirty = true;
}

void UChunkMeshCache::InvalidateFluidSurfaceForChunk(glm::ivec3 chunkCoord)
{
  const glm::ivec3 ground(chunkCoord.x, 0, chunkCoord.z);
  FluidSurfaceDirty.insert(ground);
}

void UChunkMeshCache::InvalidateFluidSurfaceForColumn(glm::ivec3 ground_chunk_coord,
                                                      bool include_neighbors)
{
  if (ground_chunk_coord.y != 0)
  {
    ground_chunk_coord.y = 0;
  }
  const int x0 = include_neighbors ? ground_chunk_coord.x - 1 : ground_chunk_coord.x;
  const int x1 = include_neighbors ? ground_chunk_coord.x + 1 : ground_chunk_coord.x;
  const int z0 = include_neighbors ? ground_chunk_coord.z - 1 : ground_chunk_coord.z;
  const int z1 = include_neighbors ? ground_chunk_coord.z + 1 : ground_chunk_coord.z;
  for (int cx = x0; cx <= x1; ++cx)
  {
    for (int cz = z0; cz <= z1; ++cz)
    {
      FluidSurfaceDirty.insert(glm::ivec3(cx, 0, cz));
    }
  }
}

void UChunkMeshCache::RebuildFluidSurfaceSlice(const UBlockWorld &world,
                                               UBlockRegistry &registry,
                                               glm::ivec3 groundChunkCoord,
                                               int scanHintY)
{
  FluidSurfaceCache[groundChunkCoord] = BuildFluidSurfaceColumnSlice(
      world, registry, groundChunkCoord, scanHintY);
  FluidSurfaceDirty.erase(groundChunkCoord);
}

const FluidSurfaceColumnSlice *UChunkMeshCache::GetFluidSurfaceSlice(
    const UBlockWorld &world, UBlockRegistry &registry,
    glm::ivec3 groundChunkCoord, int scanHintY)
{
  const auto dirtyIt = FluidSurfaceDirty.find(groundChunkCoord);
  const auto cacheIt = FluidSurfaceCache.find(groundChunkCoord);
  if (dirtyIt != FluidSurfaceDirty.end() || cacheIt == FluidSurfaceCache.end())
  {
    RebuildFluidSurfaceSlice(world, registry, groundChunkCoord, scanHintY);
  }
  const auto it = FluidSurfaceCache.find(groundChunkCoord);
  if (it == FluidSurfaceCache.end())
  {
    return nullptr;
  }
  return &it->second;
}
} // namespace cutum
