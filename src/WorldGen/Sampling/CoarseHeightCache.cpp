#include "WorldGen/Sampling/CoarseHeightCache.h"

#include <algorithm>

namespace cutum
{

UCoarseHeightCache::UCoarseHeightCache(int origin_x, int origin_z, int size_x,
                                       int size_z)
{
  Reset(origin_x, origin_z, size_x, size_z);
}

void UCoarseHeightCache::Reset(int origin_x, int origin_z, int size_x,
                               int size_z)
{
  OriginX = origin_x;
  OriginZ = origin_z;
  SizeX = std::max(0, size_x);
  SizeZ = std::max(0, size_z);
  const size_t n = static_cast<size_t>(SizeX) * static_cast<size_t>(SizeZ);
  Values.assign(n, 0);
  Valid.assign(n, 0);
}

bool UCoarseHeightCache::Contains(int x, int z) const
{
  const int lx = x - OriginX;
  const int lz = z - OriginZ;
  return lx >= 0 && lz >= 0 && lx < SizeX && lz < SizeZ;
}

int UCoarseHeightCache::GetOrCompute(
    int x, int z, const std::function<int(int, int)> &compute)
{
  if (!Contains(x, z))
  {
    return compute(x, z);
  }
  const int lx = x - OriginX;
  const int lz = z - OriginZ;
  const size_t idx =
      static_cast<size_t>(lz) * static_cast<size_t>(SizeX) +
      static_cast<size_t>(lx);
  if (Valid[idx])
  {
    return Values[idx];
  }
  const int y = compute(x, z);
  Values[idx] = y;
  Valid[idx] = 1;
  return y;
}

void UCoarseHeightCache::Put(int x, int z, int value)
{
  if (!Contains(x, z))
  {
    return;
  }
  const int lx = x - OriginX;
  const int lz = z - OriginZ;
  const size_t idx =
      static_cast<size_t>(lz) * static_cast<size_t>(SizeX) +
      static_cast<size_t>(lx);
  Values[idx] = value;
  Valid[idx] = 1;
}

} // namespace cutum
