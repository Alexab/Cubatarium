#include "ChunkMeshCache.h"
#include "BlockRegistry.h"
#include "BlockWorld.h"
#include "Frustum.h"
#include "GreedyMesher.h"
#include "GreedyMeshEmitter.h"
#include "GridMath.h"
#include <glm/gtc/matrix_transform.hpp>
#include <unordered_map>

namespace cutum {

namespace {

bool IsFullyEnclosed(const BlockWorld& world, glm::ivec3 pos)
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

void ChunkMeshCache::SetRenderSettings(const RenderSettings& settings)
{
 const bool meshPathChanged = settings.greedyMeshing != renderSettings_.greedyMeshing;
 renderSettings_ = settings;
 if (meshPathChanged) {
  for (const auto& entry : cache_) {
   dirtyChunks_.push_back(entry.first);
  }
  for (const auto& entry : greedyCache_) {
   dirtyChunks_.push_back(entry.first);
  }
  instancesDirty_ = true;
  greedyBatchesDirty_ = true;
  visibleListValid_ = false;
  ++meshRevision_;
 }
}

void ChunkMeshCache::MarkAllDirty()
{
 dirtyChunks_.clear();
 cache_.clear();
 greedyCache_.clear();
 instances_.clear();
 greedyBatches_.clear();
 ++meshRevision_;
 instancesDirty_ = true;
 greedyBatchesDirty_ = true;
 visibleListValid_ = false;
}

void ChunkMeshCache::MarkAllDirtyFromWorld(const BlockWorld& world)
{
 MarkAllDirty();
 world.GetChunkManager().ForEachChunk([this](const Chunk& chunk) {
  dirtyChunks_.push_back(chunk.GetCoord());
 });
}

void ChunkMeshCache::RebuildAll(BlockWorld& world, BlockRegistry& registry)
{
 MarkAllDirtyFromWorld(world);
 RebuildDirtyChunks(world, registry, 10000);
}

void ChunkMeshCache::MarkDirty(glm::ivec3 chunkCoord)
{
 dirtyChunks_.push_back(chunkCoord);
 ++meshRevision_;
 instancesDirty_ = true;
 greedyBatchesDirty_ = true;
 visibleListValid_ = false;
}

void ChunkMeshCache::RemoveChunk(glm::ivec3 chunkCoord)
{
 cache_.erase(chunkCoord);
 greedyCache_.erase(chunkCoord);
 ++meshRevision_;
 instancesDirty_ = true;
 greedyBatchesDirty_ = true;
 visibleListValid_ = false;
}

size_t ChunkMeshCache::GetGreedyVertexCount() const
{
 size_t count = 0;
 for (const GreedyMeshBatch& batch : greedyBatches_) {
  count += batch.vertices.size();
 }
 return count;
}

void ChunkMeshCache::RebuildFlatInstanceList(const Frustum* frustum, const glm::vec3* cameraPos)
{
 instances_.clear();
 for (const auto& entry : cache_) {
  if (frustum && cameraPos) {
   if (!frustum->IntersectsChunkAABB(
           ChunkAABBMin(entry.first), ChunkAABBMax(entry.first), *cameraPos)) {
    continue;
   }
  }
  instances_.insert(instances_.end(), entry.second.begin(), entry.second.end());
 }
 instancesDirty_ = false;
 visibleListValid_ = true;
}

void ChunkMeshCache::RebuildFlatGreedyBatches(const Frustum* frustum, const glm::vec3* cameraPos)
{
 greedyBatches_.clear();
 std::unordered_map<BlockId, GreedyMeshBatch> merged;

 for (const auto& entry : greedyCache_) {
  if (frustum && cameraPos) {
   if (!frustum->IntersectsChunkAABB(
           ChunkAABBMin(entry.first), ChunkAABBMax(entry.first), *cameraPos)) {
    continue;
   }
  }
  for (const GreedyMeshBatch& chunkBatch : entry.second.batches) {
   if (chunkBatch.vertices.empty()) {
    continue;
   }
   GreedyMeshBatch& batch = merged[chunkBatch.blockId];
   if (batch.blockId == BLOCK_AIR) {
    batch.blockId = chunkBatch.blockId;
   }
   MergeGreedyBatch(batch, chunkBatch);
  }
 }

 greedyBatches_.reserve(merged.size());
 for (auto& pair : merged) {
  pair.second.blockId = pair.first;
  greedyBatches_.push_back(std::move(pair.second));
 }
 greedyBatchesDirty_ = false;
 visibleListValid_ = true;
}

void ChunkMeshCache::UpdateVisibleInstances(
    const Frustum& frustum, const glm::mat4& viewProj, const glm::vec3& cameraPos)
{
 (void)viewProj;
 (void)lastCullVP_;
 if (renderSettings_.greedyMeshing) {
  if (renderSettings_.frustumCulling) {
   RebuildFlatGreedyBatches(&frustum, &cameraPos);
  } else {
   RebuildFlatGreedyBatches(nullptr, nullptr);
  }
 } else {
  if (renderSettings_.frustumCulling) {
   RebuildFlatInstanceList(&frustum, &cameraPos);
  } else {
   RebuildFlatInstanceList(nullptr, nullptr);
  }
 }
}

void ChunkMeshCache::RebuildDirtyChunks(BlockWorld& world, BlockRegistry& registry, int maxChunksPerFrame)
{
 int rebuilt = 0;
 for (auto it = dirtyChunks_.begin(); it != dirtyChunks_.end() && rebuilt < maxChunksPerFrame;) {
  RebuildChunk(world, registry, *it);
  it = dirtyChunks_.erase(it);
  ++rebuilt;
 }
 if (instancesDirty_ || greedyBatchesDirty_) {
  if (renderSettings_.greedyMeshing) {
   RebuildFlatGreedyBatches(nullptr, nullptr);
  } else {
   RebuildFlatInstanceList(nullptr, nullptr);
  }
 }
}

void ChunkMeshCache::RebuildChunkLegacy(
    const BlockWorld& world, BlockRegistry& registry, glm::ivec3 chunkCoord,
    std::vector<FaceInstance>& chunkInstances)
{
 const Chunk* chunk = world.GetChunkManager().GetChunk(chunkCoord);
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

void ChunkMeshCache::RebuildChunk(const BlockWorld& world, BlockRegistry& registry, glm::ivec3 chunkCoord)
{
 const Chunk* chunk = world.GetChunkManager().GetChunk(chunkCoord);
 if (!chunk) {
  cache_.erase(chunkCoord);
  greedyCache_.erase(chunkCoord);
  ++meshRevision_;
  instancesDirty_ = true;
  greedyBatchesDirty_ = true;
  visibleListValid_ = false;
  return;
 }

 if (renderSettings_.greedyMeshing) {
  cache_.erase(chunkCoord);
  std::unordered_map<BlockId, GreedyMeshBatch> byBlockId;
  const auto quads = GreedyMesher::BuildChunkMesh(world, chunkCoord, registry);
  for (const GreedyQuad& q : quads) {
   GreedyMeshBatch& batch = byBlockId[q.id];
   batch.blockId = q.id;
   AppendGreedyQuad(q, chunkCoord, batch.vertices, batch.indices);
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
 visibleListValid_ = false;
}

}
