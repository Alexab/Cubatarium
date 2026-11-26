#pragma once

#include "Core/Memory/VisualAssetCensus.h"
#include "Items/ItemDefinitionStorage.h"
#include <mutex>
#include <optional>
#include <unordered_map>

namespace cutum
{

template<class Asset>
class LegacyVisualAssetCache
{
public:
  template<class Load>
  std::shared_ptr<Asset> Get(const std::string &key, Load load)
  {
    const std::lock_guard lock(Mutex);
    const auto revision = UItemDefinitionStorage::ContentRevision();
    if (Table)
    {
      if (Revision != revision)
      {
        Table->clear();
        Revision = revision;
      }
      if (const auto found = Table->find(key); found != Table->end())
        return found->second;
    }
    std::shared_ptr<Asset> asset = load();
    if (!asset || UItemDefinitionStorage::ContentRevision() != revision)
      return asset;
    if (!Table)
    {
      Map candidate;
      candidate.try_emplace(key, asset);
      Table.emplace(std::move(candidate));
      Revision = revision;
    }
    else
      Table->try_emplace(key, asset);
    return asset;
  }

  void Inspect(VisualAssetCensus &census, VisualAssetOwner owner) const
  {
    const std::lock_guard lock(Mutex);
    census.AddLegacyCache(sizeof(*this), Table ? &*Table : nullptr, owner);
  }

private:
  using Map = std::unordered_map<std::string, std::shared_ptr<Asset>>;
  mutable std::mutex Mutex;
  uint64_t Revision{};
  std::optional<Map> Table;
};

} // namespace cutum
