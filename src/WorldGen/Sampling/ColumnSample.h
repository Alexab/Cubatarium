#pragma once

#include "WorldGen/Sampling/BiomeSampler.h"
#include "WorldGen/Sampling/ClimateSampler.h"
#include "WorldGen/Sampling/OverworldHeightSampler.h"
#include "WorldGen/Stages/WorldGenStages.h"
#include <cmath>

namespace cutum
{

class UOverworldHeightSampler;
class UBiomeSampler;

struct ColumnSampleContext
{
  ClimateSample Climate{};
  float MacroHeight01{0.f};
  float HeightNorm{0.f};
  int PreliminarySurfaceY{0};
  int SurfaceY{0};
  float SurfaceGradient{0.f};
  BiomeWeightSet Biomes{};
  BiomeId DominantBiome{BiomeId::Plains};
  BiomeId SurfaceBiome{BiomeId::Plains};
  bool SpawnSurfaceOverride{false};
  float CoastFactor{0.f};
};

class UColumnSampleBuilder
{
public:
  UColumnSampleBuilder(const UOverworldHeightSampler *height_sampler,
                       const UBiomeSampler *biome_sampler,
                       const ProceduralSettings &settings);

  ColumnSampleContext Build(int world_x, int world_z) const;

private:
  const UOverworldHeightSampler *HeightSampler;
  const UBiomeSampler *BiomeSampler;
  ProceduralSettings Settings;
};

bool ShouldSpawnSurfaceOverride(int world_x, int world_z,
                                const ProceduralSettings &settings,
                                int center_x = 0, int center_z = 0);

} // namespace cutum
