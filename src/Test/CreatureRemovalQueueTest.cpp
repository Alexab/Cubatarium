#include "Creatures/Core/CreatureRemovalQueue.h"

#include <cstdlib>
#include <iostream>
#include <memory>
#include <unordered_map>

static void Expect(bool cond, const char *message)
{
  if (!cond)
  {
    std::cerr << "creature_removal_queue_test: " << message << std::endl;
    std::exit(1);
  }
}

namespace
{

// Mirrors UWorldEnvironment: a creature map plus the removal queue.
struct FakeEnvironment
{
  std::unordered_map<uint64_t, std::unique_ptr<int>> Creatures;
  cutum::UCreatureRemovalQueue Removals;

  const int *Get(uint64_t id) const
  {
    if (Removals.IsPending(id))
    {
      return nullptr;
    }
    const auto it = Creatures.find(id);
    return it != Creatures.end() ? it->second.get() : nullptr;
  }
  void Remove(uint64_t id)
  {
    if (Creatures.find(id) == Creatures.end() || Removals.IsPending(id))
    {
      return;
    }
    if (Removals.IsIterating())
    {
      Removals.Defer(id);
      return;
    }
    Creatures.erase(id);
  }
  template <typename Fn> void ForEach(Fn fn)
  {
    Removals.BeginIteration();
    for (auto &entry : Creatures)
    {
      if (!Removals.IsPending(entry.first))
      {
        fn(entry.first, *entry.second);
      }
    }
    if (Removals.EndIteration())
    {
      for (const uint64_t id : Removals.TakePending())
      {
        Creatures.erase(id);
      }
    }
  }
};

} // namespace

int main()
{
  FakeEnvironment env;
  for (uint64_t id = 1; id <= 20; ++id)
  {
    env.Creatures[id] = std::make_unique<int>(static_cast<int>(id));
  }

  // Outside a loop removal is immediate.
  env.Remove(20);
  Expect(env.Creatures.count(20) == 0, "removal outside a loop is immediate");

  // Every creature kills the next one; the killer itself is also removed
  // (a status effect finishing it in the same visit). Nothing is destroyed
  // mid-loop, killed creatures are not visited afterwards, and lookups
  // already miss them.
  int visited = 0;
  bool lookupHidden = true;
  env.ForEach(
      [&](uint64_t id, int &value)
      {
        ++visited;
        value += 100; // touch the entry: must still be alive
        const uint64_t victim = id % 19 + 1;
        env.Remove(victim);
        env.Remove(victim); // a second request is a no-op
        lookupHidden = lookupHidden && env.Get(victim) == nullptr;
        env.Remove(id);
        Expect(env.Creatures.count(id) == 1,
               "self-removal inside the loop is deferred");
      });
  Expect(lookupHidden, "a queued creature is hidden from lookups at once");
  Expect(visited >= 1 && visited < 19,
         "creatures killed earlier in the loop are skipped");
  Expect(env.Creatures.empty(), "all queued removals applied after the loop");
  Expect(!env.Removals.HasPending(), "queue drained");

  // Nested loops flush only when the outermost one ends.
  env.Creatures[1] = std::make_unique<int>(1);
  env.Creatures[2] = std::make_unique<int>(2);
  env.ForEach(
      [&](uint64_t outer, int &)
      {
        env.ForEach([&](uint64_t inner, int &)
                    {
                      if (inner != outer)
                      {
                        env.Remove(inner);
                      }
                    });
        Expect(!env.Creatures.empty(), "inner loop end does not flush");
      });
  Expect(env.Creatures.size() == 1, "outer loop end flushes the nested kill");

  std::cout << "creature_removal_queue_test: ok" << std::endl;
  return 0;
}
