#include "WorldGen/Core/IUWorldGenPipeline.h"
#include <algorithm>
#include <functional>
#include <vector>
#include "World/Chunks/Chunk.h"
#include "World/Math/GridMath.h"
#include "World/Objects/ObjectUtil.h"

namespace cutum
{

namespace
{

void GenerateAllColumnsInChunkRange(IUWorldGenPipeline &pipeline, int centerX,
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

IUWorldGenPipeline::IUWorldGenPipeline(WorldGenContext ctx) : Ctx(ctx) {}

glm::vec3 IUWorldGenPipeline::DefaultSpawnPosition(int worldX, int worldZ,
                                                  float eyeHeight) const
{
  const int sy = SurfaceYAt(worldX, worldZ);
  return glm::vec3(static_cast<float>(worldX),
                   static_cast<float>(sy) + eyeHeight + 0.5f,
                   static_cast<float>(worldZ));
}

glm::vec3 IUWorldGenPipeline::ResolvePlayerSpawnPosition(
    const UBlockWorld &world, UBlockRegistry &registry, int centerX,
    int centerZ, float eyeHeight) const
{
  constexpr int kMaxRadius = 96;
  constexpr int kStep = 4;

  struct SpawnCandidate
  {
    int x;
    int z;
    int dist2;
  };
  std::vector<SpawnCandidate> candidates;
  candidates.reserve(
      static_cast<size_t>((kMaxRadius / kStep + 1) * (kMaxRadius / kStep + 1) * 4));

  for (int x = centerX - kMaxRadius; x <= centerX + kMaxRadius; x += kStep)
  {
    for (int z = centerZ - kMaxRadius; z <= centerZ + kMaxRadius; z += kStep)
    {
      const int dx = x - centerX;
      const int dz = z - centerZ;
      const int dist2 = dx * dx + dz * dz;
      if (dist2 > kMaxRadius * kMaxRadius)
      {
        continue;
      }
      candidates.push_back({x, z, dist2});
    }
  }
  std::sort(candidates.begin(), candidates.end(),
            [](const SpawnCandidate &a, const SpawnCandidate &b)
            { return a.dist2 < b.dist2; });

  const ProceduralSettings &settings = Ctx.Settings;
  for (const SpawnCandidate &candidate : candidates)
  {
    const int heightmap_y = SurfaceYAt(candidate.x, candidate.z);
    const PlacementSurfaceInfo surface = ResolvePlacementSurfaceY(
        world, registry, candidate.x, candidate.z, heightmap_y,
        settings.MaxHeight, settings.SeaLevel);
    if (surface.topSolidY < 0)
    {
      continue;
    }
    if (!IsExposedLandSurface(world, registry, candidate.x, candidate.z,
                              surface.topSolidY))
    {
      continue;
    }
    return glm::vec3(static_cast<float>(candidate.x),
                     static_cast<float>(surface.topSolidY) + eyeHeight + 0.5f,
                     static_cast<float>(candidate.z));
  }

  return DefaultSpawnPosition(centerX, centerZ, eyeHeight);
}

void IUWorldGenPipeline::GenerateSpawnPatch(int centerX, int centerZ,
                                           int radiusBlocks,
                                           WorldGenColumnProgressFn onProgress)
{
  GenerateAllColumnsInChunkRange(
      *this, centerX, centerZ, centerX - radiusBlocks, centerX + radiusBlocks,
      centerZ - radiusBlocks, centerZ + radiusBlocks, std::move(onProgress));
}

void IUWorldGenPipeline::GenerateFullPatch(int centerX, int centerZ,
                                          int halfExtent,
                                          WorldGenColumnProgressFn onProgress)
{
  GenerateAllColumnsInChunkRange(*this, centerX, centerZ, centerX - halfExtent,
                                 centerX + halfExtent, centerZ - halfExtent,
                                 centerZ + halfExtent, std::move(onProgress));
}

} // namespace cutum
