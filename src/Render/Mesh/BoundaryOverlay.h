#pragma once

#include <cstdint>

namespace cutum
{

/// Audit S4 / R06: temporary closing faces while a neighbor is not published.
/// Independent of permanent ChunkInputStamp (Strategy A). When neighbor
/// published coverage arrives, clear active and bump version — do not invalidate
/// permanent mesh stamps solely from drawable flip.
struct BoundaryOverlayState
{
  uint64_t version{0};
  bool active{false};
  /// Bit i set => face i (ChunkMeshSnapshot face index 0..5) needs overlay.
  uint8_t missingNeighborFaces{0};
};

inline void BoundaryOverlaySetMissingFaces(BoundaryOverlayState &overlay,
                                           uint8_t mask)
{
  const bool want = mask != 0;
  if (overlay.active == want && overlay.missingNeighborFaces == mask)
    return;
  overlay.missingNeighborFaces = mask;
  overlay.active = want;
  ++overlay.version;
}

inline void BoundaryOverlayClear(BoundaryOverlayState &overlay)
{
  if (!overlay.active && overlay.missingNeighborFaces == 0)
    return;
  overlay.missingNeighborFaces = 0;
  overlay.active = false;
  ++overlay.version;
}

} // namespace cutum
