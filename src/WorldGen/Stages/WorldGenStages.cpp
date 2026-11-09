#include "WorldGen/Stages/WorldGenStages.h"
#include "App/Platform/Log.h"
#include "Blocks/BlockRegistry.h"
#include "World/Chunks/Chunk.h"
#include "World/Core/BlockWorld.h"
#include "World/Math/FluidCellState.h"
#include "World/Objects/ObjectUtil.h"
#include "World/Physics/FluidSpreadSystem.h"
#include "WorldGen/Core/Noise.h"
#include "WorldGen/Core/ColumnHash.h"
#include "WorldGen/Core/WorldGenBlockResolver.h"
#include "WorldGen/Core/WorldGenPack.h"
#include "WorldGen/Sampling/AquiferSampler.h"
#include <algorithm>
#include <array>
#include <atomic>
#include <chrono>
#include <cmath>
#include <cstdlib>
#include <iostream>
#include <optional>

namespace cutum
{

namespace
{

std::optional<UAquiferSampler>
MakeAquiferSealSampler(const WorldGenContext &ctx)
{
  if (ctx.Settings.TerrainBackendMode != TerrainBackend::Density3D ||
      !ctx.Settings.EnableCaves ||
      (UWorldGenPack::Get().Pipeline.Loaded &&
       !UWorldGenPack::Get().Pipeline.Aquifers))
  {
    return std::nullopt;
  }
  return UAquiferSampler(ctx.Settings.Seed, ctx.Settings.SeaLevel,
                         ctx.Settings.MaxHeight,
                         UWorldGenPack::AquifersConfig(),
                         ctx.Settings.MinHeight);
}

bool AllowsAquiferWater(const UAquiferSampler *sampler, glm::ivec3 pos,
                        int sea_level)
{
  // Preserve the surface ocean/shore band. Below it, connected-pocket sealing
  // may only fill cells that the generation sampler classified as water.
  if (sampler == nullptr || pos.y >= sea_level - 1)
  {
    return true;
  }
  return sampler->SampleAt(pos.x, pos.y, pos.z).Fluid == AquiferFluid::Water;
}

/// Highest liquid block in the chunk within the above-sea band. Valleys
/// (rivers) may carry water up to sea+24 and lakes sit above sea too — the
/// seal passes historically stopped at sea and left those bodies unprotected
/// (water audit A-3). Bounded scan window keeps this cheap.
int HighestFluidTopInChunk(const WorldGenContext &ctx, int base_x, int base_z)
{
  constexpr int kMaxAboveSea = 25; // valleys max_water_elevation_above_sea=24
  const int sea = ctx.Settings.SeaLevel;
  const int top = std::min(ctx.Settings.MaxHeight - 1, sea + kMaxAboveSea);
  for (int y = top; y > sea; --y)
  {
    for (int z = base_z; z < base_z + CHUNK_SIZE; ++z)
    {
      for (int x = base_x; x < base_x + CHUNK_SIZE; ++x)
      {
        const BlockId id = ctx.World.GetBlock(glm::ivec3(x, y, z));
        if (id != BLOCK_AIR && ctx.Registry.IsLiquid(id))
        {
          return y;
        }
      }
    }
  }
  return sea;
}

BlockId ResolveGeologyBlock(WorldGenContext &ctx, const char *name)
{
  const BlockId resolved = ResolvePackScatterBlockId(
      ctx.Registry, ctx.WorldgenOwnerPackId, name);
  return resolved != BLOCK_AIR ? resolved : ctx.Blocks.Stone;
}

bool IsGeodeHost(BlockId id, BlockId stone, BlockId granite, BlockId diorite,
                 BlockId andesite, BlockId deepslate, BlockId tuff)
{
  return id == stone || id == granite || id == diorite || id == andesite ||
         id == deepslate || id == tuff;
}

bool TryGenerateGeode(WorldGenContext &ctx, int x, int z, uint32_t seed,
                      BlockId granite, BlockId diorite, BlockId andesite,
                      BlockId deepslate, BlockId tuff, BlockId smoothBasalt,
                      BlockId calcite, BlockId amethyst)
{
  constexpr int kCellSize = 32;
  const int cell_x = x >= 0 ? x / kCellSize : (x - kCellSize + 1) / kCellSize;
  const int cell_z = z >= 0 ? z / kCellSize : (z - kCellSize + 1) / kCellSize;
  const int anchor_x = cell_x * kCellSize + 8;
  const int anchor_z = cell_z * kCellSize + 8;
  if (x != anchor_x || z != anchor_z || smoothBasalt == ctx.Blocks.Stone ||
      calcite == ctx.Blocks.Stone || amethyst == ctx.Blocks.Stone)
  {
    return false;
  }

  const uint32_t hash = ColumnHash(x, z, seed + 8301);
  uint32_t candidate_hash = hash ^ (hash >> 16U);
  candidate_hash *= 0x7feb352dU;
  candidate_hash ^= candidate_hash >> 15U;
  if (candidate_hash % 4U != 0U)
  {
    return false;
  }
  const int radius = 4 + static_cast<int>((hash >> 8U) % 3U);
  const int highest_center = ctx.Settings.SeaLevel - radius - 2;
  const int lowest_center = radius + 2;
  if (highest_center < lowest_center)
  {
    return false;
  }
  const int center_y = lowest_center +
                       static_cast<int>((hash >> 16U) % static_cast<uint32_t>(
                           highest_center - lowest_center + 1));

  for (int dy = -radius; dy <= radius; ++dy)
  {
    for (int dx = -radius; dx <= radius; ++dx)
    {
      for (int dz = -radius; dz <= radius; ++dz)
      {
        const int distance_squared = dx * dx + dy * dy + dz * dz;
        if (distance_squared > radius * radius)
        {
          continue;
        }
        const BlockId host = ctx.World.GetBlock(
            glm::ivec3(anchor_x + dx, center_y + dy, anchor_z + dz));
        if (!IsGeodeHost(host, ctx.Blocks.Stone, granite, diorite, andesite,
                         deepslate, tuff))
        {
          return false;
        }
      }
    }
  }

  for (int dy = -radius; dy <= radius; ++dy)
  {
    for (int dx = -radius; dx <= radius; ++dx)
    {
      for (int dz = -radius; dz <= radius; ++dz)
      {
        const float distance = std::sqrt(static_cast<float>(dx * dx + dy * dy + dz * dz));
        if (distance > static_cast<float>(radius))
        {
          continue;
        }
        BlockId shell = BLOCK_AIR;
        if (distance >= static_cast<float>(radius) - 0.85f)
        {
          shell = smoothBasalt;
        }
        else if (distance >= static_cast<float>(radius) - 1.8f)
        {
          shell = calcite;
        }
        else if (distance >= static_cast<float>(radius) - 2.7f)
        {
          shell = amethyst;
        }
        else
        {
          const uint32_t interior_hash =
              ColumnHash(anchor_x + dx + (center_y + dy) * 31,
                         anchor_z + dz, seed + 8302);
          if (interior_hash % 11U == 0U)
          {
            shell = amethyst;
          }
        }
        ctx.World.SetBlock(
            glm::ivec3(anchor_x + dx, center_y + dy, anchor_z + dz), shell);
      }
    }
  }
  ctx.AccumulateDirtyColumn(center_y - radius, center_y + radius);
  return true;
}

} // namespace

int AdjustSurfaceYForSpawnIsland(int worldX, int worldZ, int naturalSurfaceY,
                                 const ProceduralSettings &settings,
                                 int centerX, int centerZ)
{
  (void)worldX;
  (void)worldZ;
  (void)centerX;
  (void)centerZ;
  return std::clamp(naturalSurfaceY, settings.MinHeight + 1,
                    settings.MaxHeight);
}

void FillTerrainColumn(WorldGenContext &ctx, int x, int z, int surfaceY,
                       const ColumnLayerRule &rule)
{
  if (surfaceY < ctx.Settings.MinHeight + 1)
  {
    return;
  }
  const int bedrockTop = std::clamp(ctx.Settings.BedrockTopY,
                                    ctx.Settings.MinHeight, surfaceY);
  for (int y = ctx.Settings.MinHeight; y <= surfaceY; ++y)
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
  ctx.AccumulateDirtyColumn(ctx.Settings.MinHeight, maxY);
}

bool ApplyGeologyColumn(WorldGenContext &ctx, int x, int z, int surfaceY,
                        uint32_t seed)
{
  if (ctx.Blocks.Stone == BLOCK_AIR ||
      surfaceY < ctx.Settings.MinHeight + 1)
  {
    return false;
  }

  const BlockId granite = ResolveGeologyBlock(ctx, "granite");
  const BlockId diorite = ResolveGeologyBlock(ctx, "diorite");
  const BlockId andesite = ResolveGeologyBlock(ctx, "andesite");
  const BlockId deepslate = ResolveGeologyBlock(ctx, "deepslate");
  const BlockId tuff = ResolveGeologyBlock(ctx, "tuff");
  const BlockId smooth_basalt = ResolveGeologyBlock(ctx, "smooth_basalt");
  const BlockId calcite = ResolveGeologyBlock(ctx, "calcite");
  const BlockId amethyst = ResolveGeologyBlock(ctx, "amethyst_block");
  const float regional = FBM2D(static_cast<float>(x) * 0.009f,
                               static_cast<float>(z) * 0.009f, seed + 8101,
                               3, 0.5f, 2.0f);
  const int deep_transition = std::max(4, ctx.Settings.SeaLevel / 4);
  bool changed = false;
  int min_dirty = surfaceY;
  int max_dirty = 0;

  for (int y = ctx.Settings.MinHeight; y <= surfaceY; ++y)
  {
    const glm::ivec3 pos(x, y, z);
    if (ctx.World.GetBlock(pos) != ctx.Blocks.Stone)
    {
      continue;
    }

    BlockId replacement = ctx.Blocks.Stone;
    if (y <= deep_transition)
    {
      replacement = deepslate;
    }
    else
    {
      const float patch = FBM3D(static_cast<float>(x) * 0.032f,
                                static_cast<float>(y) * 0.037f,
                                static_cast<float>(z) * 0.032f, seed + 8102,
                                2, 0.5f, 2.0f);
      if (patch > 0.52f)
      {
        replacement = regional > 0.12f ? granite : diorite;
      }
      else if (patch < -0.60f)
      {
        replacement = andesite;
      }
      else if (patch < -0.47f && regional < -0.20f)
      {
        replacement = tuff;
      }
    }

    if (replacement == ctx.Blocks.Stone)
    {
      continue;
    }
    ctx.World.SetBlock(pos, replacement);
    min_dirty = std::min(min_dirty, y);
    max_dirty = std::max(max_dirty, y);
    changed = true;
  }
  if (changed)
  {
    ctx.AccumulateDirtyColumn(min_dirty, max_dirty);
  }
  const bool geode = TryGenerateGeode(ctx, x, z, seed, granite, diorite,
                                      andesite, deepslate, tuff, smooth_basalt,
                                      calcite, amethyst);
  return changed || geode;
}

bool FillAquiferColumn(WorldGenContext &ctx, int x, int z, int surfaceY,
                       const UAquiferSampler &sampler)
{
  const bool water_available =
      ctx.Settings.FillWater && ctx.Blocks.Water != BLOCK_AIR;
  const bool lava_available =
      ctx.Settings.FillLava && ctx.Blocks.Lava != BLOCK_AIR;
  if ((!water_available && !lava_available) || surfaceY < 4)
  {
    return false;
  }

  const int scan_top = std::min(surfaceY - 1, ctx.Settings.MaxHeight - 1);
  bool changed = false;
  for (int y = ctx.Settings.MinHeight; y <= scan_top; ++y)
  {
    const glm::ivec3 pos(x, y, z);
    if (ctx.World.GetBlock(pos) != BLOCK_AIR)
    {
      continue;
    }

    const AquiferSample sample = sampler.SampleAt(x, y, z);
    if (sample.Fluid == AquiferFluid::Barrier && ctx.Blocks.Stone != BLOCK_AIR)
    {
      ctx.World.SetBlock(pos, ctx.Blocks.Stone);
      ctx.World.SetFluidState(pos, FluidCellState{});
      changed = true;
      continue;
    }

    BlockId fluid_block = BLOCK_AIR;
    FluidKind fluid_kind = FluidKind::None;
    if (sample.Fluid == AquiferFluid::Water && water_available)
    {
      fluid_block = ctx.Blocks.Water;
      fluid_kind = FluidKind::Water;
    }
    else if (sample.Fluid == AquiferFluid::Lava && lava_available)
    {
      fluid_block = ctx.Blocks.Lava;
      fluid_kind = FluidKind::Lava;
    }
    if (fluid_block == BLOCK_AIR)
    {
      continue;
    }
    ctx.World.SetBlock(pos, fluid_block);
    ctx.World.SetFluidState(pos, FluidCellState::Source().WithKind(fluid_kind));
    changed = true;
  }
  if (changed)
  {
    ctx.AccumulateDirtyColumn(ctx.Settings.MinHeight, scan_top);
  }
  return changed;
}

void FillFluidColumn(WorldGenContext &ctx, int x, int z, int surfaceY)
{
  if (!ctx.Settings.FillWater || ctx.Blocks.Water == BLOCK_AIR)
  {
    return;
  }
  const int sea = ctx.Settings.SeaLevel;
  const int maxScanY = std::max(ctx.Settings.MinHeight + 1,
                                ctx.Settings.MaxHeight - 1);
  const int topSolid =
      FindTopSolidSurfaceY(ctx.World, ctx.Registry, x, z, maxScanY,
                           ctx.Settings.MinHeight);
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

bool SealFluidPocketsInChunk(WorldGenContext &ctx, int base_x, int base_z,
                             int *out_voxels_filled)
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
  // W5.4 (water audit A-2): deeper pass count — air chains longer than 8
  // cells from any water stayed unfilled (TestSealChain9). The seal runs
  // off the hot path (worker scratch once + budgeted deferred drains).
  options.max_passes = 16;
  const std::optional<UAquiferSampler> aquifer_sampler =
      MakeAquiferSealSampler(ctx);
  if (aquifer_sampler)
  {
    options.allow_air_fill = [&aquifer_sampler, sea](glm::ivec3 pos)
    { return AllowsAquiferWater(&*aquifer_sampler, pos, sea); };
  }
  // W5.2 (water audit A-3): rivers (up to sea+24) and lakes sit above sea
  // with no seal at all — a cave piercing their floor left hanging water
  // over air. Extend the flood ceiling to the chunk's highest fluid.
  const int seal_top =
      std::max(sea, HighestFluidTopInChunk(ctx, base_x, base_z));
  const int filled = UFluidSpreadSystem::FloodWetPocketsInBox(
      ctx.World, *definitions,
      glm::ivec3(base_x, ctx.Settings.MinHeight, base_z),
      glm::ivec3(base_x + CHUNK_SIZE - 1, seal_top, base_z + CHUNK_SIZE - 1),
      options);
  if (out_voxels_filled)
  {
    *out_voxels_filled = filled;
  }
  if (filled > 0)
  {
    ctx.AccumulateDirtyColumn(ctx.Settings.MinHeight, seal_top);
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

/// Water above or beside the cell (water only below it is a legitimate pool
/// surface inside a cave).
bool AirTouchesLiquidSideOrAbove(const WorldGenContext &ctx, glm::ivec3 pos)
{
  static constexpr std::array<glm::ivec3, 5> kDirs = {
      glm::ivec3(0, 1, 0), glm::ivec3(-1, 0, 0), glm::ivec3(1, 0, 0),
      glm::ivec3(0, 0, -1), glm::ivec3(0, 0, 1)};
  for (const glm::ivec3 &offset : kDirs)
  {
    const BlockId id = ctx.World.GetBlock(pos + offset);
    if (id != BLOCK_AIR && ctx.Registry.IsLiquid(id))
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
  const std::optional<UAquiferSampler> aquifer_sampler =
      MakeAquiferSealSampler(ctx);
  // W5.2 (A-3): shore air sealing extends to above-sea rivers/lakes, not
  // just the ocean band.
  const int seal_top =
      std::max(sea, HighestFluidTopInChunk(ctx, base_x, base_z));
  bool any = false;
  // 8 full-volume passes were 200–470ms on main (CB hole spikes). Two passes
  // close the common shore strip; remaining edges catch up on later frames.
  for (int pass = 0; pass < 2; ++pass)
  {
    bool changed = false;
    for (int y = ctx.Settings.MinHeight; y <= seal_top; ++y)
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
          if (!AllowsAquiferWater(aquifer_sampler ? &*aquifer_sampler : nullptr,
                                  pos, sea))
          {
            // A dry aquifer pocket must not border water directly: that shows
            // as a translucent water wall/ceiling from inside the sea. Seal
            // the interface with a barrier (as the aquifer sampler's own
            // Barrier class does) instead of leaving a bare water face.
            if (ctx.Blocks.Stone != BLOCK_AIR && AirTouchesLiquidSideOrAbove(ctx, pos))
            {
              ctx.World.SetBlock(pos, ctx.Blocks.Stone);
              ctx.World.SetFluidState(pos, FluidCellState{});
              changed = true;
              any = true;
            }
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
    ctx.AccumulateDirtyColumn(ctx.Settings.MinHeight, seal_top);
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

int FindWaterColumnFloorY(const WorldGenContext &ctx, int x, int z, int sea,
                          FluidPermeableSealStats *stats)
{
  // The former bottom-up scan retained the highest valid ground. Returning the
  // first valid ground from the top selects exactly the same floor and avoids
  // rescanning every lower candidate. Neither predicate changes the world.
  for (int y = sea - 1; y >= ctx.Settings.MinHeight; --y)
  {
    if (stats)
    {
      ++stats->floorGroundChecks;
    }
    const glm::ivec3 ground(x, y, z);
    if (!IsSolidPlantGround(ctx.World, ctx.Registry, ground))
    {
      continue;
    }
    for (int wy = y + 1; wy <= sea; ++wy)
    {
      if (stats)
      {
        ++stats->floorSlotChecks;
      }
      const BlockId above_id = ctx.World.GetBlock(glm::ivec3(x, wy, z));
      if (!IsWaterColumnSlotBlock(ctx, ctx.Registry, above_id, wy, y))
      {
        // Here wy > y. The same cell also fails for every lower floor: the
        // dirt exception remains true and every other accepted type does not
        // depend on floor_y. Therefore no lower candidate can become valid.
        return ctx.Settings.MinHeight - 1;
      }
    }
    return y;
  }
  return ctx.Settings.MinHeight - 1;
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
    if (pos.y <= sea && ctx.Registry.GetTypeNameById(id) == "lily_pad")
    {
      // A floating pad only lives on the surface. When the column is restored
      // to sea level the pad is submerged: turn the cell into plain water
      // instead of a waterlogged plant (a water cube with a pad inside).
      ctx.World.SetBlock(pos, water);
      ctx.World.SetFluidState(pos, FluidCellState::Source().WithKind(FluidKind::Water));
      return true;
    }
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
                                   int floor_y, int sea,
                                   FluidPermeableSealStats *stats)
{
  if (floor_y < ctx.Settings.MinHeight || floor_y >= sea)
  {
    return false;
  }
  // Never waterlog above the sea surface: floating decor (lily pads) and plant
  // tops sit in the air cell over the water; flooding them draws a water cube
  // standing on the surface with the plant inside.
  const int max_y = std::min(ctx.Settings.MaxHeight - 1, sea);
  bool changed = false;
  for (int y = floor_y + 1; y <= max_y; ++y)
  {
    if (stats)
    {
      ++stats->waterlogCells;
    }
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

bool RestoreWaterColumnAt(WorldGenContext &ctx, int x, int z, int sea,
                          FluidPermeableSealStats *stats)
{
  const auto floorStart = std::chrono::steady_clock::now();
  const int floor_y = FindWaterColumnFloorY(ctx, x, z, sea, stats);
  if (stats)
  {
    stats->floorScanMs += std::chrono::duration<double, std::milli>(
                              std::chrono::steady_clock::now() - floorStart)
                              .count();
  }
  if (floor_y < ctx.Settings.MinHeight || floor_y >= sea)
  {
    return false;
  }
  const auto restoreStart = std::chrono::steady_clock::now();
  bool changed = false;
  for (int y = floor_y + 1; y <= sea; ++y)
  {
    if (stats)
    {
      ++stats->restoreCells;
    }
    if (RestoreWaterColumnCell(ctx, glm::ivec3(x, y, z), floor_y, sea))
    {
      changed = true;
    }
  }
  if (stats)
  {
    stats->restoreMs += std::chrono::duration<double, std::milli>(
                            std::chrono::steady_clock::now() - restoreStart)
                            .count();
  }
  const auto waterlogStart = std::chrono::steady_clock::now();
  if (WaterlogCoastalPermeableStack(ctx, x, z, floor_y, sea, stats))
  {
    changed = true;
  }
  if (stats)
  {
    stats->waterlogMs += std::chrono::duration<double, std::milli>(
                             std::chrono::steady_clock::now() - waterlogStart)
                             .count();
  }
  return changed;
}

} // namespace

bool SealFluidPermeableDecorInChunk(WorldGenContext &ctx, int base_x, int base_z,
                                    int *out_columns_changed,
                                    FluidPermeableSealStats *out_stats)
{
  if (!ctx.Settings.FillWater || ctx.Blocks.Water == BLOCK_AIR)
  {
    return false;
  }
  FluidPermeableSealStats stats;
  const int sea = ctx.Settings.SeaLevel;
  bool any_filled = false;
  int columns_changed = 0;
  for (int lz = 0; lz < CHUNK_SIZE; ++lz)
  {
    for (int lx = 0; lx < CHUNK_SIZE; ++lx)
    {
      if (RestoreWaterColumnAt(ctx, base_x + lx, base_z + lz, sea,
                               out_stats ? &stats : nullptr))
      {
        any_filled = true;
        ++columns_changed;
      }
    }
  }
  if (out_stats)
  {
    *out_stats = stats;
  }
  if (out_columns_changed)
  {
    *out_columns_changed = columns_changed;
  }
  if (any_filled)
  {
    ctx.AccumulateDirtyColumn(ctx.Settings.MinHeight, sea + 8);
  }
  return any_filled;
}

namespace
{

/// Removes single-cell water spikes: a surface water cell whose horizontal
/// neighbours (at least three) are air above lower water. River columns derive
/// their own surface from local terrain, so an isolated column can end up one
/// block higher than the whole surrounding body and reads as a water cube
/// standing on the water.
bool TrimWaterSurfaceSpikesInChunk(WorldGenContext &ctx, int base_x, int base_z)
{
  const BlockId water = ctx.Blocks.Water;
  const int top_y = std::min(ctx.Settings.MaxHeight - 1, ctx.Settings.SeaLevel + 1);
  static constexpr glm::ivec3 kHoriz[4] = {
      {1, 0, 0}, {-1, 0, 0}, {0, 0, 1}, {0, 0, -1}};
  bool changed = false;
  for (int lz = 0; lz < CHUNK_SIZE; ++lz)
  {
    for (int lx = 0; lx < CHUNK_SIZE; ++lx)
    {
      const int x = base_x + lx;
      const int z = base_z + lz;
      int surface_y = ctx.Settings.MinHeight - 1;
      for (int y = top_y; y >= ctx.Settings.MinHeight; --y)
      {
        const BlockId id = ctx.World.GetBlock(glm::ivec3(x, y, z));
        if (id == BLOCK_AIR)
        {
          continue;
        }
        if (id == water)
        {
          surface_y = y;
        }
        break;
      }
      if (surface_y < ctx.Settings.MinHeight)
      {
        continue;
      }
      const glm::ivec3 pos(x, surface_y, z);
      if (PackFluidCellState(ctx.World.GetFluidState(pos)) == 0 ||
          !ctx.World.IsAir(pos + glm::ivec3(0, 1, 0)))
      {
        continue;
      }
      int lower_neighbours = 0;
      int overtopped_banks = 0;
      for (const glm::ivec3 &offset : kHoriz)
      {
        const glm::ivec3 n = pos + offset;
        if (!ctx.World.IsAir(n))
        {
          continue;
        }
        const BlockId below_n = ctx.World.GetBlock(n - glm::ivec3(0, 1, 0));
        if (below_n == water)
        {
          ++lower_neighbours;
        }
        else if (below_n != BLOCK_AIR && !ctx.Registry.IsLiquid(below_n) &&
                 ctx.Registry.BlocksMovement(below_n))
        {
          ++overtopped_banks;
        }
      }
      // River/lake water below sea level standing one block above the
      // neighbouring bank is a free-standing water wall: lower it to the bank.
      if (lower_neighbours >= 3 ||
          (overtopped_banks >= 1 && surface_y <= ctx.Settings.SeaLevel - 2))
      {
        ctx.World.SetBlock(pos, BLOCK_AIR);
        ctx.World.SetFluidState(pos, FluidCellState{});
        changed = true;
      }
    }
  }
  if (changed)
  {
    ctx.AccumulateDirtyColumn(ctx.Settings.MinHeight, top_y);
  }
  return changed;
}

/// Floating surface decor (lily pads) is not fluid permeable and lives only on
/// the water surface (water directly below, nothing but air above). A pad that ends up with water above it (the shore/pocket
/// seals fill air above sea-level basins) would sit in the water body as an
/// isolated pocket with water walls around it: turn that cell into water.
bool RemoveSubmergedFloatingDecorInChunk(WorldGenContext &ctx, int base_x,
                                         int base_z)
{
  const BlockId water = ctx.Blocks.Water;
  const BlockId pad = ctx.Registry.GetIdByTypeName("lily_pad");
  if (pad == BLOCK_AIR || water == BLOCK_AIR)
  {
    return false;
  }
  const int top_y = std::min(ctx.Settings.MaxHeight - 1, ctx.Settings.SeaLevel + 2);
  bool changed = false;
  // One column of margin: a pad prefab spilling over a chunk border is written
  // into the neighbouring column after that column's own pass ran.
  for (int lz = -1; lz <= CHUNK_SIZE; ++lz)
  {
    for (int lx = -1; lx <= CHUNK_SIZE; ++lx)
    {
      const int x = base_x + lx;
      const int z = base_z + lz;
      // Walk the open stack (air / pad / water) down to the first solid cell.
      for (int y = top_y; y >= ctx.Settings.MinHeight; --y)
      {
        const glm::ivec3 pos(x, y, z);
        const BlockId id = ctx.World.GetBlock(pos);
        if (id == pad)
        {
          // The two-pad prefab places its second pad blindly: a pad without
          // water directly below would float over air or sit on land.
          if (!ctx.Registry.IsLiquid(ctx.World.GetBlock(pos - glm::ivec3(0, 1, 0))))
          {
            ctx.World.SetBlock(pos, BLOCK_AIR);
            ctx.World.SetFluidState(pos, FluidCellState{});
            changed = true;
            continue;
          }
          if (ctx.Registry.IsLiquid(ctx.World.GetBlock(pos + glm::ivec3(0, 1, 0))))
          {
            ctx.World.SetBlock(pos, water);
            ctx.World.SetFluidState(
                pos, FluidCellState::Source().WithKind(FluidKind::Water));
            changed = true;
          }
          continue;
        }
        if (id != BLOCK_AIR && id != water)
        {
          break;
        }
      }
    }
  }
  if (changed)
  {
    ctx.AccumulateDirtyColumn(ctx.Settings.MinHeight, top_y);
  }
  return changed;
}

} // namespace

bool SealFluidShoreOnChunkCommitted(UBlockWorld &world, UBlockRegistry &registry,
                                    const ProceduralSettings &settings,
                                    const std::string &worldgen_owner_pack_id,
                                    glm::ivec3 chunk_coord,
                                    bool include_shore_air)
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
  bool shore_air = false;
  if (include_shore_air)
  {
    shore_air = SealShoreAirAdjacencyInChunk(ctx, base_x, base_z);
  }
  const bool trimmed = TrimWaterSurfaceSpikesInChunk(ctx, base_x, base_z);
  const bool pads_fixed = RemoveSubmergedFloatingDecorInChunk(ctx, base_x, base_z);
  return sealed || permeable || sealed_after_permeable || shore_air || trimmed ||
         pads_fixed;
}

bool SealFluidShoreAirOnChunkCommitted(
    UBlockWorld &world, UBlockRegistry &registry,
    const ProceduralSettings &settings, const std::string &worldgen_owner_pack_id,
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
  return SealShoreAirAdjacencyInChunk(ctx, base_x, base_z);
}

int PruneFloatingVegetationInChunk(WorldGenContext &ctx, int base_x, int base_z)
{
  const int max_y = ctx.Settings.MaxHeight - 1;
  // Diagnostics hook (CUBA_DEBUG_PRUNE=1): report how the worldgen thread
  // actually sees flora physics, to compare against pack JSON and fixture
  // registries. Limited to the first three populated chunks per process.
  static const bool dbg_prune = std::getenv("CUBA_DEBUG_PRUNE") != nullptr;
  static std::atomic<int> dbg_chunks_reported{0};
  const int dbg_index = dbg_prune ? dbg_chunks_reported.fetch_add(1) : -1;
  if (dbg_prune && dbg_index < 3)
  {
    const auto report = [&](const char *type_name)
    {
      const BlockId id = ctx.Registry.GetIdByTypeName(type_name);
      if (id == BLOCK_AIR)
      {
        CubatariumLogInfo("PruneDbg",
                          "chunk#" + std::to_string(dbg_index) + " (" +
                              std::to_string(base_x) + ',' +
                              std::to_string(base_z) + ") " + type_name +
                              ": not resolved");
        return;
      }
      CubatariumLogInfo(
          "PruneDbg",
          "chunk#" + std::to_string(dbg_index) + " (" +
              std::to_string(base_x) + ',' + std::to_string(base_z) + ") " +
              type_name + " id=" + std::to_string(id) + " occupancy=" +
              std::to_string(ctx.Registry.Physics(id).Movement.Occupancy) +
              " blocksMovement=" +
              std::to_string(ctx.Registry.BlocksMovement(id) ? 1 : 0) +
              " removable=" +
              std::to_string(IsRemovableFloatingPlantBlock(ctx.Registry, id)
                                 ? 1
                                 : 0) +
              " hasDefinitions=" +
              std::to_string(ctx.Registry.GetDefinitions() != nullptr ? 1 : 0) +
              " pack=" + ctx.WorldgenOwnerPackId);
    };
    report("tree_leaves");
    report("mtg_leaves");
    report("leaves_spruce");
  }
  int removed = 0;
  for (int lz = 0; lz < CHUNK_SIZE; ++lz)
  {
    for (int lx = 0; lx < CHUNK_SIZE; ++lx)
    {
      removed += PruneFloatingPlantsInColumn(ctx.World, ctx.Registry, base_x + lx,
                                             base_z + lz, max_y);
    }
  }
  if (dbg_prune && dbg_index < 3)
  {
    CubatariumLogInfo("PruneDbg",
                      "chunk#" + std::to_string(dbg_index) + " (" +
                          std::to_string(base_x) + ',' +
                          std::to_string(base_z) + ") pruned=" +
                          std::to_string(removed));
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
  return std::clamp(surfaceY, settings.MinHeight + 1, settings.MaxHeight);
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

  for (int y = ctx.Settings.MinHeight; y <= surfaceY; ++y)
  {
    BlockId Id = stone;
    if (y <= ctx.Settings.BedrockTopY)
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
  ctx.AccumulateDirtyColumn(ctx.Settings.MinHeight, surfaceY);
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
  ctx.AccumulateDirtyColumn(ctx.Settings.MinHeight, surfaceY);
}

} // namespace cutum
