#include "ChunkMeshCache.h"
#include "BlockRegistry.h"
#include "BlockWorld.h"
#include "Frustum.h"
#include "GreedyMesher.h"
#include "GreedyMeshMath.h"

namespace cutum {

void ChunkMeshCache::MarkAllDirty()
{
 dirtyChunks_.clear();
 cache_.clear();
 instances_.clear();
 ++meshRevision_;
 instancesDirty_ = true;
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
 visibleListValid_ = false;
}

void ChunkMeshCache::RemoveChunk(glm::ivec3 chunkCoord)
{
 cache_.erase(chunkCoord);
 ++meshRevision_;
 instancesDirty_ = true;
 visibleListValid_ = false;
}

void ChunkMeshCache::RebuildFlatInstanceList(const Frustum* frustum)
{
 instances_.clear();
 for (const auto& entry : cache_) {
  if (frustum) {
   if (!frustum->IntersectsAABB(ChunkAABBMin(entry.first), ChunkAABBMax(entry.first))) {
    continue;
   }
  }
  instances_.insert(instances_.end(), entry.second.begin(), entry.second.end());
 }
 instancesDirty_ = false;
 visibleListValid_ = true;
}

void ChunkMeshCache::UpdateVisibleInstances(const Frustum& frustum, const glm::mat4& viewProj)
{
 if (visibleListValid_ && viewProj == lastCullVP_) {
  return;
 }
 lastCullVP_ = viewProj;
 RebuildFlatInstanceList(&frustum);
}

void ChunkMeshCache::RebuildDirtyChunks(BlockWorld& world, BlockRegistry& registry, int maxChunksPerFrame)
{
 int rebuilt = 0;
 for (auto it = dirtyChunks_.begin(); it != dirtyChunks_.end() && rebuilt < maxChunksPerFrame;) {
  RebuildChunk(world, registry, *it);
  it = dirtyChunks_.erase(it);
  ++rebuilt;
 }
 if (instancesDirty_) {
  RebuildFlatInstanceList(nullptr);
 }
}

void ChunkMeshCache::RebuildChunk(const BlockWorld& world, BlockRegistry& registry, glm::ivec3 chunkCoord)
{
 std::vector<FaceInstance> chunkInstances;
 const Chunk* chunk = world.GetChunkManager().GetChunk(chunkCoord);
 if (!chunk) {
  cache_[chunkCoord] = std::move(chunkInstances);
  ++meshRevision_;
  instancesDirty_ = true;
  visibleListValid_ = false;
  return;
 }

 const auto quads = GreedyMesher::BuildChunkMesh(world, chunkCoord, registry);
 chunkInstances.reserve(quads.size());
 for (const GreedyQuad& q : quads) {
  chunkInstances.push_back(MakeFaceInstanceFromQuad(q, chunkCoord));
 }
 cache_[chunkCoord] = std::move(chunkInstances);
 ++meshRevision_;
 instancesDirty_ = true;
 visibleListValid_ = false;
}

}
