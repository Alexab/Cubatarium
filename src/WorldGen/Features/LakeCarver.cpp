#include "WorldGen/Features/LakeCarver.h"

#include "Blocks/BlockRegistry.h"
#include "World/Core/BlockWorld.h"
#include "World/Math/FluidCellState.h"
#include "WorldGen/Core/ColumnHash.h"
#include "WorldGen/Core/Noise.h"
#include "WorldGen/Core/WorldGenContext.h"
#include "WorldGen/Sampling/ClimateSampler.h"
#include <algorithm>
#include <array>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <utility>

namespace cutum
{

namespace
{

int LakeCellFloorDiv(int value, int divisor)
{
  int quotient = value / divisor;
  if (value % divisor < 0)
  {
    --quotient;
  }
  return quotient;
}

struct LakeSite
{
  bool valid{false};
  int centerX{0};
  int centerZ{0};
  int waterY{0};
  int radiusX{0};
  int radiusZ{0};
  int maxDepth{0};
  uint32_t shapeSeed{0};
};

LakeSite SampleLakeSite(int cellX, int cellZ, int seaLevel, uint32_t seed,
                        const LakeParams &params,
                        const LakeSurfaceYCallback &get_surface_y)
{
  LakeSite site;
  const int radiusMin = std::clamp(params.radiusMin, 4, 24);
  const int radiusMax = std::clamp(params.radiusMax, radiusMin, 24);
  const int cellSize = std::max(
      params.cellSize, radiusMax * 2 + 8);
  const int margin = radiusMax + 2;
  const int span = std::max(1, cellSize - margin * 2);
  const int cellOriginX = cellX * cellSize;
  const int cellOriginZ = cellZ * cellSize;
  const uint32_t hash = ColumnHash(cellX, cellZ, seed + 9601U);
  const int chance = std::max(1, params.chancePerCell);
  if (hash % static_cast<uint32_t>(chance) != 0U)
  {
    return site;
  }

  const uint32_t placementHash = ColumnHash(cellX, cellZ, seed + 9602U);
  if (!get_surface_y)
  {
    return site;
  }
  const int anchorX = cellOriginX + margin +
                      static_cast<int>(placementHash % static_cast<uint32_t>(span));
  const int anchorZ = cellOriginZ + margin +
                      static_cast<int>((placementHash / 31U) %
                                       static_cast<uint32_t>(span));
  const int minCenterX = cellOriginX + margin;
  const int minCenterZ = cellOriginZ + margin;
  const int maxCenterX = cellOriginX + cellSize - margin - 1;
  const int maxCenterZ = cellOriginZ + cellSize - margin - 1;
  const int candidateStep = std::max(radiusMax + 4, cellSize / 4);
  static constexpr std::array<std::pair<int, int>, 8> kCandidateOffsets = {{
      {1, 0}, {1, 1}, {0, 1}, {-1, 1}, {-1, 0}, {-1, -1}, {0, -1}, {1, -1}}};
  const int minElevation = std::max(1, params.minElevationAboveSea);
  const int maxElevation = std::max(minElevation, params.maxElevationAboveSea);
  bool foundLandSite = false;
  std::array<std::pair<int, int>, 9> testedCandidates{};
  std::size_t testedCandidateCount = 0;
  for (int attempt = 0; attempt < 9; ++attempt)
  {
    int candidateX = anchorX;
    int candidateZ = anchorZ;
    if (attempt > 0)
    {
      const std::size_t offsetIndex = static_cast<std::size_t>(
          (placementHash + static_cast<uint32_t>(attempt - 1)) %
          static_cast<uint32_t>(kCandidateOffsets.size()));
      candidateX += kCandidateOffsets[offsetIndex].first * candidateStep;
      candidateZ += kCandidateOffsets[offsetIndex].second * candidateStep;
      candidateX = std::clamp(candidateX, minCenterX, maxCenterX);
      candidateZ = std::clamp(candidateZ, minCenterZ, maxCenterZ);
    }
    const std::pair<int, int> candidate{candidateX, candidateZ};
    if (std::find(testedCandidates.begin(),
                  testedCandidates.begin() + testedCandidateCount,
                  candidate) != testedCandidates.begin() + testedCandidateCount)
    {
      continue;
    }
    testedCandidates[testedCandidateCount++] = candidate;

    const ClimateSample climate = SampleClimate(candidateX, candidateZ, seed);
    if (!IsLakeClimateSuitable(climate, params))
    {
      continue;
    }
    const int centerSurfaceY = get_surface_y(candidateX, candidateZ);
    if (centerSurfaceY < seaLevel + minElevation + 1)
    {
      continue;
    }

    const int waterDrop = 2 + static_cast<int>((hash >> 5U) % 3U);
    const int waterY = std::clamp(centerSurfaceY - waterDrop,
                                  seaLevel + minElevation,
                                  seaLevel + maxElevation);
    if (centerSurfaceY - waterY > std::max(2, params.maxExcavationDepth))
    {
      continue;
    }

    site.centerX = candidateX;
    site.centerZ = candidateZ;
    site.waterY = waterY;
    foundLandSite = true;
    break;
  }
  if (!foundLandSite)
  {
    return site;
  }

  site.radiusX = radiusMin + static_cast<int>((hash >> 8U) % static_cast<uint32_t>(
                                                  radiusMax - radiusMin + 1));
  site.radiusZ = radiusMin + static_cast<int>((hash >> 13U) % static_cast<uint32_t>(
                                                  radiusMax - radiusMin + 1));
  site.maxDepth = std::clamp(params.basinDepthMin, 1, 12) +
                  static_cast<int>((hash >> 24U) % static_cast<uint32_t>(
                      std::max(1, std::clamp(params.basinDepthMax, 1, 12) -
                                      std::clamp(params.basinDepthMin, 1, 12) +
                                  1)));
  site.shapeSeed = seed + hash + 9603U;
  site.valid = true;
  return site;
}

} // namespace

bool IsLakeClimateSuitable(const ClimateSample &climate,
                           const LakeParams &params)
{
  return climate.moisture >= params.minMoisture &&
         climate.temperature <= params.maxTemperature &&
         climate.continentalness >= params.minContinentalness &&
         climate.continentalness <= params.maxContinentalness &&
         climate.erosion <= 0.92f;
}

void CarveLakeColumn(WorldGenContext &ctx, int x, int z, int surfaceY,
                     uint32_t seed, const LakeParams &params, int seaLevel,
                     const LakeSurfaceYCallback &get_surface_y)
{
  if (!params.enabled || ctx.Blocks.Water == BLOCK_AIR)
  {
    return;
  }
  const int radiusMax = std::clamp(params.radiusMax, 4, 24);
  const int cellSize = std::max(params.cellSize, radiusMax * 2 + 8);
  const int cellX = LakeCellFloorDiv(x, cellSize);
  const int cellZ = LakeCellFloorDiv(z, cellSize);
  const LakeSite site =
      SampleLakeSite(cellX, cellZ, seaLevel, seed, params, get_surface_y);
  if (!site.valid)
  {
    return;
  }

  const int dx = x - site.centerX;
  const int dz = z - site.centerZ;
  if (std::abs(dx) > site.radiusX + 1 || std::abs(dz) > site.radiusZ + 1)
  {
    return;
  }
  const float distance =
      std::sqrt(static_cast<float>(dx * dx) /
                    static_cast<float>(site.radiusX * site.radiusX) +
                static_cast<float>(dz * dz) /
                    static_cast<float>(site.radiusZ * site.radiusZ));
  const float shorelineNoise = NormalizedFBM2D(
      static_cast<float>(x) * 0.075f, static_cast<float>(z) * 0.075f,
      site.shapeSeed, 2, 0.5f, 2.0f);
  const float irregularDistance = distance + shorelineNoise * 0.075f;
  if (irregularDistance >= 1.0f)
  {
    return;
  }

  const int maxExcavation = std::max(2, params.maxExcavationDepth);
  if (surfaceY - site.waterY > maxExcavation)
  {
    return;
  }
  const float basin =
      1.0f - Smoothstep(0.0f, 1.0f, std::clamp(irregularDistance, 0.0f, 1.0f));
  const int depth = std::max(
      1, static_cast<int>(std::floor(1.0f +
                                     static_cast<float>(site.maxDepth - 1) * basin +
                                     0.5f)));
  const int floorY = site.waterY - depth;
  if (floorY < ctx.Settings.MinHeight || surfaceY < floorY)
  {
    return;
  }

  const BlockId floorBlock =
      ctx.Blocks.Dirt != BLOCK_AIR ? ctx.Blocks.Dirt : ctx.Blocks.Stone;
  if (floorBlock == BLOCK_AIR)
  {
    return;
  }
  for (int y = std::max(surfaceY, site.waterY); y > floorY; --y)
  {
    const glm::ivec3 position(x, y, z);
    if (y <= site.waterY)
    {
      ctx.World.SetBlock(position, ctx.Blocks.Water);
      ctx.World.SetFluidState(
          position, FluidCellState::Source().WithKind(FluidKind::Water));
    }
    else
    {
      ctx.World.SetBlock(position, BLOCK_AIR);
    }
  }
  ctx.World.SetBlock(glm::ivec3(x, floorY, z), floorBlock);
  ctx.AccumulateDirtyColumn(floorY, std::max(surfaceY, site.waterY));
}

} // namespace cutum
