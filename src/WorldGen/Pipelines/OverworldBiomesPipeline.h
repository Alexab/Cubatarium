#pragma once

#include "WorldGen/Sampling/BiomeSampler.h"
#include "WorldGen/Pipelines/OverworldPipeline.h"
#include "WorldGen/Features/PrefabFeaturePlacer.h"

namespace cutum
{

class UOverworldBiomesPipeline : public IWorldGenPipeline
{
public:
  explicit UOverworldBiomesPipeline(WorldGenContext ctx);

  void GenerateColumn(int worldX, int worldZ) override;
  int SurfaceYAt(int worldX, int worldZ) const override;

private:
  UOverworldHeightSampler heightSampler_;
  UBiomeSampler biomeSampler_;
  FeatureParams featureParams_;
};

} // namespace cutum
