#include "WorldGen/Core/Noise.h"

#include <cmath>
#include <cstdint>
#include <iostream>

namespace
{

constexpr const char *kTestName = "noise_permutation_cache_test";

void Expect(bool ok, const char *msg)
{
  if (!ok)
  {
    std::cerr << kTestName << " FAIL: " << msg << std::endl;
    std::exit(1);
  }
}

} // namespace

int main()
{
  const uint32_t seeds[] = {0u, 1u, 42u, 12345u, 0x9E3779B9u, 0xDEADBEEFu};
  for (uint32_t seed : seeds)
  {
    const auto uncached = cutum::BuildNoisePermutationForSeed(seed);
    // Warm + hit the thread-local cache via public FBM (uses PermutationForSeed).
    const float a = cutum::NormalizedFBM2D(1.25f, 3.5f, seed, 4, 0.5f, 2.0f);
    const float b = cutum::NormalizedFBM2D(1.25f, 3.5f, seed, 4, 0.5f, 2.0f);
    Expect(a == b, "cached FBM must be bit-stable across calls");

    // Rebuild uncached again — must match first uncached (deterministic builder).
    const auto uncached2 = cutum::BuildNoisePermutationForSeed(seed);
    Expect(uncached == uncached2, "uncached permutation must be deterministic");

    // Exercise many seeds so LRU rotates; values must stay stable.
    for (uint32_t offset = 0; offset < 32; ++offset)
    {
      const uint32_t s = seed + offset * 17u;
      const float v0 = cutum::FBM2D(0.1f, 0.2f, s, 3, 0.5f, 2.0f);
      const float v1 = cutum::FBM2D(0.1f, 0.2f, s, 3, 0.5f, 2.0f);
      Expect(v0 == v1, "FBM2D must match after LRU churn");
      const float p0 = cutum::Perlin3D(0.3f, 0.4f, 0.5f, s);
      const float p1 = cutum::Perlin3D(0.3f, 0.4f, 0.5f, s);
      Expect(p0 == p1, "Perlin3D must match after LRU churn");
      (void)std::isfinite(v0);
    }
  }

  std::cout << kTestName << " OK" << std::endl;
  return 0;
}
