#pragma once

#include "World/Math/BlockTypes.h"

namespace cutum
{

/// Shell neighbor policy (Luanti CONTENT_IGNORE parity + lit-gate seam).
/// Unknown — neighbor chunk unloaded (conservative cull).
/// Air — loaded open / empty shell (emit boundary faces).
/// Unlit — neighbor loaded with solids but lit-gate hides drawable mesh.
/// LitDark — neighbor drawable but contributes dark occlusion (reserved).
/// Loaded — normal visible neighbor occlusion.
enum class NeighborLoadState : uint8_t
{
  Loaded = 0,
  Air = 1,
  Unknown = 2,
  Unlit = 3,
  LitDark = 4,
};

inline bool ShouldSkipFaceForNeighbor(NeighborLoadState state)
{
  return state == NeighborLoadState::Unknown ||
         state == NeighborLoadState::LitDark;
}

/// Era39 + M09: neighbor_visually_drawable=false (SoftDefer / lit-gate hide)
/// distinguishes Unlit (solid hidden) from Air (empty / air shell).
inline NeighborLoadState ClassifyShellCell(bool neighbor_chunk_loaded,
                                           BlockId block,
                                           bool neighbor_visually_drawable = true)
{
  if (!neighbor_chunk_loaded)
  {
    return NeighborLoadState::Unknown;
  }
  if (!neighbor_visually_drawable)
  {
    if (block == BLOCK_AIR)
    {
      return NeighborLoadState::Air;
    }
    return NeighborLoadState::Unlit;
  }
  if (block == BLOCK_AIR)
  {
    return NeighborLoadState::Air;
  }
  return NeighborLoadState::Loaded;
}

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
