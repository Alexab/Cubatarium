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

/// Era39: neighbor_visually_drawable=false (SoftDefer empty / Held / lit-gate
/// hide) ⇒ treat as Air so ready chunks emit boundary faces (not Unknown).
inline NeighborLoadState ClassifyShellCell(bool neighbor_chunk_loaded,
                                           BlockId block,
                                           bool neighbor_visually_drawable = true)
{
  if (!neighbor_chunk_loaded)
  {
    return NeighborLoadState::Unknown;
  }
  if (!neighbor_visually_drawable || block == BLOCK_AIR)
  {
    return NeighborLoadState::Air;
  }
  return NeighborLoadState::Loaded;
}

/// Era39: shell block used for occlusion when neighbor is SoftDefer-hidden.
inline BlockId ShellBlockForNeighborOcclusion(BlockId block,
                                              bool neighbor_visually_drawable)
{
  if (!neighbor_visually_drawable)
  {
    return BLOCK_AIR;
  }
  return block;
}

} // namespace cutum
