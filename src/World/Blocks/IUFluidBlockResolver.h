#pragma once

#include "Render/Mesh/IUChunkMeshReader.h"
#include "World/Math/BlockTypes.h"
#include "World/Math/FluidCellState.h"
#include <glm/glm.hpp>

namespace cutum
{

class UBlockWorld;
class UBlockDefinitionStorage;

class IUFluidBlockResolver
{
public:
  virtual ~IUFluidBlockResolver() = default;

  virtual BlockId ResolveFluidBlockId(const UBlockWorld &block_world,
                                      glm::ivec3 block_pos) const = 0;
  virtual BlockId ResolveFluidBlockIdForMesh(const IUChunkMeshReader &reader,
                                             glm::ivec3 block_pos) const = 0;
  virtual FluidKind ResolveFluidKind(const UBlockWorld &block_world,
                                     glm::ivec3 block_pos,
                                     BlockId block_id) const = 0;
};

} // namespace cutum
