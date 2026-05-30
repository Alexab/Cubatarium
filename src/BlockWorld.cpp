#include "BlockWorld.h"

namespace cutum {

BlockId BlockWorld::GetBlock(glm::ivec3 pos) const
{
 return chunks_.GetBlock(pos);
}

void BlockWorld::SetBlock(glm::ivec3 pos, BlockId id)
{
 if (id == BLOCK_AIR) {
  chunks_.SetBlock(pos, BLOCK_AIR);
  return;
 }
 chunks_.SetBlock(pos, id);
}

bool BlockWorld::IsAir(glm::ivec3 pos) const
{
 return GetBlock(pos) == BLOCK_AIR;
}

void BlockWorld::Clear()
{
 chunks_.Clear();
}

size_t BlockWorld::CountNonAir() const
{
 size_t count = 0;
 ForEachBlock([&count](glm::ivec3, BlockId) { ++count; });
 return count;
}

void BlockWorld::ForEachBlock(const std::function<void(glm::ivec3, BlockId)>& fn) const
{
 chunks_.ForEachBlock(fn);
}

}
