#include "WorldGen/Pipelines/OverworldFullPipeline.h"
#include "WorldGen/Features/CaveCarver.h"
#include "WorldGen/Features/PrefabFeaturePlacer.h"
#include "WorldGen/Stages/WorldGenStages.h"

namespace cutum
{

UOverworldFullPipeline::UOverworldFullPipeline(WorldGenContext ctx)
    : IWorldGenPipeline(ctx),
      heightSampler_(ctx.Settings.seed, ctx.Settings.seaLevel,
                     ctx.Settings.maxHeight, HeightPreset::Overworld),
      biomeSampler_(ctx.Settings.seed)
{
}

void UOverworldFullPipeline::GenerateColumn(int worldX, int worldZ)
{
  const int naturalY = heightSampler_.SurfaceYAt(worldX, worldZ);
  const int surfaceY =
      AdjustSurfaceYForSpawnIsland(worldX, worldZ, naturalY, ctx_.Settings);
  const BiomeId biome =
      biomeSampler_.At(worldX, worldZ, surfaceY, ctx_.Settings.seaLevel,
                       ctx_.Settings.maxHeight);
  const BiomeSurfaceRule biomeRule = biomeSampler_.SurfaceRule(biome, ctx_);

  ColumnLayerRule rule;
  rule.surfaceBlock = biomeRule.surface;
  rule.subsurfaceBlock = biomeRule.subsurface;
  rule.fillerBlock = ctx_.Stone;

  FillTerrainColumn(ctx_, worldX, worldZ, surfaceY, rule);
  FillFluidColumn(ctx_, worldX, worldZ, surfaceY);

  if (ctx_.Settings.enableCaves)
  {
    CarveColumnCaves(ctx_, worldX, worldZ, surfaceY, ctx_.Settings.seed,
                     caveParams_);
  }

  if (ctx_.Settings.enableTrees)
  {
    TryPlaceTree(ctx_, worldX, worldZ, surfaceY, biome, featureParams_);
  }
  TryPlaceLavaPool(ctx_, worldX, worldZ, surfaceY, biome);
  TryPlaceFirePatch(ctx_, worldX, worldZ, surfaceY, biome, ctx_.Grass);
}

int UOverworldFullPipeline::SurfaceYAt(int worldX, int worldZ) const
{
  const int naturalY = heightSampler_.SurfaceYAt(worldX, worldZ);
  return AdjustSurfaceYForSpawnIsland(worldX, worldZ, naturalY, ctx_.Settings);
}

} // namespace cutum
