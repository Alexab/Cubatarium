#pragma once

#include "World/Core/BlockWorld.h"
#include "World/Objects/ObjectLibrary.h"
#include <functional>
#include <glm/glm.hpp>
#include <optional>

namespace cutum
{

class UBlockRegistry;

bool CanPlaceObjectAt(const UBlockWorld &world,
                      const WorldObjectDefinition &object,
                      glm::ivec3 anchorWorldPos);

bool IsSurfaceLayerPrefab(const WorldObjectDefinition &object,
                          UBlockRegistry &registry);

int ResolveWorldGenAnchorY(const WorldObjectDefinition &object,
                           UBlockRegistry &registry, int topSolid,
                           int yOffset);

bool CanPlaceObjectAtForWorldGen(const UBlockWorld &world,
                                 UBlockRegistry &registry,
                                 const WorldObjectDefinition &object,
                                 glm::ivec3 anchorWorldPos, int maxScanY,
                                 int seaLevel);

bool HasOpenSurfaceAbove(const UBlockWorld &world, UBlockRegistry &registry,
                         int x, int y, int z, int maxY);

bool IsSolidPlantGround(const UBlockWorld &world, UBlockRegistry &registry,
                        glm::ivec3 groundPos);
bool CanPlacePlantAt(const UBlockWorld &world, UBlockRegistry &registry,
                     glm::ivec3 worldPos);
bool IsExposedLandSurface(const UBlockWorld &world, UBlockRegistry &registry,
                          int x, int z, int surfaceY);
int FindTopSolidSurfaceY(const UBlockWorld &world, UBlockRegistry &registry,
                         int x, int z, int maxY, int minY = 0);
std::optional<int> FindTopSolidSurfaceYOptional(const UBlockWorld &world,
                                                UBlockRegistry &registry,
                                                int x, int z, int maxY,
                                                int minY = 0);

struct PlacementSurfaceInfo
{
  int topSolidY{-1};
  int maxScanY{0};
};

PlacementSurfaceInfo ResolvePlacementSurfaceY(const UBlockWorld &world,
                                              UBlockRegistry &registry, int x,
                                              int z, int heightmapSurfaceY,
                                              int maxHeight, int seaLevel,
                                              int minHeight = 0);

bool HasBlockSupportBelow(const UBlockWorld &world, UBlockRegistry &registry,
                          glm::ivec3 pos);

BlockId ResolveObjectVoxelPlacementId(const ObjectVoxel &voxel,
                                      const UBlockRegistry &registry);

bool IsRemovableFloatingPlantBlock(const UBlockRegistry &registry, BlockId id);

int PruneFloatingPlantsInColumn(UBlockWorld &world, UBlockRegistry &registry,
                                int x, int z, int maxY);

struct ObjectPlacementStats
{
  int placedCount{0};
  int minY{0};
  int maxY{0};
};

struct ObjectWorldGenPlacementOptions
{
  int maxScanY{-1};
  int seaLevel{-1};
};

using ObjectVoxelWriter = std::function<void(glm::ivec3, BlockId)>;

ObjectPlacementStats PlaceObjectAt(UBlockWorld &world,
                                   UBlockRegistry &registry,
                                   const WorldObjectDefinition &object,
                                   glm::ivec3 anchorWorldPos,
                                   bool skipOccupied,
                                   const ObjectWorldGenPlacementOptions &options =
                                       ObjectWorldGenPlacementOptions{},
                                   ObjectVoxelWriter writeVoxel = {});

std::vector<glm::ivec3> BreakUnsupportedBlocksAbove(UBlockWorld &world,
                                                      UBlockRegistry &registry,
                                                      glm::ivec3 groundPos,
                                                      int maxY = 320);

} // namespace cutum
