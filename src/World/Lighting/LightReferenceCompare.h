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

/// A26 N3: slow BFS block-light propagation on an NxNxN packed sky/block field.
/// `solid` is occupancy (1=occlude). Writes packed (sky<<4)|block into `out`.
/// Finite steps: at most n*n*n*6 relaxations (no endless relight).
inline size_t PropagateReferenceBlockLight(uint8_t *out, const uint8_t *solid,
                                           int n, int src_x, int src_y,
                                           int src_z, int src_level)
{
  if (!out || !solid || n <= 0 || src_level <= 0)
  {
    return 0;
  }
  const size_t cells = static_cast<size_t>(n) * static_cast<size_t>(n) *
                       static_cast<size_t>(n);
  for (size_t i = 0; i < cells; ++i)
  {
    const uint8_t sky = static_cast<uint8_t>(out[i] >> 4);
    out[i] = static_cast<uint8_t>(sky << 4); // clear block channel
  }
  if (src_x < 0 || src_x >= n || src_y < 0 || src_y >= n || src_z < 0 ||
      src_z >= n)
  {
    return 0;
  }
  auto idx = [n](int x, int y, int z) -> size_t {
    return static_cast<size_t>((y * n + z) * n + x);
  };
  auto set_block = [&](size_t i, int level) {
    const uint8_t sky = static_cast<uint8_t>(out[i] >> 4);
    const int cur = out[i] & 0x0F;
    if (level > cur)
    {
      out[i] = static_cast<uint8_t>((sky << 4) |
                                    std::max(0, std::min(15, level)));
    }
  };
  set_block(idx(src_x, src_y, src_z), src_level);
  size_t updates = 1;
  const int max_passes = n * n * n;
  for (int pass = 0; pass < max_passes; ++pass)
  {
    bool changed = false;
    for (int y = 0; y < n; ++y)
    {
      for (int z = 0; z < n; ++z)
      {
        for (int x = 0; x < n; ++x)
        {
          const size_t i = idx(x, y, z);
          if (solid[i] != 0)
          {
            continue;
          }
          const int level = out[i] & 0x0F;
          if (level <= 1)
          {
            continue;
          }
          const int next = level - 1;
          const int nbs[6][3] = {{1, 0, 0}, {-1, 0, 0}, {0, 1, 0},
                                 {0, -1, 0}, {0, 0, 1}, {0, 0, -1}};
          for (const auto &d : nbs)
          {
            const int nx = x + d[0];
            const int ny = y + d[1];
            const int nz = z + d[2];
            if (nx < 0 || nx >= n || ny < 0 || ny >= n || nz < 0 || nz >= n)
            {
              continue;
            }
            const size_t ni = idx(nx, ny, nz);
            if (solid[ni] != 0)
            {
              continue;
            }
            const int before = out[ni] & 0x0F;
            set_block(ni, next);
            if ((out[ni] & 0x0F) != before)
            {
              changed = true;
              ++updates;
            }
          }
        }
      }
    }
    if (!changed)
    {
      break;
    }
  }
  return updates;
}

} // namespace cutum

#endif
