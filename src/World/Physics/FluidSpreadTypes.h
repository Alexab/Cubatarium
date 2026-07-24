#pragma once

#include "World/Math/BlockTypes.h"
#include "World/Math/FluidCellState.h"
#include <glm/glm.hpp>
#include <cstdint>
#include <vector>

namespace cutum
{

struct FluidSpreadChange
{
  glm::ivec3 BlockPos{0};
  glm::ivec3 NeighborPos{0};
  BlockId FluidId{BLOCK_AIR};
  FluidCellState NewState{};
  bool RemovedFluid{false};
};

struct FluidSpreadStats
{
  uint64_t Candidates{0};
  uint64_t Applied{0};
  std::vector<FluidSpreadChange> Changes;
};

} // namespace cutum
