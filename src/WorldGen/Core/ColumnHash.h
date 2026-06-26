#pragma once

#include <cstdint>

namespace cutum
{

inline uint32_t ColumnHash(int x, int z, uint32_t seed)
{
  return static_cast<uint32_t>(x * 374761393 + z * 668265263) ^ seed;
}

} // namespace cutum
