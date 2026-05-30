#include "ChunkMeshCache.h"
#include "BlockRegistry.h"
#include "BlockWorld.h"
#include "Frustum.h"
#include "GreedyMesher.h"
#include "GreedyMeshMath.h"
#include "GridMath.h"
#include <glm/gtc/matrix_transform.hpp>

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

} // namespace

void ChunkMeshCache::SetRenderSettings(const RenderSettings& settings)
{
 const bool meshPathChanged = settings.greedyMeshing != renderSettings_.greedyMeshing;
 renderSettings_ = settings;
 if (meshPathChanged) {
  for (const auto& entry : cache_) {
   dirtyChunks_.push_back(entry.first);
  }
  instancesDirty_ = true;
  visibleListValid_ = false;
  ++meshRevision_;
 }
}

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
 (void)viewProj;
 (void)lastCullVP_;
 if (renderSettings_.frustumCulling) {
  RebuildFlatInstanceList(&frustum);
 } else {
  RebuildFlatInstanceList(nullptr);
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
 if (instancesDirty_) {
  RebuildFlatInstanceList(nullptr);
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
 std::vector<FaceInstance> chunkInstances;
 const Chunk* chunk = world.GetChunkManager().GetChunk(chunkCoord);
 if (!chunk) {
  cache_[chunkCoord] = std::move(chunkInstances);
  ++meshRevision_;
  instancesDirty_ = true;
  visibleListValid_ = false;
  return;
 }

 if (renderSettings_.greedyMeshing) {
  const auto quads = GreedyMesher::BuildChunkMesh(world, chunkCoord, registry);
  chunkInstances.reserve(quads.size());
  for (const GreedyQuad& q : quads) {
   chunkInstances.push_back(MakeFaceInstanceFromQuad(q, chunkCoord));
  }
 } else {
  RebuildChunkLegacy(world, registry, chunkCoord, chunkInstances);
 }

 cache_[chunkCoord] = std::move(chunkInstances);
 ++meshRevision_;
 instancesDirty_ = true;
 visibleListValid_ = false;
}

}
