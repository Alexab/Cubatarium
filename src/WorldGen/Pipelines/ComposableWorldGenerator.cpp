#include "WorldGen/Pipelines/ComposableWorldGenerator.h"
#include "World/Chunks/Chunk.h"
#include "WorldGen/Core/WorldGenPack.h"
#include "WorldGen/Core/WorldGenStageMask.h"
#include "WorldGen/Pipelines/ColumnGenerationService.h"
#include "WorldGen/Pipelines/WorldGenStageRunner.h"
#include "WorldGen/Features/CaveCarver.h"
#include "WorldGen/Features/OreVeinPlacer.h"
#include "WorldGen/Features/ObjectFeaturePlacer.h"
#include "WorldGen/Features/RavineCarver.h"
#include "WorldGen/Sampling/ClimateSampler.h"
#include "WorldGen/Stages/WorldGenStages.h"

namespace cutum
{

// Pipeline: climate sample -> height -> biomes -> surface fill -> ravines ->
// caves -> fluids -> ores -> vegetation -> ground_cover -> decoration ->
// structures -> lava -> fire.
UComposableWorldGenerator::UComposableWorldGenerator(WorldGenContext ctx,
                                                   ComposableWorldGenConfig config)
    : IUWorldGenPipeline(ctx), Config(ApplyPackPipelineMask(config)),
      StageMask(BuildWorldGenStageMask(Config, ctx.Settings,
                                       UWorldGenPack::Get().Pipeline))
{
  if (Config.TerrainMode == ComposableTerrainMode::Density3D)
  {
    DensityFieldParams density_params;
    density_params.cavesInDensity = false;
    DensitySampler.emplace(ctx.Settings.Seed, ctx.Settings.SeaLevel,
                           ctx.Settings.MaxHeight,
                           ctx.Settings.Tuning.terrainRoughness, density_params,
                           ctx.Settings.Caves);
  }
  else if (Config.TerrainMode == ComposableTerrainMode::NoiseHeightmap)
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
      DirectCoarseHeightFn = [this](int hx, int hz)
      { return HeightSampler->CoarseSurfaceYAt(hx, hz); };
    }
    else if (DensitySampler)
    {
      DirectCoarseHeightFn = [this](int hx, int hz)
      { return DensitySampler->CoarseSurfaceYAt(hx, hz); };
    }
    if (DirectCoarseHeightFn)
    {
      BiomeSampler->SetCoarseHeightCallback(DirectCoarseHeightFn);
    }
  }
  if (HeightSampler || DensitySampler)
  {
    SampleBuilder.emplace(HeightSampler.has_value() ? &*HeightSampler : nullptr,
                          BiomeSampler.has_value() ? &*BiomeSampler : nullptr,
                          ctx.Settings);
  }
}

void UComposableWorldGenerator::BeginChunkCoarseCache(int base_x, int base_z,
                                                      int pad)
{
  if (!BiomeSampler || !DirectCoarseHeightFn)
  {
    return;
  }
  const int origin_x = base_x - pad;
  const int origin_z = base_z - pad;
  const int size = CHUNK_SIZE + pad * 2;
  ChunkCoarseCache.emplace(origin_x, origin_z, size, size);
  UCoarseHeightCache *cache = &*ChunkCoarseCache;
  CoarseHeightCallback direct = DirectCoarseHeightFn;
  BiomeSampler->SetCoarseHeightCallback(
      [cache, direct](int hx, int hz)
      { return cache->GetOrCompute(hx, hz, direct); });
}

void UComposableWorldGenerator::PrimeChunkCoarseY(int x, int z, int y)
{
  if (ChunkCoarseCache)
  {
    ChunkCoarseCache->Put(x, z, y);
  }
}

void UComposableWorldGenerator::EndChunkCoarseCache()
{
  if (!BiomeSampler)
  {
    ChunkCoarseCache.reset();
    return;
  }
  if (DirectCoarseHeightFn)
  {
    BiomeSampler->SetCoarseHeightCallback(DirectCoarseHeightFn);
  }
  ChunkCoarseCache.reset();
}

ColumnSampleContext UComposableWorldGenerator::BuildColumnSample(
    int world_x, int world_z) const
{
  if (SampleBuilder && HeightSampler)
  {
    return SampleBuilder->Build(world_x, world_z);
  }
  if (DensitySampler && BiomeSampler)
  {
    ColumnSampleContext sample;
    sample.Climate = SampleClimate(world_x, world_z, Ctx.Settings.Seed);
    sample.MacroHeight01 =
        std::clamp(OverworldMacroHeight01(world_x, world_z, Ctx.Settings.Seed),
                   0.f, 1.f);
    sample.PreliminarySurfaceY = DensitySampler->CoarseSurfaceYAt(world_x, world_z);
    sample.Biomes =
        BiomeSampler->WeightsAt(world_x, world_z, sample.PreliminarySurfaceY,
                                Ctx.Settings.SeaLevel, Ctx.Settings.MaxHeight);
    sample.DominantBiome = DominantBiome(sample.Biomes);
    const int raw_y = DensitySampler->SurfaceYAt(world_x, world_z);
    if (Ctx.Settings.Tuning.useDensityRefineParity)
    {
      sample.SurfaceY = BiomeSampler->RefineSurfaceY(world_x, world_z, raw_y,
                                                       Ctx.Settings);
    }
    else
    {
      sample.SurfaceY = raw_y;
    }
    const CoarseHeightCallback coarse_fn = [this](int hx, int hz)
    { return DensitySampler->CoarseSurfaceYAt(hx, hz); };
    sample.SurfaceGradient =
        SampleCoarseHeightGradient(world_x, world_z, coarse_fn);
    sample.SurfaceBiome = PickSurfaceBiome(world_x, world_z, sample.Biomes);
    sample.SpawnSurfaceOverride =
        ShouldSpawnSurfaceOverride(world_x, world_z, Ctx.Settings);
    const float denom = std::max(
        1.f, static_cast<float>(Ctx.Settings.MaxHeight - Ctx.Settings.SeaLevel));
    sample.HeightNorm = std::clamp(
        static_cast<float>(sample.SurfaceY - Ctx.Settings.SeaLevel) / denom, 0.f,
        1.f);
    sample.CoastFactor = ComputeCoastBeachStrength(
        world_x, world_z, sample.SurfaceY, Ctx.Settings, coarse_fn);
    return sample;
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
  if (SampleBuilder && HeightSampler)
  {
    return BuildColumnSample(world_x, world_z).SurfaceY;
  }
  if (DensitySampler && BiomeSampler)
  {
    return BuildColumnSample(world_x, world_z).SurfaceY;
  }
  switch (Config.TerrainMode)
  {
  case ComposableTerrainMode::Flat:
    return Ctx.Settings.FlatSurfaceY;
  case ComposableTerrainMode::LegacyHash:
    return LegacyHashSurfaceY(world_x, world_z, Ctx.Settings);
  case ComposableTerrainMode::NoiseHeightmap:
  default:
  {
    int natural_y = HeightSampler->SurfaceYAt(world_x, world_z);
    if (Config.UseBiomeSurface && BiomeSampler)
    {
      natural_y = BiomeSampler->RefineSurfaceY(world_x, world_z, natural_y,
                                                Ctx.Settings);
    }
    return natural_y;
  }
  case ComposableTerrainMode::Density3D:
    return DensitySampler->SurfaceYAt(world_x, world_z);
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
  if (Config.TerrainMode == ComposableTerrainMode::Density3D && DensitySampler)
  {
    return DensitySampler->CoarseSurfaceYAt(world_x, world_z);
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
  rule.fillerBlock = Ctx.Blocks.Stone;

  if (Config.UseBiomeSurface && BiomeSampler)
  {
    const BiomeSurfaceRule biome_rule =
        EvaluateSurfaceRule(world_x, world_z, sample, Ctx);
    rule.surfaceBlock = biome_rule.surface;
    rule.subsurfaceBlock = biome_rule.subsurface;
    return rule;
  }

  rule.surfaceBlock = Ctx.Blocks.Grass;
  rule.subsurfaceBlock = Ctx.Blocks.Dirt;
  if ((Config.TerrainMode == ComposableTerrainMode::NoiseHeightmap ||
       Config.TerrainMode == ComposableTerrainMode::Density3D) &&
      Config.HeightPreset == HeightPreset::Mountains &&
      (HeightSampler || DensitySampler))
  {
    int stone_above = -1;
    if (HeightSampler)
    {
      stone_above = HeightSampler->params().stoneSurfaceAboveY;
    }
    else if (DensitySampler)
    {
      stone_above =
          MountainsStoneSurfaceAboveY(Ctx.Settings.SeaLevel, Ctx.Settings.MaxHeight);
    }
    if (stone_above > 0 && sample.SurfaceY >= stone_above)
    {
      rule.surfaceBlock = Ctx.Blocks.Stone;
      rule.subsurfaceBlock = Ctx.Blocks.Stone;
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
  UColumnGenerationService::GenerateColumn(*this, world_x, world_z);
}

int UComposableWorldGenerator::SurfaceYAt(int world_x, int world_z) const
{
  return SampleSurfaceY(world_x, world_z);
}

} // namespace cutum
