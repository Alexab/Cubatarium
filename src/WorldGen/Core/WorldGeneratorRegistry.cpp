#include "WorldGen/Core/WorldGeneratorDescriptor.h"
#include "WorldGen/Core/WorldGenStageMask.h"
#include "WorldGen/Pipelines/ComposableWorldGenerator.h"
#include <array>

namespace cutum
{

namespace
{

void ApplyFlatDefaults(ProceduralSettings &s)
{
  s.Generator = ProceduralGenerator::Flat;
  s.FillWater = false;
  s.FillFire = false;
  s.EnableTrees = false;
  s.EnableCaves = false;
  s.EnableOres = false;
}

void ApplyHeightmapDefaults(ProceduralSettings &s)
{
  s.Generator = ProceduralGenerator::Heightmap;
  s.FillWater = false;
  s.FillFire = false;
  s.EnableTrees = false;
  s.EnableCaves = false;
  s.EnableOres = false;
}

void ApplyHillsDefaults(ProceduralSettings &s)
{
  s.Generator = ProceduralGenerator::Hills;
  ApplyGeneratorTierDefaults(s);
  s.EnableTrees = false;
  s.EnableCaves = false;
  s.EnableOres = false;
}

void ApplyMountainsDefaults(ProceduralSettings &s)
{
  s.Generator = ProceduralGenerator::Mountains;
  ApplyGeneratorTierDefaults(s);
  s.EnableTrees = false;
  s.EnableCaves = false;
  s.EnableOres = false;
}

void ApplyOverworldDefaults(ProceduralSettings &s)
{
  s.Generator = ProceduralGenerator::Overworld;
  ApplyGeneratorTierDefaults(s);
}

void ApplyBetaRetroDefaults(ProceduralSettings &s)
{
  s.Generator = ProceduralGenerator::BetaRetro;
  ApplyGeneratorTierDefaults(s);
  s.EnableTrees = true;
  s.EnableCaves = false;
  s.EnableOres = false;
  s.Tuning.biomeHillsWeight = 0.6f;
  s.Tuning.vegetationDensity = 0.85f;
  s.Tuning.decorationDensity = 0.6f;
}

std::unique_ptr<IUWorldGenPipeline> MakeComposable(WorldGenContext ctx,
                                                  ComposableWorldGenConfig config)
{
  return std::make_unique<UComposableWorldGenerator>(ctx, config);
}

std::unique_ptr<IUWorldGenPipeline> CreateFlat(WorldGenContext ctx)
{
  ComposableWorldGenConfig config;
  config.TerrainMode = ComposableTerrainMode::Flat;
  config.Fluids = false;
  return MakeComposable(ctx, config);
}

std::unique_ptr<IUWorldGenPipeline> CreateHeightmap(WorldGenContext ctx)
{
  ComposableWorldGenConfig config;
  config.TerrainMode = ComposableTerrainMode::LegacyHash;
  config.Fluids = ctx.Settings.FillWater;
  return MakeComposable(ctx, config);
}

std::unique_ptr<IUWorldGenPipeline> CreateHills(WorldGenContext ctx)
{
  ComposableWorldGenConfig config;
  config.TerrainMode = ComposableTerrainMode::NoiseHeightmap;
  config.HeightPreset = HeightPreset::Hills;
  config.Fluids = true;
  return MakeComposable(ctx, config);
}

std::unique_ptr<IUWorldGenPipeline> CreateMountains(WorldGenContext ctx)
{
  ComposableWorldGenConfig config;
  config.TerrainMode = ComposableTerrainMode::NoiseHeightmap;
  config.HeightPreset = HeightPreset::Mountains;
  config.Fluids = true;
  return MakeComposable(ctx, config);
}

std::unique_ptr<IUWorldGenPipeline> CreateOverworld(WorldGenContext ctx)
{
  ComposableWorldGenConfig config;
  config.TerrainMode = ComposableTerrainMode::NoiseHeightmap;
  config.HeightPreset = HeightPreset::Overworld;
  config.UseBiomeSurface = true;
  config.Fluids = ctx.Settings.FillWater;
  config.Caves = ctx.Settings.EnableCaves;
  config.Ores = ctx.Settings.EnableOres;
  config.Ravines = ctx.Settings.Ravines.enabled;
  config.Vegetation = ctx.Settings.EnableTrees;
  config.GroundCover = ctx.Settings.EnableGroundCover;
  config.Decoration = ctx.Settings.EnableDecoration;
  config.Structures = ctx.Settings.EnableStructures;
  config.LavaPools = ctx.Settings.FillLava;
  config.FirePatch = ctx.Settings.FillFire;
  return MakeComposable(ctx, config);
}

std::unique_ptr<IUWorldGenPipeline> CreateBetaRetro(WorldGenContext ctx)
{
  ComposableWorldGenConfig config;
  config.TerrainMode = ComposableTerrainMode::NoiseHeightmap;
  config.HeightPreset = HeightPreset::BetaRetro;
  config.UseBiomeSurface = true;
  config.Fluids = true;
  config.Vegetation = true;
  config.Decoration = true;
  config.Structures = true;
  config.LavaPools = true;
  config.FirePatch = true;
  return MakeComposable(ctx, config);
}

constexpr WorldGeneratorDescriptor kDescriptors[] = {
    {ProceduralGenerator::Flat,
     "Flat",
     "Flat grass platform for creative builds.",
     "default",
     kFeatureFlatSurfaceY,
     ApplyFlatDefaults,
     CreateFlat},
    {ProceduralGenerator::Heightmap,
     "Heightmap",
     "Legacy hash hills with optional water.",
     "default",
     kFeatureFluids,
     ApplyHeightmapDefaults,
     CreateHeightmap},
    {ProceduralGenerator::Hills,
     "Hills",
     "Rolling hills with stronger height variation.",
     "default",
     kFeatureFluids | kFeatureTuning,
     ApplyHillsDefaults,
     CreateHills},
    {ProceduralGenerator::Mountains,
     "Mountains",
     "Taller peaks with stone caps at high elevation.",
     "default",
     kFeatureFluids | kFeatureTuning,
     ApplyMountainsDefaults,
     CreateMountains},
    {ProceduralGenerator::Overworld,
     "Overworld",
     "Biome world with stage toggles (default: full preset).",
     "default",
     kFeatureBiomes | kFeatureTrees | kFeatureStructures | kFeatureCaves |
         kFeatureOres | kFeatureFluids | kFeatureTuning,
     ApplyOverworldDefaults,
     CreateOverworld},
    {ProceduralGenerator::BetaRetro,
     "Overworld (BetaRetro)",
     "Classic beta-style cliffs with biomes and sparse decor.",
     "default",
     kFeatureBiomes | kFeatureTrees | kFeatureStructures | kFeatureFluids |
         kFeatureTuning,
     ApplyBetaRetroDefaults,
     CreateBetaRetro},
};

} // namespace

size_t UWorldGeneratorRegistry::Count()
{
  return std::size(kDescriptors);
}

const WorldGeneratorDescriptor &UWorldGeneratorRegistry::Get(size_t index)
{
  return kDescriptors[index];
}

const WorldGeneratorDescriptor *UWorldGeneratorRegistry::Find(
    ProceduralGenerator id)
{
  for (const WorldGeneratorDescriptor &descriptor : kDescriptors)
  {
    if (descriptor.Id == id)
    {
      return &descriptor;
    }
  }
  return nullptr;
}

int UWorldGeneratorRegistry::IndexOf(ProceduralGenerator id)
{
  for (size_t i = 0; i < std::size(kDescriptors); ++i)
  {
    if (kDescriptors[i].Id == id)
    {
      return static_cast<int>(i);
    }
  }
  for (size_t i = 0; i < std::size(kDescriptors); ++i)
  {
    if (kDescriptors[i].Id == ProceduralGenerator::Overworld)
    {
      return static_cast<int>(i);
    }
  }
  return 0;
}

std::unique_ptr<IUWorldGenPipeline> UWorldGeneratorRegistry::Create(
    WorldGenContext ctx)
{
  const WorldGeneratorDescriptor *descriptor = Find(ctx.Settings.Generator);
  if (!descriptor)
  {
    descriptor = Find(ProceduralGenerator::Overworld);
    if (!descriptor)
    {
      descriptor = &kDescriptors[0];
    }
  }
  return descriptor->Create(ctx);
}

} // namespace cutum
