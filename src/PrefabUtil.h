#pragma once

#include "BlockWorld.h"
#include "Prefab.h"
#include <glm/glm.hpp>

namespace cutum
{

bool CanPlacePrefabAt(const UBlockWorld &world, const Prefab &prefab,
                      glm::ivec3 anchorWorldPos);

struct PrefabPlacementStats
{
  int placedCount{0};
  int minY{0};
  int maxY{0};
};

PrefabPlacementStats PlacePrefabAt(UBlockWorld &world, const Prefab &prefab,
                                   glm::ivec3 anchorWorldPos,
                                   bool skipOccupied);

} // namespace cutum
