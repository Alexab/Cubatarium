#pragma once

#include "World/Math/BlockTypes.h"
#include <glm/glm.hpp>
#include <vector>

namespace cutum
{

class UWorld;

class UWorldFluidFacade
{
public:
  static bool TryAddFluidObject(UWorld &world, glm::ivec3 block_pos,
                                BlockId liquid_id);
  static void
  ApplyBreakSiteFluidFlood(UWorld &world, glm::ivec3 block_pos,
                           std::vector<glm::ivec3> &mesh_touch_blocks);
  static void MarkFluidRegionDirty(UWorld &world, glm::ivec3 center,
                                   int block_radius);
  static void
  MarkFluidFloodMeshDirty(UWorld &world, glm::ivec3 block_pos,
                          const std::vector<glm::ivec3> &filled_blocks);
};

} // namespace cutum
