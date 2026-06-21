#pragma once

#include "World/Core/BlockWorld.h"
#include "World/Prefabs/Prefab.h"
#include <glm/glm.hpp>

namespace cutum
{

class UBlockRegistry;

bool CanPlacePrefabAt(const UBlockWorld &world, const Prefab &prefab,
                      glm::ivec3 anchorWorldPos);

/// Worldgen prefabs: per-voxel local surface; plants above ground or replace top
/// surface block; never place high above a distant column's surface.
bool CanPlacePrefabAtForWorldGen(const UBlockWorld &world,
                                 UBlockRegistry &registry, const Prefab &prefab,
                                 glm::ivec3 anchorWorldPos, int maxScanY);

bool IsSolidPlantGround(const UBlockWorld &world, UBlockRegistry &registry,
                        glm::ivec3 groundPos);
bool CanPlacePlantAt(const UBlockWorld &world, UBlockRegistry &registry,
                     glm::ivec3 worldPos);
bool IsExposedLandSurface(const UBlockWorld &world, UBlockRegistry &registry,
                          int x, int z, int surfaceY);
int FindTopSolidSurfaceY(const UBlockWorld &world, UBlockRegistry &registry,
                         int x, int z, int maxY);

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
