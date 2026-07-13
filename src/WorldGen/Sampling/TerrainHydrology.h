#pragma once

#include "WorldGen/Core/ProceduralSettings.h"
#include <cstdint>

namespace cutum
{

enum class TerrainHydrologyClass
{
  Ocean,
  Coast,
  Land,
};

/// Continental macro mask in [0, 1] (low = ocean basin, high = land mass).
float SampleContinentalMask01(int x, int z, uint32_t seed);

TerrainHydrologyClass ClassifyTerrainHydrology(int x, int z, uint32_t seed);

int ApplyLandSeaHeightPolicy(int x, int z, int surface_y, uint32_t seed,
                             const ProceduralSettings &settings);

/// Inclusive top Y for fluid fill above surface_y; returns surface_y when dry.
int FluidFillTopY(int x, int z, int surface_y, uint32_t seed,
                  const ProceduralSettings &settings);

} // namespace cutum
