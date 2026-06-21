#pragma once

#include "World/Chunks/ChunkManager.h"
#include <glm/glm.hpp>

namespace cutum
{

class UBlockWorld;

bool TerrainColumnNeedsFill(const UBlockWorld &world, int worldX, int worldZ,
                            int maxWorldY);
bool IsTerrainChunkComplete(const UBlockWorld &world, glm::ivec3 groundCoord,
                            int maxWorldY);

} // namespace cutum
