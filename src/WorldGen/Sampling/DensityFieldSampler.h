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
  struct Column
  {
    int x{0};
    int z{0};
    int surfaceY{0};
    DensityRouteParams route{};
    float horizontalWobble{0.0f};
  };

  UDensityFieldSampler(uint32_t seed, int seaLevel, int maxHeight,
                       float terrainRoughness,
                       const DensityFieldParams &params = DensityFieldParams{},
                       const CaveParams &caves = CaveParams{},
                       int minHeight = 0);

  float SampleTerrainDensity(int x, int y, int z, int octaves = -1) const;
  float SampleDensity(int x, int y, int z, int surface_y) const;
  Column PrepareColumn(int x, int z, int surface_y) const;
  float SampleDensity(const Column &column, int y) const;
  int SurfaceYAt(int x, int z) const;
  int CoarseSurfaceYAt(int x, int z) const;

private:
  float SampleTerrainDensityAt(int x, int y, int z, int octaves,
                               const DensityRouteParams &route,
                               float horizontal_wobble) const;
  float ApplyCaveDensityOffset(int x, int y, int z, int surface_y,
                               float density) const;
  DensityRouteParams RouteAt(int x, int z) const;
  float HorizontalWobbleAt(int x, int z,
                           const DensityRouteParams &route) const;

  uint32_t Seed;
  int SeaLevel;
  int MaxHeight;
  int MinHeight;
  float TerrainRoughness;
  DensityFieldParams Params;
  CaveParams Caves;
};

} // namespace cutum
