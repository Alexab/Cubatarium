#ifndef CREATURE_REMOVAL_QUEUE_H
#define CREATURE_REMOVAL_QUEUE_H

#include <algorithm>
#include <cstdint>
#include <vector>

namespace cutum
{

/// Defers creature destruction while the creature map is being iterated.
/// A kill inside ForEachCreature (melee in the NPC tick) used to erase the
/// target from the unordered_map mid-loop: safe only by the container's
/// erase rules and only while the victim is never the creature being
/// visited. Removals requested inside an iteration are queued and applied
/// when the outermost iteration ends; queued creatures already count as
/// gone for lookups and iteration.
class UCreatureRemovalQueue
{
public:
  using Id = uint64_t;

  void BeginIteration() { ++Depth; }
  /// True when the outermost iteration just ended (time to flush).
  bool EndIteration()
  {
    if (Depth > 0)
    {
      --Depth;
    }
    return Depth == 0 && !Pending.empty();
  }
  bool IsIterating() const { return Depth > 0; }

  /// Queue `id` (once). Returns false when it was already queued.
  bool Defer(Id id)
  {
    if (IsPending(id))
    {
      return false;
    }
    Pending.push_back(id);
    return true;
  }
  bool IsPending(Id id) const
  {
    return !Pending.empty() &&
           std::find(Pending.begin(), Pending.end(), id) != Pending.end();
  }
  bool HasPending() const { return !Pending.empty(); }
  std::vector<Id> TakePending()
  {
    std::vector<Id> out;
    out.swap(Pending);
    return out;
  }
  void Clear() { Pending.clear(); }

private:
  int Depth{0};
  std::vector<Id> Pending;
};

} // namespace cutum

#endif
