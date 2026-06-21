#pragma once

#include "WorldGen/Core/IWorldGenPipeline.h"
#include "WorldGen/Features/CaveCarver.h"
#include "WorldGen/Sampling/BiomeSampler.h"
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

private:
  int SampleSurfaceY(int worldX, int worldZ) const;
  BiomeId SampleBiome(int worldX, int worldZ, int surfaceY) const;
  BiomeWeightSet SampleBiomeWeights(int worldX, int worldZ, int surfaceY) const;
  ColumnLayerRule BuildTerrainRule(int worldX, int worldZ, int surfaceY,
                                   BiomeId biome,
                                   const BiomeWeightSet &weights) const;

  ComposableWorldGenConfig Config;
  std::optional<UOverworldHeightSampler> HeightSampler;
  std::optional<UBiomeSampler> BiomeSampler;
};

} // namespace cutum
