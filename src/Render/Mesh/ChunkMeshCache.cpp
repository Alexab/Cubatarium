#include "Render/Mesh/ChunkMeshCache.h"
#include "Blocks/BlockRegistry.h"
#include "Render/Mesh/CrossMeshEmitter.h"
#include "Render/Mesh/GreedyMeshEmitter.h"
#include "Render/Mesh/GreedyMesher.h"
#include "Render/Camera/Frustum.h"
#include "World/Core/BlockWorld.h"
#include "World/Math/GridMath.h"
#include <algorithm>
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
  return static_cast<float>(RenderDistanceChunks + 2) *
         static_cast<float>(CHUNK_SIZE);
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
  RebuildDirtyChunks(world, registry, 10000);
}
void UChunkMeshCache::MarkDirty(glm::ivec3 chunkCoord)
{
  if (!DirtyChunkSet.insert(chunkCoord).second)
  {
    return;
  }
  DirtyChunks.push_back(chunkCoord);
  ++MeshRevision;
  InstancesDirty = true;
  GreedyBatchesDirty = true;
  InvalidateVisibleList();
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
  GreedyBatches.clear();
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
    for (const GreedyMeshBatch &chunkBatch : entry.second.batches)
    {
      if (chunkBatch.vertices.empty())
      {
        continue;
      }
      GreedyBatches.push_back(chunkBatch);
    }
  }
  // Safety: never drop all geometry when cache has data (bad frustum state).
  if (frustum && cameraPos && GreedyBatches.empty() && !GreedyCache.empty())
  {
    RebuildFlatGreedyBatches(nullptr, nullptr, 0.0f);
    return;
  }
  GreedyBatchesDirty = false;
  ++CullRevision;
}
void UChunkMeshCache::UpdateVisibleInstances(const Frustum &frustum,
                                             const glm::mat4 &viewProj,
                                             const glm::vec3 &cameraPos)
{
  (void)viewProj;
  const glm::ivec3 cameraChunk =
      UChunkManager::WorldToChunk(WorldPosToBlock(cameraPos));
  const float maxCullDistance = MaxCullDistance();
  if (!InstancesDirty && !GreedyBatchesDirty &&
      cameraChunk == LastCullCameraChunk &&
      MeshRevision == LastCullMeshRevision &&
      !(Render.GreedyMeshing && GreedyBatches.empty() && !GreedyCache.empty()))
  {
    return;
  }
  LastCullCameraChunk = cameraChunk;
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
void UChunkMeshCache::RebuildDirtyChunks(UBlockWorld &world,
                                         UBlockRegistry &registry,
                                         int maxChunksPerFrame)
{
  int rebuilt = 0;
  for (auto it = DirtyChunks.begin();
       it != DirtyChunks.end() && rebuilt < maxChunksPerFrame;)
  {
    RebuildChunk(world, registry, *it);
    DirtyChunkSet.erase(*it);
    it = DirtyChunks.erase(it);
    ++rebuilt;
  }
  if (InstancesDirty || GreedyBatchesDirty)
  {
    if (Render.GreedyMeshing)
    {
      RebuildFlatGreedyBatches(nullptr, nullptr, 0.0f);
    }
    else
    {
      RebuildFlatInstanceList(nullptr, nullptr, 0.0f);
    }
    LastCullCameraChunk = glm::ivec3(INT32_MAX, INT32_MAX, INT32_MAX);
    LastCullMeshRevision = 0;
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
    for (int lx = 0; lx < CHUNK_SIZE; ++lx)
    {
      for (int ly = 0; ly < CHUNK_SIZE; ++ly)
      {
        for (int lz = 0; lz < CHUNK_SIZE; ++lz)
        {
          const glm::ivec3 local(lx, ly, lz);
          const BlockId Id = chunk->GetBlockLocal(local);
          if (Id == BLOCK_AIR ||
              registry.GetRenderStyle(Id) != BlockRenderStyle::Cross)
          {
            continue;
          }
          const glm::ivec3 worldPos(chunkCoord.x * CHUNK_SIZE + lx,
                                    chunkCoord.y * CHUNK_SIZE + ly,
                                    chunkCoord.z * CHUNK_SIZE + lz);
          GreedyMeshBatch &batch = byBlockId[Id];
          batch.blockId = Id;
          batch.Transparent = true;
          AppendCrossSprite(BlockCenter(worldPos), batch.vertices,
                            batch.indices);
        }
      }
    }
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
    RebuildChunkLegacy(world, registry, chunkCoord, chunkInstances);
    Cache[chunkCoord] = std::move(chunkInstances);
  }
  ++MeshRevision;
  InstancesDirty = true;
  GreedyBatchesDirty = true;
  InvalidateVisibleList();
}
} // namespace cutum
