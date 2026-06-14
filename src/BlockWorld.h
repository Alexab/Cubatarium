#ifndef BLOCKWORLD_H
#define BLOCKWORLD_H

#include "BlockTypes.h"
#include "ChunkManager.h"
#include <functional>
#include <glm/glm.hpp>

namespace cutum
{

class UBlockWorld
{
public:
  BlockId GetBlock(glm::ivec3 pos) const;
  void SetBlock(glm::ivec3 pos, BlockId id);
  bool IsAir(glm::ivec3 pos) const;
  void Clear();
  size_t CountNonAir() const;
  void ForEachBlock(const std::function<void(glm::ivec3, BlockId)> &fn) const;

  UChunkManager &GetChunkManager() { return Chunks; }
  const UChunkManager &GetChunkManager() const { return Chunks; }

private:
  UChunkManager Chunks;
};

} // namespace cutum

#endif
