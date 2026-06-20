#pragma once

#include "WorldGen/Core/ProceduralSettings.h"
#include "WorldGen/Core/WorldGenContext.h"
#include <array>
#include <cstdint>

namespace cutum
{

enum class BiomeId
{
  Plains,
  Forest,
  Desert,
  Hills,
  Tundra
};

enum class SubBiomeId
{
  Default,
  Woodland,
  DenseForest,
  SparseForest,
  ScrubDesert,
  Dunes,
};

constexpr int kBiomeCount = 5;

struct BiomeSurfaceRule
{
  BlockId surface{BLOCK_AIR};
  BlockId subsurface{BLOCK_AIR};
};

struct BiomeHeightProfile
{
  float baseOffsetBlocks{0.f};
  float amplitudeMultiplier{1.f};
  float volatilityMultiplier{1.f};
};

struct BiomeWeightSet
{
  std::array<float, kBiomeCount> weights{};
};

BiomeHeightProfile BiomeHeightProfileFor(BiomeId biome);
int ApplyBiomeHeightProfile(int coarseY, int seaLevel, int maxHeight,
                            const BiomeHeightProfile &profile);
int RefineSurfaceYWithBiomes(int x, int z, int coarseY,
                             const ProceduralSettings &settings, uint32_t seed,
                             const WorldGenTuning &tuning);

void ComputeBiomeClimate(int x, int z, uint32_t seed, float &temperature,
                         float &moisture);
BiomeWeightSet ComputeBiomeWeights(int x, int z, int coarseY, int seaLevel,
                                   int maxHeight, uint32_t seed,
                                   const WorldGenTuning &tuning);
BiomeWeightSet BlendedBiomeWeights(int x, int z, int coarseY, int seaLevel,
                                   int maxHeight, uint32_t seed,
                                   const WorldGenTuning &tuning);
BiomeId DominantBiome(const BiomeWeightSet &weights);
BiomeId PickSurfaceBiome(int x, int z, const BiomeWeightSet &weights);
SubBiomeId SubBiomeFor(int x, int z, BiomeId biome, uint32_t seed);

class UBiomeSampler
{
public:
  UBiomeSampler(uint32_t Seed, const WorldGenTuning &tuning);

  BiomeId At(int x, int z, int surfaceY, int SeaLevel, int MaxHeight) const;
  BiomeWeightSet WeightsAt(int x, int z, int surfaceY, int SeaLevel,
                           int MaxHeight) const;
  int RefineSurfaceY(int x, int z, int coarseY,
                     const ProceduralSettings &settings) const;
  BiomeSurfaceRule SurfaceRule(BiomeId biome, const WorldGenContext &ctx) const;
  BiomeSurfaceRule BlendedSurfaceRule(int x, int z, const BiomeWeightSet &weights,
                                      const WorldGenContext &ctx,
                                      int surfaceY) const;

private:
  uint32_t Seed;
  WorldGenTuning Tuning;
};

BiomeId ClassifyBiome(float temperature, float moisture, float localHeightNorm);
BiomeId PickWeightedBiome(int x, int z, uint32_t seed, float temperature,
                          float moisture, float localHeightNorm,
                          const WorldGenTuning &tuning);

float BiomeTuningWeight(BiomeId biome, const WorldGenTuning &tuning);
int BiomeIndex(BiomeId biome);

} // namespace cutum
