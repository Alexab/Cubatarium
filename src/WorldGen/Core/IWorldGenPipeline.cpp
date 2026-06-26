#include "WorldGen/Core/IWorldGenPipeline.h"
#include <algorithm>
#include <functional>
#include <vector>
#include "World/Chunks/Chunk.h"
#include "World/Math/GridMath.h"

namespace cutum
{

namespace
{

void GenerateAllColumnsInChunkRange(IWorldGenPipeline &pipeline, int centerX,
                                    int centerZ, int minWorldX, int maxWorldX,
                                    int minWorldZ, int maxWorldZ,
                                    WorldGenColumnProgressFn onProgress)
{
  struct ColumnCoord
  {
    int x;
    int z;
    int dist2;
  };
  std::vector<ColumnCoord> columns;
  columns.reserve(static_cast<size_t>((maxWorldX - minWorldX + 1) *
                                    (maxWorldZ - minWorldZ + 1)));
  for (int x = minWorldX; x <= maxWorldX; ++x)
  {
    for (int z = minWorldZ; z <= maxWorldZ; ++z)
    {
      const int dx = x - centerX;
      const int dz = z - centerZ;
      columns.push_back({x, z, dx * dx + dz * dz});
    }
  }
  std::sort(columns.begin(), columns.end(),
            [](const ColumnCoord &a, const ColumnCoord &b)
            { return a.dist2 < b.dist2; });

  const int totalColumns = static_cast<int>(columns.size());
  int done = 0;
  for (const ColumnCoord &column : columns)
  {
    pipeline.GenerateColumn(column.x, column.z);
    ++done;
    if (onProgress)
    {
      onProgress(done, totalColumns);
    }
  }
}

} // namespace

IWorldGenPipeline::IWorldGenPipeline(WorldGenContext ctx) : Ctx(ctx) {}

glm::vec3 IWorldGenPipeline::DefaultSpawnPosition(int worldX, int worldZ,
                                                  float eyeHeight) const
{
  const int sy = SurfaceYAt(worldX, worldZ);
  return glm::vec3(static_cast<float>(worldX),
                   static_cast<float>(sy) + eyeHeight + 0.5f,
                   static_cast<float>(worldZ));
}

void IWorldGenPipeline::GenerateSpawnPatch(int centerX, int centerZ,
                                           int radiusBlocks,
                                           WorldGenColumnProgressFn onProgress)
{
  GenerateAllColumnsInChunkRange(
      *this, centerX, centerZ, centerX - radiusBlocks, centerX + radiusBlocks,
      centerZ - radiusBlocks, centerZ + radiusBlocks, std::move(onProgress));
}

void IWorldGenPipeline::GenerateFullPatch(int centerX, int centerZ,
                                          int halfExtent,
                                          WorldGenColumnProgressFn onProgress)
{
  GenerateAllColumnsInChunkRange(*this, centerX, centerZ, centerX - halfExtent,
                                 centerX + halfExtent, centerZ - halfExtent,
                                 centerZ + halfExtent, std::move(onProgress));
}

} // namespace cutum
