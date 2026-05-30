#pragma once

#include "IWorldGenPipeline.h"
#include "OverworldHeightSampler.h"

namespace cutum {

class OverworldPipeline : public IWorldGenPipeline {
public:
 OverworldPipeline(WorldGenContext ctx, HeightPreset preset);

 void GenerateColumn(int worldX, int worldZ) override;
 int SurfaceYAt(int worldX, int worldZ) const override;

private:
 OverworldHeightSampler heightSampler_;
 HeightPreset preset_;
};

} // namespace cutum
