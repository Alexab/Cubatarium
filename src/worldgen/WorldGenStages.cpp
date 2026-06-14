#include "WorldGenStages.h"
#include "BlockWorld.h"
#include "Noise.h"
#include <algorithm>
#include <cmath>
#include <iostream>

namespace cutum {

namespace {

constexpr int kSpawnIslandFlatRadius = 48;
constexpr int kSpawnIslandBlendRadius = 16;

float Smoothstep(float edge0, float edge1, float x)
{
 const float t = std::clamp((x - edge0) / (edge1 - edge0), 0.0f, 1.0f);
 return t * t * (3.0f - 2.0f * t);
}

} // namespace

int AdjustSurfaceYForSpawnIsland(int worldX, int worldZ, int naturalSurfaceY,
    const ProceduralSettings& settings, int centerX, int centerZ)
{
 if (!settings.fillWater) {
  return naturalSurfaceY;
 }
 const float dx = static_cast<float>(worldX - centerX);
 const float dz = static_cast<float>(worldZ - centerZ);
 const float dist = std::sqrt(dx * dx + dz * dz);
 const int minLandY = settings.seaLevel + 1;

 int adjusted = naturalSurfaceY;
 if (dist <= static_cast<float>(kSpawnIslandFlatRadius)) {
  adjusted = std::max(naturalSurfaceY, minLandY);
 } else if (dist < static_cast<float>(kSpawnIslandFlatRadius + kSpawnIslandBlendRadius)) {
  const float u = Smoothstep(static_cast<float>(kSpawnIslandFlatRadius),
      static_cast<float>(kSpawnIslandFlatRadius + kSpawnIslandBlendRadius), dist);
  const float minFloor = static_cast<float>(minLandY) * (1.0f - u)
      + static_cast<float>(naturalSurfaceY) * u;
  adjusted = std::max(naturalSurfaceY, static_cast<int>(std::lround(minFloor)));
 }
 return std::clamp(adjusted, 1, settings.maxHeight);
}

void FillTerrainColumn(WorldGenContext& ctx, int x, int z, int surfaceY, const ColumnLayerRule& rule)
{
 if (surfaceY < 1) {
  return;
 }
 for (int y = 0; y <= surfaceY; ++y) {
  BlockId id = rule.fillerBlock;
  if (y == 0) {
   id = ctx.Bedrock;
  } else if (y < surfaceY - rule.dirtDepth - rule.stoneDepthBelowDirt) {
   id = rule.fillerBlock;
  } else if (y < surfaceY - rule.dirtDepth) {
   id = rule.fillerBlock;
  } else if (y < surfaceY) {
   id = rule.subsurfaceBlock;
  } else {
   id = rule.surfaceBlock;
  }
  ctx.World.SetBlock(glm::ivec3(x, y, z), id);
 }
 const int maxY = std::max(surfaceY, ctx.Settings.seaLevel);
 ctx.MarkDirtyColumn(x, z, 0, maxY);
}

void FillFluidColumn(WorldGenContext& ctx, int x, int z, int surfaceY)
{
 if (!ctx.Settings.fillWater || ctx.Water == BLOCK_AIR) {
  return;
 }
 const int sea = ctx.Settings.seaLevel;
 if (surfaceY >= sea) {
  return;
 }
 for (int y = surfaceY + 1; y <= sea; ++y) {
  const glm::ivec3 pos(x, y, z);
  if (ctx.World.GetBlock(pos) == BLOCK_AIR) {
   ctx.World.SetBlock(pos, ctx.Water);
  }
 }
 ctx.MarkDirtyColumn(x, z, surfaceY, sea);
}

int LegacyHashSurfaceY(int x, int z, const ProceduralSettings& settings)
{
 if (settings.vertical == VerticalMode::Compact) {
  return LegacyHeightAt(x, z, settings.seed, 0, 8);
 }
 const int range = std::max(1, settings.maxHeight - settings.seaLevel);
 const int surfaceY = LegacyHeightAt(x, z, settings.seed, settings.seaLevel, range);
 return std::clamp(surfaceY, 1, settings.maxHeight);
}

void FillLegacyHashColumn(WorldGenContext& ctx, int x, int z)
{
 const BlockId bedrock = ctx.Bedrock;
 const BlockId stone = ctx.Stone;
 const BlockId grass = ctx.Grass;
 if (bedrock == BLOCK_AIR || stone == BLOCK_AIR || grass == BLOCK_AIR) {
  std::cerr << "WorldGen: missing block types for legacy column" << std::endl;
  return;
 }
 const BlockId dirtOrStone = ctx.Dirt != BLOCK_AIR ? ctx.Dirt : stone;
 const int naturalY = LegacyHashSurfaceY(x, z, ctx.Settings);
 const int surfaceY = AdjustSurfaceYForSpawnIsland(x, z, naturalY, ctx.Settings);

 for (int y = 0; y <= surfaceY; ++y) {
  BlockId id = stone;
  if (y == 0) {
   id = bedrock;
  } else if (y < surfaceY - 1) {
   id = stone;
  } else if (y == surfaceY - 1) {
   id = dirtOrStone;
  } else if (y == surfaceY) {
   id = grass;
  }
  ctx.World.SetBlock(glm::ivec3(x, y, z), id);
 }
 FillFluidColumn(ctx, x, z, surfaceY);
}

void FillFlatColumn(WorldGenContext& ctx, int x, int z)
{
 const int surfaceY = ctx.Settings.flatSurfaceY;
 if (ctx.Bedrock == BLOCK_AIR || ctx.Stone == BLOCK_AIR || ctx.Grass == BLOCK_AIR) {
  return;
 }
 ctx.World.SetBlock(glm::ivec3(x, 0, z), ctx.Bedrock);
 ctx.World.SetBlock(glm::ivec3(x, 1, z), ctx.Stone);
 ctx.World.SetBlock(glm::ivec3(x, 2, z), ctx.Stone);
 ctx.World.SetBlock(glm::ivec3(x, surfaceY, z), ctx.Grass);
 ctx.MarkDirtyColumn(x, z, 0, surfaceY);
}

} // namespace cutum
