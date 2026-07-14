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

int AdjustSurfaceYForSpawnIsland(int worldX, int worldZ, int naturalSurfaceY,
                                 const ProceduralSettings &settings,
                                 int centerX, int centerZ)
{
  (void)worldX;
  (void)worldZ;
  (void)centerX;
  (void)centerZ;
  return std::clamp(naturalSurfaceY, 1, settings.MaxHeight);
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
  const int maxScanY = std::max(1, ctx.Settings.MaxHeight - 1);
  const int topSolid =
      FindTopSolidSurfaceY(ctx.World, ctx.Registry, x, z, maxScanY);
  const int fillBase = topSolid >= 0 ? topSolid : surfaceY;
  if (fillBase >= sea)
  {
    return;
  }
  for (int y = fillBase + 1; y <= sea; ++y)
  {
    const glm::ivec3 pos(x, y, z);
    if (ctx.World.GetBlock(pos) == BLOCK_AIR)
    {
      ctx.World.SetBlock(pos, ctx.Blocks.Water);
      ctx.World.SetFluidState(pos,
                              FluidCellState::Source().WithKind(FluidKind::Water));
    }
  }
  ctx.AccumulateDirtyColumn(fillBase, sea);
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

bool AirTouchesLiquidBlock(const WorldGenContext &ctx, glm::ivec3 pos)
{
  static constexpr std::array<glm::ivec3, 6> kDirs = {
      glm::ivec3(0, 1, 0),  glm::ivec3(-1, 0, 0), glm::ivec3(1, 0, 0),
      glm::ivec3(0, 0, -1), glm::ivec3(0, 0, 1),  glm::ivec3(0, -1, 0)};
  for (const glm::ivec3 &offset : kDirs)
  {
    const BlockId id = ctx.World.GetBlock(pos + offset);
    if (id == ctx.Blocks.Water || ctx.Registry.IsLiquid(id))
    {
      return true;
    }
  }
  return false;
}

bool SealShoreAirAdjacencyInChunk(WorldGenContext &ctx, int base_x, int base_z)
{
  const int sea = ctx.Settings.SeaLevel;
  const int min_x = base_x - 1;
  const int max_x = base_x + CHUNK_SIZE;
  const int min_z = base_z - 1;
  const int max_z = base_z + CHUNK_SIZE;
  const BlockId water = ctx.Blocks.Water;
  bool any = false;
  for (int pass = 0; pass < 8; ++pass)
  {
    bool changed = false;
    for (int y = 1; y <= sea; ++y)
    {
      for (int x = min_x; x <= max_x; ++x)
      {
        for (int z = min_z; z <= max_z; ++z)
        {
          const glm::ivec3 pos(x, y, z);
          if (ctx.World.GetBlock(pos) != BLOCK_AIR)
          {
            continue;
          }
          if (!AirTouchesLiquidBlock(ctx, pos))
          {
            continue;
          }
          ctx.World.SetBlock(pos, water);
          ctx.World.SetFluidState(pos,
                                  FluidCellState::Source().WithKind(FluidKind::Water));
          changed = true;
          any = true;
        }
      }
    }
    if (!changed)
    {
      break;
    }
  }
  if (any)
  {
    ctx.AccumulateDirtyColumn(0, sea);
  }
  return any;
}

} // namespace

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
  const bool shore_air = SealShoreAirAdjacencyInChunk(ctx, base_x, base_z);
  return sealed || permeable || sealed_after_permeable || shore_air;
}

int PruneFloatingVegetationInChunk(WorldGenContext &ctx, int base_x, int base_z)
{
  const int max_y = ctx.Settings.MaxHeight - 1;
  int removed = 0;
  for (int lz = 0; lz < CHUNK_SIZE; ++lz)
  {
    for (int lx = 0; lx < CHUNK_SIZE; ++lx)
    {
      removed += PruneFloatingPlantsInColumn(ctx.World, ctx.Registry, base_x + lx,
                                             base_z + lz, max_y);
    }
  }
  if (removed > 0)
  {
    ctx.AccumulateDirtyColumn(ctx.Settings.SeaLevel, max_y);
  }
  return removed;
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
  const int surfaceY = naturalY;

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
