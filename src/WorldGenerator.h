#ifndef WORLDGENERATOR_H
#define WORLDGENERATOR_H

#include <cstdint>
#include <glm/glm.hpp>

namespace cutum {

class BlockRegistry;
class BlockWorld;

class WorldGenerator {
public:
 static void GenerateFlat(BlockWorld& world, BlockRegistry& registry, int halfExtent = 16, int surfaceY = 3);

 static void GenerateHeightmap(BlockWorld& world, BlockRegistry& registry,
     int halfExtent, uint32_t seed, int baseY = 0, int maxHeight = 8);

 static void GenerateColumn(BlockWorld& world, BlockRegistry& registry,
     int x, int z, uint32_t seed, int baseY, int maxHeight);

 static int SurfaceYAt(int x, int z, uint32_t seed, int baseY, int maxHeight);

 static glm::vec3 DefaultSpawnPosition(int x, int z, uint32_t seed,
     int baseY, int maxHeight, float eyeHeight = 1.6f);

 static void GenerateSpawnArea(BlockWorld& world, BlockRegistry& registry,
     int centerX, int centerZ, int radiusChunks, uint32_t seed, int baseY, int maxHeight);
};

}

#endif
