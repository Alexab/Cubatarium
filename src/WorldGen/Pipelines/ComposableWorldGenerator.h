#pragma once

#include "WorldGen/Core/IWorldGenPipeline.h"
#include "WorldGen/Core/WorldGenStageMask.h"
#include "WorldGen/Features/CaveCarver.h"
#include "WorldGen/Sampling/BiomeSampler.h"
#include "WorldGen/Sampling/ColumnSample.h"
#include "WorldGen/Sampling/OverworldHeightSampler.h"
#include "WorldGen/Stages/WorldGenStages.h"
#include <optional>

namespace cutum
{

enum class ComposableTerrainMode
{
  Flat,
  LegacyHash,
  NoiseHeightmap,
};

struct ComposableWorldGenConfig
{
  ComposableTerrainMode TerrainMode{ComposableTerrainMode::NoiseHeightmap};
  HeightPreset HeightPreset{HeightPreset::Overworld};
  bool UseBiomeSurface{false};
  bool Fluids{true};
  bool Ravines{false};
  bool Caves{false};
  bool Ores{false};
  bool Vegetation{false};
  bool GroundCover{false};
  bool Decoration{false};
  bool Structures{false};
  bool LavaPools{false};
  bool FirePatch{false};
};

class UComposableWorldGenerator : public IWorldGenPipeline
{
public:
  UComposableWorldGenerator(WorldGenContext ctx, ComposableWorldGenConfig config);

  void GenerateColumn(int worldX, int worldZ) override;
  int SurfaceYAt(int worldX, int worldZ) const override;

  ColumnSampleContext BuildColumnSample(int world_x, int world_z) const;
  ColumnLayerRule BuildTerrainRuleFromSample(
      int world_x, int world_z, const ColumnSampleContext &sample) const;

  const ComposableWorldGenConfig &GetConfig() const { return Config; }
  const WorldGenStageMask &GetStageMask() const { return StageMask; }
  WorldGenContext &GetContext() { return Ctx; }

private:
  int SampleSurfaceY(int worldX, int worldZ) const;
  int SampleCoarseSurfaceY(int worldX, int worldZ) const;
  BiomeId SampleBiome(int worldX, int worldZ, int coarseY) const;
  BiomeWeightSet SampleBiomeWeights(int worldX, int worldZ, int coarseY) const;
  ColumnLayerRule BuildTerrainRule(int worldX, int worldZ, int surfaceY,
                                   BiomeId biome,
                                   const BiomeWeightSet &weights) const;

  ComposableWorldGenConfig Config;
  WorldGenStageMask StageMask;
  std::optional<UOverworldHeightSampler> HeightSampler;
  std::optional<UBiomeSampler> BiomeSampler;
  std::optional<UColumnSampleBuilder> SampleBuilder;
};

} // namespace cutum
