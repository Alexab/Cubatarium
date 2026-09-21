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

/// Sysreset v2: Unknown (unloaded) always hides — including liquid void-emit.
inline bool NeighborUnknownAlwaysHidesFace(NeighborLoadState state)
{
  return state == NeighborLoadState::Unknown;
}

/// NeighborBecameKnown: coalesce seam remesh 1/column/frame when a face-neighbor
/// flips to loaded/drawable (overlay Missing and/or FaceDebt toward publisher).
inline bool ShouldCoalesceNeighborBecameKnownSeam(
    bool neighbor_now_known, bool self_has_overlay_face_toward,
    bool already_coalesced_this_frame,
    bool self_has_face_debt_toward = false)
{
  if (!neighbor_now_known || already_coalesced_this_frame)
  {
    return false;
  }
  return self_has_overlay_face_toward || self_has_face_debt_toward;
}

/// Sysreset v5/v6: FaceDebt holder remeshes when a face-peer is already
/// drawable (BecameKnown FirstDrawable will not fire again for that peer).
/// SoftDefer peer → census only (no Dirty); default cap 2 (was 4).
inline bool ShouldRemeshFaceDebtHolderWhenPeerDrawable(
    bool has_face_debt_bit, bool peer_drawable, bool focus_in_ring,
    bool already_coalesced, int remesh_n, int remesh_cap = 2,
    bool peer_softdefer_empty = false)
{
  return has_face_debt_bit && peer_drawable && !peer_softdefer_empty &&
         focus_in_ring && !already_coalesced && remesh_n < remesh_cap;
}

inline bool ShouldSkipFaceForNeighbor(NeighborLoadState state)
{
  return state == NeighborLoadState::Unknown ||
         state == NeighborLoadState::LitDark;
}

/// Sysreset phase4: unloaded neighbor must never be classified as Air (would
/// emit sticky closing walls). Air is only for loaded empty shell cells.
inline bool NeighborShellAirRequiresLoadedChunk(NeighborLoadState state,
                                                bool neighbor_chunk_loaded)
{
  if (state != NeighborLoadState::Air)
  {
    return true;
  }
  return neighbor_chunk_loaded;
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
