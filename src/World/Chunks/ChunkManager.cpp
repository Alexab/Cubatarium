#include "World/Chunks/ChunkManager.h"
#include "World/Math/GridMath.h"

namespace cutum
{

glm::ivec3 UChunkManager::WorldToChunk(glm::ivec3 worldPos)
{
  return glm::ivec3(FloorDiv(worldPos.x, CHUNK_SIZE),
                    FloorDiv(worldPos.y, CHUNK_SIZE),
                    FloorDiv(worldPos.z, CHUNK_SIZE));
}

glm::ivec3 UChunkManager::WorldToLocal(glm::ivec3 worldPos)
{
  return glm::ivec3(PositiveMod(worldPos.x, CHUNK_SIZE),
                    PositiveMod(worldPos.y, CHUNK_SIZE),
                    PositiveMod(worldPos.z, CHUNK_SIZE));
}

BlockId UChunkManager::GetBlock(glm::ivec3 worldPos) const
{
  const glm::ivec3 chunkCoord = WorldToChunk(worldPos);
  auto it = Chunks.find(chunkCoord);
  if (it == Chunks.end())
  {
    return BLOCK_AIR;
  }
  return it->second->GetBlockLocal(WorldToLocal(worldPos));
}

void UChunkManager::SetBlock(glm::ivec3 worldPos, BlockId Id)
{
  UChunk &chunk = GetOrCreateChunk(WorldToChunk(worldPos));
  chunk.SetBlockLocal(WorldToLocal(worldPos), Id);
}

void UChunkManager::Clear() { Chunks.clear(); }

void UChunkManager::ForEachBlock(
    const std::function<void(glm::ivec3, BlockId)> &fn) const
{
  for (const auto &entry : Chunks)
  {
    const glm::ivec3 chunkCoord = entry.first;
    const UChunk &chunk = *entry.second;
    for (int z = 0; z < CHUNK_SIZE; ++z)
    {
      for (int y = 0; y < CHUNK_SIZE; ++y)
      {
        for (int x = 0; x < CHUNK_SIZE; ++x)
        {
          const glm::ivec3 local(x, y, z);
          const BlockId Id = chunk.GetBlockLocal(local);
          if (Id == BLOCK_AIR)
          {
            continue;
          }
          const glm::ivec3 worldPos(chunkCoord.x * CHUNK_SIZE + x,
                                    chunkCoord.y * CHUNK_SIZE + y,
                                    chunkCoord.z * CHUNK_SIZE + z);
          fn(worldPos, Id);
        }
      }
    }
  }
}

UChunk *UChunkManager::GetChunk(glm::ivec3 chunkCoord)
{
  auto it = Chunks.find(chunkCoord);
  if (it == Chunks.end())
  {
    return nullptr;
  }
  return it->second.get();
}

const UChunk *UChunkManager::GetChunk(glm::ivec3 chunkCoord) const
{
  auto it = Chunks.find(chunkCoord);
  if (it == Chunks.end())
  {
    return nullptr;
  }
  return it->second.get();
}

bool UChunkManager::HasChunk(glm::ivec3 chunkCoord) const
{
  return Chunks.find(chunkCoord) != Chunks.end();
}

void UChunkManager::ForEachChunk(
    const std::function<void(const UChunk &)> &fn) const
{
  for (const auto &entry : Chunks)
  {
    fn(*entry.second);
  }
}

void UChunkManager::RemoveChunk(glm::ivec3 chunkCoord)
{
  Chunks.erase(chunkCoord);
}

UChunk &UChunkManager::GetOrCreateChunk(glm::ivec3 chunkCoord)
{
  auto it = Chunks.find(chunkCoord);
  if (it != Chunks.end())
  {
    return *it->second;
  }
  auto chunk = std::make_unique<UChunk>(chunkCoord);
  UChunk &ref = *chunk;
  Chunks.emplace(chunkCoord, std::move(chunk));
  return ref;
}

} // namespace cutum
