#include "ChunkStreamer.h"
#include "BlockWorld.h"
#include "BlockRegistry.h"
#include "Chunk.h"
#include <algorithm>
#include <cmath>

namespace cutum {

ChunkStreamer::ChunkStreamer(BlockWorld& world, BlockRegistry& registry, uint32_t seed,
    int baseY, int maxHeight)
 : world_(world)
 , registry_(registry)
 , seed_(seed)
 , baseY_(baseY)
 , maxHeight_(maxHeight)
{
}

void ChunkStreamer::SetWorldFolder(const std::string& path)
{
 worldFolder_ = path;
}

void ChunkStreamer::SetCallbacks(LoadChunkFn loadFn, SaveChunkFn saveFn, MarkDirtyFn markDirtyFn,
    GenerateColumnFn generateColumnFn, UnloadChunkFn unloadFn)
{
 loadChunkFn_ = std::move(loadFn);
 saveChunkFn_ = std::move(saveFn);
 markDirtyFn_ = std::move(markDirtyFn);
 generateColumnFn_ = std::move(generateColumnFn);
 unloadChunkFn_ = std::move(unloadFn);
}

void ChunkStreamer::EnsureChunkLoaded(glm::ivec3 chunkCoord)
{
 if (world_.GetChunkManager().GetChunk(chunkCoord) != nullptr) {
  return;
 }

 if (loadChunkFn_ && loadChunkFn_(chunkCoord)) {
  if (markDirtyFn_) {
   markDirtyFn_(chunkCoord);
  }
  return;
 }

 if (!generateColumnFn_) {
  return;
 }

 for (int lx = 0; lx < CHUNK_SIZE; ++lx) {
  for (int lz = 0; lz < CHUNK_SIZE; ++lz) {
   const int worldX = chunkCoord.x * CHUNK_SIZE + lx;
   const int worldZ = chunkCoord.z * CHUNK_SIZE + lz;
   generateColumnFn_(worldX, worldZ);
  }
 }

 if (markDirtyFn_) {
  markDirtyFn_(chunkCoord);
 }
 procedurallyGenerated_.insert(chunkCoord);
}

void ChunkStreamer::UnloadDistantChunks(glm::ivec3 centerChunk)
{
 std::vector<glm::ivec3> toUnload;
 const int limit = renderDistance_ + unloadMargin_;

 world_.GetChunkManager().ForEachChunk([&](const Chunk& chunk) {
  const glm::ivec3 coord = chunk.GetCoord();
  const int dx = std::abs(coord.x - centerChunk.x);
  const int dy = std::abs(coord.y - centerChunk.y);
  const int dz = std::abs(coord.z - centerChunk.z);
  const int dist = std::max({dx, dy, dz});
  if (dist > limit) {
   toUnload.push_back(coord);
  }
 });

 for (const glm::ivec3& coord : toUnload) {
  if (saveChunkFn_) {
   saveChunkFn_(coord);
  }
  world_.GetChunkManager().RemoveChunk(coord);
  procedurallyGenerated_.erase(coord);
  if (unloadChunkFn_) {
   unloadChunkFn_(coord);
  }
 }
}

void ChunkStreamer::Update(glm::ivec3 cameraBlockPos)
{
 if (!enabled_) {
  return;
 }

 const glm::ivec3 centerChunk = ChunkManager::WorldToChunk(cameraBlockPos);

 for (int cx = centerChunk.x - renderDistance_; cx <= centerChunk.x + renderDistance_; ++cx) {
  for (int cz = centerChunk.z - renderDistance_; cz <= centerChunk.z + renderDistance_; ++cz) {
   for (int cy = centerChunk.y - 1; cy <= centerChunk.y + 1; ++cy) {
    EnsureChunkLoaded(glm::ivec3(cx, cy, cz));
   }
  }
 }

 UnloadDistantChunks(centerChunk);
}

}
