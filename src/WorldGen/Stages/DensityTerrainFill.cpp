#include "WorldGen/Stages/WorldGenStages.h"
#include "WorldGen/Sampling/DensityFieldSampler.h"
#include "World/Core/BlockWorld.h"
#include <algorithm>

namespace cutum
{

void FillTerrainColumnFromDensity(WorldGenContext &ctx, int x, int z,
                                  int surfaceY, const ColumnLayerRule &rule,
                                  const UDensityFieldSampler &sampler)
{
  if (surfaceY < 1)
  {
    return;
  }
  const int bedrockTop = std::clamp(ctx.Settings.BedrockTopY, 0, surfaceY);
  int maxY = surfaceY;
  for (int y = 0; y <= ctx.Settings.MaxHeight; ++y)
  {
    if (sampler.SampleDensity(x, y, z, surfaceY) <= 0.0f)
    {
      continue;
    }
    BlockId id = rule.fillerBlock;
    if (y <= bedrockTop)
    {
      id = ctx.Blocks.Bedrock;
    }
    else if (y < surfaceY - rule.dirtDepth - rule.stoneDepthBelowDirt)
    {
      id = rule.fillerBlock;
    }
    else if (y < surfaceY - rule.dirtDepth)
    {
      id = rule.fillerBlock;
    }
    else if (y < surfaceY)
    {
      id = rule.subsurfaceBlock;
    }
    else
    {
      id = rule.surfaceBlock;
    }
    ctx.World.SetBlock(glm::ivec3(x, y, z), id);
    maxY = std::max(maxY, y);
  }
  const int dirtyMax = std::max(maxY, ctx.Settings.SeaLevel);
  ctx.AccumulateDirtyColumn(0, dirtyMax);
}

} // namespace cutum
