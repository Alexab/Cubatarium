#pragma once

#include "WorldGen/Features/PrefabFeaturePlacer.h"
#include "WorldGen/Pipelines/OverworldPipeline.h"
#include "WorldGen/Sampling/BiomeSampler.h"

namespace cutum
{

class UOverworldBiomesPipeline : public IWorldGenPipeline
{
public:
  explicit UOverworldBiomesPipeline(WorldGenContext ctx);

  void GenerateColumn(int worldX, int worldZ) override;
  int SurfaceYAt(int worldX, int worldZ) const override;

private:
  UOverworldHeightSampler HeightSampler;
  UBiomeSampler UBiomeSampler;
  FeatureParams FeatureParams;
};

} // namespace cutum
