#include "Render/Mesh/ChunkMeshCache.h"
#include "Blocks/BlockRegistry.h"
#include "Render/Camera/Frustum.h"
#include "Render/Engine/DistanceFog.h"
#include "Render/Mesh/AsyncMeshBuilder.h"
#include "Render/Mesh/ChunkMeshSnapshot.h"
#include "Render/Mesh/CrossMeshEmitter.h"
#include "Render/Mesh/GreedyMeshEmitter.h"
#include "Render/Mesh/GreedyMesher.h"
#include "World/Core/BlockWorld.h"
#include "World/Math/GridMath.h"
#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstdint>
#include <glm/gtc/matrix_transform.hpp>
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

void AppendCrossBlocksInBand(
    const UChunk &chunk, glm::ivec3 chunk_coord, const UBlockRegistry &registry,
    int max_local_y, std::unordered_map<BlockId, GreedyMeshBatch> &by_block_id)
{
  const int y_min = std::max(0, max_local_y - kCrossScanBelow);
  const int y_max = std::min(CHUNK_SIZE - 1, max_local_y + kCrossScanAbove);
  for (int ly = y_min; ly <= y_max; ++ly)
  {
    for (int lz = 0; lz < CHUNK_SIZE; ++lz)
    {
      for (int lx = 0; lx < CHUNK_SIZE; ++lx)
      {
        const glm::ivec3 local(lx, ly, lz);
        const BlockId id = chunk.GetBlockLocal(local);
        if (id == BLOCK_AIR ||
            registry.GetRenderStyle(id) != BlockRenderStyle::Cross)
        {
          continue;
        }
        const glm::ivec3 world_pos(chunk_coord.x * CHUNK_SIZE + lx,
                                   chunk_coord.y * CHUNK_SIZE + ly,
                                   chunk_coord.z * CHUNK_SIZE + lz);
        GreedyMeshBatch &batch = by_block_id[id];
        batch.blockId = id;
        batch.Transparent = false;
        batch.AlphaCutout = true;
        AppendCrossSprite(BlockCenter(world_pos), batch.vertices,
                          batch.indices);
      }
    }
  }
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
} // namespace
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
  DirtyChunks.clear();
  DirtyChunkSet.clear();
  Cache.clear();
  GreedyCache.clear();
  Instances.clear();
  GreedyBatches.clear();
  ++MeshRevision;
  InstancesDirty = true;
  GreedyBatchesDirty = true;
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
  return !DirtyChunks.empty() || HasPendingAsyncMeshWork();
}
void UChunkMeshCache::MarkDirty(glm::ivec3 chunkCoord)
{
  if (!DirtyChunkSet.insert(chunkCoord).second)
  {
    return;
  }
  DirtyChunks.push_back(chunkCoord);
  PendingMeshRevisionBump = true;
  InstancesDirty = true;
  GreedyBatchesDirty = true;
}
void UChunkMeshCache::RemoveChunk(glm::ivec3 chunkCoord)
{
  Cache.erase(chunkCoord);
  GreedyCache.erase(chunkCoord);
  DirtyChunkSet.erase(chunkCoord);
  ++MeshRevision;
  InstancesDirty = true;
  GreedyBatchesDirty = true;
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
  Instances.clear();
  for (const auto &entry : Cache)
  {
    if (frustum && cameraPos)
    {
      if (!frustum->IntersectsChunkAABB(ChunkAABBMin(entry.first),
                                        ChunkAABBMax(entry.first), *cameraPos,
                                        maxCullDistance))
      {
        continue;
      }
    }
    Instances.insert(Instances.end(), entry.second.begin(), entry.second.end());
  }
  InstancesDirty = false;
  ++CullRevision;
}
void UChunkMeshCache::RebuildFlatGreedyBatches(const Frustum *frustum,
                                               const glm::vec3 *cameraPos,
                                               float maxCullDistance)
{
  const auto t0 = std::chrono::high_resolution_clock::now();
  GreedyBatches.clear();
  GreedyBatches.reserve(GreedyCache.size() * 4);
  std::unordered_map<BlockId, GreedyMeshBatch> merged_cross;
  for (const auto &entry : GreedyCache)
  {
    if (frustum && cameraPos)
    {
      if (!frustum->IntersectsChunkAABB(ChunkAABBMin(entry.first),
                                        ChunkAABBMax(entry.first), *cameraPos,
                                        maxCullDistance))
      {
        continue;
      }
    }
    for (const GreedyMeshBatch &chunk_batch : entry.second.batches)
    {
      if (chunk_batch.vertices.empty())
      {
        continue;
      }
      if (!chunk_batch.Transparent && chunk_batch.AlphaCutout)
      {
        GreedyMeshBatch &dst = merged_cross[chunk_batch.blockId];
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
      GreedyBatches.push_back(chunk_batch);
    }
  }
  for (auto &pair : merged_cross)
  {
    GreedyBatches.push_back(std::move(pair.second));
  }
  // Safety: never drop all geometry when cache has data (bad frustum state).
  if (frustum && cameraPos && GreedyBatches.empty() && !GreedyCache.empty())
  {
    RebuildFlatGreedyBatches(nullptr, nullptr, 0.0f);
    return;
  }
  GreedyBatchesDirty = false;
  ++CullRevision;
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
  if (!InstancesDirty && !GreedyBatchesDirty &&
      MeshRevision == LastCullMeshRevision &&
      !(Render.GreedyMeshing && GreedyBatches.empty() && !GreedyCache.empty()))
  {
    return;
  }
  LastCullCameraChunk = UChunkManager::WorldToChunk(WorldPosToBlock(cameraPos));
  LastCullMeshRevision = MeshRevision;
  if (Render.GreedyMeshing)
  {
    if (Render.FrustumCulling)
    {
      RebuildFlatGreedyBatches(&frustum, &cameraPos, maxCullDistance);
    }
    else
    {
      RebuildFlatGreedyBatches(nullptr, nullptr, 0.0f);
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
  ++MeshRevision;
  InstancesDirty = true;
  GreedyBatchesDirty = true;
}

void UChunkMeshCache::RebuildDirtyChunks(UBlockWorld &world,
                                         UBlockRegistry &registry,
                                         int max_drain_per_frame,
                                         int max_schedule_per_frame)
{
  if (Render.AsyncMeshing && Render.GreedyMeshing)
  {
    EnsureAsyncBuilder();
    for (MeshBuildResult &result :
         AsyncBuilder->DrainCompleted(max_drain_per_frame))
    {
      ApplyMeshResult(world, std::move(result));
    }

    int scheduled = 0;
    for (auto it = DirtyChunks.begin();
         it != DirtyChunks.end() && scheduled < max_schedule_per_frame;)
    {
      if (AsyncBuilder->IsInFlight(*it))
      {
        ++it;
        continue;
      }
      if (!world.GetChunkManager().HasChunk(*it))
      {
        DirtyChunkSet.erase(*it);
        it = DirtyChunks.erase(it);
        continue;
      }
      ChunkMeshSnapshot snapshot =
          ChunkMeshSnapshot::Capture(world, *it, MeshRevision);
      AsyncBuilder->Enqueue(std::move(snapshot), registry);
      DirtyChunkSet.erase(*it);
      it = DirtyChunks.erase(it);
      ++scheduled;
    }
    if (InstancesDirty)
    {
      InstancesDirty = false;
    }
    GreedyBatchesDirty = true;
    BumpMeshRevisionIfNeeded();
    return;
  }

  int rebuilt = 0;
  const int sync_budget = std::max(max_drain_per_frame, max_schedule_per_frame);
  for (auto it = DirtyChunks.begin();
       it != DirtyChunks.end() && rebuilt < sync_budget;)
  {
    RebuildChunk(world, registry, *it);
    DirtyChunkSet.erase(*it);
    it = DirtyChunks.erase(it);
    ++rebuilt;
  }
  if (InstancesDirty)
  {
    InstancesDirty = false;
  }
  GreedyBatchesDirty = true;
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
  DirtyChunkSet.erase(chunkCoord);
  DirtyChunks.erase(
      std::remove(DirtyChunks.begin(), DirtyChunks.end(), chunkCoord),
      DirtyChunks.end());
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
    ++MeshRevision;
    InstancesDirty = true;
    GreedyBatchesDirty = true;
    InvalidateVisibleList();
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
      AppendGreedyQuad(q, chunkCoord, batch.vertices, batch.indices);
    }
    const int max_local_y = MaxSolidLocalY(*chunk, registry);
    AppendCrossBlocksInBand(*chunk, chunkCoord, registry, max_local_y,
                            byBlockId);
    ChunkGreedyMesh &chunkMesh = GreedyCache[chunkCoord];
    chunkMesh.batches.clear();
    chunkMesh.batches.reserve(byBlockId.size());
    for (auto &pair : byBlockId)
    {
      pair.second.blockId = pair.first;
      chunkMesh.batches.push_back(std::move(pair.second));
    }
  }
  else
  {
    GreedyCache.erase(chunkCoord);
    std::vector<FaceInstance> chunkInstances;
    chunkInstances.reserve(512);
    RebuildChunkLegacy(world, registry, chunkCoord, chunkInstances);
    Cache[chunkCoord] = std::move(chunkInstances);
  }
  ++MeshRevision;
  InstancesDirty = true;
  GreedyBatchesDirty = true;
  InvalidateVisibleList();
}
} // namespace cutum
