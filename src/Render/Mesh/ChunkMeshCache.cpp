#include "Render/Mesh/ChunkMeshCache.h"
#include "Blocks/BlockRegistry.h"
#include "Render/Camera/Frustum.h"
#include "Render/Engine/DistanceFog.h"
#include "Render/Mesh/AsyncMeshBuilder.h"
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

void CollectCrossCentersInBand(
    const UChunk &chunk, glm::ivec3 chunk_coord, const UBlockRegistry &registry,
    int max_local_y,
    std::unordered_map<BlockId, std::vector<glm::vec3>> &cross_centers)
{
  (void)max_local_y;
  CollectCrossCentersFromChunk(chunk, chunk_coord, registry, cross_centers);
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
  FluidSurfaceCache.clear();
  FluidSurfaceDirty.clear();
  Instances.clear();
  GreedyBatches.clear();
  ++MeshRevision;
  InstancesDirty = true;
  GreedyBatchesDirty = true;
  CrossBatchesDirty = true;
  InvalidateVisibleList();
}
void UChunkMeshCache::MarkAllDirtyFromWorld(const UBlockWorld &world)
{
  MarkAllDirty();
  world.GetChunkManager().ForEachChunk([this](const UChunk &chunk)
                                       { MarkDirty(chunk.GetCoord()); });
}
void UChunkMeshCache::RebuildAll(UBlockWorld &world, UBlockRegistry &registry)
{
  MarkAllDirtyFromWorld(world);
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

bool UChunkMeshCache::HasPendingDirty() const
{
  return !Dirty.empty() || HasPendingAsyncMeshWork();
}
void UChunkMeshCache::MarkDirty(glm::ivec3 chunkCoord)
{
  const size_t before = Dirty.GetCount();
  Dirty.MarkDirty(chunkCoord);
  if (Dirty.GetCount() == before)
  {
    return;
  }
  InvalidateFluidSurfaceForChunk(chunkCoord);
  PendingMeshRevisionBump = true;
  InstancesDirty = true;
  GreedyBatchesDirty = true;
  CrossBatchesDirty = true;
}
void UChunkMeshCache::MarkDirtyPriority(glm::ivec3 chunkCoord)
{
  Dirty.MarkDirtyPriority(chunkCoord);
  InvalidateFluidSurfaceForChunk(chunkCoord);
  PendingMeshRevisionBump = true;
  InstancesDirty = true;
  GreedyBatchesDirty = true;
  CrossBatchesDirty = true;
}
void UChunkMeshCache::RemoveChunk(glm::ivec3 chunkCoord)
{
  Cache.erase(chunkCoord);
  GreedyCache.erase(chunkCoord);
  Dirty.Erase(chunkCoord);
  const glm::ivec3 ground(chunkCoord.x, 0, chunkCoord.z);
  FluidSurfaceCache.erase(ground);
  FluidSurfaceDirty.erase(ground);
  ++MeshRevision;
  InstancesDirty = true;
  GreedyBatchesDirty = true;
  CrossBatchesDirty = true;
  InvalidateVisibleList();
}
size_t UChunkMeshCache::GetGreedyVertexCount() const
{
  size_t count = 0;
  for (const GreedyMeshBatch &batch : GreedyBatches)
  {
    count += batch.vertices.size();
  }
  return count;
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
  if (frustum && cameraPos &&
      TrySkipFlatRebuildForVisibleChunks(frustum, cameraPos, maxCullDistance))
  {
    GreedyBatchesDirty = false;
    return;
  }
  const auto t0 = std::chrono::high_resolution_clock::now();
  const bool horizontal_cull = UseHorizontalCullDistance();
  const auto merge_from_cache =
      [&](const Frustum *cull_frustum, const glm::vec3 *cull_camera,
          float cull_distance) -> std::vector<GreedyMeshBatch>
  {
    std::vector<GreedyMeshBatch> merged;
    merged.reserve(GreedyCache.size() * 4);
    std::unordered_map<BlockId, GreedyMeshBatch> merged_cutout;
    for (const auto &entry : GreedyCache)
    {
      if (!ChunkPassesFrustum(cull_frustum, cull_camera, cull_distance,
                              entry.first, horizontal_cull))
      {
        continue;
      }
      for (const GreedyMeshBatch &chunk_batch : entry.second.batches)
      {
        if (chunk_batch.vertices.empty())
        {
          continue;
        }
        if (!chunk_batch.Transparent && chunk_batch.AlphaCutout)
        {
          GreedyMeshBatch &dst = merged_cutout[chunk_batch.blockId];
          if (dst.vertices.empty())
          {
            dst = chunk_batch;
          }
          else
          {
            MergeGreedyBatch(dst, chunk_batch);
          }
          continue;
        }
        merged.push_back(chunk_batch);
      }
    }
    for (auto &pair : merged_cutout)
    {
      merged.push_back(std::move(pair.second));
    }
    return merged;
  };

  std::vector<GreedyMeshBatch> merged =
      merge_from_cache(frustum, cameraPos, maxCullDistance);
  if (frustum && cameraPos && merged.empty() && !GreedyCache.empty() &&
      (GreedyBatchesDirty || GreedyBatches.empty()))
  {
    merged = merge_from_cache(nullptr, nullptr, 0.0f);
  }
  else if (frustum && cameraPos && merged.empty() && !GreedyCache.empty())
  {
    return;
  }
  GreedyBatches = std::move(merged);
  GreedyBatchesDirty = false;
  ++CullRevision;
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
      -> std::unordered_map<BlockId, std::vector<glm::vec3>>
  {
    std::unordered_map<BlockId, std::vector<glm::vec3>> merged;
    for (const auto &entry : GreedyCache)
    {
      if (!ChunkPassesFrustum(cull_frustum, cull_camera, cull_distance,
                              entry.first, horizontal_cull))
      {
        continue;
      }
      for (const auto &pair : entry.second.crossCenters)
      {
        std::vector<glm::vec3> &dst = merged[pair.first];
        dst.insert(dst.end(), pair.second.begin(), pair.second.end());
      }
    }
    return merged;
  };

  std::unordered_map<BlockId, std::vector<glm::vec3>> merged =
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
    batch.centers = std::move(pair.second);
    CrossBatches.push_back(std::move(batch));
  }
  CrossBatchesDirty = false;
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
      GreedyBatchesDirty || (GreedyBatches.empty() && !GreedyCache.empty());
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
  ChunkGreedyMesh &chunkMesh = GreedyCache[result.coord];
  chunkMesh.batches = std::move(result.batches);
  chunkMesh.crossCenters = std::move(result.crossCenters);
  PendingMeshRevisionBump = true;
  InstancesDirty = true;
  GreedyBatchesDirty = true;
  CrossBatchesDirty = true;
  InvalidateFluidSurfaceForChunk(result.coord);
}

void UChunkMeshCache::RebuildDirtyChunks(UBlockWorld &world,
                                         UBlockRegistry &registry,
                                         int max_drain_per_frame,
                                         int max_schedule_per_frame)
{
  bool mesh_data_changed = false;
  if (Render.AsyncMeshing && Render.GreedyMeshing)
  {
    EnsureAsyncBuilder();
    for (MeshBuildResult &result :
         AsyncBuilder->DrainCompleted(max_drain_per_frame))
    {
      ApplyMeshResult(world, std::move(result));
      mesh_data_changed = true;
    }

    int scheduled = 0;
    for (auto it = Dirty.begin();
         it != Dirty.end() && scheduled < max_schedule_per_frame;)
    {
      if (AsyncBuilder->IsInFlight(*it))
      {
        ++it;
        continue;
      }
      if (!world.GetChunkManager().HasChunk(*it))
      {
        it = Dirty.RemoveAt(it);
        continue;
      }
      ChunkMeshSnapshot snapshot =
          ChunkMeshSnapshot::Capture(world, *it, MeshRevision);
      AsyncBuilder->Enqueue(std::move(snapshot), registry);
      it = Dirty.RemoveAt(it);
      ++scheduled;
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
    return;
  }

  int rebuilt = 0;
  const int sync_budget = std::max(max_drain_per_frame, max_schedule_per_frame);
  for (auto it = Dirty.begin(); it != Dirty.end() && rebuilt < sync_budget;)
  {
    RebuildChunk(world, registry, *it);
    it = Dirty.RemoveAt(it);
    ++rebuilt;
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
}

int UChunkMeshCache::GetAsyncInFlightCount() const
{
  if (!AsyncBuilder)
  {
    return 0;
  }
  return AsyncBuilder->GetInFlightCount();
}

void UChunkMeshCache::DrainAsyncMeshResults(UBlockWorld &world,
                                            UBlockRegistry &registry,
                                            int max_per_frame)
{
  if (!Render.AsyncMeshing || !Render.GreedyMeshing || !AsyncBuilder)
  {
    return;
  }
  for (MeshBuildResult &result : AsyncBuilder->DrainCompleted(max_per_frame))
  {
    ApplyMeshResult(world, std::move(result));
  }
}

void UChunkMeshCache::RebuildChunkImmediate(const UBlockWorld &world,
                                            UBlockRegistry &registry,
                                            glm::ivec3 chunkCoord)
{
  RebuildChunk(world, registry, chunkCoord);
  Dirty.Erase(chunkCoord);
  InvalidateVisibleList();
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
    chunkMesh.crossCenters.clear();
    CollectCrossCentersInBand(*chunk, chunkCoord, registry, max_local_y,
                              chunkMesh.crossCenters);
  }
  else
  {
    GreedyCache.erase(chunkCoord);
    std::vector<FaceInstance> chunkInstances;
    chunkInstances.reserve(512);
    RebuildChunkLegacy(world, registry, chunkCoord, chunkInstances);
    Cache[chunkCoord] = std::move(chunkInstances);
  }
  PendingMeshRevisionBump = true;
  InstancesDirty = true;
  GreedyBatchesDirty = true;
  CrossBatchesDirty = true;
  InvalidateFluidSurfaceForChunk(chunkCoord);
}

void UChunkMeshCache::InvalidateFluidSurfaceForChunk(glm::ivec3 chunkCoord)
{
  const glm::ivec3 ground(chunkCoord.x, 0, chunkCoord.z);
  FluidSurfaceDirty.insert(ground);
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
