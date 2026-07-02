#pragma once

#include "World/Chunks/Chunk.h"
#include "World/Chunks/ChunkManager.h"
#include "World/Math/BlockTypes.h"
#include "World/Math/FluidCellState.h"
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
  std::array<uint8_t, CHUNK_VOLUME> fluid_packed{};
  std::unordered_map<glm::ivec3, BlockId, IVec3Hash> shellBlocks;
  std::unordered_map<glm::ivec3, uint8_t, IVec3Hash> shellFluid;
  uint64_t sourceRevision{0};

  static ChunkMeshSnapshot Capture(const UBlockWorld &world, glm::ivec3 chunkCoord,
                                   uint64_t sourceRevision);

  BlockId GetBlock(glm::ivec3 worldPos) const;
  BlockId GetBlockLocal(glm::ivec3 local) const;
  uint8_t GetFluidPackedLocal(glm::ivec3 local) const;
  uint8_t GetFluidPacked(glm::ivec3 worldPos) const;
  FluidCellState GetFluidLocal(glm::ivec3 local) const;
  FluidCellState GetFluid(glm::ivec3 worldPos) const;
  glm::ivec3 ChunkOrigin() const;
};

} // namespace cutum
