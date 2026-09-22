#ifndef LIGHT_REFERENCE_COMPARE_H
#define LIGHT_REFERENCE_COMPARE_H

#include <algorithm>
#include <cstdint>
#include <cstddef>

namespace cutum
{

/// A21 P4.3: compare incremental light field bytes to a slow full-recompute
/// reference on small worlds. Returns mismatch count (0 = match within tol).
/// `tol` allows channel quantization noise; 0 = exact.
inline size_t CountLightFieldMismatches(const uint8_t *incremental,
                                        const uint8_t *reference, size_t n,
                                        uint8_t tol = 0)
{
  if (!incremental || !reference)
  {
    return n;
  }
  size_t mism = 0;
  for (size_t i = 0; i < n; ++i)
  {
    const int d = static_cast<int>(incremental[i]) -
                  static_cast<int>(reference[i]);
    if (d > static_cast<int>(tol) || d < -static_cast<int>(tol))
    {
      ++mism;
    }
  }
  return mism;
}

/// Stale halo: center revision matches but neighbor halo revision moved → reject.
inline bool ShouldRejectStaleHaloLight(uint64_t center_rev_got,
                                       uint64_t center_rev_expected,
                                       uint64_t halo_rev_got,
                                       uint64_t halo_rev_expected)
{
  if (center_rev_got != center_rev_expected)
  {
    return true;
  }
  return halo_rev_got != halo_rev_expected;
}

/// A25 R3: tiny deterministic reference light field for unit tests — sky=15 on
/// open cells, block light from point sources. Not a full world solver.
inline void FillReferenceSkyBlockLight(uint8_t *out, int n, int sky_level,
                                       int block_x, int block_y, int block_z,
                                       int block_level)
{
  if (!out || n <= 0)
  {
    return;
  }
  const size_t cells = static_cast<size_t>(n) * static_cast<size_t>(n) *
                       static_cast<size_t>(n);
  for (size_t i = 0; i < cells; ++i)
  {
    out[i] = static_cast<uint8_t>(sky_level);
  }
  if (block_x >= 0 && block_x < n && block_y >= 0 && block_y < n &&
      block_z >= 0 && block_z < n)
  {
    const size_t idx = static_cast<size_t>(
        (block_y * n + block_z) * n + block_x);
    const int packed =
        (std::max(0, std::min(15, sky_level)) << 4) |
        std::max(0, std::min(15, block_level));
    out[idx] = static_cast<uint8_t>(packed);
  }
}

} // namespace cutum

#endif
