#include "ChunkMeshCache.h"
#include "BlockRegistry.h"
#include "BlockWorld.h"
#include "GreedyMesher.h"
#include "GreedyMeshMath.h"

namespace cutum {

void ChunkMeshCache::MarkAllDirty()
{
 dirtyChunks_.clear();
 cache_.clear();
 instances_.clear();
 instancesDirty_ = true;
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
 instancesDirty_ = true;
}

void ChunkMeshCache::RemoveChunk(glm::ivec3 chunkCoord)
{
 cache_.erase(chunkCoord);
 instancesDirty_ = true;
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
  instances_.clear();
  for (const auto& entry : cache_) {
   instances_.insert(instances_.end(), entry.second.begin(), entry.second.end());
  }
  instancesDirty_ = false;
 }
}

void ChunkMeshCache::RebuildChunk(const BlockWorld& world, BlockRegistry& registry, glm::ivec3 chunkCoord)
{
 std::vector<FaceInstance> chunkInstances;
 const Chunk* chunk = world.GetChunkManager().GetChunk(chunkCoord);
 if (!chunk) {
  cache_[chunkCoord] = std::move(chunkInstances);
  instancesDirty_ = true;
  return;
 }

 const auto quads = GreedyMesher::BuildChunkMesh(world, chunkCoord, registry);
 chunkInstances.reserve(quads.size());
 for (const GreedyQuad& q : quads) {
  chunkInstances.push_back(MakeFaceInstanceFromQuad(q, chunkCoord));
 }
 cache_[chunkCoord] = std::move(chunkInstances);
 instancesDirty_ = true;
}

}
