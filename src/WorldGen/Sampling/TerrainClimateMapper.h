#pragma once

#include "WorldGen/Sampling/ClimateSampler.h"
#include <cstdint>

namespace cutum
{

float PeaksAndValleys(float weirdness);
float ContinentalHeightBlocks(float continentalness, int seaLevel, int maxHeight);
float ErosionAmplitudeMultiplier(float erosion);
float ClimateTerrainOffset(const ClimateSample &climate, int seaLevel, int maxHeight,
                           float regionalNoise01, float detailNoise01,
                           float detailWeight, float amplitudeBlocks,
                           float terrainRoughness);

} // namespace cutum
