#ifndef IUCHUNKMESHREADER_H
#define IUCHUNKMESHREADER_H

#include "World/Math/BlockTypes.h"
#include <glm/glm.hpp>

namespace cutum
{

/// Read-only voxel view for greedy chunk meshing (local chunk + neighbor shell).
class IUChunkMeshReader
{
public:
  virtual ~IUChunkMeshReader() = default;

  virtual glm::ivec3 ChunkCoord() const = 0;
  virtual BlockId GetBlockLocal(glm::ivec3 local) const = 0;
  virtual BlockId GetBlock(glm::ivec3 world_pos) const = 0;
};

} // namespace cutum

#endif
