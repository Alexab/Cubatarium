#include "WorldGen/Stages/WorldGenStages.h"
#include "World/Core/BlockWorld.h"
#include "WorldGen/Core/Noise.h"
#include <algorithm>
#include <cmath>
#include <iostream>

namespace cutum
{

namespace
{

constexpr int kSpawnIslandFlatRadius = 48;
constexpr int kSpawnIslandBlendRadius = 16;

float SpawnSmoothstep(float edge0, float edge1, float x)
{
  const float t = std::clamp((x - edge0) / (edge1 - edge0), 0.0f, 1.0f);
  return t * t * (3.0f - 2.0f * t);
}

} // namespace

int AdjustSurfaceYForSpawnIsland(int worldX, int worldZ, int naturalSurfaceY,
                                 const ProceduralSettings &settings,
                                 int centerX, int centerZ)
{
  if (!settings.FillWater)
  {
    return naturalSurfaceY;
  }
  const float dx = static_cast<float>(worldX - centerX);
  const float dz = static_cast<float>(worldZ - centerZ);
  const float dist = std::sqrt(dx * dx + dz * dz);
  const int minLandY = settings.SeaLevel + 1;

  int adjusted = naturalSurfaceY;
  if (dist <= static_cast<float>(kSpawnIslandFlatRadius))
  {
    adjusted = std::max(naturalSurfaceY, minLandY);
  }
  else if (dist <
           static_cast<float>(kSpawnIslandFlatRadius + kSpawnIslandBlendRadius))
  {
    const float u = SpawnSmoothstep(
        static_cast<float>(kSpawnIslandFlatRadius),
        static_cast<float>(kSpawnIslandFlatRadius + kSpawnIslandBlendRadius),
        dist);
    const float minFloor = static_cast<float>(minLandY) * (1.0f - u) +
                           static_cast<float>(naturalSurfaceY) * u;
    adjusted =
        std::max(naturalSurfaceY, static_cast<int>(std::lround(minFloor)));
  }
  return std::clamp(adjusted, 1, settings.MaxHeight);
}

void FillTerrainColumn(WorldGenContext &ctx, int x, int z, int surfaceY,
                       const ColumnLayerRule &rule)
{
  if (surfaceY < 1)
  {
    return;
  }
  const int bedrockTop = std::clamp(ctx.Settings.BedrockTopY, 0, surfaceY);
  for (int y = 0; y <= surfaceY; ++y)
  {
    BlockId Id = rule.fillerBlock;
    if (y <= bedrockTop)
    {
      Id = ctx.Blocks.Bedrock;
    }
    else if (y < surfaceY - rule.dirtDepth - rule.stoneDepthBelowDirt)
    {
      Id = rule.fillerBlock;
    }
    else if (y < surfaceY - rule.dirtDepth)
    {
      Id = rule.fillerBlock;
    }
    else if (y < surfaceY)
    {
      Id = rule.subsurfaceBlock;
    }
    else
    {
      Id = rule.surfaceBlock;
    }
    ctx.World.SetBlock(glm::ivec3(x, y, z), Id);
  }
  const int maxY = std::max(surfaceY, ctx.Settings.SeaLevel);
  ctx.AccumulateDirtyColumn(0, maxY);
}

void FillFluidColumn(WorldGenContext &ctx, int x, int z, int surfaceY)
{
  if (!ctx.Settings.FillWater || ctx.Blocks.Water == BLOCK_AIR)
  {
    return;
  }
  const int sea = ctx.Settings.SeaLevel;
  if (surfaceY >= sea)
  {
    return;
  }
  for (int y = surfaceY + 1; y <= sea; ++y)
  {
    const glm::ivec3 pos(x, y, z);
    if (ctx.World.GetBlock(pos) == BLOCK_AIR)
    {
      ctx.World.SetBlock(pos, ctx.Blocks.Water);
    }
  }
  ctx.AccumulateDirtyColumn(surfaceY, sea);
}

int LegacyHashSurfaceY(int x, int z, const ProceduralSettings &settings)
{
  const int range = std::max(1, settings.MaxHeight - settings.SeaLevel);
  const int surfaceY =
      LegacyHeightAt(x, z, settings.Seed, settings.SeaLevel, range);
  return std::clamp(surfaceY, 1, settings.MaxHeight);
}

void FillLegacyHashColumn(WorldGenContext &ctx, int x, int z)
{
  const BlockId bedrock = ctx.Blocks.Bedrock;
  const BlockId stone = ctx.Blocks.Stone;
  const BlockId grass = ctx.Blocks.Grass;
  if (bedrock == BLOCK_AIR || stone == BLOCK_AIR || grass == BLOCK_AIR)
  {
    std::cerr << "WorldGen: missing block Types for legacy column" << std::endl;
    return;
  }
  const BlockId dirtOrStone = ctx.Blocks.Dirt != BLOCK_AIR ? ctx.Blocks.Dirt : stone;
  const int naturalY = LegacyHashSurfaceY(x, z, ctx.Settings);
  const int surfaceY =
      AdjustSurfaceYForSpawnIsland(x, z, naturalY, ctx.Settings);

  for (int y = 0; y <= surfaceY; ++y)
  {
    BlockId Id = stone;
    if (y == 0)
    {
      Id = bedrock;
    }
    else if (y < surfaceY - 1)
    {
      Id = stone;
    }
    else if (y == surfaceY - 1)
    {
      Id = dirtOrStone;
    }
    else if (y == surfaceY)
    {
      Id = grass;
    }
    ctx.World.SetBlock(glm::ivec3(x, y, z), Id);
  }
  FillFluidColumn(ctx, x, z, surfaceY);
  ctx.AccumulateDirtyColumn(0, surfaceY);
}

void FillFlatColumn(WorldGenContext &ctx, int x, int z)
{
  const int surfaceY = ctx.Settings.FlatSurfaceY;
  if (ctx.Blocks.Bedrock == BLOCK_AIR || ctx.Blocks.Stone == BLOCK_AIR ||
      ctx.Blocks.Grass == BLOCK_AIR)
  {
    return;
  }
  ctx.World.SetBlock(glm::ivec3(x, 0, z), ctx.Blocks.Bedrock);
  ctx.World.SetBlock(glm::ivec3(x, 1, z), ctx.Blocks.Stone);
  ctx.World.SetBlock(glm::ivec3(x, 2, z), ctx.Blocks.Stone);
  ctx.World.SetBlock(glm::ivec3(x, surfaceY, z), ctx.Blocks.Grass);
  ctx.AccumulateDirtyColumn(0, surfaceY);
}

} // namespace cutum
