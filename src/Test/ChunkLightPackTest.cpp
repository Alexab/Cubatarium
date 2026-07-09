#include "World/Lighting/LightUtil.h"

#include <cstdlib>
#include <iostream>

static void Expect(bool cond, const char *message)
{
  if (!cond)
  {
    std::cerr << "chunk_light_pack_test: " << message << std::endl;
    std::exit(1);
  }
}

int main()
{
  Expect(cutum::UnpackSky(0) == 0, "zero sky");
  Expect(cutum::UnpackBlock(0) == 0, "zero block");

  const uint8_t packed = cutum::PackLight(15, 13);
  Expect(cutum::UnpackSky(packed) == 15, "max sky round-trip");
  Expect(cutum::UnpackBlock(packed) == 13, "block round-trip");

  const uint8_t clamped = cutum::PackLight(99, -3);
  Expect(cutum::UnpackSky(clamped) == 15, "sky clamp high");
  Expect(cutum::UnpackBlock(clamped) == 0, "block clamp low");

  for (int sky = 0; sky <= cutum::kMaxLightLevel; ++sky)
  {
    for (int block = 0; block <= cutum::kMaxLightLevel; ++block)
    {
      const uint8_t value = cutum::PackLight(sky, block);
      Expect(cutum::UnpackSky(value) == sky, "sky exhaustive");
      Expect(cutum::UnpackBlock(value) == block, "block exhaustive");
    }
  }

  std::cout << "chunk_light_pack_test: OK" << std::endl;
  return 0;
}
