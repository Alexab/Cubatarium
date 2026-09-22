#ifndef LIGHT_REFERENCE_COMPARE_H
#define LIGHT_REFERENCE_COMPARE_H

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

} // namespace cutum

#endif
