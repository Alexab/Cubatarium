#pragma once

#include <cstdint>
#include <limits>
#include <string>
#include <vector>

namespace cutum
{

/// Stable identifier stored in pack data. Handles are temporary indices into
/// the immutable registry snapshot that produced them.
using BiomeKey = std::string;

struct BiomeHandle
{
  uint32_t index{std::numeric_limits<uint32_t>::max()};
  bool IsValid() const { return index != std::numeric_limits<uint32_t>::max(); }
  friend bool operator==(BiomeHandle a, BiomeHandle b)
  {
    return a.index == b.index;
  }
  friend bool operator!=(BiomeHandle a, BiomeHandle b) { return !(a == b); }
};

struct BiomeBlendEntry
{
  BiomeHandle biome;
  float weight{0.0f};
};

struct BiomeBlend
{
  std::vector<BiomeBlendEntry> entries;

  float Weight(BiomeHandle handle) const
  {
    for (const BiomeBlendEntry &entry : entries)
    {
      if (entry.biome == handle)
      {
        return entry.weight;
      }
    }
    return 0.0f;
  }
};

} // namespace cutum
