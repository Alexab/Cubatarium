#ifndef WORLDGENERATOR_H
#define WORLDGENERATOR_H

#include <cstdint>
#include <glm/glm.hpp>

namespace cutum
{

class UBlockRegistry;
class UBlockWorld;

class UWorldGenerator
{
public:
  static void GenerateFlat(UBlockWorld &world, UBlockRegistry &registry,
                           int halfExtent = 16, int surfaceY = 3);

  static void GenerateFlatColumn(UBlockWorld &world, UBlockRegistry &registry,
                                 int x, int z, int surfaceY = 3);

  static void GenerateFlatArea(UBlockWorld &world, UBlockRegistry &registry,
                               int centerX, int centerZ, int radiusChunks,
                               int surfaceY = 3);

  static void GenerateHeightmap(UBlockWorld &world, UBlockRegistry &registry,
                                int halfExtent, uint32_t seed, int baseY = 0,
                                int maxHeight = 8);

  static void GenerateColumn(UBlockWorld &world, UBlockRegistry &registry,
                             int x, int z, uint32_t seed, int baseY,
                             int maxHeight);

  static int SurfaceYAt(int x, int z, uint32_t seed, int baseY, int maxHeight);

  static glm::vec3 DefaultSpawnPosition(int x, int z, uint32_t seed, int baseY,
                                        int maxHeight, float eyeHeight = 1.62f);

  static void GenerateSpawnArea(UBlockWorld &world, UBlockRegistry &registry,
                                int centerX, int centerZ, int radiusChunks,
                                uint32_t seed, int baseY, int maxHeight);
};

} // namespace cutum

#endif
