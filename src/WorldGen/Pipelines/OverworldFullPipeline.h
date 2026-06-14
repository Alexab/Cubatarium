#pragma once

#include "WorldGen/Features/CaveCarver.h"
#include "WorldGen/Pipelines/OverworldBiomesPipeline.h"
#include "WorldGen/Features/PrefabFeaturePlacer.h"

namespace cutum
{

class UOverworldFullPipeline : public IWorldGenPipeline
{
public:
  explicit UOverworldFullPipeline(WorldGenContext ctx);

  void GenerateColumn(int worldX, int worldZ) override;
  int SurfaceYAt(int worldX, int worldZ) const override;

private:
  UOverworldHeightSampler heightSampler_;
  UBiomeSampler biomeSampler_;
  CaveParams caveParams_;
  FeatureParams featureParams_;
};

} // namespace cutum
