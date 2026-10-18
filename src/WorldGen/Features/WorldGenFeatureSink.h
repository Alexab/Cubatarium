#pragma once

#include "World/Math/BlockTypes.h"
#include "World/Core/BlockWorld.h"
#include <glm/glm.hpp>

namespace cutum
{

/// Representation boundary for an already-selected feature placement.
/// Alternate mesh/instance sinks can be added without changing selection or
/// species geometry, while current worldgen continues to emit block voxels.
class IWorldGenFeatureSink
{
public:
  virtual ~IWorldGenFeatureSink() = default;
  virtual void WriteVoxel(glm::ivec3 worldPosition, BlockId block) = 0;
};

class VoxelFeatureSink final : public IWorldGenFeatureSink
{
public:
  explicit VoxelFeatureSink(UBlockWorld &world) : World(world) {}
  void WriteVoxel(glm::ivec3 worldPosition, BlockId block) override
  {
    World.SetBlock(worldPosition, block);
  }

private:
  UBlockWorld &World;
};

} // namespace cutum
