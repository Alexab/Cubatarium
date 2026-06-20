#pragma once

#include "World/Core/BlockWorld.h"
#include "World/Prefabs/Prefab.h"
#include <glm/glm.hpp>

namespace cutum
{

bool CanPlacePrefabAt(const UBlockWorld &world, const Prefab &prefab,
                      glm::ivec3 anchorWorldPos);

/// Worldgen placement: blocks at or below \p surfaceY may replace terrain;
/// blocks above \p surfaceY must be air.
bool CanPlacePrefabAtForWorldGen(const UBlockWorld &world, const Prefab &prefab,
                                 glm::ivec3 anchorWorldPos, int surfaceY);

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
