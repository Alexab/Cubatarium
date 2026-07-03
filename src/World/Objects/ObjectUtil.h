#pragma once

#include "World/Core/BlockWorld.h"
#include "World/Objects/ObjectLibrary.h"
#include <glm/glm.hpp>

namespace cutum
{

class UBlockRegistry;

bool CanPlaceObjectAt(const UBlockWorld &world,
                      const WorldObjectDefinition &object,
                      glm::ivec3 anchorWorldPos);

bool CanPlaceObjectAtForWorldGen(const UBlockWorld &world,
                                 UBlockRegistry &registry,
                                 const WorldObjectDefinition &object,
                                 glm::ivec3 anchorWorldPos, int maxScanY,
                                 int seaLevel, int anchorSurfaceY);

bool IsSolidPlantGround(const UBlockWorld &world, UBlockRegistry &registry,
                        glm::ivec3 groundPos);
bool CanPlacePlantAt(const UBlockWorld &world, UBlockRegistry &registry,
                     glm::ivec3 worldPos);
bool IsExposedLandSurface(const UBlockWorld &world, UBlockRegistry &registry,
                          int x, int z, int surfaceY);
int FindTopSolidSurfaceY(const UBlockWorld &world, UBlockRegistry &registry,
                         int x, int z, int maxY);

struct ObjectPlacementStats
{
  int placedCount{0};
  int minY{0};
  int maxY{0};
};

ObjectPlacementStats PlaceObjectAt(UBlockWorld &world,
                                   const WorldObjectDefinition &object,
                                   glm::ivec3 anchorWorldPos,
                                   bool skipOccupied);

std::vector<glm::ivec3> BreakUnsupportedBlocksAbove(UBlockWorld &world,
                                                      UBlockRegistry &registry,
                                                      glm::ivec3 groundPos,
                                                      int maxY = 320);

} // namespace cutum
