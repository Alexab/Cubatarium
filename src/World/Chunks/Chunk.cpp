#include "World/Chunks/Chunk.h"

namespace cutum
{

UChunk::UChunk(glm::ivec3 chunkCoord) : coord_(chunkCoord)
{
  data_.fill(BLOCK_AIR);
}

int UChunk::LocalIndex(glm::ivec3 local)
{
  return local.x + CHUNK_SIZE * local.y + CHUNK_SIZE * CHUNK_SIZE * local.z;
}

BlockId UChunk::GetBlockLocal(glm::ivec3 local) const
{
  if (local.x < 0 || local.x >= CHUNK_SIZE || local.y < 0 ||
      local.y >= CHUNK_SIZE || local.z < 0 || local.z >= CHUNK_SIZE)
  {
    return BLOCK_AIR;
  }
  return data_[static_cast<size_t>(LocalIndex(local))];
}

void UChunk::SetBlockLocal(glm::ivec3 local, BlockId id)
{
  if (local.x < 0 || local.x >= CHUNK_SIZE || local.y < 0 ||
      local.y >= CHUNK_SIZE || local.z < 0 || local.z >= CHUNK_SIZE)
  {
    return;
  }
  data_[static_cast<size_t>(LocalIndex(local))] = id;
  dirty_ = true;
}

} // namespace cutum
