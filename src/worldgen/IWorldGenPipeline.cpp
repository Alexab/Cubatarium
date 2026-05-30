#include "IWorldGenPipeline.h"

namespace cutum {

IWorldGenPipeline::IWorldGenPipeline(WorldGenContext ctx)
 : ctx_(ctx)
{
}

glm::vec3 IWorldGenPipeline::DefaultSpawnPosition(int worldX, int worldZ, float eyeHeight) const
{
 const int sy = SurfaceYAt(worldX, worldZ);
 return glm::vec3(static_cast<float>(worldX),
                  static_cast<float>(sy) + eyeHeight + 0.5f,
                  static_cast<float>(worldZ));
}

void IWorldGenPipeline::GenerateSpawnPatch(int centerX, int centerZ, int radiusBlocks)
{
 for (int x = centerX - radiusBlocks; x <= centerX + radiusBlocks; ++x) {
  for (int z = centerZ - radiusBlocks; z <= centerZ + radiusBlocks; ++z) {
   GenerateColumn(x, z);
  }
 }
}

void IWorldGenPipeline::GenerateFullPatch(int centerX, int centerZ, int halfExtent)
{
 for (int x = centerX - halfExtent; x <= centerX + halfExtent; ++x) {
  for (int z = centerZ - halfExtent; z <= centerZ + halfExtent; ++z) {
   GenerateColumn(x, z);
  }
 }
}

} // namespace cutum
