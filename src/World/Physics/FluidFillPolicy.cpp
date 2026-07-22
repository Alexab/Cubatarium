#include "World/Physics/FluidFillPolicy.h"

#include "Blocks/BlockDefinitionStorage.h"
#include "World/Chunks/ChunkManager.h"
#include "World/Core/BlockWorld.h"
#include "World/Math/FluidCellState.h"
#include "World/Physics/FluidKindPresetUtil.h"
#include "World/Physics/FluidPermeabilityUtil.h"

namespace cutum
{

namespace
{

bool IsLiquidId(const UBlockDefinitionStorage &definitions, BlockId id)
{
  if (const BlockDefinition *def = definitions.GetById(id))
  {
    return def->Physics.IsLiquid;
  }
  return false;
}

bool IsFluidPermeableId(const UBlockDefinitionStorage &definitions, BlockId id)
{
  return IsFluidPermeableFromDefinition(id, definitions.GetById(id),
                                        IsLiquidId(definitions, id));
}

bool IsWaterKind(const UBlockDefinitionStorage &definitions, BlockId id)
{
  return IsWaterFluidDefinition(definitions.GetById(id));
}

FluidKind FluidKindFromBlockId(const UBlockDefinitionStorage &definitions,
                               BlockId id)
{
  return FluidKindFromDefinition(definitions.GetById(id));
}

} // namespace

FluidCellState UFluidFillPolicy::NormalizeFluidKind(
    const UBlockDefinitionStorage &definitions, BlockId fluid_id,
    FluidCellState state)
{
  if (state.HasExplicitKind())
  {
    return state;
  }
  const FluidKind kind = FluidKindFromBlockId(definitions, fluid_id);
  if (kind == FluidKind::None)
  {
    return state;
  }
  return state.WithKind(kind);
}

bool UFluidFillPolicy::CanReceiveFluid(const UBlockWorld &blockWorld,
                                       const UBlockDefinitionStorage &definitions,
                                       glm::ivec3 pos)
{
  // Missing chunks read as AIR — without this guard fluid GetOrCreateChunk'd
  // unbounded columns outside the streamer (manual 204426: 300MB→13GB after
  // water+lava spread).
  if (!blockWorld.GetChunkManager().HasChunk(
          UChunkManager::WorldToChunk(pos)))
  {
    return false;
  }
  if (blockWorld.IsAir(pos))
  {
    return true;
  }
  const BlockId id = blockWorld.GetBlock(pos);
  if (IsFluidPermeableId(definitions, id))
  {
    return true;
  }
  if (const BlockDefinition *def = definitions.GetById(id))
  {
    return def->Physics.Floodable && !def->Physics.IsLiquid;
  }
  return false;
}

bool UFluidFillPolicy::ShouldReplaceBlockWithFluid(
    const UBlockWorld &blockWorld, const UBlockDefinitionStorage &definitions,
    glm::ivec3 pos)
{
  if (blockWorld.IsAir(pos))
  {
    return true;
  }
  const BlockId id = blockWorld.GetBlock(pos);
  if (IsFluidPermeableId(definitions, id))
  {
    return false;
  }
  if (const BlockDefinition *def = definitions.GetById(id))
  {
    return def->Physics.Floodable && !def->Physics.IsLiquid;
  }
  return false;
}

FluidCellState UFluidFillPolicy::StoredFluidStateForCell(
    const UBlockWorld &blockWorld, const UBlockDefinitionStorage &definitions,
    glm::ivec3 pos, FluidCellState state)
{
  if (ShouldReplaceBlockWithFluid(blockWorld, definitions, pos))
  {
    return state;
  }
  const BlockId id = blockWorld.GetBlock(pos);
  if (IsFluidPermeableId(definitions, id) && state.IsSource())
  {
    FluidCellState flowing = FluidCellState::Flowing(1);
    if (state.HasExplicitKind())
    {
      flowing.SetKind(state.GetKind());
    }
    return flowing;
  }
  return state;
}

void UFluidFillPolicy::ApplyFluidFill(UBlockWorld &blockWorld,
                                      const UBlockDefinitionStorage &definitions,
                                      glm::ivec3 pos, BlockId fluid_id,
                                      FluidCellState state)
{
  if (!blockWorld.GetChunkManager().HasChunk(
          UChunkManager::WorldToChunk(pos)))
  {
    return;
  }
  const FluidCellState with_kind =
      NormalizeFluidKind(definitions, fluid_id, state);
  const FluidCellState stored =
      StoredFluidStateForCell(blockWorld, definitions, pos, with_kind);
  if (ShouldReplaceBlockWithFluid(blockWorld, definitions, pos))
  {
    blockWorld.SetBlock(pos, fluid_id);
  }
  blockWorld.SetFluidState(pos, stored);
}

} // namespace cutum
