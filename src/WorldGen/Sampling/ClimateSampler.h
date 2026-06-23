#pragma once

#include <cstdint>

namespace cutum
{

struct ClimateSample
{
  float temperature{0.5f};
  float moisture{0.5f};
  float continentalness{0.5f};
  float erosion{0.5f};
  float weirdness{0.5f};
  float ridge{0.5f};
};

ClimateSample SampleClimate(int x, int z, uint32_t seed);
float ClimateAxis01(float x, float z, uint32_t seed, int seedOffset, float scale,
                    int octaves);

} // namespace cutum
