#ifndef CHUNK_H
#define CHUNK_H

#include "World/Math/BlockTypes.h"
#include <array>
#include <glm/glm.hpp>

namespace cutum
{

constexpr int CHUNK_SIZE = 16;
constexpr int CHUNK_VOLUME = CHUNK_SIZE * CHUNK_SIZE * CHUNK_SIZE;

class UChunk
{
public:
  explicit UChunk(glm::ivec3 chunkCoord);

  glm::ivec3 GetCoord() const { return Coord; }
  BlockId GetBlockLocal(glm::ivec3 local) const;
  void SetBlockLocal(glm::ivec3 local, BlockId Id);
  bool IsDirty() const { return Dirty; }
  void ClearDirty() { Dirty = false; }
  void MarkDirty() { Dirty = true; }

  const std::array<BlockId, CHUNK_VOLUME> &GetData() const { return Data; }

  static int LocalIndex(glm::ivec3 local);

private:
  glm::ivec3 Coord;
  std::array<BlockId, CHUNK_VOLUME> Data{};
  bool Dirty{true};
};

} // namespace cutum

#endif
