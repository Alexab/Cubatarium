#include "ChunkManager.h"
#include "GridMath.h"

namespace cutum {

glm::ivec3 ChunkManager::WorldToChunk(glm::ivec3 worldPos)
{
 return glm::ivec3(
     FloorDiv(worldPos.x, CHUNK_SIZE),
     FloorDiv(worldPos.y, CHUNK_SIZE),
     FloorDiv(worldPos.z, CHUNK_SIZE));
}

glm::ivec3 ChunkManager::WorldToLocal(glm::ivec3 worldPos)
{
 return glm::ivec3(
     PositiveMod(worldPos.x, CHUNK_SIZE),
     PositiveMod(worldPos.y, CHUNK_SIZE),
     PositiveMod(worldPos.z, CHUNK_SIZE));
}

BlockId ChunkManager::GetBlock(glm::ivec3 worldPos) const
{
 const glm::ivec3 chunkCoord = WorldToChunk(worldPos);
 auto it = chunks_.find(chunkCoord);
 if (it == chunks_.end()) {
  return BLOCK_AIR;
 }
 return it->second->GetBlockLocal(WorldToLocal(worldPos));
}

void ChunkManager::SetBlock(glm::ivec3 worldPos, BlockId id)
{
 Chunk& chunk = GetOrCreateChunk(WorldToChunk(worldPos));
 chunk.SetBlockLocal(WorldToLocal(worldPos), id);
}

void ChunkManager::Clear()
{
 chunks_.clear();
}

void ChunkManager::ForEachBlock(const std::function<void(glm::ivec3, BlockId)>& fn) const
{
 for (const auto& entry : chunks_) {
  const glm::ivec3 chunkCoord = entry.first;
  const Chunk& chunk = *entry.second;
  for (int z = 0; z < CHUNK_SIZE; ++z) {
   for (int y = 0; y < CHUNK_SIZE; ++y) {
    for (int x = 0; x < CHUNK_SIZE; ++x) {
     const glm::ivec3 local(x, y, z);
     const BlockId id = chunk.GetBlockLocal(local);
     if (id == BLOCK_AIR) {
      continue;
     }
     const glm::ivec3 worldPos(
         chunkCoord.x * CHUNK_SIZE + x,
         chunkCoord.y * CHUNK_SIZE + y,
         chunkCoord.z * CHUNK_SIZE + z);
     fn(worldPos, id);
    }
   }
  }
 }
}

Chunk* ChunkManager::GetChunk(glm::ivec3 chunkCoord)
{
 auto it = chunks_.find(chunkCoord);
 if (it == chunks_.end()) {
  return nullptr;
 }
 return it->second.get();
}

const Chunk* ChunkManager::GetChunk(glm::ivec3 chunkCoord) const
{
 auto it = chunks_.find(chunkCoord);
 if (it == chunks_.end()) {
  return nullptr;
 }
 return it->second.get();
}

void ChunkManager::ForEachChunk(const std::function<void(const Chunk&)>& fn) const
{
 for (const auto& entry : chunks_) {
  fn(*entry.second);
 }
}

Chunk& ChunkManager::GetOrCreateChunk(glm::ivec3 chunkCoord)
{
 auto it = chunks_.find(chunkCoord);
 if (it != chunks_.end()) {
  return *it->second;
 }
 auto chunk = std::make_unique<Chunk>(chunkCoord);
 Chunk& ref = *chunk;
 chunks_.emplace(chunkCoord, std::move(chunk));
 return ref;
}

}
