#include "WorldGen/Pipelines/ComposableWorldGenerator.h"
#include "WorldGen/Core/WorldGenPack.h"
#include "WorldGen/Features/CaveCarver.h"
#include "WorldGen/Features/OreVeinPlacer.h"
#include "WorldGen/Features/PrefabFeaturePlacer.h"
#include "WorldGen/Features/RavineCarver.h"
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
    int naturalY = HeightSampler->SurfaceYAt(worldX, worldZ);
    if (Config.UseBiomeSurface && BiomeSampler)
    {
      naturalY = BiomeSampler->RefineSurfaceY(worldX, worldZ, naturalY,
                                                Ctx.Settings);
    }
    return AdjustSurfaceYForSpawnIsland(worldX, worldZ, naturalY, Ctx.Settings);
  }
  }
}

BiomeWeightSet UComposableWorldGenerator::SampleBiomeWeights(int worldX,
                                                             int worldZ,
                                                             int surfaceY) const
{
  if (UWorldGenPack::Get().BiomeMode == WorldGenBiomeMode::Image)
  {
    BiomeWeightSet weights;
    weights.weights[BiomeIndex(UWorldGenPack::BiomeAtImage(worldX, worldZ))] =
        1.0f;
    return weights;
  }
  if (!BiomeSampler)
  {
    BiomeWeightSet weights;
    weights.weights[BiomeIndex(BiomeId::Plains)] = 1.0f;
    return weights;
  }
  return BiomeSampler->WeightsAt(worldX, worldZ, surfaceY, Ctx.Settings.SeaLevel,
                                 Ctx.Settings.MaxHeight);
}

BiomeId UComposableWorldGenerator::SampleBiome(int worldX, int worldZ,
                                               int surfaceY) const
{
  return DominantBiome(SampleBiomeWeights(worldX, worldZ, surfaceY));
}

ColumnLayerRule UComposableWorldGenerator::BuildTerrainRule(
    int worldX, int worldZ, int surfaceY, BiomeId biome,
    const BiomeWeightSet &weights) const
{
  ColumnLayerRule rule;
  rule.fillerBlock = Ctx.Stone;

  if (Config.UseBiomeSurface && BiomeSampler)
  {
    const BiomeSurfaceRule biomeRule =
        BiomeSampler->BlendedSurfaceRule(worldX, worldZ, weights, Ctx, surfaceY);
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
  (void)biome;
  return rule;
}

void UComposableWorldGenerator::GenerateColumn(int worldX, int worldZ)
{
  Ctx.ResetColumnDirty(worldX, worldZ);
  if (Config.TerrainMode == ComposableTerrainMode::Flat)
  {
    FillFlatColumn(Ctx, worldX, worldZ);
    Ctx.FlushColumnDirty();
    return;
  }
  if (Config.TerrainMode == ComposableTerrainMode::LegacyHash)
  {
    FillLegacyHashColumn(Ctx, worldX, worldZ);
    Ctx.FlushColumnDirty();
    return;
  }

  const int surfaceY = SampleSurfaceY(worldX, worldZ);
  const BiomeWeightSet weights =
      SampleBiomeWeights(worldX, worldZ, surfaceY);
  const BiomeId biome = DominantBiome(weights);
  const BiomeId featureBiome = PickSurfaceBiome(worldX, worldZ, weights);
  const ColumnLayerRule rule =
      BuildTerrainRule(worldX, worldZ, surfaceY, biome, weights);

  FillTerrainColumn(Ctx, worldX, worldZ, surfaceY, rule);
  if (Config.Ravines && Ctx.Settings.Ravines.enabled)
  {
    CarveColumnRavines(Ctx, worldX, worldZ, surfaceY, Ctx.Settings.Seed,
                       Ctx.Settings.Ravines);
  }
  if (Config.Fluids)
  {
    FillFluidColumn(Ctx, worldX, worldZ, surfaceY);
  }
  if (Config.Caves && Ctx.Settings.EnableCaves)
  {
    CarveColumnCaves(Ctx, worldX, worldZ, surfaceY, Ctx.Settings.Seed,
                     Ctx.Settings.Caves);
  }
  if (Config.Ores && Ctx.Settings.EnableOres)
  {
    FillOreVeins(Ctx, worldX, worldZ, surfaceY, Ctx.Settings.Seed,
                 Ctx.Settings.Tuning.oreDensity);
  }
  bool placedTree = false;
  if (Config.Vegetation && Ctx.Settings.EnableTrees)
  {
    placedTree =
        TryPlaceVegetationFeatures(Ctx, worldX, worldZ, surfaceY, featureBiome);
  }
  if (Config.GroundCover && Ctx.Settings.EnableTrees)
  {
    TryPlaceGroundCoverFeatures(Ctx, worldX, worldZ, surfaceY, featureBiome,
                                placedTree);
  }
  if (Config.Decoration)
  {
    TryPlaceDecorationFeatures(Ctx, worldX, worldZ, surfaceY, featureBiome);
  }
  if (Config.Structures)
  {
    TryPlaceStructureFeatures(Ctx, worldX, worldZ, surfaceY, featureBiome);
  }
  if (Config.LavaPools)
  {
    TryPlaceLavaPool(Ctx, worldX, worldZ, surfaceY, featureBiome);
  }
  if (Config.FirePatch)
  {
    TryPlaceFirePatch(Ctx, worldX, worldZ, surfaceY, featureBiome, Ctx.Grass);
  }
  Ctx.FlushColumnDirty();
}

int UComposableWorldGenerator::SurfaceYAt(int worldX, int worldZ) const
{
  return SampleSurfaceY(worldX, worldZ);
}

} // namespace cutum
