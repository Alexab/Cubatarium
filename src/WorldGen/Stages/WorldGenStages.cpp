#include "WorldGen/Stages/WorldGenStages.h"
#include "Blocks/BlockRegistry.h"
#include "World/Chunks/Chunk.h"
#include "World/Core/BlockWorld.h"
#include "World/Math/FluidCellState.h"
#include "World/Objects/ObjectUtil.h"
#include "World/Physics/FluidSpreadSystem.h"
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
      ctx.World.SetFluidState(pos,
                              FluidCellState::Source().WithKind(FluidKind::Water));
    }
  }
  ctx.AccumulateDirtyColumn(surfaceY, sea);
}

bool SealFluidPocketsInChunk(WorldGenContext &ctx, int base_x, int base_z)
{
  if (!ctx.Settings.FillWater || ctx.Blocks.Water == BLOCK_AIR)
  {
    return false;
  }
  const int sea = ctx.Settings.SeaLevel;
  const UBlockDefinitionStorage *definitions = ctx.Registry.GetDefinitions();
  if (definitions == nullptr)
  {
    return false;
  }

  FluidFloodOptions options;
  options.fluid_id = ctx.Blocks.Water;
  options.source_for_air = true;
  options.max_passes = 8;
  const int filled = UFluidSpreadSystem::FloodWetPocketsInBox(
      ctx.World, *definitions,
      glm::ivec3(base_x, 1, base_z),
      glm::ivec3(base_x + CHUNK_SIZE - 1, sea, base_z + CHUNK_SIZE - 1),
      options);
  if (filled > 0)
  {
    ctx.AccumulateDirtyColumn(0, sea);
  }
  return filled > 0;
}

namespace
{

bool IsWaterColumnSlotBlock(const WorldGenContext &ctx, UBlockRegistry &registry,
                            BlockId id, int y, int floor_y)
{
  if (id == BLOCK_AIR)
  {
    return true;
  }
  if (id == ctx.Blocks.Water || registry.IsLiquid(id))
  {
    return true;
  }
  if (registry.IsFluidPermeable(id))
  {
    return true;
  }
  if (y > floor_y && id == ctx.Blocks.Dirt)
  {
    return true;
  }
  return false;
}

int FindWaterColumnFloorY(const WorldGenContext &ctx, int x, int z, int sea)
{
  int floor_y = -1;
  for (int y = 1; y < sea; ++y)
  {
    const glm::ivec3 ground(x, y, z);
    if (!IsSolidPlantGround(ctx.World, ctx.Registry, ground))
    {
      continue;
    }
    bool column_above = true;
    for (int wy = y + 1; wy <= sea; ++wy)
    {
      const BlockId above_id = ctx.World.GetBlock(glm::ivec3(x, wy, z));
      if (!IsWaterColumnSlotBlock(ctx, ctx.Registry, above_id, wy, y))
      {
        column_above = false;
        break;
      }
    }
    if (column_above)
    {
      floor_y = y;
    }
  }
  return floor_y;
}

bool RestoreWaterColumnCell(WorldGenContext &ctx, glm::ivec3 pos, int floor_y,
                            int sea)
{
  const BlockId water = ctx.Blocks.Water;
  BlockId id = ctx.World.GetBlock(pos);
  if (id == water)
  {
    if (PackFluidCellState(ctx.World.GetFluidState(pos)) == 0)
    {
      ctx.World.SetFluidState(pos, FluidCellState::Source().WithKind(FluidKind::Water));
      return true;
    }
    return false;
  }
  if (ctx.Registry.IsFluidPermeable(id))
  {
    if (PackFluidCellState(ctx.World.GetFluidState(pos)) != 0)
    {
      return false;
    }
    ctx.World.SetFluidState(pos, FluidCellState::Flowing(1).WithKind(FluidKind::Water));
    return true;
  }
  if (pos.y <= floor_y)
  {
    return false;
  }
  if (pos.y > sea)
  {
    return false;
  }
  if (id == BLOCK_AIR || id == ctx.Blocks.Dirt)
  {
    ctx.World.SetBlock(pos, water);
    ctx.World.SetFluidState(pos, FluidCellState::Source().WithKind(FluidKind::Water));
    return true;
  }
  if (ctx.Registry.BlocksMovement(id))
  {
    ctx.World.SetBlock(pos, water);
    ctx.World.SetFluidState(pos, FluidCellState::Source().WithKind(FluidKind::Water));
    return true;
  }
  return false;
}

bool WaterlogCoastalPermeableStack(WorldGenContext &ctx, int x, int z,
                                   int floor_y, int sea)
{
  if (floor_y < 0 || floor_y >= sea)
  {
    return false;
  }
  const int max_y =
      std::min(ctx.Settings.MaxHeight - 1, sea + 8);
  bool changed = false;
  for (int y = floor_y + 1; y <= max_y; ++y)
  {
    const glm::ivec3 pos(x, y, z);
    const BlockId id = ctx.World.GetBlock(pos);
    if (id == BLOCK_AIR)
    {
      break;
    }
    if (ctx.Registry.IsLiquid(id))
    {
      continue;
    }
    if (!ctx.Registry.IsFluidPermeable(id))
    {
      break;
    }
    if (PackFluidCellState(ctx.World.GetFluidState(pos)) != 0)
    {
      continue;
    }
    ctx.World.SetFluidState(pos, FluidCellState::Flowing(1).WithKind(FluidKind::Water));
    changed = true;
  }
  return changed;
}

bool RestoreWaterColumnAt(WorldGenContext &ctx, int x, int z, int sea)
{
  const int floor_y = FindWaterColumnFloorY(ctx, x, z, sea);
  if (floor_y < 0 || floor_y >= sea)
  {
    return false;
  }
  bool changed = false;
  for (int y = floor_y + 1; y <= sea; ++y)
  {
    if (RestoreWaterColumnCell(ctx, glm::ivec3(x, y, z), floor_y, sea))
    {
      changed = true;
    }
  }
  if (WaterlogCoastalPermeableStack(ctx, x, z, floor_y, sea))
  {
    changed = true;
  }
  return changed;
}

} // namespace

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
      if (RestoreWaterColumnAt(ctx, base_x + lx, base_z + lz, sea))
      {
        any_filled = true;
      }
    }
  }
  if (any_filled)
  {
    ctx.AccumulateDirtyColumn(0, sea + 8);
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
  const bool sealed_after_permeable =
      permeable ? SealFluidPocketsInChunk(ctx, base_x, base_z) : false;
  return sealed || permeable || sealed_after_permeable;
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
