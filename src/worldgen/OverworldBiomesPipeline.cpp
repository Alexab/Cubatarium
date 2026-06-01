#include "OverworldBiomesPipeline.h"
#include "WorldGenStages.h"
#include "PrefabFeaturePlacer.h"

namespace cutum {

OverworldBiomesPipeline::OverworldBiomesPipeline(WorldGenContext ctx)
 : IWorldGenPipeline(ctx)
 , heightSampler_(ctx.settings.seed, ctx.settings.seaLevel, ctx.settings.maxHeight, HeightPreset::Overworld)
 , biomeSampler_(ctx.settings.seed)
{
}

void OverworldBiomesPipeline::GenerateColumn(int worldX, int worldZ)
{
 const int naturalY = heightSampler_.SurfaceYAt(worldX, worldZ);
 const int surfaceY = AdjustSurfaceYForSpawnIsland(worldX, worldZ, naturalY, ctx_.settings);
 const BiomeId biome = biomeSampler_.At(worldX, worldZ, surfaceY,
     ctx_.settings.seaLevel, ctx_.settings.maxHeight);
 const BiomeSurfaceRule biomeRule = biomeSampler_.SurfaceRule(biome, ctx_);

 ColumnLayerRule rule;
 rule.surfaceBlock = biomeRule.surface;
 rule.subsurfaceBlock = biomeRule.subsurface;
 rule.fillerBlock = ctx_.stone;

 FillTerrainColumn(ctx_, worldX, worldZ, surfaceY, rule);
 FillFluidColumn(ctx_, worldX, worldZ, surfaceY);

 if (ctx_.settings.enableTrees) {
  TryPlaceTree(ctx_, worldX, worldZ, surfaceY, biome, featureParams_);
 }
 TryPlaceLavaPool(ctx_, worldX, worldZ, surfaceY, biome);
 TryPlaceFirePatch(ctx_, worldX, worldZ, surfaceY, biome, ctx_.grass);
}

int OverworldBiomesPipeline::SurfaceYAt(int worldX, int worldZ) const
{
 const int naturalY = heightSampler_.SurfaceYAt(worldX, worldZ);
 return AdjustSurfaceYForSpawnIsland(worldX, worldZ, naturalY, ctx_.settings);
}

} // namespace cutum
