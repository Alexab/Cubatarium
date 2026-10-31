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

  // Sample cell: blocks are centred on integers; ground block at y = 64
  // (top at 64.5). A tortoise (height 0.4) standing on it has its centre at
  // y = 64.7: that is cell 65 (air), not 64 (the ground, light 0).
  const auto groundOnly = [](const glm::ivec3 &c) { return c.y <= 64; };
  const glm::ivec3 tortoise =
      CreatureLightSampleCell(glm::vec3(10.3f, 64.7f, -3.6f), groundOnly);
  Expect(tortoise == glm::ivec3(10, 65, -4),
         "low body samples the air cell it stands in, rounded per axis");
  // A body centre inside a solid cell (clipping a slab) samples the cell above.
  const glm::ivec3 clipped =
      CreatureLightSampleCell(glm::vec3(0.0f, 64.2f, 0.0f), groundOnly);
  Expect(clipped == glm::ivec3(0, 65, 0), "solid centre cell: sample above");

  std::cout << "creature_light_cache_test: ok" << std::endl;
  return 0;
}
