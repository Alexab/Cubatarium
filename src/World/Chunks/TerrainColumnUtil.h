#pragma once

#include "World/Chunks/ChunkManager.h"
#include <glm/glm.hpp>

namespace cutum
{

class UBlockWorld;

bool TerrainColumnNeedsFill(const UBlockWorld &world, int worldX, int worldZ,
                            int maxWorldY);
int GetHighestNonAirChunkSlice(const UBlockWorld &world, glm::ivec3 groundCoord,
                               int maxWorldY);
int GetRequiredTerrainColumnTopCy(const UBlockWorld &world, glm::ivec3 groundCoord,
                                  int maxWorldY, int highestCyOnDisk = -1);
void MaterializeTerrainColumnSliceRange(UBlockWorld &world, glm::ivec3 groundCoord,
                                      int fromCy, int toCy);
void MaterializeRequiredTerrainColumnSlices(UBlockWorld &world,
                                            glm::ivec3 groundCoord,
                                            int maxWorldY, int highestCyOnDisk = -1);
void MaterializeTerrainColumnAirSlices(UBlockWorld &world, glm::ivec3 groundCoord,
                                     int maxWorldY);
bool AreTerrainColumnSlicesLoaded(const UBlockWorld &world,
                                  glm::ivec3 groundCoord, int maxWorldY,
                                  int minimumSliceCy = -1);
bool IsTerrainChunkComplete(const UBlockWorld &world, glm::ivec3 groundCoord,
                            int maxWorldY, int minimumSliceCy = -1);
void ClearTerrainColumnChunks(UBlockWorld &world, glm::ivec3 groundCoord,
                              int maxWorldY);

} // namespace cutum
