#include "World/Core/WorldFluidFacade.h"

#include "ResourcePacks/BlockNameUtil.h"
#include "World/Core/World.h"
#include "World/Physics/FluidReflowScan.h"
#include "World/Physics/FluidSpreadSystem.h"
#include "World/Physics/WorldBlockPhysicsService.h"
#include "WorldGen/Core/ProceduralSettings.h"
#include "World/Math/GridMath.h"

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

FluidFloodOptions MakeBreakSiteFloodOptions(const UBlockRegistry &registry,
                                            const ProceduralSettings &settings,
                                            const std::string &worldgen_owner_pack_id,
                                            glm::ivec3 break_pos)
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
    const FluidFloodOptions flood_options = MakeBreakSiteFloodOptions(
        registry, world.GetProceduralSettings(), world.GetWorldgenOwnerPackId(),
        block_pos);
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

} // namespace cutum
