#include "World/Core/WorldFluidFacade.h"
#include <unordered_set>

#include "Blocks/BlockRegistry.h"
#include "ResourcePacks/BlockNameUtil.h"
#include "World/Chunks/ChunkManager.h"
#include "World/Core/World.h"
#include "World/Math/GridMath.h"
#include "World/Physics/FluidReflowScan.h"
#include "World/Physics/FluidSpreadSystem.h"
#include "World/Physics/WorldBlockPhysicsService.h"
#include "WorldGen/Core/ProceduralSettings.h"

namespace cutum
{

namespace
{

BlockId ResolveWaterBlockId(const UBlockRegistry &registry,
                            const std::string &worldgen_owner_pack_id)
{
  BlockId water_id = registry.GetIdByTypeName("water");
  if (water_id == BLOCK_AIR && !worldgen_owner_pack_id.empty())
  {
    water_id = registry.GetIdByTypeName(
        MakeQualifiedBlockName(worldgen_owner_pack_id, "water"));
  }
  return water_id;
}

FluidFloodOptions MakeBreakSiteFloodOptions(
    const UBlockRegistry &registry, const ProceduralSettings &settings,
    const std::string &worldgen_owner_pack_id, glm::ivec3 break_pos)
{
  FluidFloodOptions options;
  options.water_id = ResolveWaterBlockId(registry, worldgen_owner_pack_id);
  options.source_for_air = false;
  if (settings.FillWater && options.water_id != BLOCK_AIR)
  {
    options.sea_level = settings.SeaLevel;
    if (break_pos.y <= settings.SeaLevel)
    {
      options.fluid_id = options.water_id;
    }
  }
  return options;
}

} // namespace

bool UWorldFluidFacade::TryAddFluidObject(UWorld &world, glm::ivec3 block_pos,
                                          BlockId liquid_id)
{
  UBlockRegistry &registry = world.GetBlockRegistry();
  if (!registry.IsLiquid(liquid_id))
  {
    return false;
  }
  const UBlockDefinitionStorage *definitions = registry.GetDefinitions();
  if (definitions == nullptr)
  {
    return false;
  }
  UBlockWorld &block_world = world.GetBlockWorld();
  if (!UFluidSpreadSystem::CanReceiveFluid(block_world, registry, block_pos))
  {
    return false;
  }

  const FluidKind kind =
      UFluidSpreadSystem::FluidKindFromBlockId(*definitions, liquid_id);
  const FluidCellState place_state = FluidCellState::Source().WithKind(kind);
  UFluidSpreadSystem::ApplyFluidFill(block_world, *definitions, block_pos,
                                     liquid_id, place_state);
  ++world.CachedBlockCount;
  world.BlockWorldReady = true;
  world.MarkBlockChunkDirty(block_pos);
  world.PublishBlockPhysicsEvent(block_pos);
  world.PublishNeighborPhysicsEvents(block_pos);
  if (world.GetPhysicsFeatureFlags().EnableFluids)
  {
    EnqueueFluidFrontierAt(world, block_pos);
  }
  return true;
}

void UWorldFluidFacade::ApplyBreakSiteFluidFlood(
    UWorld &world, glm::ivec3 block_pos,
    std::vector<glm::ivec3> &mesh_touch_blocks)
{
  UBlockRegistry &registry = world.GetBlockRegistry();
  if (!world.GetPhysicsFeatureFlags().EnableFluids)
  {
    return;
  }
  const UBlockDefinitionStorage *definitions = registry.GetDefinitions();
  if (definitions != nullptr)
  {
    const FluidFloodOptions flood_options =
        MakeBreakSiteFloodOptions(registry, world.GetProceduralSettings(),
                                  world.GetWorldgenOwnerPackId(), block_pos);
    std::vector<glm::ivec3> flood_changed;
    UFluidSpreadSystem::FloodBreakSiteFromWetNeighbors(
        world.GetBlockWorld(), *definitions, block_pos, flood_options,
        &flood_changed);
    mesh_touch_blocks.insert(mesh_touch_blocks.end(), flood_changed.begin(),
                             flood_changed.end());
  }
  EnqueueFluidFrontierAt(world, block_pos);
  UBlockWorld &block_world = world.GetBlockWorld();
  if (block_world.IsAir(block_pos) && world.BlockPhysicsService)
  {
    world.TryEnqueueFluidAt(block_pos);
  }
  for (const glm::ivec3 &offset : NEIGHBOR_OFFSETS)
  {
    const glm::ivec3 neighbor = block_pos + offset;
    if (registry.IsLiquid(block_world.GetBlock(neighbor)))
    {
      mesh_touch_blocks.push_back(neighbor);
    }
  }
}

void UWorldFluidFacade::MarkFluidRegionDirty(UWorld &world, glm::ivec3 center,
                                             int block_radius)
{
  const int radius = std::max(0, block_radius);
  if (!world.IsWithinLiquidUpdateRadius(center))
  {
    world.MarkBlockChunkDirtyFromPhysics(center);
    return;
  }

  std::unordered_set<glm::ivec3, IVec3Hash> chunk_coords;
  const auto add_block = [&](glm::ivec3 block_pos)
  {
    chunk_coords.insert(UChunkManager::WorldToChunk(block_pos));
    for (const glm::ivec3 &offset : NEIGHBOR_OFFSETS)
    {
      chunk_coords.insert(UChunkManager::WorldToChunk(block_pos + offset));
    }
  };

  for (int dx = -radius; dx <= radius; ++dx)
  {
    for (int dy = -radius; dy <= radius; ++dy)
    {
      for (int dz = -radius; dz <= radius; ++dz)
      {
        add_block(center + glm::ivec3(dx, dy, dz));
      }
    }
  }

  world.ModifiedChunks.insert(chunk_coords.begin(), chunk_coords.end());
  const bool immediate = world.BlockRegistry != nullptr;
  for (const glm::ivec3 &chunk_coord : chunk_coords)
  {
    if (immediate)
    {
      world.MeshService->RebuildChunkImmediate(
          world.BlockWorld, *world.BlockRegistry, chunk_coord);
    }
    else
    {
      world.MeshService->MarkDirty(chunk_coord);
    }
  }
}

void UWorldFluidFacade::MarkFluidFloodMeshDirty(
    UWorld &world, glm::ivec3 block_pos,
    const std::vector<glm::ivec3> &filled_blocks)
{
  if (filled_blocks.empty() || !world.BlockRegistry)
  {
    return;
  }
  std::unordered_set<glm::ivec3, IVec3Hash> chunk_coords;
  const auto add_block = [&](glm::ivec3 pos)
  {
    chunk_coords.insert(UChunkManager::WorldToChunk(pos));
    for (const glm::ivec3 &offset : NEIGHBOR_OFFSETS)
    {
      chunk_coords.insert(UChunkManager::WorldToChunk(pos + offset));
    }
  };
  add_block(block_pos);
  for (const glm::ivec3 &pos : filled_blocks)
  {
    add_block(pos);
  }
  world.ModifiedChunks.insert(chunk_coords.begin(), chunk_coords.end());
  const glm::ivec3 center_chunk = UChunkManager::WorldToChunk(block_pos);
  const bool immediate = world.IsWithinLiquidUpdateRadius(block_pos);
  for (const glm::ivec3 &chunk_coord : chunk_coords)
  {
    if (immediate)
    {
      const int dist = std::max({std::abs(chunk_coord.x - center_chunk.x),
                                 std::abs(chunk_coord.y - center_chunk.y),
                                 std::abs(chunk_coord.z - center_chunk.z)});
      if (dist <= 1)
      {
        world.MeshService->RebuildChunkImmediate(
            world.BlockWorld, *world.BlockRegistry, chunk_coord);
        continue;
      }
    }
    world.MeshService->MarkDirty(chunk_coord);
  }
}

} // namespace cutum
