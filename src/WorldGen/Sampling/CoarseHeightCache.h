#pragma once

#include <cstdint>
#include <functional>
#include <vector>

namespace cutum
{

/// Dense rectangular memo for CoarseSurfaceY lookups during one chunk populate.
/// Same (x,z) always returns the same value as compute(x,z) would.
struct CoarseHeightCacheStats
{
  uint64_t Hits{0};
  uint64_t Misses{0};
  uint64_t OutOfRange{0};
};

class UCoarseHeightCache
{
public:
  UCoarseHeightCache() = default;
  UCoarseHeightCache(int origin_x, int origin_z, int size_x, int size_z);

  void Reset(int origin_x, int origin_z, int size_x, int size_z);
  bool Contains(int x, int z) const;
  int GetOrCompute(int x, int z, const std::function<int(int, int)> &compute);
  void Put(int x, int z, int value);
  const CoarseHeightCacheStats &GetStats() const { return Stats; }

private:
  int OriginX{0};
  int OriginZ{0};
  int SizeX{0};
  int SizeZ{0};
  std::vector<int> Values;
  std::vector<uint8_t> Valid;
  CoarseHeightCacheStats Stats;
};

} // namespace cutum
