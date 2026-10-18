#pragma once

#include "World/Chunks/ChunkManager.h"
#include "World/Math/GridMath.h"
#include <glm/glm.hpp>

namespace cutum
{

class UBlockWorld;

inline constexpr int kNoTerrainChunkSlice = kNoChunkSlice;

bool TerrainColumnNeedsFill(const UBlockWorld &world, int worldX, int worldZ,
                            int maxWorldY, int minWorldY = 0);
int GetHighestNonAirChunkSlice(const UBlockWorld &world, glm::ivec3 groundCoord,
                               int maxWorldY, int minWorldY = 0);
int GetRequiredTerrainColumnTopCy(const UBlockWorld &world, glm::ivec3 groundCoord,
                                  int maxWorldY,
                                  int highestCyOnDisk = kNoTerrainChunkSlice,
                                  int minWorldY = 0);
void MaterializeTerrainColumnSliceRange(UBlockWorld &world, glm::ivec3 groundCoord,
                                      int fromCy, int toCy);
void MaterializeRequiredTerrainColumnSlices(UBlockWorld &world,
                                            glm::ivec3 groundCoord,
                                            int maxWorldY,
                                            int highestCyOnDisk = kNoTerrainChunkSlice,
                                            int minWorldY = 0);
void MaterializeTerrainColumnAirSlices(UBlockWorld &world, glm::ivec3 groundCoord,
                                     int maxWorldY, int minWorldY = 0);
bool AreTerrainColumnSlicesLoaded(const UBlockWorld &world,
                                  glm::ivec3 groundCoord, int maxWorldY,
                                  int minimumSliceCy = kNoTerrainChunkSlice,
                                  int minWorldY = 0);
bool IsTerrainChunkComplete(const UBlockWorld &world, glm::ivec3 groundCoord,
                            int maxWorldY,
                            int minimumSliceCy = kNoTerrainChunkSlice,
                            int minWorldY = 0);
void ClearTerrainColumnChunks(UBlockWorld &world, glm::ivec3 groundCoord,
                              int maxWorldY, int minWorldY = 0);

} // namespace cutum
