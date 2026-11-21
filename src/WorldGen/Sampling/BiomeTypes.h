#pragma once
#include "Core/Memory/GenerationAllocator.h"

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <limits>
#include <string>
#include <memory>
#include <string_view>
#include <vector>

namespace cutum
{

/// Stable identifier stored in pack data. Handles are temporary indices into
/// the immutable registry snapshot that produced them.
using BiomeKey = std::string;

// Runtime assignments alias a key in an immutable registry (or the fixed
// builtin table). The shared owner keeps escaped samples valid after reload.
class BiomeKeyRef
{
  std::shared_ptr<const BiomeKey> Key;
public:
  BiomeKeyRef()=default;
  explicit BiomeKeyRef(std::shared_ptr<const BiomeKey> key) noexcept : Key(std::move(key)) {}
  const BiomeKey &Get() const noexcept {static const BiomeKey empty;return Key ? *Key : empty;}
  operator const BiomeKey &() const noexcept {return Get();}
  bool empty() const noexcept {return Get().empty();}
  const char *data() const noexcept {return Get().data();}
  size_t size() const noexcept {return Get().size();}
  friend bool operator==(const BiomeKeyRef &a,const BiomeKeyRef &b) {return a.Get()==b.Get();}
  friend bool operator!=(const BiomeKeyRef &a,const BiomeKeyRef &b) {return !(a==b);}
  friend bool operator==(const BiomeKeyRef &a,std::string_view b) {return std::string_view(a.Get())==b;}
  friend bool operator==(std::string_view a,const BiomeKeyRef &b) {return b==a;}
  friend bool operator!=(const BiomeKeyRef &a,std::string_view b) {return !(a==b);}
  friend bool operator!=(std::string_view a,const BiomeKeyRef &b) {return !(b==a);}
};

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
  GenerationVector<BiomeBlendEntry> entries;

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

  void AddWeight(BiomeHandle handle, float weight)
  {
    if (!handle.IsValid() || !std::isfinite(weight) || !(weight > 0.0f))
    {
      return;
    }
    for (BiomeBlendEntry &entry : entries)
    {
      if (entry.biome == handle)
      {
        entry.weight += weight;
        return;
      }
    }
    entries.push_back({handle, weight});
  }

  void SetWeight(BiomeHandle handle, float weight)
  {
    for (auto it = entries.begin(); it != entries.end(); ++it)
    {
      if (it->biome == handle)
      {
        if (std::isfinite(weight) && weight > 0.0f)
        {
          it->weight = weight;
        }
        else
        {
          entries.erase(it);
        }
        return;
      }
    }
    if (std::isfinite(weight) && weight > 0.0f)
    {
      AddWeight(handle, weight);
    }
  }

  float TotalWeight() const
  {
    float total = 0.0f;
    for (const BiomeBlendEntry &entry : entries)
    {
      total += entry.weight;
    }
    return total;
  }

  void Normalize()
  {
    entries.erase(std::remove_if(entries.begin(), entries.end(),
                                 [](const BiomeBlendEntry &entry)
                                 { return !entry.biome.IsValid() ||
                                          !std::isfinite(entry.weight) ||
                                          !(entry.weight > 0.0f); }),
                  entries.end());
    const float total = TotalWeight();
    if (std::isfinite(total) && total > 0.0f)
    {
      for (BiomeBlendEntry &entry : entries)
      {
        entry.weight /= total;
      }
    }
    std::sort(entries.begin(), entries.end(),
              [](const BiomeBlendEntry &a, const BiomeBlendEntry &b)
              { return a.biome.index < b.biome.index; });
  }
};

} // namespace cutum
