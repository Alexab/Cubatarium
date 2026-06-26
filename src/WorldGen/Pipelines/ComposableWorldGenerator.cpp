#include "WorldGen/Pipelines/ComposableWorldGenerator.h"
#include "WorldGen/Core/WorldGenPack.h"
#include "WorldGen/Pipelines/WorldGenStageRunner.h"
#include "WorldGen/Features/CaveCarver.h"
#include "WorldGen/Features/OreVeinPlacer.h"
#include "WorldGen/Features/PrefabFeaturePlacer.h"
#include "WorldGen/Features/RavineCarver.h"
#include "WorldGen/Stages/WorldGenStages.h"

namespace cutum
{

// Pipeline: climate sample -> height -> biomes -> surface fill -> ravines ->
// caves -> fluids -> ores -> vegetation -> ground_cover -> decoration ->
// structures -> lava -> fire.
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
    if (HeightSampler)
    {
      BiomeSampler->SetCoarseHeightCallback(
          [this](int hx, int hz)
          { return HeightSampler->CoarseSurfaceYAt(hx, hz); });
    }
    SampleBuilder.emplace(HeightSampler.has_value() ? &*HeightSampler : nullptr,
                          BiomeSampler.has_value() ? &*BiomeSampler : nullptr,
                          ctx.Settings);
  }
}

ColumnSampleContext UComposableWorldGenerator::BuildColumnSample(
    int world_x, int world_z) const
{
  if (SampleBuilder)
  {
    return SampleBuilder->Build(world_x, world_z);
  }
  ColumnSampleContext sample;
  sample.PreliminarySurfaceY = SampleCoarseSurfaceY(world_x, world_z);
  sample.SurfaceY = SampleSurfaceY(world_x, world_z);
  sample.Biomes =
      SampleBiomeWeights(world_x, world_z, sample.PreliminarySurfaceY);
  sample.DominantBiome = DominantBiome(sample.Biomes);
  sample.SurfaceBiome = PickSurfaceBiome(world_x, world_z, sample.Biomes);
  return sample;
}

int UComposableWorldGenerator::SampleSurfaceY(int world_x, int world_z) const
{
  if (SampleBuilder)
  {
    return BuildColumnSample(world_x, world_z).SurfaceY;
  }
  switch (Config.TerrainMode)
  {
  case ComposableTerrainMode::Flat:
    return Ctx.Settings.FlatSurfaceY;
  case ComposableTerrainMode::LegacyHash:
  {
    const int natural_y = LegacyHashSurfaceY(world_x, world_z, Ctx.Settings);
    return AdjustSurfaceYForSpawnIsland(world_x, world_z, natural_y,
                                        Ctx.Settings);
  }
  case ComposableTerrainMode::NoiseHeightmap:
  default:
  {
    int natural_y = HeightSampler->SurfaceYAt(world_x, world_z);
    if (Config.UseBiomeSurface && BiomeSampler)
    {
      natural_y = BiomeSampler->RefineSurfaceY(world_x, world_z, natural_y,
                                                Ctx.Settings);
    }
    return AdjustSurfaceYForSpawnIsland(world_x, world_z, natural_y,
                                        Ctx.Settings);
  }
  }
}

int UComposableWorldGenerator::SampleCoarseSurfaceY(int world_x,
                                                    int world_z) const
{
  if (Config.TerrainMode == ComposableTerrainMode::NoiseHeightmap &&
      HeightSampler)
  {
    return HeightSampler->CoarseSurfaceYAt(world_x, world_z);
  }
  return SampleSurfaceY(world_x, world_z);
}

BiomeWeightSet UComposableWorldGenerator::SampleBiomeWeights(
    int world_x, int world_z, int coarse_y) const
{
  if (UWorldGenPack::Get().BiomeMode == WorldGenBiomeMode::Image)
  {
    BiomeWeightSet weights;
    weights.weights[BiomeIndex(UWorldGenPack::BiomeAtImage(world_x, world_z))] =
        1.0f;
    return weights;
  }
  if (!BiomeSampler)
  {
    BiomeWeightSet weights;
    weights.weights[BiomeIndex(BiomeId::Plains)] = 1.0f;
    return weights;
  }
  return BiomeSampler->WeightsAt(world_x, world_z, coarse_y,
                                  Ctx.Settings.SeaLevel, Ctx.Settings.MaxHeight);
}

BiomeId UComposableWorldGenerator::SampleBiome(int world_x, int world_z,
                                               int coarse_y) const
{
  return DominantBiome(SampleBiomeWeights(world_x, world_z, coarse_y));
}

ColumnLayerRule UComposableWorldGenerator::BuildTerrainRuleFromSample(
    int world_x, int world_z, const ColumnSampleContext &sample) const
{
  ColumnLayerRule rule;
  rule.fillerBlock = Ctx.Stone;

  if (Config.UseBiomeSurface && BiomeSampler)
  {
    const BiomeSurfaceRule biome_rule =
        EvaluateSurfaceRule(world_x, world_z, sample, Ctx);
    rule.surfaceBlock = biome_rule.surface;
    rule.subsurfaceBlock = biome_rule.subsurface;
    return rule;
  }

  rule.surfaceBlock = Ctx.Grass;
  rule.subsurfaceBlock = Ctx.Dirt;
  if (Config.TerrainMode == ComposableTerrainMode::NoiseHeightmap &&
      Config.HeightPreset == HeightPreset::Mountains && HeightSampler)
  {
    const int stone_above = HeightSampler->params().stoneSurfaceAboveY;
    if (stone_above > 0 && sample.SurfaceY >= stone_above)
    {
      rule.surfaceBlock = Ctx.Stone;
      rule.subsurfaceBlock = Ctx.Stone;
    }
  }
  return rule;
}

ColumnLayerRule UComposableWorldGenerator::BuildTerrainRule(
    int world_x, int world_z, int surface_y, BiomeId biome,
    const BiomeWeightSet &weights) const
{
  ColumnSampleContext sample;
  sample.SurfaceY = surface_y;
  sample.Biomes = weights;
  sample.DominantBiome = biome;
  sample.SurfaceBiome = PickSurfaceBiome(world_x, world_z, weights);
  sample.MacroHeight01 =
      std::clamp(OverworldMacroHeight01(world_x, world_z, Ctx.Settings.Seed),
                 0.f, 1.f);
  sample.SpawnSurfaceOverride =
      ShouldSpawnSurfaceOverride(world_x, world_z, Ctx.Settings);
  return BuildTerrainRuleFromSample(world_x, world_z, sample);
}

void UComposableWorldGenerator::GenerateColumn(int world_x, int world_z)
{
  Ctx.ResetColumnDirty(world_x, world_z);
  if (Config.TerrainMode == ComposableTerrainMode::Flat)
  {
    FillFlatColumn(Ctx, world_x, world_z);
    Ctx.FlushColumnDirty();
    return;
  }
  if (Config.TerrainMode == ComposableTerrainMode::LegacyHash)
  {
    FillLegacyHashColumn(Ctx, world_x, world_z);
    Ctx.FlushColumnDirty();
    return;
  }

  const ColumnSampleContext sample = BuildColumnSample(world_x, world_z);
  RunTerrainStage(*this, sample, world_x, world_z);
  RunPostTerrainStages(*this, sample, world_x, world_z);
  Ctx.FlushColumnDirty();
}

int UComposableWorldGenerator::SurfaceYAt(int world_x, int world_z) const
{
  return SampleSurfaceY(world_x, world_z);
}

} // namespace cutum
