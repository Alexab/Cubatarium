#pragma once

#include "WorldGen/Sampling/ClimateSampler.h"
#include <cstdint>

namespace cutum
{

float PeaksAndValleys(float weirdness);
/// Shared climate mask for mountain terrain uplift and mountain biomes.
float MountainClimateWeight(const ClimateSample &climate);
float ContinentalHeightBlocks(float continentalness, int seaLevel, int maxHeight,
                              int minHeight);
float ErosionAmplitudeMultiplier(float erosion);
float ClimateTerrainOffset(const ClimateSample &climate, int seaLevel,
                           int maxHeight, int minHeight,
                           float regionalNoise01, float detailNoise01,
                           float detailWeight, float amplitudeBlocks,
                           float terrainRoughness, int worldX, int worldZ,
                           uint32_t seed, float rollingWeight,
                           float rollingScale, int rollingOctaves);

} // namespace cutum
