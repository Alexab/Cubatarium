#pragma once

#include "OverworldPipeline.h"
#include "BiomeSampler.h"
#include "PrefabFeaturePlacer.h"

namespace cutum {

class OverworldBiomesPipeline : public IWorldGenPipeline {
public:
 explicit OverworldBiomesPipeline(WorldGenContext ctx);

 void GenerateColumn(int worldX, int worldZ) override;
 int SurfaceYAt(int worldX, int worldZ) const override;

private:
 OverworldHeightSampler heightSampler_;
 BiomeSampler biomeSampler_;
 FeatureParams featureParams_;
};

} // namespace cutum
