#include "ChunkMeshCache.h"
#include "BlockRegistry.h"
#include "BlockWorld.h"
#include "Frustum.h"
#include "GreedyMesher.h"
#include "GreedyMeshEmitter.h"
#include "CrossMeshEmitter.h"
#include "GridMath.h"
#include <algorithm>
#include <cmath>
#include <cstdint>
#include <glm/gtc/matrix_transform.hpp>
#include <unordered_map>

namespace cutum {

namespace {
bool IsFullyEnclosed(const UBlockWorld& world, glm::ivec3 pos)
{
 for (const glm::ivec3& offset : NEIGHBOR_OFFSETS) {
  if (world.IsAir(pos + offset)) {
   return false;
  }
 }
 return true;
}
void MergeGreedyBatch(GreedyMeshBatch& dst, const GreedyMeshBatch& src)
{
 if (src.vertices.empty()) {
  return;
 }
 const uint32_t base = static_cast<uint32_t>(dst.vertices.size());
 dst.vertices.insert(dst.vertices.end(), src.vertices.begin(), src.vertices.end());
 dst.indices.reserve(dst.indices.size() + src.indices.size());
 for (uint32_t index : src.indices) {
  dst.indices.push_back(base + index);
 }
}
} // namespace
float UChunkMeshCache::MaxCullDistance() const
{
 return static_cast<float>(RenderDistanceChunks + 2) * static_cast<float>(CHUNK_SIZE);
}
void UChunkMeshCache::InvalidateVisibleList()
{
 instancesDirty_ = true;
 greedyBatchesDirty_ = true;
 lastCullCameraChunk_ = glm::ivec3(INT32_MAX, INT32_MAX, INT32_MAX);
 lastCullMeshRevision_ = 0;
}
void UChunkMeshCache::SetRenderSettings(const RenderSettings& settings)
{
 const bool meshPathChanged = settings.greedyMeshing != Render.greedyMeshing;
 Render = settings;
 if (meshPathChanged) {
  for (const auto& entry : cache_) {
   MarkDirty(entry.first);
  }
  for (const auto& entry : greedyCache_) {
   MarkDirty(entry.first);
  }
  InvalidateVisibleList();
  ++meshRevision_;
 }
}
void UChunkMeshCache::MarkAllDirty()
{
 dirtyChunks_.clear();
 dirtyChunkSet_.clear();
 cache_.clear();
 greedyCache_.clear();
 instances_.clear();
 greedyBatches_.clear();
 ++meshRevision_;
 instancesDirty_ = true;
 greedyBatchesDirty_ = true;
 InvalidateVisibleList();
}
void UChunkMeshCache::MarkAllDirtyFromWorld(const UBlockWorld& world)
{
 MarkAllDirty();
 world.GetChunkManager().ForEachChunk([this](const UChunk& chunk) {
  MarkDirty(chunk.GetCoord());
 });
}
void UChunkMeshCache::RebuildAll(UBlockWorld& world, UBlockRegistry& registry)
{
 MarkAllDirtyFromWorld(world);
 RebuildDirtyChunks(world, registry, 10000);
}
void UChunkMeshCache::MarkDirty(glm::ivec3 chunkCoord)
{
 if (dirtyChunkSet_.insert(chunkCoord).second) {
  dirtyChunks_.push_back(chunkCoord);
 }
 ++meshRevision_;
 instancesDirty_ = true;
 greedyBatchesDirty_ = true;
 InvalidateVisibleList();
}
void UChunkMeshCache::RemoveChunk(glm::ivec3 chunkCoord)
{
 cache_.erase(chunkCoord);
 greedyCache_.erase(chunkCoord);
 dirtyChunkSet_.erase(chunkCoord);
 ++meshRevision_;
 instancesDirty_ = true;
 greedyBatchesDirty_ = true;
 InvalidateVisibleList();
}
size_t UChunkMeshCache::GetGreedyVertexCount() const
{
 size_t count = 0;
 for (const GreedyMeshBatch& batch : greedyBatches_) {
  count += batch.vertices.size();
 }
 return count;
}
void UChunkMeshCache::RebuildFlatInstanceList(const Frustum* frustum, const glm::vec3* cameraPos,
                                             float maxCullDistance)
{
 instances_.clear();
 for (const auto& entry : cache_) {
  if (frustum && cameraPos) {
   if (!frustum->IntersectsChunkAABB(
           ChunkAABBMin(entry.first), ChunkAABBMax(entry.first), *cameraPos, maxCullDistance)) {
    continue;
   }
  }
  instances_.insert(instances_.end(), entry.second.begin(), entry.second.end());
 }
 instancesDirty_ = false;
 ++cullRevision_;
}
void UChunkMeshCache::RebuildFlatGreedyBatches(const Frustum* frustum, const glm::vec3* cameraPos,
                                              float maxCullDistance)
{
 greedyBatches_.clear();
 for (const auto& entry : greedyCache_) {
  if (frustum && cameraPos) {
   if (!frustum->IntersectsChunkAABB(
           ChunkAABBMin(entry.first), ChunkAABBMax(entry.first), *cameraPos, maxCullDistance)) {
    continue;
   }
  }
  for (const GreedyMeshBatch& chunkBatch : entry.second.batches) {
   if (chunkBatch.vertices.empty()) {
    continue;
   }
   greedyBatches_.push_back(chunkBatch);
  }
 }
 // Safety: never drop all geometry when cache has data (bad frustum state).
 if (frustum && cameraPos && greedyBatches_.empty() && !greedyCache_.empty()) {
  RebuildFlatGreedyBatches(nullptr, nullptr, 0.0f);
  return;
 }
 greedyBatchesDirty_ = false;
 ++cullRevision_;
}
void UChunkMeshCache::UpdateVisibleInstances(
    const Frustum& frustum, const glm::mat4& viewProj, const glm::vec3& cameraPos)
{
 (void)viewProj;
 const glm::ivec3 cameraChunk = UChunkManager::WorldToChunk(WorldPosToBlock(cameraPos));
 const float maxCullDistance = MaxCullDistance();
 if (!instancesDirty_ && !greedyBatchesDirty_
     && cameraChunk == lastCullCameraChunk_
     && meshRevision_ == lastCullMeshRevision_
     && !(Render.greedyMeshing && greedyBatches_.empty() && !greedyCache_.empty())) {
  return;
 }
 lastCullCameraChunk_ = cameraChunk;
 lastCullMeshRevision_ = meshRevision_;
 if (Render.greedyMeshing) {
  if (Render.frustumCulling) {
   RebuildFlatGreedyBatches(&frustum, &cameraPos, maxCullDistance);
  } else {
   RebuildFlatGreedyBatches(nullptr, nullptr, 0.0f);
  }
 } else {
  if (Render.frustumCulling) {
   RebuildFlatInstanceList(&frustum, &cameraPos, maxCullDistance);
  } else {
   RebuildFlatInstanceList(nullptr, nullptr, 0.0f);
  }
 }
}
void UChunkMeshCache::RebuildDirtyChunks(UBlockWorld& world, UBlockRegistry& registry, int maxChunksPerFrame)
{
 int rebuilt = 0;
 for (auto it = dirtyChunks_.begin(); it != dirtyChunks_.end() && rebuilt < maxChunksPerFrame;) {
  RebuildChunk(world, registry, *it);
  dirtyChunkSet_.erase(*it);
  it = dirtyChunks_.erase(it);
  ++rebuilt;
 }
 if (instancesDirty_ || greedyBatchesDirty_) {
  if (Render.greedyMeshing) {
   RebuildFlatGreedyBatches(nullptr, nullptr, 0.0f);
  } else {
   RebuildFlatInstanceList(nullptr, nullptr, 0.0f);
  }
  lastCullCameraChunk_ = glm::ivec3(INT32_MAX, INT32_MAX, INT32_MAX);
  lastCullMeshRevision_ = 0;
 }
}
void UChunkMeshCache::RebuildChunkImmediate(
    const UBlockWorld& world, UBlockRegistry& registry, glm::ivec3 chunkCoord)
{
 RebuildChunk(world, registry, chunkCoord);
 dirtyChunkSet_.erase(chunkCoord);
 dirtyChunks_.erase(
     std::remove(dirtyChunks_.begin(), dirtyChunks_.end(), chunkCoord),
     dirtyChunks_.end());
 InvalidateVisibleList();
}
void UChunkMeshCache::RebuildChunkLegacy(
    const UBlockWorld& world, UBlockRegistry& registry, glm::ivec3 chunkCoord,
    std::vector<FaceInstance>& chunkInstances)
{
 const UChunk* chunk = world.GetChunkManager().GetChunk(chunkCoord);
 if (!chunk) {
  return;
 }
 for (int z = 0; z < CHUNK_SIZE; ++z) {
  for (int y = 0; y < CHUNK_SIZE; ++y) {
   for (int x = 0; x < CHUNK_SIZE; ++x) {
    const glm::ivec3 local(x, y, z);
    const BlockId id = chunk->GetBlockLocal(local);
    if (!registry.IsSolid(id)) {
     continue;
    }
    const glm::ivec3 worldPos(
        chunkCoord.x * CHUNK_SIZE + x,
        chunkCoord.y * CHUNK_SIZE + y,
        chunkCoord.z * CHUNK_SIZE + z);
    if (IsFullyEnclosed(world, worldPos)) {
     continue;
    }
    FaceInstance instance;
    instance.id = id;
    instance.model = glm::translate(glm::mat4(1.0f), BlockCenter(worldPos));
    chunkInstances.push_back(instance);
   }
  }
 }
}
void UChunkMeshCache::RebuildChunk(const UBlockWorld& world, UBlockRegistry& registry, glm::ivec3 chunkCoord)
{
 const UChunk* chunk = world.GetChunkManager().GetChunk(chunkCoord);
 if (!chunk) {
  cache_.erase(chunkCoord);
  greedyCache_.erase(chunkCoord);
  ++meshRevision_;
  instancesDirty_ = true;
  greedyBatchesDirty_ = true;
  InvalidateVisibleList();
  return;
 }
 if (Render.greedyMeshing) {
  cache_.erase(chunkCoord);
  std::unordered_map<BlockId, GreedyMeshBatch> byBlockId;
  const auto quads = UGreedyMesher::BuildChunkMesh(world, chunkCoord, registry);
  for (const GreedyQuad& q : quads) {
   GreedyMeshBatch& batch = byBlockId[q.id];
   batch.blockId = q.id;
   batch.transparent = registry.IsTransparent(q.id);
   AppendGreedyQuad(q, chunkCoord, batch.vertices, batch.indices);
  }
  for (int lx = 0; lx < CHUNK_SIZE; ++lx) {
   for (int ly = 0; ly < CHUNK_SIZE; ++ly) {
    for (int lz = 0; lz < CHUNK_SIZE; ++lz) {
     const glm::ivec3 local(lx, ly, lz);
     const BlockId id = chunk->GetBlockLocal(local);
     if (id == BLOCK_AIR || registry.GetRenderStyle(id) != BlockRenderStyle::Cross) {
      continue;
     }
     const glm::ivec3 worldPos(
         chunkCoord.x * CHUNK_SIZE + lx,
         chunkCoord.y * CHUNK_SIZE + ly,
         chunkCoord.z * CHUNK_SIZE + lz);
     GreedyMeshBatch& batch = byBlockId[id];
     batch.blockId = id;
     batch.transparent = true;
     AppendCrossSprite(BlockCenter(worldPos), batch.vertices, batch.indices);
    }
   }
  }
  ChunkGreedyMesh& chunkMesh = greedyCache_[chunkCoord];
  chunkMesh.batches.clear();
  chunkMesh.batches.reserve(byBlockId.size());
  for (auto& pair : byBlockId) {
   pair.second.blockId = pair.first;
   chunkMesh.batches.push_back(std::move(pair.second));
  }
 } else {
  greedyCache_.erase(chunkCoord);
  std::vector<FaceInstance> chunkInstances;
  RebuildChunkLegacy(world, registry, chunkCoord, chunkInstances);
  cache_[chunkCoord] = std::move(chunkInstances);
 }
 ++meshRevision_;
 instancesDirty_ = true;
 greedyBatchesDirty_ = true;
 InvalidateVisibleList();
}
}
