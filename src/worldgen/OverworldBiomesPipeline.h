#pragma once

#include "BiomeSampler.h"
#include "OverworldPipeline.h"
#include "PrefabFeaturePlacer.h"

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
