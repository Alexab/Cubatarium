#ifndef CREATURE_TICK_BUDGET_H
#define CREATURE_TICK_BUDGET_H

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <unordered_map>
#include <utility>
#include <vector>

namespace cutum
{

/// Round-robin over creatures for AI ticks under frame stress. The director
/// used to tick 1-2 behavior agents per stress tick, each running every
/// creature of its behavior: all melee mobs could go several ticks without a
/// brain update while every wanderer ran. Now each stress tick runs a slice
/// of creatures (in id order, continuing after the last one ticked), and a
/// creature that waited gets the time it missed (capped) so its timers keep
/// pace.
class UCreatureTickBudget
{
public:
  using Id = uint64_t;
  static constexpr size_t kMinPerTick = 16;
  static constexpr float kMaxCreatureDt = 0.5f;

  /// Slice size for `count` creatures: everyone at least every 4 stress
  /// ticks, never fewer than kMinPerTick.
  static size_t BudgetFor(size_t count)
  {
    return std::min(count, std::max(kMinPerTick, (count + 3) / 4));
  }

  /// `sortedIds` ascending. Accrues `dt` for every creature, then returns
  /// the next `budget` creatures after the last one ticked, each with its
  /// accrued time (capped).
  std::vector<std::pair<Id, float>> Select(const std::vector<Id> &sortedIds,
                                           float dt, size_t budget)
  {
    std::vector<std::pair<Id, float>> out;
    if (sortedIds.empty() || budget == 0)
    {
      return out;
    }
    for (const Id id : sortedIds)
    {
      Pending[id] += dt;
    }
    size_t start = static_cast<size_t>(
        std::upper_bound(sortedIds.begin(), sortedIds.end(), LastTicked) -
        sortedIds.begin());
    const size_t n = std::min(budget, sortedIds.size());
    out.reserve(n);
    for (size_t i = 0; i < n; ++i)
    {
      const Id id = sortedIds[(start + i) % sortedIds.size()];
      float &pending = Pending[id];
      out.emplace_back(id, std::min(pending, kMaxCreatureDt));
      pending = 0.0f;
      LastTicked = id;
    }
    return out;
  }

  /// Everyone was ticked (normal tick): nothing is owed.
  void Reset() { Pending.clear(); }
  void Remove(Id id) { Pending.erase(id); }

private:
  Id LastTicked{0};
  std::unordered_map<Id, float> Pending;
};

} // namespace cutum

#endif
