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
#include "World/Streaming/MeshLitGate.h"
#include "Render/Mesh/GreedyMeshEmitter.h"
#include "Render/Mesh/GreedyMesher.h"
#include "Render/Mesh/GpuGreedyFaceExtract.h"
#include "Render/Mesh/GpuMeshPipeline.h"
#include "Render/Mesh/IUChunkCull.h"
#include "Render/Mesh/IUChunkMesher.h"
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

bool UChunkMeshCache::HasDrawableGreedyMesh(glm::ivec3 chunk_coord) const
{
  const auto it = GreedyCache.find(chunk_coord);
  if (it == GreedyCache.end())
  {
    return false;
  }
  if (it->second.GpuResident && it->second.GpuQuadCount > 0)
  {
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

bool UChunkMeshCache::IsGpuExtractInFlight(glm::ivec3 chunk_coord) const
{
  return GpuExtractInFlight.count(chunk_coord) > 0;
}

bool UChunkMeshCache::IsPendingGpuApply(glm::ivec3 chunk_coord) const
{
  for (const PendingGpuApply &pending : PendingGpuApplies)
  {
    if (pending.coord == chunk_coord)
    {
      return true;
    }
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
        ++count;
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
            ++stale_n;
          }
          else
          {
            ++void_n;
          }
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
        if (HasDrawableGreedyMesh(coord))
        {
          return;
        }
        // Committed cache entry (incl. intentional 0-quad occluded) is not a
        // hole. SoftDefer uses HasDrawable separately so empty remesh is not
        // deferred as "already meshed". Orphaned GpuExtract (no pending apply)
        // falls through.
        if (HasGreedyMesh(coord))
        {
          return;
        }
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

void UChunkMeshCache::BeginHoleQueryFrame()
{
  ++HoleQueryEpoch;
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
    if (HasDrawableGreedyMesh(coord))
    {
      return false;
    }
    if (HasGreedyMesh(coord))
    {
      return false;
    }
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
    if (remesh_only && !HasGreedyMesh(*it) &&
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
    // Hole (!Drawable): Active+RemeshAfterApply-only left miss sticky after
    // CancelOutside / epoch DiscardedLate (manual 213543). Invalidate so
    // FirstMesh can re-enter Dirty while a drawable mesh keeps Remesh deferral.
    if (!HasDrawableGreedyMesh(chunkCoord))
    {
      InvalidateInFlightMeshBuild(chunkCoord);
      if (AsyncBuilder)
      {
        AsyncBuilder->ForgetInflight(chunkCoord);
      }
      GpuExtractInFlight.erase(chunkCoord);
    }
    else
    {
      RemeshAfterApply.insert(chunkCoord);
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
    if (GpuPipeline)
    {
      GpuPipeline->FreeChunk(coord);
    }
    return false;
  }
  const bool defer_until_lit = DeferMeshUntilLit && DeferMeshUntilLit(coord);
  const bool had_mesh = HasDrawableGreedyMesh(coord);
  const bool had_lit_mesh = had_mesh && !ChunkHasFullyDarkFace(coord);
  if (ShouldRejectDarkMeshCommit(gpu_result.hasFullyDarkFace, defer_until_lit,
                                 had_lit_mesh))
  {
    if (GpuPipeline)
    {
      GpuPipeline->FreeChunk(coord);
    }
    if (RemeshAfterApply.erase(coord) > 0 || defer_until_lit || !had_mesh)
    {
      MarkDirtyPriority(coord);
    }
    return false;
  }

  ChunkGreedyMesh &chunkMesh = GreedyCache[coord];
  if (chunkMesh.GpuResident && chunkMesh.GpuSlotIndex >= 0 && GpuPipeline)
  {
    GpuPipeline->FreeChunk(coord);
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
  if (RemeshAfterApply.erase(coord) > 0)
  {
    MarkDirtyPriority(coord);
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

  const auto t0 = std::chrono::high_resolution_clock::now();
  int processed = 0;
  while (!PendingGpuApplies.empty() && processed < max_count)
  {
    if (budget_ms > 0.0)
    {
      const double elapsed = std::chrono::duration<double, std::milli>(
                                 std::chrono::high_resolution_clock::now() - t0)
                                 .count();
      if (elapsed >= budget_ms)
      {
        break;
      }
    }

    PendingGpuApply pending = std::move(PendingGpuApplies.front());
    PendingGpuApplies.pop_front();
    GpuExtractInFlight.erase(pending.coord);

    const uint64_t expected_revision = MeshRevisions.Current(pending.coord);
    const auto revisionIt = ActiveMeshSourceRevision.find(pending.coord);
    const bool has_active = revisionIt != ActiveMeshSourceRevision.end();
    const uint64_t active_rev = has_active ? revisionIt->second : 0;
    const MeshApplyRevDecision decision = ClassifyMeshApplyRevision(
        has_active, active_rev, pending.sourceRevision, expected_revision);
    if (decision == MeshApplyRevDecision::DropNoActive)
    {
      // CancelOutside / Invalidate cleared Active. If nothing drawable remains,
      // re-queue FirstMesh — silent drop left forever-holes that place-block
      // instantly healed (manual 215919). Guard dirty to avoid apply thrash.
      if (!HasDrawableGreedyMesh(pending.coord) &&
          !Dirty.Contains(pending.coord))
      {
        Dirty.MarkDirtyPriority(pending.coord);
      }
      continue;
    }
    if (decision == MeshApplyRevDecision::DiscardOlderKeepActive)
    {
      ++MeshApplySupersededCount;
      continue;
    }
    if (decision == MeshApplyRevDecision::RemeshObsoleteTracked)
    {
      ActiveMeshSourceRevision.erase(pending.coord);
      ++MeshApplyStaleCount;
      Dirty.MarkDirty(pending.coord); // Remesh class, not FirstMesh
      continue;
    }

    GpuMeshProcessResult gpu_result;
    if (!pipeline->ProcessSnapshot(pending.snapshot, registry, gpu_result) ||
        !gpu_result.success)
    {
      ActiveMeshSourceRevision.erase(pending.coord);
      if (GpuPipeline)
      {
        GpuPipeline->FreeChunk(pending.coord);
      }
      Dirty.MarkDirtyPriority(pending.coord);
      continue;
    }

    ActiveMeshSourceRevision.erase(pending.coord);
    if (CommitGpuMeshResult(world, registry, pending.coord,
                            pending.sourceRevision, std::move(gpu_result),
                            std::move(pending.crossCenters)))
    {
      ++processed;
      ++stats.Completed;
    }
  }
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
        const bool missing =
            GreedyCache.find(pending.coord) == GreedyCache.end();
        if (missing && horiz <= MeshFocusRadiusChunks)
        {
          PendingGpuApplies.push_front(std::move(pending));
        }
        else
        {
          PendingGpuApplies.push_back(std::move(pending));
        }
      }
      else
      {
        PendingGpuApplies.push_back(std::move(pending));
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
  const bool had_mesh = GreedyCache.find(result.coord) != GreedyCache.end();
  const bool had_lit_mesh = had_mesh && !ChunkHasFullyDarkFace(result.coord);
  const bool new_dark = BatchesHaveFullyDarkFace(result.batches);
  if (ShouldRejectDarkMeshCommit(new_dark, defer_until_lit, had_lit_mesh))
  {
    // Keep prior lit mesh (or hole). SoftDefer/MarkRelit requeues deferred.
    if (RemeshAfterApply.erase(result.coord) > 0 || defer_until_lit ||
        !had_mesh)
    {
      MarkDirtyPriority(result.coord);
    }
    return;
  }

  ChunkGreedyMesh &chunkMesh = GreedyCache[result.coord];
  chunkMesh.GpuResident = false;
  chunkMesh.GpuSlotIndex = -1;
  chunkMesh.GpuQuadCount = 0;
  chunkMesh.GpuHasDarkFace = false;
  chunkMesh.GpuBlockRanges.clear();
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
  NoteGeometryDirty(result.coord);
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
    // When focus holes/pending-light debt are active, huge Dirty sets were being
    // fully sorted on the main thread only to drop most remesh entries later in
    // try_schedule(). Prune obviously unschedulable remesh work before the sort
    // so mesh_dirty_tick_ms cannot dominate the frame at Dirty~400-900.
    if (MeshFocusValid && Dirty.GetCount() > 256)
    {
      for (auto it = Dirty.begin(); it != Dirty.end();)
      {
        // SoftDefer first: drop deferred work (remesh OR SoftDefer-blocked empty
        // outside focus). In-focus empty SoftDefer returns Defer=false so they
        // stay as FirstMesh. Keeping SoftDefer-true empties bloated Dirty~440
        // (land_south_short undrawn_fix).
        if (DeferMeshUntilLit && DeferMeshUntilLit(*it))
        {
          it = Dirty.RemoveAt(it);
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
          continue;
        }
        if (StarveOutsideFocusMesh)
        {
          const int horiz = std::max(std::abs(it->x - MeshFocusGroundChunk.x),
                                     std::abs(it->z - MeshFocusGroundChunk.z));
          if (horiz > MeshFocusRadiusChunks)
          {
            it = Dirty.RemoveAt(it);
            continue;
          }
        }
        ++it;
      }
    }
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
      // Full stable_sort on Dirty~650 dominated mesh_dirty_tick (~100ms) while
      // schedule budget is only a handful — partial_sort the front window.
      constexpr size_t kMinSortFront = 64;
      const size_t sort_front = std::max(
          kMinSortFront,
          static_cast<size_t>(std::max(8, max_schedule_per_frame) * 8 + 32));
      if (Dirty.GetCount() > sort_front * 2)
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
      MarkDirtyPriority(coord);
    }
    for (const glm::ivec3 &coord : AsyncBuilder->TakeDiscardedCoords())
    {
      // Epoch / jobId DiscardedLate frees InFlight but leaves Active orphan —
      // force FirstMesh path via MarkDirtyPriority hole invalidate.
      MarkDirtyPriority(coord);
    }
    for (MeshBuildResult &result :
         AsyncBuilder->DrainCompleted(max_drain_per_frame))
    {
      ApplyMeshResult(world, registry, std::move(result));
      mesh_data_changed = true;
      ++stats.Completed;
    }

    if (Render.GpuPackedMeshing && !PendingGpuApplies.empty())
    {
      const bool focus_missing =
          HasMissingGreedyMeshInHorizontalRadius(world, MeshFocusGroundChunk,
                                                 RenderDistanceChunks);
      const size_t pending_n = PendingGpuApplies.size();
      // Emerge may clamp schedule when pending_gpu≥12; do not let that starve
      // apply drain (manual 194759: med≈16 while mesh_emerge~60ms).
      double gpu_budget =
          focus_missing
              ? std::max(8.0, MeshEmergeTotalBudgetMs * 0.6)
              : std::max(6.0, MeshEmergeTotalBudgetMs * 0.5);
      int gpu_max =
          std::max(3, std::max(max_drain_per_frame, max_schedule_per_frame));
      // Aggressive drain only when schedule clamp can starve apply (healed
      // undrawn + pending_gpu backlog). With focus holes keep base caps.
      if (!focus_missing && pending_n >= 12)
      {
        gpu_max = std::max(gpu_max, 16);
        gpu_budget = std::max(gpu_budget, MeshEmergeTotalBudgetMs * 0.75);
      }
      const int gpu_done = ProcessPendingGpuMeshes(world, registry, gpu_max,
                                                 gpu_budget, stats);
      if (gpu_done > 0)
      {
        mesh_data_changed = true;
        GreedyBatchesDirty = true;
        CrossBatchesDirty = true;
      }
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
      {
        const double total_elapsed =
            std::chrono::duration<double, std::milli>(
                std::chrono::high_resolution_clock::now() - dirty_tick_t0)
                .count();
        if (total_elapsed > MeshEmergeTotalBudgetMs)
        {
          return Dirty.end();
        }
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
        // SoftDefer-blocked: drop from Dirty until MarkRelit / undrawn heal /
        // SoftDefer opens (in-focus empty SoftDefer returns false → schedule).
        // Do not keep SoftDefer-true empties — Dirty bloat 184035/autofly.
        return Dirty.RemoveAt(it);
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
            return Dirty.RemoveAt(it);
          }
        }
        else
        {
          return Dirty.RemoveAt(it);
        }
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
        if (horiz > MeshFocusRadiusChunks || HasDrawableGreedyMesh(*it))
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
        // SoftDefer-blocked: drop until MarkRelit / undrawn heal re-admits.
        it = Dirty.RemoveAt(it);
        continue;
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
    if (Render.GpuPackedMeshing && !PendingGpuApplies.empty())
    {
      const double tick_elapsed =
          std::chrono::duration<double, std::milli>(
              std::chrono::high_resolution_clock::now() - dirty_tick_t0)
              .count();
      const double remain =
          std::max(0.0, MeshEmergeTotalBudgetMs - tick_elapsed);
      const size_t pending_n = PendingGpuApplies.size();
      const bool focus_missing =
          HasMissingGreedyMeshInHorizontalRadius(world, MeshFocusGroundChunk,
                                                 RenderDistanceChunks);
      double gpu_budget =
          std::min(remain, std::max(4.0, MeshEmergeTotalBudgetMs * 0.4));
      int gpu_max =
          std::max(2, std::max(max_drain_per_frame, max_schedule_per_frame) / 2);
      // Only steal leftover budget for backlog when visuals are already healed
      // (manual 194759); during holes prefer FirstMesh / sync fill.
      if (!focus_missing && pending_n >= 12 && remain > 1.0)
      {
        gpu_max = std::max(gpu_max, 12);
        gpu_budget = std::max(
            gpu_budget, std::min(remain, MeshEmergeTotalBudgetMs * 0.5));
      }
      const int gpu_done = ProcessPendingGpuMeshes(world, registry, gpu_max,
                                                 gpu_budget, stats);
      if (gpu_done > 0)
      {
        mesh_data_changed = true;
        GreedyBatchesDirty = true;
        CrossBatchesDirty = true;
      }
    }
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
    const bool new_dark = BatchesHaveFullyDarkFace(new_batches);
    if (ShouldRejectDarkMeshCommit(new_dark, defer_until_lit, had_lit_mesh))
    {
      // Keep prior lit mesh. Requeue only while SoftDefer owns the column —
      // otherwise MarkDirty thrash rebuilds the same unlit result every tick.
      if (defer_until_lit || !had_mesh)
      {
        MarkDirtyPriority(chunkCoord);
      }
      return;
    }
    const int max_local_y = MaxSolidLocalY(*chunk, registry);
    ChunkGreedyMesh &chunkMesh = GreedyCache[chunkCoord];
    chunkMesh.batches = std::move(new_batches);
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
    NoteGeometryDirty(chunkCoord);
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
