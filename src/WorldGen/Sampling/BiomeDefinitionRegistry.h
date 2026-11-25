#pragma once

#include "WorldGen/Sampling/BiomeTypes.h"
#include <algorithm>
#include <string>
#include <stdexcept>
#include <unordered_map>
#include <vector>

namespace cutum
{

constexpr bool BiomeHandleCountFits(size_t count) noexcept
{
  return count <= std::numeric_limits<uint32_t>::max();
}

/// Compiled immutable key/handle table owned by one WorldGenPack snapshot.
/// Handles must never be persisted or resolved against another snapshot.
class UBiomeDefinitionRegistry
{
public:
  explicit UBiomeDefinitionRegistry(std::vector<BiomeKey> keys = {})
  {
    std::sort(keys.begin(), keys.end());
    keys.erase(std::unique(keys.begin(), keys.end()), keys.end());
    if (!BiomeHandleCountFits(keys.size()))
      throw std::length_error("biome key count exceeds handle domain");
    Keys = std::move(keys);
    for (size_t i = 0; i < Keys.size(); ++i)
    {
      Handles.emplace(Keys[i], BiomeHandle{static_cast<uint32_t>(i)});
    }
  }

  BiomeHandle Find(const BiomeKey &key) const
  {
    const auto it = Handles.find(key);
    return it == Handles.end() ? BiomeHandle{} : it->second;
  }

  const BiomeKey *Key(BiomeHandle handle) const
  {
    return handle.IsValid() && handle.index < Keys.size() ? &Keys[handle.index]
                                                          : nullptr;
  }

  size_t Size() const { return Keys.size(); }

  template<class Inspector> void InspectStorage(Inspector &out) const {
    out.Vector(Keys);out.Map(Handles);
    for(const auto &key:Keys) out.Text(key);
    for(const auto &entry:Handles) out.Text(entry.first);
  }

private:
  std::vector<BiomeKey> Keys;
  std::unordered_map<BiomeKey, BiomeHandle> Handles;
};

} // namespace cutum
