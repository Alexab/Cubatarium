#include "WorldGen/Stages/WorldGenStages.h"
#include "Blocks/BlockRegistry.h"
#include "World/Chunks/Chunk.h"
#include "World/Core/BlockWorld.h"
#include "World/Math/FluidCellState.h"
#include "WorldGen/Core/Noise.h"
#include <algorithm>
#include <array>
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
      ctx.World.SetFluidState(pos, FluidCellState::Source());
    }
  }
  ctx.AccumulateDirtyColumn(surfaceY, sea);
}

namespace
{

bool CellTouchesWet(const WorldGenContext &ctx, glm::ivec3 pos)
{
  static constexpr std::array<glm::ivec3, 6> kNeighborOffsets = {
      glm::ivec3(0, 1, 0),  glm::ivec3(-1, 0, 0), glm::ivec3(1, 0, 0),
      glm::ivec3(0, 0, -1), glm::ivec3(0, 0, 1),  glm::ivec3(0, -1, 0)};
  for (const glm::ivec3 &offset : kNeighborOffsets)
  {
    const glm::ivec3 neighbor = pos + offset;
    if (ctx.World.GetBlock(neighbor) == ctx.Blocks.Water)
    {
      return true;
    }
    if (PackFluidCellState(ctx.World.GetFluidState(neighbor)) != 0)
    {
      return true;
    }
  }
  return false;
}

} // namespace

bool SealFluidPocketsInChunk(WorldGenContext &ctx, int base_x, int base_z)
{
  if (!ctx.Settings.FillWater || ctx.Blocks.Water == BLOCK_AIR)
  {
    return false;
  }
  const int sea = ctx.Settings.SeaLevel;

  bool any_filled = false;
  bool changed = true;
  for (int pass = 0; changed && pass < 8; ++pass)
  {
    changed = false;
    std::vector<glm::ivec3> to_fill_air;
    std::vector<glm::ivec3> to_fill_permeable;
    to_fill_air.reserve(64);
    to_fill_permeable.reserve(64);
    for (int lz = 0; lz < CHUNK_SIZE; ++lz)
    {
      for (int lx = 0; lx < CHUNK_SIZE; ++lx)
      {
        for (int y = 1; y <= sea; ++y)
        {
          const glm::ivec3 pos(base_x + lx, y, base_z + lz);
          const BlockId block_id = ctx.World.GetBlock(pos);
          if (!CellTouchesWet(ctx, pos))
          {
            continue;
          }
          if (block_id == BLOCK_AIR)
          {
            to_fill_air.push_back(pos);
            continue;
          }
          if (ctx.Registry.IsFluidPermeable(block_id) &&
              PackFluidCellState(ctx.World.GetFluidState(pos)) == 0)
          {
            to_fill_permeable.push_back(pos);
          }
        }
      }
    }
    for (const glm::ivec3 &pos : to_fill_air)
    {
      ctx.World.SetBlock(pos, ctx.Blocks.Water);
      ctx.World.SetFluidState(pos, FluidCellState::Source());
      changed = true;
      any_filled = true;
    }
    for (const glm::ivec3 &pos : to_fill_permeable)
    {
      ctx.World.SetFluidState(pos, FluidCellState::Flowing(1));
      changed = true;
      any_filled = true;
    }
  }
  if (any_filled)
  {
    ctx.AccumulateDirtyColumn(0, sea);
  }
  return any_filled;
}

bool SealFluidPermeableDecorInChunk(WorldGenContext &ctx, int base_x, int base_z)
{
  if (!ctx.Settings.FillWater || ctx.Blocks.Water == BLOCK_AIR)
  {
    return false;
  }
  const int sea = ctx.Settings.SeaLevel;
  bool any_filled = false;
  for (int lz = 0; lz < CHUNK_SIZE; ++lz)
  {
    for (int lx = 0; lx < CHUNK_SIZE; ++lx)
    {
      for (int y = 1; y <= sea; ++y)
      {
        const glm::ivec3 pos(base_x + lx, y, base_z + lz);
        const BlockId block_id = ctx.World.GetBlock(pos);
        if (!ctx.Registry.IsFluidPermeable(block_id))
        {
          continue;
        }
        if (PackFluidCellState(ctx.World.GetFluidState(pos)) != 0)
        {
          continue;
        }
        if (!CellTouchesWet(ctx, pos))
        {
          continue;
        }
        ctx.World.SetFluidState(pos, FluidCellState::Flowing(1));
        any_filled = true;
      }
    }
  }
  if (any_filled)
  {
    ctx.AccumulateDirtyColumn(0, sea);
  }
  return any_filled;
}

bool SealFluidShoreOnChunkCommitted(UBlockWorld &world, UBlockRegistry &registry,
                                    const ProceduralSettings &settings,
                                    const std::string &worldgen_owner_pack_id,
                                    glm::ivec3 chunk_coord)
{
  if (!settings.FillWater)
  {
    return false;
  }
  WorldGenContext ctx(world, registry, settings);
  ctx.WorldgenOwnerPackId = worldgen_owner_pack_id;
  ctx.ResolveBlockIds();
  if (ctx.Blocks.Water == BLOCK_AIR)
  {
    return false;
  }
  const int base_x = chunk_coord.x * CHUNK_SIZE;
  const int base_z = chunk_coord.z * CHUNK_SIZE;
  const bool sealed = SealFluidPocketsInChunk(ctx, base_x, base_z);
  const bool permeable = SealFluidPermeableDecorInChunk(ctx, base_x, base_z);
  return sealed || permeable;
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
  const BlockId dirtOrStone =
      ctx.Blocks.Dirt != BLOCK_AIR ? ctx.Blocks.Dirt : stone;
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
