#pragma once

#include "WorldGen/Features/CaveCarver.h"
#include "WorldGen/Sampling/DensityRouter.h"
#include <cstdint>

namespace cutum
{

struct DensityFieldParams
{
  float noiseScale{0.014f};
  int octaves{4};
  float persistence{0.5f};
  float lacunarity{2.0f};
  bool cavesInDensity{false};
  float caveAmplitude{18.0f};
};

class UDensityFieldSampler
{
public:
  UDensityFieldSampler(uint32_t seed, int seaLevel, int maxHeight,
                       float terrainRoughness,
                       const DensityFieldParams &params = DensityFieldParams{},
                       const CaveParams &caves = CaveParams{});

  float SampleDensity(int x, int y, int z) const;
  int SurfaceYAt(int x, int z) const;
  int CoarseSurfaceYAt(int x, int z) const;

private:
  float SampleDensityAt(int x, int y, int z, int octaves) const;
  DensityRouteParams RouteAt(int x, int z) const;

  uint32_t Seed;
  int SeaLevel;
  int MaxHeight;
  float TerrainRoughness;
  DensityFieldParams Params;
  CaveParams Caves;
};

} // namespace cutum
