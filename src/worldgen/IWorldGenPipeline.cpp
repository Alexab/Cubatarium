#include "IWorldGenPipeline.h"
#include "Chunk.h"
#include "GridMath.h"

namespace cutum {

namespace {

void GenerateAllColumnsInChunkRange(IWorldGenPipeline& pipeline, int centerX, int centerZ,
    int minWorldX, int maxWorldX, int minWorldZ, int maxWorldZ)
{
 const int minCx = FloorDiv(minWorldX, CHUNK_SIZE);
 const int maxCx = FloorDiv(maxWorldX, CHUNK_SIZE);
 const int minCz = FloorDiv(minWorldZ, CHUNK_SIZE);
 const int maxCz = FloorDiv(maxWorldZ, CHUNK_SIZE);
 for (int cx = minCx; cx <= maxCx; ++cx) {
  for (int cz = minCz; cz <= maxCz; ++cz) {
   for (int lx = 0; lx < CHUNK_SIZE; ++lx) {
    for (int lz = 0; lz < CHUNK_SIZE; ++lz) {
     pipeline.GenerateColumn(cx * CHUNK_SIZE + lx, cz * CHUNK_SIZE + lz);
    }
   }
  }
 }
}

} // namespace

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
 GenerateAllColumnsInChunkRange(*this, centerX, centerZ,
     centerX - radiusBlocks, centerX + radiusBlocks,
     centerZ - radiusBlocks, centerZ + radiusBlocks);
}

void IWorldGenPipeline::GenerateFullPatch(int centerX, int centerZ, int halfExtent)
{
 GenerateAllColumnsInChunkRange(*this, centerX, centerZ,
     centerX - halfExtent, centerX + halfExtent,
     centerZ - halfExtent, centerZ + halfExtent);
}

} // namespace cutum
