#include "World/Chunks/Chunk.h"

#include <cassert>

namespace cutum
{

UChunk::UChunk(glm::ivec3 chunkCoord) : Coord(chunkCoord)
{
  Data.fill(BLOCK_AIR);
  FluidData.fill(0);
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
  return Data[static_cast<size_t>(LocalIndex(local))];
}

void UChunk::SetBlockLocal(glm::ivec3 local, BlockId Id)
{
  if (local.x < 0 || local.x >= CHUNK_SIZE || local.y < 0 ||
      local.y >= CHUNK_SIZE || local.z < 0 || local.z >= CHUNK_SIZE)
  {
    return;
  }
  Data[static_cast<size_t>(LocalIndex(local))] = Id;
  if (Id == BLOCK_AIR)
  {
    ClearFluidLocal(local);
  }
  Dirty = true;
}

FluidCellState UChunk::GetFluidLocal(glm::ivec3 local) const
{
  if (local.x < 0 || local.x >= CHUNK_SIZE || local.y < 0 ||
      local.y >= CHUNK_SIZE || local.z < 0 || local.z >= CHUNK_SIZE)
  {
    return {};
  }
  return UnpackFluidCellState(
      FluidData[static_cast<size_t>(LocalIndex(local))]);
}

void UChunk::SetFluidLocal(glm::ivec3 local, FluidCellState state)
{
  if (local.x < 0 || local.x >= CHUNK_SIZE || local.y < 0 ||
      local.y >= CHUNK_SIZE || local.z < 0 || local.z >= CHUNK_SIZE)
  {
    return;
  }
#ifndef NDEBUG
  assert(GetBlockLocal(local) != BLOCK_AIR);
#endif
  FluidData[static_cast<size_t>(LocalIndex(local))] =
      PackFluidCellState(state);
  Dirty = true;
}

void UChunk::ClearFluidLocal(glm::ivec3 local)
{
  if (local.x < 0 || local.x >= CHUNK_SIZE || local.y < 0 ||
      local.y >= CHUNK_SIZE || local.z < 0 || local.z >= CHUNK_SIZE)
  {
    return;
  }
  FluidData[static_cast<size_t>(LocalIndex(local))] = 0;
  Dirty = true;
}

} // namespace cutum
