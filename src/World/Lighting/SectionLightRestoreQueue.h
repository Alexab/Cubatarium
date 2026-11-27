#pragma once

#include "World/Lighting/PersistedSectionLight.h"
#include <list>

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
    std::array<size_t, static_cast<size_t>(SectionLightRestoreFailure::Count)>
        reasons{};
    std::optional<std::pair<glm::ivec3, SectionLightRestoreDiagnostic>>
        first_rejected;
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
      *prior = Entry{std::move(receipt), owner, bytes};
      Entries.splice(Entries.end(), Entries, prior);
    }
    else
    {
      try
      {
        Entries.push_back({std::move(receipt), owner, bytes});
      }
      catch (const std::bad_alloc &)
      {
        return false; // The ordinary relight path still owns the obligation.
      }
    }
    Bytes = Bytes - prior_bytes + bytes;
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
      const auto entry = Entries.begin();
      const auto coord = entry->receipt->coord;
      if (!owner_current(coord, entry->owner))
      {
        Erase(entry);
        ++stats.cancelled;
        continue;
      }
      if (world.HasCompleteLightSamplingCalculation(coord, catalog))
      {
        Erase(entry);
        ++stats.current;
        on_ready(coord);
        continue;
      }
      SectionLightRestoreDiagnostic diagnostic;
      switch (RestoreSectionLight(world, *entry->receipt, catalog, &diagnostic))
      {
      case SectionLightRestoreResult::Restored:
        Erase(entry);
        ++stats.restored;
        on_ready(coord);
        break;
      case SectionLightRestoreResult::Rejected:
        Erase(entry);
        ++stats.rejected;
        ++stats.reasons[static_cast<size_t>(diagnostic.reason)];
        if (!stats.first_rejected)
          stats.first_rejected = std::make_pair(coord, diagnostic);
        break;
      case SectionLightRestoreResult::InputsUnavailable:
        ++stats.waiting;
        Entries.splice(Entries.end(), Entries, entry);
        break;
      }
    }
    return stats;
  }
  void CancelColumn(glm::ivec3 column)
  {
    for (auto it = Entries.begin(); it != Entries.end();)
      if (it->receipt->coord.x == column.x && it->receipt->coord.z == column.z)
        it = Erase(it);
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
  using EntriesList = std::list<Entry>;
  EntriesList::iterator Erase(EntriesList::iterator entry)
  {
    Bytes -= entry->bytes;
    return Entries.erase(entry);
  }

  EntriesList Entries;
  size_t Bytes{0};
};
} // namespace cutum
