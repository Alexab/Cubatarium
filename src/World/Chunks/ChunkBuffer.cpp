#include "World/Chunks/ChunkBuffer.h"
#include "World/Core/BlockWorld.h"
#include "World/Math/FluidCellState.h"
#include <algorithm>

namespace cutum
{

UBlockWorldWriter::UBlockWorldWriter(UBlockWorld &world) : World(world) {}

void UBlockWorldWriter::SetBlock(glm::ivec3 worldPos, BlockId id)
{
  World.SetBlock(worldPos, id);
}

BlockId UBlockWorldWriter::GetBlock(glm::ivec3 worldPos) const
{
  return World.GetBlock(worldPos);
}

void UChunkBuffer::SetBlock(glm::ivec3 worldPos, BlockId id)
{
  if (id == BLOCK_AIR)
  {
    Blocks.erase(worldPos);
    FluidPacked.erase(worldPos);
    return;
  }
  Blocks[worldPos] = id;
  if (!HasBounds)
  {
    HasBounds = true;
    MinY = worldPos.y;
    MaxY = worldPos.y;
    return;
  }
  MinY = std::min(MinY, worldPos.y);
  MaxY = std::max(MaxY, worldPos.y);
}

void UChunkBuffer::SetFluidPacked(glm::ivec3 worldPos, uint8_t packed)
{
  if (packed == 0)
  {
    FluidPacked.erase(worldPos);
    return;
  }
  FluidPacked[worldPos] = packed;
}

BlockId UChunkBuffer::GetBlock(glm::ivec3 worldPos) const
{
  const auto it = Blocks.find(worldPos);
  if (it == Blocks.end())
  {
    return BLOCK_AIR;
  }
  return it->second;
}

uint8_t UChunkBuffer::GetFluidPacked(glm::ivec3 worldPos) const
{
  const auto it = FluidPacked.find(worldPos);
  if (it == FluidPacked.end())
  {
    return 0;
  }
  return it->second;
}

void UChunkBuffer::ApplyTo(UBlockWorld &world) const
{
  for (const auto &entry : Blocks)
  {
    world.SetBlock(entry.first, entry.second);
    const auto fluid_it = FluidPacked.find(entry.first);
    if (fluid_it != FluidPacked.end() && fluid_it->second != 0)
    {
      world.SetFluidState(entry.first,
                         UnpackFluidCellState(fluid_it->second));
    }
  }
}

void UChunkBuffer::Clear()
{
  Blocks.clear();
  FluidPacked.clear();
  HasBounds = false;
  MinY = 0;
  MaxY = -1;
}

} // namespace cutum
