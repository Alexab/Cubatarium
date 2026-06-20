#include "WorldGen/Pipelines/ComposableWorldGenerator.h"
#include "WorldGen/Features/PrefabFeaturePlacer.h"
#include "WorldGen/Stages/WorldGenStages.h"

namespace cutum
{

UComposableWorldGenerator::UComposableWorldGenerator(WorldGenContext ctx,
                                                   ComposableWorldGenConfig config)
    : IWorldGenPipeline(ctx), Config(config)
{
  if (Config.TerrainMode == ComposableTerrainMode::NoiseHeightmap)
  {
    HeightSampler.emplace(ctx.Settings.Seed, ctx.Settings.SeaLevel,
                          ctx.Settings.MaxHeight, Config.HeightPreset,
                          ctx.Settings.Tuning.terrainRoughness);
  }
  if (Config.UseBiomeSurface)
  {
    BiomeSampler.emplace(ctx.Settings.Seed, ctx.Settings.Tuning);
  }
}

int UComposableWorldGenerator::SampleSurfaceY(int worldX, int worldZ) const
{
  switch (Config.TerrainMode)
  {
  case ComposableTerrainMode::Flat:
    return Ctx.Settings.FlatSurfaceY;
  case ComposableTerrainMode::LegacyHash:
  {
    const int naturalY = LegacyHashSurfaceY(worldX, worldZ, Ctx.Settings);
    return AdjustSurfaceYForSpawnIsland(worldX, worldZ, naturalY, Ctx.Settings);
  }
  case ComposableTerrainMode::NoiseHeightmap:
  default:
  {
    const int naturalY = HeightSampler->SurfaceYAt(worldX, worldZ);
    return AdjustSurfaceYForSpawnIsland(worldX, worldZ, naturalY, Ctx.Settings);
  }
  }
}

BiomeId UComposableWorldGenerator::SampleBiome(int worldX, int worldZ,
                                             int surfaceY) const
{
  if (!BiomeSampler)
  {
    return BiomeId::Plains;
  }
  return BiomeSampler->At(worldX, worldZ, surfaceY, Ctx.Settings.SeaLevel,
                          Ctx.Settings.MaxHeight);
}

ColumnLayerRule UComposableWorldGenerator::BuildTerrainRule(int worldX,
                                                            int worldZ,
                                                            int surfaceY,
                                                            BiomeId biome) const
{
  ColumnLayerRule rule;
  rule.fillerBlock = Ctx.Stone;

  if (Config.UseBiomeSurface && BiomeSampler)
  {
    const BiomeSurfaceRule biomeRule = BiomeSampler->SurfaceRule(biome, Ctx);
    rule.surfaceBlock = biomeRule.surface;
    rule.subsurfaceBlock = biomeRule.subsurface;
    return rule;
  }

  rule.surfaceBlock = Ctx.Grass;
  rule.subsurfaceBlock = Ctx.Dirt;
  if (Config.TerrainMode == ComposableTerrainMode::NoiseHeightmap &&
      Config.HeightPreset == HeightPreset::Mountains && HeightSampler)
  {
    const int stoneAbove = HeightSampler->params().stoneSurfaceAboveY;
    if (stoneAbove > 0 && surfaceY >= stoneAbove)
    {
      rule.surfaceBlock = Ctx.Stone;
      rule.subsurfaceBlock = Ctx.Stone;
    }
  }
  (void)worldX;
  (void)worldZ;
  return rule;
}

void UComposableWorldGenerator::GenerateColumn(int worldX, int worldZ)
{
  if (Config.TerrainMode == ComposableTerrainMode::Flat)
  {
    FillFlatColumn(Ctx, worldX, worldZ);
    return;
  }
  if (Config.TerrainMode == ComposableTerrainMode::LegacyHash)
  {
    FillLegacyHashColumn(Ctx, worldX, worldZ);
    return;
  }

  const int surfaceY = SampleSurfaceY(worldX, worldZ);
  const BiomeId biome = SampleBiome(worldX, worldZ, surfaceY);
  const ColumnLayerRule rule =
      BuildTerrainRule(worldX, worldZ, surfaceY, biome);

  FillTerrainColumn(Ctx, worldX, worldZ, surfaceY, rule);
  if (Config.Fluids)
  {
    FillFluidColumn(Ctx, worldX, worldZ, surfaceY);
  }
  if (Config.Caves && Ctx.Settings.EnableCaves)
  {
    CarveColumnCaves(Ctx, worldX, worldZ, surfaceY, Ctx.Settings.Seed,
                     CaveParams);
  }
  if (Config.Vegetation && Ctx.Settings.EnableTrees)
  {
    TryPlaceVegetationFeatures(Ctx, worldX, worldZ, surfaceY, biome);
  }
  if (Config.Decoration && Ctx.Settings.EnableTrees)
  {
    TryPlaceDecorationFeatures(Ctx, worldX, worldZ, surfaceY, biome);
  }
  if (Config.Structures)
  {
    TryPlaceStructureFeatures(Ctx, worldX, worldZ, surfaceY, biome);
  }
  if (Config.LavaPools)
  {
    TryPlaceLavaPool(Ctx, worldX, worldZ, surfaceY, biome);
  }
  if (Config.FirePatch)
  {
    TryPlaceFirePatch(Ctx, worldX, worldZ, surfaceY, biome, Ctx.Grass);
  }
}

int UComposableWorldGenerator::SurfaceYAt(int worldX, int worldZ) const
{
  return SampleSurfaceY(worldX, worldZ);
}

} // namespace cutum
