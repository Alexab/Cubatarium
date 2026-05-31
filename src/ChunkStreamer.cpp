#include "ChunkStreamer.h"
#include "BlockWorld.h"
#include "BlockRegistry.h"
#include "Chunk.h"
#include "GridMath.h"
#include <algorithm>
#include <cmath>

namespace cutum {

namespace {

bool ChunkAabbIntersectsPlayer(glm::ivec3 chunkCoord, const glm::vec3& playerPos, float playerSize)
{
 const float half = playerSize * 0.5f;
 const float px0 = playerPos.x - half;
 const float px1 = playerPos.x + half;
 const float py0 = playerPos.y - half;
 const float py1 = playerPos.y + half;
 const float pz0 = playerPos.z - half;
 const float pz1 = playerPos.z + half;

 const float cx0 = static_cast<float>(chunkCoord.x * CHUNK_SIZE) - 0.5f;
 const float cx1 = static_cast<float>((chunkCoord.x + 1) * CHUNK_SIZE) - 0.5f;
 const float cy0 = static_cast<float>(chunkCoord.y * CHUNK_SIZE) - 0.5f;
 const float cy1 = static_cast<float>((chunkCoord.y + 1) * CHUNK_SIZE) - 0.5f;
 const float cz0 = static_cast<float>(chunkCoord.z * CHUNK_SIZE) - 0.5f;
 const float cz1 = static_cast<float>((chunkCoord.z + 1) * CHUNK_SIZE) - 0.5f;

 return px0 <= cx1 && px1 >= cx0 &&
        py0 <= cy1 && py1 >= cy0 &&
        pz0 <= cz1 && pz1 >= cz0;
}

} // namespace

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

bool ChunkStreamer::EnsureChunkLoaded(glm::ivec3 chunkCoord)
{
 const Chunk* existing = world_.GetChunkManager().GetChunk(chunkCoord);
 if (existing != nullptr) {
  return false;
 }

 if (loadChunkFn_ && loadChunkFn_(chunkCoord)) {
  if (markDirtyFn_) {
   markDirtyFn_(chunkCoord);
  }
  return true;
 }

 if (!generateColumnFn_) {
  return false;
 }

 // Terrain columns are generated at world Y=0..surface; only fill ground layer chunks.
 if (chunkCoord.y != 0) {
  return false;
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
 return true;
}

bool ChunkStreamer::ShouldKeepChunkLoaded(glm::ivec3 chunkCoord, glm::ivec3 feetBlockPos,
    const glm::vec3& playerWorldPos, float playerSize) const
{
 const glm::ivec3 feetChunk = ChunkManager::WorldToChunk(feetBlockPos);

 for (int dx = -1; dx <= 1; ++dx) {
  for (int dy = -1; dy <= 1; ++dy) {
   for (int dz = -1; dz <= 1; ++dz) {
    if (chunkCoord == feetChunk + glm::ivec3(dx, dy, dz)) {
     return true;
    }
   }
  }
 }

 const glm::ivec3 playerChunk = ChunkManager::WorldToChunk(feetBlockPos);
 const int dx = std::abs(chunkCoord.x - playerChunk.x);
 const int dy = std::abs(chunkCoord.y - playerChunk.y);
 const int dz = std::abs(chunkCoord.z - playerChunk.z);
 const int distFromPlayer = std::max({dx, dy, dz});
 if (distFromPlayer <= renderDistance_ + unloadMargin_) {
  return true;
 }

 if (ChunkAabbIntersectsPlayer(chunkCoord, playerWorldPos, playerSize)) {
  return true;
 }

 return false;
}

void ChunkStreamer::EnsureCollisionChunks(glm::ivec3 feetBlockPos)
{
 if (!enabled_) {
  return;
 }

 const glm::ivec3 feetChunk = ChunkManager::WorldToChunk(feetBlockPos);
 for (int dx = -1; dx <= 1; ++dx) {
  for (int dz = -1; dz <= 1; ++dz) {
   for (int dy = -1; dy <= 1; ++dy) {
    const glm::ivec3 coord = feetChunk + glm::ivec3(dx, dy, dz);
    EnsureChunkLoaded(coord);
   }
  }
 }
}

void ChunkStreamer::UnloadDistantChunks(glm::ivec3 centerChunk, glm::ivec3 feetBlockPos,
    const glm::vec3& playerWorldPos, float playerSize)
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

 int unloadOps = 0;
 for (const glm::ivec3& coord : toUnload) {
  if (unloadOps >= maxUnloadOpsPerFrame_) {
   break;
  }
  if (ShouldKeepChunkLoaded(coord, feetBlockPos, playerWorldPos, playerSize)) {
   continue;
  }
  if (saveChunkFn_) {
   saveChunkFn_(coord);
   ++lastFrameStats_.savesThisFrame;
  }
  world_.GetChunkManager().RemoveChunk(coord);
  procedurallyGenerated_.erase(coord);
  if (unloadChunkFn_) {
   unloadChunkFn_(coord);
  }
  lastFrameStats_.unloadedCoords.push_back(coord);
  ++lastFrameStats_.unloadsThisFrame;
  ++unloadOps;
 }
}

void ChunkStreamer::Update(glm::ivec3 cameraBlockPos, const glm::vec3& playerWorldPos, float playerSize)
{
 if (!enabled_) {
  return;
 }

 lastFrameStats_.Reset();

 const glm::ivec3 centerChunk = ChunkManager::WorldToChunk(cameraBlockPos);
 const glm::ivec3 feetBlockPos = WorldPosToBlock(
     playerWorldPos - glm::vec3(0.0f, playerSize * 0.5f + 0.25f, 0.0f));

 int loadOps = 0;
 for (int cx = centerChunk.x - renderDistance_; cx <= centerChunk.x + renderDistance_; ++cx) {
  for (int cz = centerChunk.z - renderDistance_; cz <= centerChunk.z + renderDistance_; ++cz) {
   for (int cy = centerChunk.y - 1; cy <= centerChunk.y + 1; ++cy) {
    if (loadOps >= maxLoadOpsPerFrame_) {
     goto finish_loads;
    }
    const glm::ivec3 coord(cx, cy, cz);
    if (world_.GetChunkManager().HasChunk(coord)) {
     continue;
    }
    if (EnsureChunkLoaded(coord)) {
     lastFrameStats_.loadedCoords.push_back(coord);
     ++lastFrameStats_.loadsThisFrame;
     ++loadOps;
    }
   }
  }
 }
finish_loads:

 UnloadDistantChunks(centerChunk, feetBlockPos, playerWorldPos, playerSize);
}

}
