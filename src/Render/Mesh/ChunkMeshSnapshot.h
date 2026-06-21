#pragma once

#include "World/Chunks/Chunk.h"
#include "World/Math/BlockTypes.h"
#include <array>
#include <glm/glm.hpp>
#include <unordered_map>

namespace cutum
{

class UBlockWorld;

/// Read-only voxel view for background meshing (center chunk + one-block shell).
struct ChunkMeshSnapshot
{
  glm::ivec3 coord{0};
  std::array<BlockId, CHUNK_VOLUME> blocks{};
  std::unordered_map<glm::ivec3, BlockId, IVec3Hash> shellBlocks;
  uint64_t sourceRevision{0};

  static ChunkMeshSnapshot Capture(const UBlockWorld &world, glm::ivec3 chunkCoord,
                                   uint64_t sourceRevision);

  BlockId GetBlock(glm::ivec3 worldPos) const;
  BlockId GetBlockLocal(glm::ivec3 local) const;
  glm::ivec3 ChunkOrigin() const;
};

} // namespace cutum
