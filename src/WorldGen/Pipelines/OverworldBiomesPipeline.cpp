#include "WorldGen/Pipelines/OverworldBiomesPipeline.h"
#include "WorldGen/Features/PrefabFeaturePlacer.h"
#include "WorldGen/Stages/WorldGenStages.h"

namespace cutum
{

UOverworldBiomesPipeline::UOverworldBiomesPipeline(WorldGenContext ctx)
    : IWorldGenPipeline(ctx),
      HeightSampler(ctx.Settings.Seed, ctx.Settings.SeaLevel,
                    ctx.Settings.MaxHeight, HeightPreset::Overworld,
                    ctx.Settings.Tuning.terrainRoughness),
      UBiomeSampler(ctx.Settings.Seed, ctx.Settings.Tuning)
{
}

void UOverworldBiomesPipeline::GenerateColumn(int worldX, int worldZ)
{
  const int naturalY = HeightSampler.SurfaceYAt(worldX, worldZ);
  const int surfaceY =
      AdjustSurfaceYForSpawnIsland(worldX, worldZ, naturalY, Ctx.Settings);
  const BiomeId biome = UBiomeSampler.At(
      worldX, worldZ, surfaceY, Ctx.Settings.SeaLevel, Ctx.Settings.MaxHeight);
  const BiomeSurfaceRule biomeRule = UBiomeSampler.SurfaceRule(biome, Ctx);

  ColumnLayerRule rule;
  rule.surfaceBlock = biomeRule.surface;
  rule.subsurfaceBlock = biomeRule.subsurface;
  rule.fillerBlock = Ctx.Stone;

  FillTerrainColumn(Ctx, worldX, worldZ, surfaceY, rule);
  FillFluidColumn(Ctx, worldX, worldZ, surfaceY);

  if (Ctx.Settings.EnableTrees)
  {
    TryPlaceVegetationFeatures(Ctx, worldX, worldZ, surfaceY, biome);
    TryPlaceDecorationFeatures(Ctx, worldX, worldZ, surfaceY, biome);
  }
  TryPlaceStructureFeatures(Ctx, worldX, worldZ, surfaceY, biome);
  TryPlaceLavaPool(Ctx, worldX, worldZ, surfaceY, biome);
  TryPlaceFirePatch(Ctx, worldX, worldZ, surfaceY, biome, Ctx.Grass);
}

int UOverworldBiomesPipeline::SurfaceYAt(int worldX, int worldZ) const
{
  const int naturalY = HeightSampler.SurfaceYAt(worldX, worldZ);
  return AdjustSurfaceYForSpawnIsland(worldX, worldZ, naturalY, Ctx.Settings);
}

} // namespace cutum
