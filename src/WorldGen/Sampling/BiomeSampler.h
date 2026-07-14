#pragma once

#include "WorldGen/Core/ProceduralSettings.h"
#include "WorldGen/Core/WorldGenContext.h"
#include <array>
#include <cstdint>
#include <functional>

namespace cutum
{

enum class BiomeId
{
  Plains,
  Forest,
  Desert,
  Hills,
  Tundra,
  Savanna,
  Foothills,
  Scrubland,
  ColdSteppe,
};

enum class SubBiomeId
{
  Default,
  Woodland,
  DenseForest,
  SparseForest,
  ScrubDesert,
  Dunes,
  SavannaDry,
  SavannaWet,
  ScrubDry,
  ScrubWet,
};

constexpr int kBiomeCount = 9;

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
using CoarseHeightCallback = std::function<int(int, int)>;

int RefineSurfaceYWithBiomes(int x, int z, int coarseY,
                             const ProceduralSettings &settings, uint32_t seed,
                             const WorldGenTuning &tuning,
                             const CoarseHeightCallback &getCoarseY = nullptr);

void ComputeBiomeClimate(int x, int z, uint32_t seed, float &temperature,
                         float &moisture);
BiomeWeightSet ComputeBiomeWeights(int x, int z, int coarseY, int seaLevel,
                                   int maxHeight, uint32_t seed,
                                   const WorldGenTuning &tuning);
BiomeWeightSet BlendedBiomeWeights(int x, int z, int coarseY, int seaLevel,
                                   int maxHeight, uint32_t seed,
                                   const WorldGenTuning &tuning,
                                   const CoarseHeightCallback &getCoarseY = {});
BiomeId DominantBiome(const BiomeWeightSet &weights);
BiomeId PickSurfaceBiome(int x, int z, const BiomeWeightSet &weights);
SubBiomeId SubBiomeFor(int x, int z, BiomeId biome, uint32_t seed);

class UBiomeSampler
{
public:
  UBiomeSampler(uint32_t Seed, const WorldGenTuning &tuning);

  void SetCoarseHeightCallback(CoarseHeightCallback callback);

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
  CoarseHeightCallback CoarseHeightFn;
};

BiomeId ClassifyBiome(float temperature, float moisture, float localHeightNorm);
BiomeId PickWeightedBiome(int x, int z, uint32_t seed, float temperature,
                          float moisture, float localHeightNorm,
                          const WorldGenTuning &tuning);

float BiomeTuningWeight(BiomeId biome, const WorldGenTuning &tuning);
int BiomeIndex(BiomeId biome);

float SampleCoarseHeightGradient(int x, int z,
                                 const CoarseHeightCallback &get_coarse_y);

BiomeSurfaceRule EvaluateSurfaceRule(int x, int z,
                                     const struct ColumnSampleContext &sample,
                                     const WorldGenContext &ctx);

} // namespace cutum
