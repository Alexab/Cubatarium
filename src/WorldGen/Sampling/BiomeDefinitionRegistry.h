#pragma once

#include "WorldGen/Sampling/BiomeTypes.h"
#include <algorithm>
#include <string>
#include <unordered_map>
#include <vector>

namespace cutum
{

/// Compiled immutable key/handle table owned by one WorldGenPack snapshot.
/// Handles must never be persisted or resolved against another snapshot.
class BiomeDefinitionRegistry
{
public:
  explicit BiomeDefinitionRegistry(std::vector<BiomeKey> keys = {})
  {
    std::sort(keys.begin(), keys.end());
    keys.erase(std::unique(keys.begin(), keys.end()), keys.end());
    Keys = std::move(keys);
    for (uint32_t i = 0; i < Keys.size(); ++i)
    {
      Handles.emplace(Keys[i], BiomeHandle{i});
    }
  }

  BiomeHandle Find(const BiomeKey &key) const
  {
    const auto it = Handles.find(key);
    return it == Handles.end() ? BiomeHandle{} : it->second;
  }

  const BiomeKey *Key(BiomeHandle handle) const
  {
    return handle.IsValid() && handle.index < Keys.size()
               ? &Keys[handle.index]
               : nullptr;
  }

  size_t Size() const { return Keys.size(); }

private:
  std::vector<BiomeKey> Keys;
  std::unordered_map<BiomeKey, BiomeHandle> Handles;
};

} // namespace cutum
