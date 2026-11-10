#include "World/Lighting/GpuSkylightColumnSeed.h"

#include <cstdlib>
#include <iostream>

namespace
{

int gFails = 0;

void Expect(bool cond, const char *msg)
{
  if (!cond)
  {
    std::cerr << "FAIL: " << msg << "\n";
    ++gFails;
  }
}

} // namespace

int main()
{
  using namespace cutum;
  std::array<uint8_t, CHUNK_VOLUME> occ{};
  for (int z = 0; z < CHUNK_SIZE; ++z)
  {
    for (int x = 0; x < CHUNK_SIZE; ++x)
    {
      occ[static_cast<size_t>((0 * CHUNK_SIZE + z) * CHUNK_SIZE + x)] = 1;
    }
  }
  std::array<uint8_t, CHUNK_VOLUME> sky{};
  SeedSkylightColumnsCpu(occ, sky);
  Expect(sky[0] == 0, "opaque floor sky=0");
  const size_t above =
      static_cast<size_t>((1 * CHUNK_SIZE + 0) * CHUNK_SIZE + 0);
  Expect(sky[above] == 15, "air above floor sky=15");

  // Overhang: opaque at y=4, air below should get sky=0 after opaque.
  occ[static_cast<size_t>((4 * CHUNK_SIZE + 0) * CHUNK_SIZE + 0)] = 1;
  SeedSkylightColumnsCpu(occ, sky);
  Expect(sky[above] == 0, "under overhang sky=0");
  Expect(sky[static_cast<size_t>((5 * CHUNK_SIZE) * CHUNK_SIZE)] == 15,
         "above overhang sky=15");

  if (gFails != 0)
  {
    std::cerr << "gpu_skylight_column_seed_test: " << gFails << " failures\n";
    return 1;
  }
  std::cout << "gpu_skylight_column_seed_test: ok\n";
  return 0;
}
