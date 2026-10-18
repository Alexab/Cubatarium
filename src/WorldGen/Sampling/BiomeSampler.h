#pragma once

#include "WorldGen/Core/ProceduralSettings.h"
#include "WorldGen/Core/WorldGenContext.h"
#include "WorldGen/Sampling/BiomeTypes.h"
#include <array>
#include <cstdint>
#include <functional>
#include <string>
#include <vector>

namespace cutum
{

enum class BiomeId
{
  Plains = 0,
  Forest = 1,
  Desert = 2,
  Hills = 3,
  Tundra = 4,
  Savanna = 5,
  Foothills = 6,
  Scrubland = 7,
  ColdSteppe = 8,
  Ocean = 9,
  Beach = 10,
  Mountains = 11,
  SnowySlopes = 12,
  JaggedPeaks = 13,
  LushCaves = 14,
  DripstoneCaves = 15,
  DeepDark = 16,
  Invalid = 255,
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

// Legacy classifier candidates. Surface weights themselves are always stored
// in the pack-scoped, handle-only BiomeBlend below.
constexpr int kBuiltinSurfaceBiomeCount = 14;

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

using BiomeWeightSet = BiomeBlend;

BiomeHeightProfile BiomeHeightProfileFor(BiomeId biome);
int ApplyBiomeHeightProfile(int coarseY, int seaLevel, int maxHeight,
                            const BiomeHeightProfile &profile);
using CoarseHeightCallback = std::function<int(int, int)>;

int RefineSurfaceYWithBiomes(int x, int z, int coarseY,
                             const ProceduralSettings &settings, uint32_t seed,
                             const WorldGenTuning &tuning,
                             const CoarseHeightCallback &getCoarseY = nullptr,
                             const BiomeWeightSet *weights_override = nullptr,
                             int worldgenProfileVersion = 2);

void ComputeBiomeClimate(int x, int z, uint32_t seed, float &temperature,
                         float &moisture);
BiomeWeightSet ComputeBiomeWeights(int x, int z, int coarseY, int seaLevel,
                                   int maxHeight, uint32_t seed,
                                   const WorldGenTuning &tuning,
                                   int worldgenProfileVersion = 2);
BiomeWeightSet BlendedBiomeWeights(int x, int z, int coarseY, int seaLevel,
                                   int maxHeight, uint32_t seed,
                                   const WorldGenTuning &tuning,
                                   const CoarseHeightCallback &getCoarseY = {},
                                   int worldgenProfileVersion = 2);
BiomeId DominantBiome(const BiomeWeightSet &weights);
BiomeHandle DominantBiomeHandle(const BiomeWeightSet &weights);
BiomeHandle PickSurfaceBiomeHandle(int x, int z,
                                   const BiomeWeightSet &weights);
BiomeKey BiomeKeyForHandle(BiomeHandle handle);
BiomeId PickSurfaceBiome(int x, int z, const BiomeWeightSet &weights);
SubBiomeId SubBiomeFor(int x, int z, BiomeId biome, uint32_t seed);

class UBiomeSampler
{
public:
  UBiomeSampler(uint32_t Seed, const WorldGenTuning &tuning,
                int worldgenProfileVersion = 2);

  void SetCoarseHeightCallback(CoarseHeightCallback callback);
  bool HasCoarseHeightCallback() const { return static_cast<bool>(CoarseHeightFn); }
  int CoarseYAt(int x, int z, int fallback_y) const;

  BiomeId At(int x, int z, int surfaceY, int SeaLevel, int MaxHeight) const;
  BiomeId At3D(int x, int y, int z, int surfaceY, int SeaLevel,
               int MaxHeight) const;
  BiomeKey At3DKey(int x, int y, int z, int surfaceY, int SeaLevel,
                   int MaxHeight) const;
  BiomeWeightSet WeightsAt(int x, int z, int surfaceY, int SeaLevel,
                           int MaxHeight) const;
  int RefineSurfaceY(int x, int z, int coarseY,
                     const ProceduralSettings &settings) const;
  int RefineSurfaceY(int x, int z, int coarseY,
                     const ProceduralSettings &settings,
                     const BiomeWeightSet &weights) const;
  BiomeSurfaceRule SurfaceRule(BiomeId biome, const WorldGenContext &ctx) const;
  BiomeSurfaceRule BlendedSurfaceRule(int x, int z, const BiomeWeightSet &weights,
                                      const WorldGenContext &ctx,
                                      int surfaceY) const;

private:
  uint32_t Seed;
  WorldGenTuning Tuning;
  int ProfileVersion{2};
  CoarseHeightCallback CoarseHeightFn;
};

BiomeId ClassifyBiome(float temperature, float moisture, float localHeightNorm);
BiomeId PickWeightedBiome(int x, int z, uint32_t seed, float temperature,
                          float moisture, float localHeightNorm,
                          const WorldGenTuning &tuning);

float BiomeTuningWeight(BiomeId biome, const WorldGenTuning &tuning);
int BiomeIndex(BiomeId biome);
BiomeHandle BiomeHandleForId(BiomeId biome);
float BiomeWeightFor(const BiomeWeightSet &weights, BiomeId biome);

float SampleCoarseHeightGradient(int x, int z,
                                 const CoarseHeightCallback &get_coarse_y);

BiomeSurfaceRule EvaluateSurfaceRule(int x, int z,
                                     const struct ColumnSampleContext &sample,
                                     const WorldGenContext &ctx);

float ComputeCoastBeachStrength(int x, int z, int surface_y,
                                const ProceduralSettings &settings,
                                const CoarseHeightCallback &get_coarse_y = {});
int ApplyCoastShelf(int x, int z, int surface_y,
                    const ProceduralSettings &settings,
                    const CoarseHeightCallback &get_coarse_y = {});

} // namespace cutum
