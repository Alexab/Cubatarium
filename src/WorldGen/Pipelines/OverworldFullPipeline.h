#pragma once

#include "WorldGen/Features/CaveCarver.h"
#include "WorldGen/Features/PrefabFeaturePlacer.h"
#include "WorldGen/Pipelines/OverworldBiomesPipeline.h"

namespace cutum
{

class UOverworldFullPipeline : public IWorldGenPipeline
{
public:
  explicit UOverworldFullPipeline(WorldGenContext ctx);

  void GenerateColumn(int worldX, int worldZ) override;
  int SurfaceYAt(int worldX, int worldZ) const override;

private:
  UOverworldHeightSampler HeightSampler;
  UBiomeSampler UBiomeSampler;
  CaveParams CaveParams;
  FeatureParams FeatureParams;
};

} // namespace cutum
