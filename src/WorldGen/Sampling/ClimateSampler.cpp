#include "WorldGen/Sampling/ClimateSampler.h"
#include "WorldGen/Core/Noise.h"
#include <algorithm>

namespace cutum
{

float ClimateAxis01(float x, float z, uint32_t seed, int seedOffset, float scale,
                    int octaves)
{
  const float raw =
      NormalizedFBM2D(x * scale, z * scale, seed + static_cast<uint32_t>(seedOffset),
                      octaves, 0.5f, 2.0f);
  return std::clamp((raw + 1.0f) * 0.5f, 0.0f, 1.0f);
}

ClimateSample SampleClimate(int x, int z, uint32_t seed)
{
  const float wx = static_cast<float>(x);
  const float wz = static_cast<float>(z);
  ClimateSample c;
  c.temperature = ClimateAxis01(wx, wz, seed, 1000, 0.002f, 3);
  c.moisture = ClimateAxis01(wx, wz, seed, 2000, 0.002f, 3);
  c.continentalness = ClimateAxis01(wx, wz, seed, 3000, 0.0015f, 2);
  c.erosion = ClimateAxis01(wx, wz, seed, 4000, 0.002f, 3);
  c.weirdness = ClimateAxis01(wx, wz, seed, 5000, 0.003f, 2);
  c.ridge = ClimateAxis01(wx, wz, seed, 6000, 0.004f, 2);
  return c;
}

} // namespace cutum
