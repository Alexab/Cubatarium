#pragma once
// BUDGET_MS: 0.0
// R06 field: when first drawable coverage publishes, remesh sea/subsea seams.
// Drawable stays out of InputsStillValid stamp (ADR Strategy A).
// E0 (111235): no 3x3 seamed expand; underwater Y = publisher_cy +/- 1 only.
// SoT 090834: air-near-fluid must NOT use underwater subsea_band (flicker).

#include <algorithm>
#include <cmath>

namespace cutum
{

/// Eye context for sea-seam remesh width (SoT 090834 air-view flicker).
enum class SeaSeamEyeContext : int
{
  Inland = 0,
  /// Eye above sea but near fluid — surface band only (not subsea_band=4).
  AirNearFluid = 1,
  /// Eye below sea+2 — full underwater publisher_cy slice + subsea band.
  Underwater = 2,
};

inline SeaSeamEyeContext ClassifySeaSeamEyeContext(float eye_y, float sea_level,
                                                   bool near_fluid_surface)
{
  if (eye_y < sea_level + 2.0f)
  {
    return SeaSeamEyeContext::Underwater;
  }
  if (near_fluid_surface)
  {
    return SeaSeamEyeContext::AirNearFluid;
  }
  return SeaSeamEyeContext::Inland;
}

/// True only for eye below sea — widens band / publisher_cy Y range.
inline bool SeaSeamEyeUsesUnderwaterBand(SeaSeamEyeContext ctx)
{
  return ctx == SeaSeamEyeContext::Underwater;
}

/// Surface band |cy-sea|<=2; underwater widens to subsea_band.
/// AirNearFluid uses surface_band (not subsea) — SoT 090834.
inline bool ShouldRemeshSeaSeamOnFirstDrawable(int chunk_cy, int sea_cy,
                                              SeaSeamEyeContext ctx,
                                              int surface_band = 2,
                                              int subsea_band = 4)
{
  const int band =
      SeaSeamEyeUsesUnderwaterBand(ctx) ? subsea_band : surface_band;
  return std::abs(chunk_cy - sea_cy) <= band;
}

/// Compat: true = Underwater (wide), false = Inland (surface). Prefer enum API.
inline bool ShouldRemeshSeaSeamOnFirstDrawable(int chunk_cy, int sea_cy,
                                              bool underwater_wide,
                                              int surface_band = 2,
                                              int subsea_band = 4)
{
  return ShouldRemeshSeaSeamOnFirstDrawable(
      chunk_cy, sea_cy,
      underwater_wide ? SeaSeamEyeContext::Underwater
                      : SeaSeamEyeContext::Inland,
      surface_band, subsea_band);
}

/// Inland / air-near-fluid surface: sea_level - CHUNK .. sea+2*CHUNK.
inline int SeaSeamRemeshMinYSurface(int sea_level, int chunk_size)
{
  return std::max(0, sea_level - chunk_size);
}

inline int SeaSeamRemeshMaxYSurface(int sea_level, int chunk_size,
                                   int max_height)
{
  return std::min(max_height, sea_level + chunk_size * 2);
}

/// Underwater only: peer same publisher_cy. AirNearFluid/Inland: surface range.
inline void SeaSeamRemeshYRangeForPublisher(int sea_level, int chunk_size,
                                           int max_height, int publisher_cy,
                                           SeaSeamEyeContext ctx,
                                           int &out_min_y, int &out_max_y)
{
  if (SeaSeamEyeUsesUnderwaterBand(ctx))
  {
    out_min_y = std::max(0, publisher_cy * chunk_size);
    out_max_y =
        std::min(max_height, (publisher_cy + 1) * chunk_size - 1);
    return;
  }
  out_min_y = SeaSeamRemeshMinYSurface(sea_level, chunk_size);
  out_max_y = SeaSeamRemeshMaxYSurface(sea_level, chunk_size, max_height);
}

inline void SeaSeamRemeshYRangeForPublisher(int sea_level, int chunk_size,
                                           int max_height, int publisher_cy,
                                           bool underwater_wide,
                                           int &out_min_y, int &out_max_y)
{
  SeaSeamRemeshYRangeForPublisher(
      sea_level, chunk_size, max_height, publisher_cy,
      underwater_wide ? SeaSeamEyeContext::Underwater
                      : SeaSeamEyeContext::Inland,
      out_min_y, out_max_y);
}

/// Chunks dirtied per peer column when include_horizontal_neighbors=false.
inline int SeaSeamRemeshChunksPerPeerColumn(int sea_level, int chunk_size,
                                           int max_height, int publisher_cy,
                                           SeaSeamEyeContext ctx)
{
  int min_y = 0;
  int max_y = 0;
  SeaSeamRemeshYRangeForPublisher(sea_level, chunk_size, max_height,
                                 publisher_cy, ctx, min_y, max_y);
  const int cy0 = min_y / chunk_size;
  const int cy1 = max_y / chunk_size;
  return std::max(0, cy1 - cy0 + 1);
}

inline int SeaSeamRemeshChunksPerPeerColumn(int sea_level, int chunk_size,
                                           int max_height, int publisher_cy,
                                           bool underwater_wide)
{
  return SeaSeamRemeshChunksPerPeerColumn(
      sea_level, chunk_size, max_height, publisher_cy,
      underwater_wide ? SeaSeamEyeContext::Underwater
                      : SeaSeamEyeContext::Inland);
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

/// Policy helper only: production first-drawable remesh stays face-toward +
/// sea-band (R2). Broaden-to-any-active-overlay was tried (W1 185830) and
/// regressed blacks/flicker — do not wire into CEC without a non-Dirty path.
inline bool ShouldRemeshStickyOverlayPeer(bool peer_drawable,
                                          bool peer_has_active_overlay,
                                          bool peer_face_toward_active,
                                          bool peer_in_sea_band)
{
  if (!peer_drawable)
  {
    return false;
  }
  // Production gate equivalent: face-toward OR (legacy) sea-band without sticky.
  if (peer_face_toward_active)
  {
    return true;
  }
  if (peer_has_active_overlay && peer_in_sea_band)
  {
    return true;
  }
  return false;
}

} // namespace cutum
