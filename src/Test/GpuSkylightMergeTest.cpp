#include "World/Chunks/Chunk.h"
#include "World/Lighting/GpuSkylightColumnSeed.h"
#include "World/Lighting/LightUtil.h"

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
  UChunk chunk(glm::ivec3(0, 0, 0));

  // Simulate GPU skylight seed: sky=15 in air, sky=0 under floor.
  chunk.SetLightLocal(glm::ivec3(0, 0, 0), 0, 0);
  chunk.SetLightLocal(glm::ivec3(0, 1, 0), 15, 0);
  chunk.SetLightLocal(glm::ivec3(1, 2, 3), 12, 0);

  std::array<uint8_t, CHUNK_VOLUME> async_packed{};
  for (int ly = 0; ly < CHUNK_SIZE; ++ly)
  {
    for (int lz = 0; lz < CHUNK_SIZE; ++lz)
    {
      for (int lx = 0; lx < CHUNK_SIZE; ++lx)
      {
        const int li = (ly * CHUNK_SIZE + lz) * CHUNK_SIZE + lx;
        const int block = (lx + lz + ly) % 8;
        async_packed[static_cast<size_t>(li)] = PackLight(3, block);
      }
    }
  }

  MergeBlockLightKeepingGpuSky(chunk, async_packed);

  Expect(chunk.GetSkyLightLocal(glm::ivec3(0, 0, 0)) == 0,
         "floor sky preserved");
  Expect(chunk.GetSkyLightLocal(glm::ivec3(0, 1, 0)) == 15,
         "air sky preserved");
  Expect(chunk.GetSkyLightLocal(glm::ivec3(1, 2, 3)) == 12,
         "arbitrary sky preserved");

  const int li_floor = (0 * CHUNK_SIZE + 0) * CHUNK_SIZE + 0;
  Expect(UnpackBlock(chunk.GetLightDataMutable()[static_cast<size_t>(li_floor)]) ==
             UnpackBlock(async_packed[static_cast<size_t>(li_floor)]),
         "floor block light from async");
  const int li_air = (1 * CHUNK_SIZE + 0) * CHUNK_SIZE + 0;
  Expect(UnpackBlock(chunk.GetLightDataMutable()[static_cast<size_t>(li_air)]) ==
             UnpackBlock(async_packed[static_cast<size_t>(li_air)]),
         "air block light from async");

  if (gFails != 0)
  {
    std::cerr << "gpu_skylight_merge_test: " << gFails << " failures\n";
    return 1;
  }
  std::cout << "gpu_skylight_merge_test: ok\n";
  return 0;
}
