#include "OverworldPipeline.h"
#include "WorldGenStages.h"

namespace cutum {

OverworldPipeline::OverworldPipeline(WorldGenContext ctx, HeightPreset preset)
 : IWorldGenPipeline(ctx)
 , heightSampler_(ctx.settings.seed, ctx.settings.seaLevel, ctx.settings.maxHeight, preset)
 , preset_(preset)
{
}

void OverworldPipeline::GenerateColumn(int worldX, int worldZ)
{
 const int naturalY = heightSampler_.SurfaceYAt(worldX, worldZ);
 const int surfaceY = AdjustSurfaceYForSpawnIsland(worldX, worldZ, naturalY, ctx_.settings);
 ColumnLayerRule rule;
 rule.surfaceBlock = ctx_.grass;
 rule.subsurfaceBlock = ctx_.dirt;
 rule.fillerBlock = ctx_.stone;

 if (preset_ == HeightPreset::Mountains &&
     heightSampler_.params().stoneSurfaceAboveY > 0 &&
     surfaceY >= heightSampler_.params().stoneSurfaceAboveY) {
  rule.surfaceBlock = ctx_.stone;
  rule.subsurfaceBlock = ctx_.stone;
 }

 FillTerrainColumn(ctx_, worldX, worldZ, surfaceY, rule);
 FillFluidColumn(ctx_, worldX, worldZ, surfaceY);
}

int OverworldPipeline::SurfaceYAt(int worldX, int worldZ) const
{
 const int naturalY = heightSampler_.SurfaceYAt(worldX, worldZ);
 return AdjustSurfaceYForSpawnIsland(worldX, worldZ, naturalY, ctx_.settings);
}

} // namespace cutum
