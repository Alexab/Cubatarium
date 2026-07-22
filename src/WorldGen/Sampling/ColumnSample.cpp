#include "WorldGen/Sampling/ColumnSample.h"

#include "WorldGen/Sampling/OverworldHeightSampler.h"
#include <algorithm>
#include <cmath>

namespace cutum
{

namespace
{

float HeightNormFromY(int surface_y, int sea_level, int max_height)
{
  const float denom =
      std::max(1.f, static_cast<float>(max_height - sea_level));
  return std::clamp(static_cast<float>(surface_y - sea_level) / denom, 0.f, 1.f);
}

} // namespace

bool ShouldSpawnSurfaceOverride(int world_x, int world_z,
                                const ProceduralSettings &settings,
                                int center_x, int center_z)
{
  (void)world_x;
  (void)world_z;
  (void)settings;
  (void)center_x;
  (void)center_z;
  return false;
}

UColumnSampleBuilder::UColumnSampleBuilder(
    const UOverworldHeightSampler *height_sampler,
    const UBiomeSampler *biome_sampler, const ProceduralSettings &settings)
    : HeightSampler(height_sampler), BiomeSampler(biome_sampler),
      Settings(settings)
{
}

ColumnSampleContext UColumnSampleBuilder::Build(int world_x, int world_z) const
{
  ColumnSampleContext ctx;

  if (!HeightSampler || !BiomeSampler)
  {
    ctx.Climate = SampleClimate(world_x, world_z, Settings.Seed);
    ctx.MacroHeight01 =
        std::clamp(OverworldMacroHeight01(world_x, world_z, Settings.Seed), 0.f,
                   1.f);
    ctx.PreliminarySurfaceY = Settings.SeaLevel;
    ctx.SurfaceY = ctx.PreliminarySurfaceY;
    return ctx;
  }

  const CoarseHeightCallback coarse_fn = [this](int hx, int hz)
  {
    if (BiomeSampler->HasCoarseHeightCallback())
    {
      return BiomeSampler->CoarseYAt(hx, hz, Settings.SeaLevel);
    }
    return HeightSampler->CoarseSurfaceYAt(hx, hz);
  };

  // Center column: one SampleAt for macro/climate/surface; neighbors via cache.
  const OverworldHeightSample height = HeightSampler->SampleAt(world_x, world_z);
  ctx.MacroHeight01 = std::clamp(height.h01, 0.f, 1.f);
  ctx.Climate = height.climate;
  ctx.PreliminarySurfaceY = height.surfaceY;

  ctx.Biomes = BiomeSampler->WeightsAt(world_x, world_z, ctx.PreliminarySurfaceY,
                                       Settings.SeaLevel, Settings.MaxHeight);
  ctx.DominantBiome = DominantBiome(ctx.Biomes);
  ctx.SurfaceY = BiomeSampler->RefineSurfaceY(
      world_x, world_z, ctx.PreliminarySurfaceY, Settings, ctx.Biomes);
  ctx.HeightNorm =
      HeightNormFromY(ctx.SurfaceY, Settings.SeaLevel, Settings.MaxHeight);

  ctx.SurfaceGradient =
      SampleCoarseHeightGradient(world_x, world_z, coarse_fn);
  ctx.SurfaceBiome = PickSurfaceBiome(world_x, world_z, ctx.Biomes);
  ctx.SpawnSurfaceOverride =
      ShouldSpawnSurfaceOverride(world_x, world_z, Settings);
  ctx.CoastFactor = ComputeCoastBeachStrength(
      world_x, world_z, ctx.SurfaceY, Settings, coarse_fn);
  return ctx;
}

} // namespace cutum
