#include "WorldGenStages.h"
#include "BlockWorld.h"
#include "Noise.h"
#include <algorithm>
#include <iostream>

namespace cutum {

void FillTerrainColumn(WorldGenContext& ctx, int x, int z, int surfaceY, const ColumnLayerRule& rule)
{
 if (surfaceY < 1) {
  return;
 }
 for (int y = 0; y <= surfaceY; ++y) {
  BlockId id = rule.fillerBlock;
  if (y == 0) {
   id = ctx.bedrock;
  } else if (y < surfaceY - rule.dirtDepth - rule.stoneDepthBelowDirt) {
   id = rule.fillerBlock;
  } else if (y < surfaceY - rule.dirtDepth) {
   id = rule.fillerBlock;
  } else if (y < surfaceY) {
   id = rule.subsurfaceBlock;
  } else {
   id = rule.surfaceBlock;
  }
  ctx.world.SetBlock(glm::ivec3(x, y, z), id);
 }
 const int maxY = std::max(surfaceY, ctx.settings.seaLevel);
 ctx.MarkDirtyColumn(x, z, 0, maxY);
}

void FillFluidColumn(WorldGenContext& ctx, int x, int z, int surfaceY)
{
 if (!ctx.settings.fillWater || ctx.water == BLOCK_AIR) {
  return;
 }
 const int sea = ctx.settings.seaLevel;
 if (surfaceY >= sea) {
  return;
 }
 for (int y = surfaceY + 1; y <= sea; ++y) {
  const glm::ivec3 pos(x, y, z);
  if (ctx.world.GetBlock(pos) == BLOCK_AIR) {
   ctx.world.SetBlock(pos, ctx.water);
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
 const BlockId bedrock = ctx.bedrock;
 const BlockId stone = ctx.stone;
 const BlockId grass = ctx.grass;
 if (bedrock == BLOCK_AIR || stone == BLOCK_AIR || grass == BLOCK_AIR) {
  std::cerr << "WorldGen: missing block types for legacy column" << std::endl;
  return;
 }
 const BlockId dirtOrStone = ctx.dirt != BLOCK_AIR ? ctx.dirt : stone;
 const int surfaceY = LegacyHashSurfaceY(x, z, ctx.settings);

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
  ctx.world.SetBlock(glm::ivec3(x, y, z), id);
 }
 FillFluidColumn(ctx, x, z, surfaceY);
}

void FillFlatColumn(WorldGenContext& ctx, int x, int z)
{
 const int surfaceY = ctx.settings.flatSurfaceY;
 if (ctx.bedrock == BLOCK_AIR || ctx.stone == BLOCK_AIR || ctx.grass == BLOCK_AIR) {
  return;
 }
 ctx.world.SetBlock(glm::ivec3(x, 0, z), ctx.bedrock);
 ctx.world.SetBlock(glm::ivec3(x, 1, z), ctx.stone);
 ctx.world.SetBlock(glm::ivec3(x, 2, z), ctx.stone);
 ctx.world.SetBlock(glm::ivec3(x, surfaceY, z), ctx.grass);
 ctx.MarkDirtyColumn(x, z, 0, surfaceY);
}

} // namespace cutum
