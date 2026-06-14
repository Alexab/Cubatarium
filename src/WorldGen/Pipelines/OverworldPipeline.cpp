#include "WorldGen/Pipelines/OverworldPipeline.h"
#include "WorldGen/Stages/WorldGenStages.h"

namespace cutum
{

UOverworldPipeline::UOverworldPipeline(WorldGenContext ctx, HeightPreset preset)
    : IWorldGenPipeline(ctx),
      heightSampler_(ctx.Settings.seed, ctx.Settings.seaLevel,
                     ctx.Settings.maxHeight, preset),
      preset_(preset)
{
}

void UOverworldPipeline::GenerateColumn(int worldX, int worldZ)
{
  const int naturalY = heightSampler_.SurfaceYAt(worldX, worldZ);
  const int surfaceY =
      AdjustSurfaceYForSpawnIsland(worldX, worldZ, naturalY, ctx_.Settings);
  ColumnLayerRule rule;
  rule.surfaceBlock = ctx_.Grass;
  rule.subsurfaceBlock = ctx_.Dirt;
  rule.fillerBlock = ctx_.Stone;

  if (preset_ == HeightPreset::Mountains &&
      heightSampler_.params().stoneSurfaceAboveY > 0 &&
      surfaceY >= heightSampler_.params().stoneSurfaceAboveY)
  {
    rule.surfaceBlock = ctx_.Stone;
    rule.subsurfaceBlock = ctx_.Stone;
  }

  FillTerrainColumn(ctx_, worldX, worldZ, surfaceY, rule);
  FillFluidColumn(ctx_, worldX, worldZ, surfaceY);
}

int UOverworldPipeline::SurfaceYAt(int worldX, int worldZ) const
{
  const int naturalY = heightSampler_.SurfaceYAt(worldX, worldZ);
  return AdjustSurfaceYForSpawnIsland(worldX, worldZ, naturalY, ctx_.Settings);
}

} // namespace cutum
