#include "WorldGen/Sampling/TerrainHydrology.h"

#include "WorldGen/Core/Noise.h"
#ifndef CUTUM_TERRAIN_HYDROLOGY_NO_PACK
#include "WorldGen/Core/WorldGenPack.h"
#endif
#include <algorithm>
#include <cmath>

namespace cutum
{

namespace
{

struct ContinentalParams
{
  float scale{0.003f};
  int octaves{2};
  float sea_bias{0.45f};
  float coast_band{0.05f};
};

ContinentalParams ContinentalParamsForSeed(uint32_t seed)
{
  (void)seed;
  ContinentalParams p;
#ifndef CUTUM_TERRAIN_HYDROLOGY_NO_PACK
  const PackHeightConfig &pack = UWorldGenPack::HeightConfig();
  if (pack.Loaded)
  {
    p.scale = pack.Continental.Scale;
    p.octaves = pack.Continental.Octaves;
    p.sea_bias = pack.SeaBias;
  }
#endif
  return p;
}

} // namespace

float SampleContinentalMask01(int x, int z, uint32_t seed)
{
  const ContinentalParams p = ContinentalParamsForSeed(seed);
  const float wx = static_cast<float>(x);
  const float wz = static_cast<float>(z);
  float continental = NormalizedFBM2D(wx * p.scale, wz * p.scale, seed,
                                      p.octaves, 0.5f, 2.0f);
  continental = (continental + 1.0f) * 0.5f;
  return std::clamp(continental, 0.0f, 1.0f);
}

TerrainHydrologyClass ClassifyTerrainHydrology(int x, int z, uint32_t seed)
{
  const ContinentalParams p = ContinentalParamsForSeed(seed);
  const float land01 = SampleContinentalMask01(x, z, seed);
  const float coast_half = p.coast_band * 0.5f;
  if (land01 >= p.sea_bias + coast_half)
  {
    return TerrainHydrologyClass::Land;
  }
  if (land01 <= p.sea_bias - p.coast_band)
  {
    return TerrainHydrologyClass::Ocean;
  }
  return TerrainHydrologyClass::Coast;
}

int ApplyLandSeaHeightPolicy(int x, int z, int surface_y, uint32_t seed,
                             const ProceduralSettings &settings)
{
  const int sea = settings.SeaLevel;
  switch (ClassifyTerrainHydrology(x, z, seed))
  {
  case TerrainHydrologyClass::Land:
    return std::max(surface_y, sea + 1);
  case TerrainHydrologyClass::Coast:
    return std::clamp(surface_y, sea - 1, sea + 4);
  case TerrainHydrologyClass::Ocean:
    return std::min(surface_y, sea - 1);
  }
  return surface_y;
}

int FluidFillTopY(int x, int z, int surface_y, uint32_t seed,
                  const ProceduralSettings &settings)
{
  const int sea = settings.SeaLevel;
  if (surface_y >= sea)
  {
    return surface_y;
  }

  const TerrainHydrologyClass hydrology = ClassifyTerrainHydrology(x, z, seed);
  if (hydrology == TerrainHydrologyClass::Ocean && surface_y <= sea - 4)
  {
    return sea;
  }

  switch (hydrology)
  {
  case TerrainHydrologyClass::Ocean:
    return sea;
  case TerrainHydrologyClass::Coast:
    if (surface_y >= sea - 2)
    {
      return sea - 1;
    }
    return std::min(sea, surface_y + 3);
  case TerrainHydrologyClass::Land:
    return std::min(sea - 1, surface_y + 8);
  }
  return sea;
}

} // namespace cutum
