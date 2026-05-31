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
 const int surfaceY = heightSampler_.SurfaceYAt(worldX, worldZ);
 const BiomeId biome = biomeSampler_.At(worldX, worldZ, surfaceY,
     ctx_.settings.seaLevel, ctx_.settings.maxHeight);
 const BiomeSurfaceRule biomeRule = biomeSampler_.SurfaceRule(biome, ctx_);

 ColumnLayerRule rule;
 rule.surfaceBlock = biomeRule.surface;
 rule.subsurfaceBlock = biomeRule.subsurface;
 rule.fillerBlock = ctx_.stone;

 FillTerrainColumn(ctx_, worldX, worldZ, surfaceY, rule);

 if (ctx_.settings.enableTrees) {
  TryPlaceTree(ctx_, worldX, worldZ, surfaceY, biome, featureParams_);
 }
}

int OverworldBiomesPipeline::SurfaceYAt(int worldX, int worldZ) const
{
 return heightSampler_.SurfaceYAt(worldX, worldZ);
}

} // namespace cutum
