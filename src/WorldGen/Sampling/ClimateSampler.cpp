#include "WorldGen/Sampling/ClimateSampler.h"
#include "WorldGen/Core/Noise.h"
#include "WorldGen/Core/WorldGenPack.h"
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
  const PackClimateConfig &pack = UWorldGenPack::ClimateConfig();

  ClimateAxisPackConfig temperature{0.002f, 3, 1000};
  ClimateAxisPackConfig moisture{0.002f, 3, 2000};
  ClimateAxisPackConfig continentalness{0.0015f, 2, 3000};
  ClimateAxisPackConfig erosion{0.002f, 3, 4000};
  ClimateAxisPackConfig weirdness{0.003f, 2, 5000};
  ClimateAxisPackConfig ridge{0.004f, 2, 6000};
  if (pack.Loaded)
  {
    temperature = pack.Temperature;
    moisture = pack.Moisture;
    continentalness = pack.Continentalness;
    erosion = pack.Erosion;
    weirdness = pack.Weirdness;
    ridge = pack.Ridge;
  }

  ClimateSample c;
  c.temperature =
      ClimateAxis01(wx, wz, seed, temperature.SeedOffset, temperature.Scale,
                    temperature.Octaves);
  c.moisture = ClimateAxis01(wx, wz, seed, moisture.SeedOffset, moisture.Scale,
                              moisture.Octaves);
  c.continentalness =
      ClimateAxis01(wx, wz, seed, continentalness.SeedOffset,
                    continentalness.Scale, continentalness.Octaves);
  c.erosion = ClimateAxis01(wx, wz, seed, erosion.SeedOffset, erosion.Scale,
                            erosion.Octaves);
  c.weirdness = ClimateAxis01(wx, wz, seed, weirdness.SeedOffset, weirdness.Scale,
                              weirdness.Octaves);
  c.ridge = ClimateAxis01(wx, wz, seed, ridge.SeedOffset, ridge.Scale,
                          ridge.Octaves);
  return c;
}

} // namespace cutum
