#pragma once

#include "WorldGen/Core/IWorldGenPipeline.h"
#include "WorldGen/Sampling/OverworldHeightSampler.h"

namespace cutum
{

class UOverworldPipeline : public IWorldGenPipeline
{
public:
  UOverworldPipeline(WorldGenContext ctx, HeightPreset preset);

  void GenerateColumn(int worldX, int worldZ) override;
  int SurfaceYAt(int worldX, int worldZ) const override;

private:
  UOverworldHeightSampler heightSampler_;
  HeightPreset preset_;
};

} // namespace cutum
