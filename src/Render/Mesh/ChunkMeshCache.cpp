#include "Render/Mesh/ChunkMeshCache.h"
#include "Blocks/BlockRegistry.h"
#include "Render/Camera/Frustum.h"
#include "Render/Engine/DistanceFog.h"
#include "Render/Mesh/AsyncMeshBuilder.h"
#include "Render/Mesh/ChunkMeshRevisionRegistry.h"
#include "Render/Mesh/ChunkMeshSnapshot.h"
#include "Render/Mesh/CrossInstanceCollector.h"
#include "Render/Mesh/CrossMeshEmitter.h"
#include "Render/Mesh/GreedyMeshEmitter.h"
#include "Render/Mesh/GreedyMesher.h"
#include "Render/Mesh/MeshLightSampling.h"
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
        if (GreedyCache.find(coord) != GreedyCache.end())
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
    while (HasPendingAsyncMeshWork())
    {
      RebuildDirtyChunks(world, registry, 10000, 10000);
      AsyncBuilder->WaitIdle();
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
  if (!Render.AsyncMeshing || !Render.GreedyMeshing || !AsyncBuilder)
  {
    return;
  }
  // Dirty is removed at schedule time — re-queue Active coords before drop so
  // "KeepDirty" is real (otherwise cancel orphans remesh debt).
  for (const auto &entry : ActiveMeshSourceRevision)
  {
    Dirty.MarkDirtyPriority(entry.first);
  }
  AsyncBuilder->CancelPending();
  // Builder InFlight was cleared; Active/RemeshAfterApply must follow or
  // HasInflightMeshBuild stays true and RemeshAfterApply loops forever while
  // async count (builder-only) looks drained.
  ActiveMeshSourceRevision.clear();
  RemeshAfterApply.clear();
}

void UChunkMeshCache::CancelInFlightOutsideHorizontalRadius(
    glm::ivec3 focus_ground_chunk, int radius_chunks)
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
}

bool UChunkMeshCache::HasPendingDirty() const
{
  return !Dirty.empty() || HasPendingAsyncMeshWork();
}

bool UChunkMeshCache::HasGreedyMesh(glm::ivec3 chunk_coord) const
{
  return GreedyCache.find(chunk_coord) != GreedyCache.end();
}

bool UChunkMeshCache::ChunkHasFullyDarkFace(glm::ivec3 chunk_coord) const
{
  const auto it = GreedyCache.find(chunk_coord);
  if (it == GreedyCache.end())
  {
    return false;
  }
  for (const GreedyMeshBatch &batch : it->second.batches)
  {
    for (const GreedyMeshVertex &v : batch.vertices)
    {
      if (v.skyLight <= 0.0f && v.blockLight <= 0.0f)
      {
        return true;
      }
    }
  }
  return false;
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
        if (GreedyCache.find(coord) != GreedyCache.end())
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
  return missing;
}

bool UChunkMeshCache::FindNearestMissingGreedyMesh(
    const UBlockWorld &world, glm::ivec3 center_ground_chunk, int radius_chunks,
    glm::ivec3 &out_coord) const
{
  if (radius_chunks < 0)
  {
    return false;
  }
  bool found = false;
  int best_dist = std::numeric_limits<int>::max();
  int best_vdist = std::numeric_limits<int>::max();
  glm::ivec3 best{0};
  world.GetChunkManager().ForEachChunk(
      [&](const UChunk &chunk)
      {
        const glm::ivec3 coord = chunk.GetCoord();
        const int dx = std::abs(coord.x - center_ground_chunk.x);
        const int dz = std::abs(coord.z - center_ground_chunk.z);
        const int horiz = std::max(dx, dz);
        if (horiz > radius_chunks)
        {
          return;
        }
        if (GreedyCache.find(coord) != GreedyCache.end())
        {
          return;
        }
        bool solid = false;
        for (int z = 0; z < CHUNK_SIZE && !solid; z += 4)
        {
          for (int x = 0; x < CHUNK_SIZE && !solid; x += 4)
          {
            for (int y = 0; y < CHUNK_SIZE && !solid; y += 4)
            {
              if (chunk.GetBlockLocal(glm::ivec3(x, y, z)) != BLOCK_AIR)
              {
                solid = true;
              }
            }
          }
        }
        if (!solid)
        {
          return;
        }
        const int vdist = std::abs(coord.y - center_ground_chunk.y);
        if (!found || horiz < best_dist ||
            (horiz == best_dist && vdist < best_vdist))
        {
          found = true;
          best_dist = horiz;
          best_vdist = vdist;
          best = coord;
        }
      });
  if (found)
  {
    out_coord = best;
  }
  return found;
}

void UChunkMeshCache::BumpChunkMeshRevision(glm::ivec3 chunk_coord)
{
  MeshRevisions.Bump(chunk_coord);
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

int UChunkMeshCache::DropRemeshDirtyBeyondRadius(glm::ivec3 center_chunk,
                                                int keep_radius, int keep_cy)
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
    if (beyond_keep(*it))
    {
      RemeshAfterApply.erase(*it);
      it = Dirty.RemoveAt(it);
      ++dropped;
    }
    else
    {
      ++it;
    }
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

void UChunkMeshCache::MarkDirty(glm::ivec3 chunkCoord)
{
  // Mid-flight MarkDirty used to re-insert Dirty while Active stayed set —
  // Apply then immediately rescheduled forever (standing Dirty≈535 async=42).
  // Defer one remesh after Apply instead of stacking Dirty.
  if (ActiveMeshSourceRevision.find(chunkCoord) !=
      ActiveMeshSourceRevision.end())
  {
    RemeshAfterApply.insert(chunkCoord);
    return;
  }
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
  if (ActiveMeshSourceRevision.find(chunkCoord) !=
      ActiveMeshSourceRevision.end())
  {
    RemeshAfterApply.insert(chunkCoord);
    return;
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
void UChunkMeshCache::RemoveChunk(glm::ivec3 chunkCoord)
{
  Cache.erase(chunkCoord);
  GreedyCache.erase(chunkCoord);
  const auto vIt = GreedyVertexCountByChunk.find(chunkCoord);
  if (vIt != GreedyVertexCountByChunk.end())
  {
    GreedyVertexCountTotal -= vIt->second;
    GreedyVertexCountByChunk.erase(vIt);
  }
  Dirty.Erase(chunkCoord);
  MeshRevisions.Erase(chunkCoord);
  ActiveMeshSourceRevision.erase(chunkCoord);
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
    Cache.erase(slice);
    GreedyCache.erase(slice);
    const auto vIt = GreedyVertexCountByChunk.find(slice);
    if (vIt != GreedyVertexCountByChunk.end())
    {
      GreedyVertexCountTotal -= vIt->second;
      GreedyVertexCountByChunk.erase(vIt);
    }
    Dirty.Erase(slice);
    MeshRevisions.Erase(slice);
    ActiveMeshSourceRevision.erase(slice);
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
  if (visible == LastVisibleChunks && MeshRevision == LastVisibleMeshRevision)
  {
    return true;
  }
  LastVisibleChunks = std::move(visible);
  LastVisibleMeshRevision = MeshRevision;
  return false;
}
void UChunkMeshCache::RebuildFlatGreedyBatches(const Frustum *frustum,
                                               const glm::vec3 *cameraPos,
                                               float maxCullDistance)
{
  // Rate-limit full greedy batch rebuilds: light edits can produce many mesh
  // results per second; rebuilding the full flat batch list every frame can
  // dominate CPU time.
  if (GreedyBatchesDirty)
  {
    const auto now = std::chrono::steady_clock::now();
    if (LastFlatRebuildAt != std::chrono::steady_clock::time_point{} &&
        now - LastFlatRebuildAt < std::chrono::milliseconds(50))
    {
      return;
    }
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
  GreedyOpaqueCutoutRefs.reserve(GreedyCache.size() * 4);
  GreedyTransparentRefs.reserve(GreedyCache.size());

  for (const auto &entry : GreedyCache)
  {
    if (!ChunkPassesFrustum(frustum, cameraPos, maxCullDistance, entry.first,
                            horizontal_cull))
    {
      continue;
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

void UChunkMeshCache::UpdateVisibleInstances(const Frustum &frustum,
                                             const glm::mat4 &viewProj,
                                             const glm::vec3 &cameraPos)
{
  (void)viewProj;
  const float maxCullDistance = MaxCullDistance();
  const glm::ivec3 camera_chunk =
      UChunkManager::WorldToChunk(WorldPosToBlock(cameraPos));
  const bool needs_greedy_rebuild =
      GreedyBatchesDirty ||
      ((GreedyOpaqueCutoutRefs.empty() && GreedyTransparentRefs.empty()) &&
       !GreedyCache.empty());
  const bool needs_cross_rebuild =
      CrossBatchesDirty ||
      (CrossBatches.empty() && TotalCrossCenterCount() > 0);
  // Trade-off: camera rotation inside the same chunk does not rebuild flat
  // lists.
  if (!InstancesDirty && !needs_greedy_rebuild && !needs_cross_rebuild &&
      MeshRevision == LastCullMeshRevision &&
      camera_chunk == LastCullCameraChunk)
  {
    return;
  }
  if (Render.GreedyMeshing)
  {
    if (Render.FrustumCulling)
    {
      if (needs_greedy_rebuild)
      {
        RebuildFlatGreedyBatches(&frustum, &cameraPos, maxCullDistance);
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
}
void UChunkMeshCache::EnsureAsyncBuilder()
{
  if (!AsyncBuilder)
  {
    AsyncBuilder = std::make_unique<UAsyncMeshBuilder>();
  }
}

void UChunkMeshCache::ApplyMeshResult(const UBlockWorld &world,
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
    return;
  }
  if (revisionIt->second != result.sourceRevision ||
      result.sourceRevision != expected_revision)
  {
    ActiveMeshSourceRevision.erase(revisionIt);
    // Stale result: re-queue WITHOUT bumping revision. MarkDirtyPriority used
    // to bump when !existed and restart the invalidate→stale loop (idle
    // async=42 + dirty flat, manual 210341).
    ++MeshApplyStaleCount;
    Dirty.MarkDirtyPriority(result.coord);
    InstancesDirty = true;
    GreedyBatchesDirty = true;
    CrossBatchesDirty = true;
    return;
  }
  ActiveMeshSourceRevision.erase(revisionIt);
  ChunkGreedyMesh &chunkMesh = GreedyCache[result.coord];
  size_t new_vertex_count = 0;
  for (const GreedyMeshBatch &b : result.batches)
  {
    new_vertex_count += b.vertices.size();
  }
  const auto oldIt = GreedyVertexCountByChunk.find(result.coord);
  if (oldIt != GreedyVertexCountByChunk.end())
  {
    GreedyVertexCountTotal -= oldIt->second;
  }
  GreedyVertexCountByChunk[result.coord] = new_vertex_count;
  GreedyVertexCountTotal += new_vertex_count;
  chunkMesh.batches = std::move(result.batches);
  chunkMesh.crossCenters = std::move(result.crossCenters);
  PendingMeshRevisionBump = true;
  InstancesDirty = true;
  CrossBatchesDirty = true;
  GreedyBatchesDirty = true;
  // Light/content changed while this build was Active — remesh once with a
  // fresh Capture (avoids MarkDirty mid-flight Dirty plateau).
  if (RemeshAfterApply.erase(result.coord) > 0)
  {
    MarkDirtyPriority(result.coord);
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
    double max_sync_ms)
{
  const auto dirty_tick_t0 = std::chrono::high_resolution_clock::now();
  MeshRebuildTickStats stats;
  LastMeshSyncMs = 0.0;
  LastMeshSnapshotMs = 0.0;
  LastMeshDirtyTickMs = 0.0;
  bool mesh_data_changed = false;

  if (!Dirty.empty())
  {
    // Precompute missing-mesh set once — SortByDistanceKey compares O(n log n)
    // times; per-compare GreedyCache.find was burning wall during flight.
    std::unordered_set<glm::ivec3, IVec3Hash> missing_set;
    missing_set.reserve(Dirty.GetCount());
    for (const glm::ivec3 &coord : Dirty)
    {
      if (GreedyCache.find(coord) == GreedyCache.end())
      {
        missing_set.insert(coord);
      }
    }
    auto missing_mesh = [&missing_set](glm::ivec3 coord)
    { return missing_set.find(coord) != missing_set.end(); };
    if (MeshFocusValid)
    {
      // missing → effective horiz (forward bias) → preferred cy.
      Dirty.SortByDistanceKey(MeshFocusGroundChunk, MeshVerticalPreferredCy,
                              MeshPreferLowerCy, MeshVerticalPriorityValid,
                              missing_mesh, MeshForwardBiasK, MeshForwardXz,
                              MeshFocusRadiusChunks);
    }
    else
    {
      Dirty.PrioritizeChunksWithoutMesh(missing_mesh);
    }
  }
  if (!force_sync && Render.AsyncMeshing && Render.GreedyMeshing)
  {
    const int sync_cap =
        max_sync_rebuild >= 0
            ? max_sync_rebuild
            : std::max(2, std::min(12, max_schedule_per_frame));
    const auto sync_t0 = std::chrono::high_resolution_clock::now();
    const int sync_filled =
        SyncRebuildVisibleMissing(world, registry, sync_cap, max_sync_ms);
    LastMeshSyncMs = std::chrono::duration<double, std::milli>(
                         std::chrono::high_resolution_clock::now() - sync_t0)
                         .count();
    stats.SyncRebuilt += sync_filled;
    stats.Completed += sync_filled;
    mesh_data_changed = sync_filled > 0;
  }
  if (!force_sync && Render.AsyncMeshing && Render.GreedyMeshing)
  {
    EnsureAsyncBuilder();
    // Compact far remesh when backlog starves focus missing (Dirty~800 / async~42).
    if (MeshFocusValid && Dirty.GetCount() > 300)
    {
      const int in_flight = AsyncBuilder->GetInFlightCount();
      const int compact_horiz =
          (in_flight >= 24 || Dirty.GetCount() > 400) ? MeshFocusRadiusChunks
                                                      : MeshFocusRadiusChunks + 1;
      if (StarveRemeshForHoles || in_flight >= 24 || Dirty.GetCount() > 420)
      {
        for (auto it = Dirty.begin(); it != Dirty.end();)
        {
          const int horiz = std::max(std::abs(it->x - MeshFocusGroundChunk.x),
                                     std::abs(it->z - MeshFocusGroundChunk.z));
          if (horiz > compact_horiz &&
              GreedyCache.find(*it) != GreedyCache.end())
          {
            it = Dirty.RemoveAt(it);
          }
          else
          {
            ++it;
          }
        }
      }
    }
    for (MeshBuildResult &result :
         AsyncBuilder->DrainCompleted(max_drain_per_frame))
    {
      ApplyMeshResult(world, std::move(result));
      mesh_data_changed = true;
      ++stats.Completed;
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
    const int outside_focus_cap = MaxOutsideFocusMeshPerFrame;
    constexpr int kReservedFocusMissingSlots = 16;
    const int rear_focus_cap = std::max(0, MaxRearFocusMeshPerFrame);
    int rear_focus_scheduled = 0;

    auto try_schedule = [&](auto it, bool count_outside, bool count_overflow,
                            bool count_reserved) -> decltype(it)
    {
      if (AsyncBuilder->GetInFlightCount() >= max_pipeline)
      {
        return Dirty.end();
      }
      if (LastMeshSnapshotMs >= kSnapshotBudgetMs)
      {
        return Dirty.end();
      }
      if (AsyncBuilder->IsInFlight(*it))
      {
        return std::next(it);
      }
      if (!world.GetChunkManager().HasChunk(*it))
      {
        return std::next(it);
      }
      if (DeferMeshUntilLit && DeferMeshUntilLit(*it))
      {
        // Remesh of existing mesh while PendingLight: drop from Dirty until
        // MarkRelit requeues. Leaving deferred remesh in Dirty (~500) starved
        // first-mesh Pass1 (async stuck at 1–5, holes=1 entire stop).
        // Must run before StarveRemeshForHoles.
        if (GreedyCache.find(*it) != GreedyCache.end())
        {
          return Dirty.RemoveAt(it);
        }
        return std::next(it);
      }
      if (StarveRemeshForHoles &&
          GreedyCache.find(*it) != GreedyCache.end())
      {
        // While holes exist remesh cannot fill them — drop so Admit/Pass1 missing
        // can enter Dirty. MarkRelit requeues remesh after light.
        return Dirty.RemoveAt(it);
      }
      const uint64_t source_revision = MeshRevisions.Current(*it);
      const auto snap_t0 = std::chrono::high_resolution_clock::now();
      ChunkMeshSnapshot snapshot =
          ChunkMeshSnapshot::Capture(world, *it, source_revision);
      LastMeshSnapshotMs += std::chrono::duration<double, std::milli>(
                                std::chrono::high_resolution_clock::now() -
                                snap_t0)
                                .count();
      ActiveMeshSourceRevision[*it] = snapshot.sourceRevision;
      AsyncBuilder->Enqueue(std::move(snapshot), registry);
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

    // Pass 1: reserved slots for focus missing (highest priority).
    if (MeshFocusValid)
    {
      for (auto it = Dirty.begin();
           it != Dirty.end() && scheduled < max_schedule_per_frame &&
           reserved_focus_scheduled < kReservedFocusMissingSlots;)
      {
        const int dx = std::abs(it->x - MeshFocusGroundChunk.x);
        const int dz = std::abs(it->z - MeshFocusGroundChunk.z);
        const int horiz = std::max(dx, dz);
        if (horiz > MeshFocusRadiusChunks ||
            GreedyCache.find(*it) != GreedyCache.end())
        {
          ++it;
          continue;
        }
        auto next = try_schedule(it, false, false, true);
        // try_schedule may RemoveAt(it); never compare/use it afterward.
        if (next == Dirty.end())
        {
          break;
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
      if (AsyncBuilder->IsInFlight(*it))
      {
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
            const bool missing =
                GreedyCache.find(*it) == GreedyCache.end();
            // MaxOutside==0 must stay hard zero (idle remesh). Old remesh
            // fallback to 1 fed CancelOutside discard storms.
            outside_cap =
                missing ? outside_focus_cap
                        : (outside_focus_cap > 0 ? 1 : 0);
          }
          if (outside_focus_scheduled >= outside_cap)
          {
            ++it;
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
        // Remesh deferred while PendingLight: drop until MarkRelit requeues.
        // Must run before StarveRemeshForHoles — otherwise deferred remesh stays
        // in Dirty forever while holes=1 and starves first-mesh Pass1.
        if (GreedyCache.find(*it) != GreedyCache.end())
        {
          it = Dirty.RemoveAt(it);
          continue;
        }
        ++it;
        continue;
      }
      if (StarveRemeshForHoles &&
          GreedyCache.find(*it) != GreedyCache.end())
      {
        // While holes exist remesh cannot fill them — drop so Admit/Pass1 missing
        // can enter Dirty. MarkRelit requeues remesh after light.
        it = Dirty.RemoveAt(it);
        continue;
      }
      const uint64_t source_revision = MeshRevisions.Current(*it);
      const auto snap_t0 = std::chrono::high_resolution_clock::now();
      ChunkMeshSnapshot snapshot =
          ChunkMeshSnapshot::Capture(world, *it, source_revision);
      LastMeshSnapshotMs += std::chrono::duration<double, std::milli>(
                                std::chrono::high_resolution_clock::now() -
                                snap_t0)
                                .count();
      ActiveMeshSourceRevision[*it] = snapshot.sourceRevision;
      AsyncBuilder->Enqueue(std::move(snapshot), registry);
      it = Dirty.RemoveAt(it);
      ++scheduled;
      ++stats.Scheduled;
      if (schedule_overflow)
      {
        ++overflow_scheduled;
      }
      if (outside_focus)
      {
        ++outside_focus_scheduled;
      }
    }
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
    LastRebuildTickStats = stats;
    LastMeshDirtyTickMs = std::chrono::duration<double, std::milli>(
                              std::chrono::high_resolution_clock::now() -
                              dirty_tick_t0)
                              .count();
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
  return stats;
}

int UChunkMeshCache::GetAsyncInFlightCount() const
{
  if (!AsyncBuilder)
  {
    return 0;
  }
  return AsyncBuilder->GetInFlightCount();
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
    Dirty.MarkDirtyPriority(coord);
  }
  for (MeshBuildResult &result : AsyncBuilder->DrainCompleted(max_per_frame))
  {
    ApplyMeshResult(world, std::move(result));
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
        UGreedyMesher::BuildChunkMesh(world, chunkCoord, registry);
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
    const int max_local_y = MaxSolidLocalY(*chunk, registry);
    ChunkGreedyMesh &chunkMesh = GreedyCache[chunkCoord];
    chunkMesh.batches.clear();
    chunkMesh.batches.reserve(byBlockId.size());
    for (auto &pair : byBlockId)
    {
      pair.second.blockId = pair.first;
      chunkMesh.batches.push_back(std::move(pair.second));
    }
    size_t new_vertex_count = 0;
    for (const GreedyMeshBatch &b : chunkMesh.batches)
    {
      new_vertex_count += b.vertices.size();
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
