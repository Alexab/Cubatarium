#ifndef BLOCKWORLD_H
#define BLOCKWORLD_H

#include "World/Chunks/ChunkManager.h"
#include "World/Math/BlockTypes.h"
#include "World/Math/FluidCellState.h"
#include <functional>
#include <glm/glm.hpp>

namespace cutum
{

class UBlockDefinitionStorage;

class UBlockWorld
{
public:
  void SetFluidDefinitions(const UBlockDefinitionStorage *definitions)
  {
    FluidDefinitions = definitions;
  }

  BlockId GetBlock(glm::ivec3 pos) const;
  FluidCellState GetFluidState(glm::ivec3 pos) const;
  void SetBlock(glm::ivec3 pos, BlockId Id);
  void SetFluidState(glm::ivec3 pos, FluidCellState state);
  void ClearFluidState(glm::ivec3 pos);
  bool IsAir(glm::ivec3 pos) const;
  void Clear();
  size_t CountNonAir() const;
  void ForEachBlock(const std::function<void(glm::ivec3, BlockId)> &fn) const;

  UChunkManager &GetChunkManager() { return Chunks; }
  const UChunkManager &GetChunkManager() const { return Chunks; }

private:
  UChunkManager Chunks;
  const UBlockDefinitionStorage *FluidDefinitions{nullptr};
};

} // namespace cutum

#endif
