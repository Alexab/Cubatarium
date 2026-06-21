#include "World/Chunks/ChunkBuffer.h"
#include "World/Core/BlockWorld.h"
#include <algorithm>

namespace cutum
{

BlockWorldWriter::BlockWorldWriter(UBlockWorld &world) : World(world) {}

void BlockWorldWriter::SetBlock(glm::ivec3 worldPos, BlockId id)
{
  World.SetBlock(worldPos, id);
}

BlockId BlockWorldWriter::GetBlock(glm::ivec3 worldPos) const
{
  return World.GetBlock(worldPos);
}

void ChunkBuffer::SetBlock(glm::ivec3 worldPos, BlockId id)
{
  if (id == BLOCK_AIR)
  {
    Blocks.erase(worldPos);
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

BlockId ChunkBuffer::GetBlock(glm::ivec3 worldPos) const
{
  const auto it = Blocks.find(worldPos);
  if (it == Blocks.end())
  {
    return BLOCK_AIR;
  }
  return it->second;
}

void ChunkBuffer::ApplyTo(UBlockWorld &world) const
{
  for (const auto &entry : Blocks)
  {
    world.SetBlock(entry.first, entry.second);
  }
}

void ChunkBuffer::Clear()
{
  Blocks.clear();
  HasBounds = false;
  MinY = 0;
  MaxY = -1;
}

} // namespace cutum
