#pragma once
// BUDGET_MS: 0.0
// R06 field: when first drawable coverage publishes, remesh sea/subsea seams.
// Drawable stays out of InputsStillValid stamp (ADR Strategy A).
// E0 (111235): no 3x3 seamed expand; underwater Y = publisher_cy +/- 1 only.

#include <algorithm>
#include <cmath>

namespace cutum
{

/// Surface band |cy-sea|<=2 inland; underwater/near_water widens to subsea_band.
inline bool ShouldRemeshSeaSeamOnFirstDrawable(int chunk_cy, int sea_cy,
                                              bool underwater_or_near_water,
                                              int surface_band = 2,
                                              int subsea_band = 4)
{
  const int band = underwater_or_near_water ? subsea_band : surface_band;
  return std::abs(chunk_cy - sea_cy) <= band;
}

/// Inland surface: sea_level - CHUNK .. sea+2*CHUNK.
inline int SeaSeamRemeshMinYSurface(int sea_level, int chunk_size)
{
  return std::max(0, sea_level - chunk_size);
}

inline int SeaSeamRemeshMaxYSurface(int sea_level, int chunk_size,
                                   int max_height)
{
  return std::min(max_height, sea_level + chunk_size * 2);
}

/// Underwater/near_water: peer same publisher_cy only (E2 residual after E0).
inline void SeaSeamRemeshYRangeForPublisher(int sea_level, int chunk_size,
                                           int max_height, int publisher_cy,
                                           bool underwater_or_near_water,
                                           int &out_min_y, int &out_max_y)
{
  if (underwater_or_near_water)
  {
    out_min_y = std::max(0, publisher_cy * chunk_size);
    out_max_y =
        std::min(max_height, (publisher_cy + 1) * chunk_size - 1);
    return;
  }
  out_min_y = SeaSeamRemeshMinYSurface(sea_level, chunk_size);
  out_max_y = SeaSeamRemeshMaxYSurface(sea_level, chunk_size, max_height);
}

/// Chunks dirtied per peer column when include_horizontal_neighbors=false.
inline int SeaSeamRemeshChunksPerPeerColumn(int sea_level, int chunk_size,
                                           int max_height, int publisher_cy,
                                           bool underwater_or_near_water)
{
  int min_y = 0;
  int max_y = 0;
  SeaSeamRemeshYRangeForPublisher(sea_level, chunk_size, max_height,
                                 publisher_cy, underwater_or_near_water, min_y,
                                 max_y);
  const int cy0 = min_y / chunk_size;
  const int cy1 = max_y / chunk_size;
  return std::max(0, cy1 - cy0 + 1);
}

/// Shell face index on peer looking back at publisher (ChunkMeshSnapshot faces).
/// peer = publisher + delta; axis*2+(sign>0). Returns -1 if not a face-nb.
inline int SeaSeamPeerFaceTowardPublisher(int delta_x, int delta_z)
{
  if (delta_x == 1 && delta_z == 0)
  {
    return 0; // peer +X of publisher → peer -X face
  }
  if (delta_x == -1 && delta_z == 0)
  {
    return 1; // peer -X → peer +X
  }
  if (delta_x == 0 && delta_z == 1)
  {
    return 4; // peer +Z → peer -Z
  }
  if (delta_x == 0 && delta_z == -1)
  {
    return 5; // peer -Z → peer +Z
  }
  return -1;
}

/// R2: remesh peer only while sticky overlay still owes that seam face.
inline bool PeerNeedsSeaSeamRemesh(bool overlay_active, uint8_t missing_faces,
                                  int face_toward_publisher)
{
  if (!overlay_active || face_toward_publisher < 0 || face_toward_publisher > 5)
  {
    return false;
  }
  return (missing_faces &
          static_cast<uint8_t>(1u << face_toward_publisher)) != 0;
}

/// W1 SoT 185830: sticky overlay peer remesh fires when any active overlay
/// (not only face-toward), even outside narrow sea-band.
inline bool ShouldRemeshStickyOverlayPeer(bool peer_drawable,
                                          bool peer_has_active_overlay,
                                          bool peer_face_toward_active,
                                          bool peer_in_sea_band)
{
  if (!peer_drawable)
  {
    return false;
  }
  if (peer_has_active_overlay || peer_face_toward_active)
  {
    return true;
  }
  return peer_in_sea_band;
}

} // namespace cutum
