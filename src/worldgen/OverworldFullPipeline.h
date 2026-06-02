#pragma once

#include "OverworldBiomesPipeline.h"
#include "CaveCarver.h"
#include "PrefabFeaturePlacer.h"

namespace cutum {

class OverworldFullPipeline : public IWorldGenPipeline {
public:
 explicit OverworldFullPipeline(WorldGenContext ctx);

 void GenerateColumn(int worldX, int worldZ) override;
 int SurfaceYAt(int worldX, int worldZ) const override;

private:
 OverworldHeightSampler heightSampler_;
 BiomeSampler biomeSampler_;
 CaveParams caveParams_;
 FeatureParams featureParams_;
};

} // namespace cutum
