#include "BlockWorld.h"

namespace cutum {

BlockId UBlockWorld::GetBlock(glm::ivec3 pos) const
{
 return Chunks.GetBlock(pos);
}

void UBlockWorld::SetBlock(glm::ivec3 pos, BlockId id)
{
 if (id == BLOCK_AIR) {
  Chunks.SetBlock(pos, BLOCK_AIR);
  return;
 }
 Chunks.SetBlock(pos, id);
}

bool UBlockWorld::IsAir(glm::ivec3 pos) const
{
 return GetBlock(pos) == BLOCK_AIR;
}

void UBlockWorld::Clear()
{
 Chunks.Clear();
}

size_t UBlockWorld::CountNonAir() const
{
 size_t count = 0;
 ForEachBlock([&count](glm::ivec3, BlockId) { ++count; });
 return count;
}

void UBlockWorld::ForEachBlock(const std::function<void(glm::ivec3, BlockId)>& fn) const
{
 Chunks.ForEachBlock(fn);
}

}
