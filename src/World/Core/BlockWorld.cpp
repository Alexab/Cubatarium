#include "World/Core/BlockWorld.h"
#include "Blocks/BlockDefinitionStorage.h"
#include "World/Math/FluidCellState.h"
#include "World/Physics/FluidKindPresetUtil.h"

namespace cutum
{

BlockId UBlockWorld::GetBlock(glm::ivec3 pos) const
{
  return Chunks.GetBlock(pos);
}

FluidCellState UBlockWorld::GetFluidState(glm::ivec3 pos) const
{
  return Chunks.GetFluidState(pos);
}

void UBlockWorld::SetBlock(glm::ivec3 pos, BlockId Id)
{
  if (Id == BLOCK_AIR)
  {
    Chunks.SetBlock(pos, BLOCK_AIR);
    Chunks.ClearFluidState(pos);
    return;
  }
  Chunks.SetBlock(pos, Id);
  if (FluidDefinitions != nullptr)
  {
    if (const BlockDefinition *def = FluidDefinitions->GetById(Id))
    {
      if (def->Physics.IsLiquid)
      {
        const FluidKind kind = FluidKindFromDefinition(def);
        Chunks.SetFluidState(pos, FluidCellState::Source().WithKind(kind));
      }
    }
  }
}

void UBlockWorld::SetFluidState(glm::ivec3 pos, FluidCellState state)
{
  Chunks.SetFluidState(pos, state);
}

void UBlockWorld::ClearFluidState(glm::ivec3 pos)
{
  Chunks.ClearFluidState(pos);
}

bool UBlockWorld::IsAir(glm::ivec3 pos) const
{
  return GetBlock(pos) == BLOCK_AIR;
}

void UBlockWorld::Clear() { Chunks.Clear(); }

size_t UBlockWorld::CountNonAir() const
{
  size_t count = 0;
  ForEachBlock([&count](glm::ivec3, BlockId) { ++count; });
  return count;
}

void UBlockWorld::ForEachBlock(
    const std::function<void(glm::ivec3, BlockId)> &fn) const
{
  Chunks.ForEachBlock(fn);
}

} // namespace cutum
