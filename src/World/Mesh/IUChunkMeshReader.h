#ifndef IUCHUNKMESHREADER_H
#define IUCHUNKMESHREADER_H

#include "World/Math/BlockTypes.h"
#include "World/Math/FluidCellState.h"
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
  virtual uint8_t GetLightPackedLocal(glm::ivec3 local) const
  {
    (void)local;
    return 0;
  }
  virtual uint8_t GetLightPacked(glm::ivec3 world_pos) const
  {
    (void)world_pos;
    return 0;
  }
  virtual uint8_t GetFluidPackedLocal(glm::ivec3 local) const { return 0; }
  virtual FluidCellState GetFluidLocal(glm::ivec3 local) const
  {
    return UnpackFluidCellState(GetFluidPackedLocal(local));
  }
  virtual FluidCellState GetFluid(glm::ivec3 world_pos) const
  {
    (void)world_pos;
    return {};
  }
};

} // namespace cutum

#endif
