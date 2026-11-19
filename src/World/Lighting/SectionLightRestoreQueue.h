#pragma once

#include "World/Lighting/PersistedSectionLight.h"
#include <deque>

namespace cutum
{
// Optional disk-cache owners never replace the ordinary relight obligation.
// A ticket waits for known inputs, not for a timeout that grants readiness.
class SectionLightRestoreQueue
{
public:
  static constexpr size_t MaxCount = 256;
  static constexpr size_t MaxBytes = 16 * 1024 * 1024;
  struct DrainStats
  {
    size_t restored{0}, rejected{0}, cancelled{0}, waiting{0}, current{0};
  };
  bool Offer(std::shared_ptr<const PersistedSectionLight> receipt,
             uint64_t owner)
  {
    if (!receipt)
      return false;
    const auto bytes = PersistedLightBytes(*receipt);
    auto prior =
        std::find_if(Entries.begin(), Entries.end(), [&](const Entry &e)
                     { return e.receipt->coord == receipt->coord; });
    const size_t prior_bytes = prior == Entries.end() ? 0 : prior->bytes;
    if ((prior == Entries.end() && Entries.size() >= MaxCount) ||
        bytes > MaxBytes - (Bytes - prior_bytes))
      return false;
    if (prior != Entries.end())
    {
      Bytes -= prior->bytes;
      Entries.erase(prior);
    }
    Bytes += bytes;
    Entries.push_back({std::move(receipt), owner, bytes});
    return true;
  }
  template <class OwnerCurrent, class OnReady>
  DrainStats Drain(UBlockWorld &world,
                   const std::shared_ptr<const BlockDefinitionCatalog> &catalog,
                   OwnerCurrent owner_current, OnReady on_ready,
                   size_t visit_budget = 2)
  {
    DrainStats stats;
    const auto count = std::min(visit_budget, Entries.size());
    for (size_t n = 0; n < count; ++n)
    {
      auto entry = std::move(Entries.front());
      Entries.pop_front();
      Bytes -= entry.bytes;
      const auto coord = entry.receipt->coord;
      if (!owner_current(coord, entry.owner))
      {
        ++stats.cancelled;
        continue;
      }
      if (world.HasCompleteLightSamplingCalculation(coord, catalog))
      {
        ++stats.current;
        on_ready(coord);
        continue;
      }
      switch (RestoreSectionLight(world, *entry.receipt, catalog))
      {
      case SectionLightRestoreResult::Restored:
        ++stats.restored;
        on_ready(coord);
        break;
      case SectionLightRestoreResult::Rejected:
        ++stats.rejected;
        break;
      case SectionLightRestoreResult::InputsUnavailable:
        ++stats.waiting;
        Bytes += entry.bytes;
        Entries.push_back(std::move(entry));
        break;
      }
    }
    return stats;
  }
  void CancelColumn(glm::ivec3 column)
  {
    for (auto it = Entries.begin(); it != Entries.end();)
      if (it->receipt->coord.x == column.x && it->receipt->coord.z == column.z)
      {
        Bytes -= it->bytes;
        it = Entries.erase(it);
      }
      else
        ++it;
  }
  void Clear()
  {
    Entries.clear();
    Bytes = 0;
  }
  size_t Size() const { return Entries.size(); }
  size_t RetainedBytes() const { return Bytes; }

private:
  struct Entry
  {
    std::shared_ptr<const PersistedSectionLight> receipt;
    uint64_t owner{0};
    size_t bytes{0};
  };
  std::deque<Entry> Entries;
  size_t Bytes{0};
};
} // namespace cutum
