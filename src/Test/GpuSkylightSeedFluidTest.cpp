#include "World/Lighting/GpuSkylightColumnSeed.h"

#include <array>
#include <cstdint>
#include <iostream>

namespace
{

int g_failures = 0;

void Expect(bool ok, const char *what)
{
  if (!ok)
  {
    ++g_failures;
    std::cerr << "FAIL: " << what << "\n";
  }
}

size_t At(int x, int y, int z)
{
  return static_cast<size_t>((y * cutum::CHUNK_SIZE + z) * cutum::CHUNK_SIZE + x);
}

} // namespace

int main()
{
  // Column (7, z=7): 4 fluid blocks at y=10..13, opaque floor at y=9.
  {
    std::array<uint8_t, cutum::CHUNK_VOLUME> occ{};
    std::array<uint8_t, cutum::CHUNK_VOLUME> sky{};
    for (int y = 10; y <= 13; ++y)
    {
      occ[At(7, y, 7)] = 2u;
    }
    occ[At(7, 9, 7)] = 1u;
    cutum::SeedSkylightColumnsCpu(occ, sky);
    Expect(sky[At(7, 14, 7)] == 15, "air above water keeps 15");
    Expect(sky[At(7, 13, 7)] == 15, "first fluid block gets the incoming level");
    Expect(sky[At(7, 12, 7)] == 14, "second fluid block attenuates by 1");
    Expect(sky[At(7, 11, 7)] == 13, "third fluid block attenuates by 1");
    Expect(sky[At(7, 10, 7)] == 12, "fourth fluid block attenuates by 1");
    Expect(sky[At(7, 9, 7)] == 0, "opaque floor is dark");
    Expect(sky[At(7, 8, 7)] == 0, "below opaque stays dark");
  }

  // Deep water (full 16-block column) attenuates to 0 — no negative levels.
  {
    std::array<uint8_t, cutum::CHUNK_VOLUME> occ{};
    std::array<uint8_t, cutum::CHUNK_VOLUME> sky{};
    for (int y = 0; y < cutum::CHUNK_SIZE; ++y)
    {
      occ[At(3, y, 3)] = 2u;
    }
    cutum::SeedSkylightColumnsCpu(occ, sky);
    Expect(sky[At(3, 15, 3)] == 15, "water surface keeps 15");
    Expect(sky[At(3, 8, 3)] == 8, "mid-column attenuates by 1 per block");
    Expect(sky[At(3, 0, 3)] == 0, "deep water bottom clamps at 0");
  }

  // Air column: unchanged behavior (15 everywhere, opaque zeroes below).
  {
    std::array<uint8_t, cutum::CHUNK_VOLUME> occ{};
    std::array<uint8_t, cutum::CHUNK_VOLUME> sky{};
    occ[At(1, 5, 1)] = 1u;
    cutum::SeedSkylightColumnsCpu(occ, sky);
    Expect(sky[At(1, 10, 1)] == 15, "air above opaque keeps 15");
    Expect(sky[At(1, 5, 1)] == 0, "opaque cell is 0");
    Expect(sky[At(1, 4, 1)] == 0, "below opaque is 0");
  }

  if (g_failures != 0)
  {
    std::cerr << "gpu_skylight_seed_fluid_test: " << g_failures
              << " failure(s)\n";
    return 1;
  }
  std::cout << "gpu_skylight_seed_fluid_test: OK\n";
  return 0;
}
