#include "Render/Engine/CreatureLightCache.h"

#include <cmath>
#include <cstdlib>
#include <iostream>

static void Expect(bool cond, const char *message)
{
  if (!cond)
  {
    std::cerr << "creature_light_cache_test: " << message << std::endl;
    std::exit(1);
  }
}

static bool Near(const glm::vec2 &a, const glm::vec2 &b)
{
  return std::abs(a.x - b.x) < 1e-4f && std::abs(a.y - b.y) < 1e-4f;
}

int main()
{
  using namespace cutum;
  UCreatureLightCache cache;
  const glm::vec2 cave(0.0f, 0.2f);
  const glm::vec2 torch(0.0f, 0.9f);

  // First sample is taken as is: no ease-in from full daylight.
  UCreatureLightCache::State &a = cache.Acquire(1, 7, 1);
  UCreatureLightCache::SetTarget(a, cave);
  Expect(Near(UCreatureLightCache::Ease(a, 1.0f / 60.0f), cave),
         "new creature starts at its sampled light");
  // Later samples ease.
  UCreatureLightCache::SetTarget(a, torch);
  const glm::vec2 eased = UCreatureLightCache::Ease(a, 1.0f / 60.0f);
  Expect(eased.y > cave.y && eased.y < torch.y, "later changes ease");

  // Same generation: id 1 keeps its state.
  Expect(cache.Acquire(1, 7, 2).Seeded, "same generation keeps state");

  // World reloaded: generation changes, id 1 is a different creature and
  // must not inherit the old light.
  UCreatureLightCache::State &reused = cache.Acquire(1, 8, 3);
  Expect(!reused.Seeded && cache.Size() == 1,
         "new generation drops every old state");
  UCreatureLightCache::SetTarget(reused, torch);
  Expect(Near(UCreatureLightCache::Ease(reused, 1.0f / 60.0f), torch),
         "reused id starts from its own light");

  // Pruning drops creatures unseen for too long.
  cache.Acquire(2, 8, 150);
  cache.Prune(700, 600);
  Expect(cache.Size() == 1, "creature unseen for 697 frames dropped");
  cache.Prune(1000, 600);
  Expect(cache.Size() == 0, "all stale states dropped");

  std::cout << "creature_light_cache_test: ok" << std::endl;
  return 0;
}
