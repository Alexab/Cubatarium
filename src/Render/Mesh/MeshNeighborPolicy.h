#pragma once

#include "World/Math/BlockTypes.h"

namespace cutum
{

/// Luanti CONTENT_IGNORE parity: skip face emit when neighbor chunk unloaded.
enum class NeighborLoadState : uint8_t
{
  Loaded = 0,
  Air = 1,
  Unknown = 2,
};

inline bool ShouldSkipFaceForNeighbor(NeighborLoadState state)
{
  return state == NeighborLoadState::Unknown;
}

inline NeighborLoadState ClassifyShellCell(bool neighbor_chunk_loaded, BlockId block)
{
  if (!neighbor_chunk_loaded)
  {
    return NeighborLoadState::Unknown;
  }
  if (block == BLOCK_AIR)
  {
    return NeighborLoadState::Air;
  }
  return NeighborLoadState::Loaded;
}

} // namespace cutum
