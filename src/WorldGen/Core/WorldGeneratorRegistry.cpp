#include "WorldGen/Core/WorldGeneratorDescriptor.h"
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

void ApplyOverworldDefaults(ProceduralSettings &s)
{
  s.Generator = ProceduralGenerator::Overworld;
  ApplyGeneratorTierDefaults(s);
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

void ApplyBiomesDefaults(ProceduralSettings &s)
{
  s.Generator = ProceduralGenerator::OverworldBiomes;
  ApplyGeneratorTierDefaults(s);
  s.EnableTrees = true;
  s.EnableCaves = false;
  s.EnableOres = false;
}

void ApplyFullDefaults(ProceduralSettings &s)
{
  s.Generator = ProceduralGenerator::OverworldFull;
  ApplyGeneratorTierDefaults(s);
  s.EnableTrees = true;
  s.EnableCaves = true;
  s.EnableOres = true;
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

void ApplyIndevRetroDefaults(ProceduralSettings &s)
{
  s.Generator = ProceduralGenerator::IndevRetro;
  s.FillWater = false;
  s.FillFire = false;
  s.EnableTrees = false;
  s.EnableCaves = false;
  s.EnableOres = false;
}

std::unique_ptr<IWorldGenPipeline> CreateFlat(WorldGenContext ctx)
{
  ComposableWorldGenConfig config;
  config.TerrainMode = ComposableTerrainMode::Flat;
  config.Fluids = false;
  return std::make_unique<UComposableWorldGenerator>(ctx, config);
}

std::unique_ptr<IWorldGenPipeline> CreateHeightmap(WorldGenContext ctx)
{
  ComposableWorldGenConfig config;
  config.TerrainMode = ComposableTerrainMode::LegacyHash;
  config.Fluids = ctx.Settings.FillWater;
  return std::make_unique<UComposableWorldGenerator>(ctx, config);
}

std::unique_ptr<IWorldGenPipeline> CreateOverworld(WorldGenContext ctx)
{
  ComposableWorldGenConfig config;
  config.TerrainMode = ComposableTerrainMode::NoiseHeightmap;
  config.HeightPreset = HeightPreset::Overworld;
  config.Fluids = true;
  return std::make_unique<UComposableWorldGenerator>(ctx, config);
}

std::unique_ptr<IWorldGenPipeline> CreateHills(WorldGenContext ctx)
{
  ComposableWorldGenConfig config;
  config.TerrainMode = ComposableTerrainMode::NoiseHeightmap;
  config.HeightPreset = HeightPreset::Hills;
  config.Fluids = true;
  return std::make_unique<UComposableWorldGenerator>(ctx, config);
}

std::unique_ptr<IWorldGenPipeline> CreateMountains(WorldGenContext ctx)
{
  ComposableWorldGenConfig config;
  config.TerrainMode = ComposableTerrainMode::NoiseHeightmap;
  config.HeightPreset = HeightPreset::Mountains;
  config.Fluids = true;
  return std::make_unique<UComposableWorldGenerator>(ctx, config);
}

std::unique_ptr<IWorldGenPipeline> CreateBiomes(WorldGenContext ctx)
{
  ComposableWorldGenConfig config;
  config.TerrainMode = ComposableTerrainMode::NoiseHeightmap;
  config.HeightPreset = HeightPreset::Overworld;
  config.UseBiomeSurface = true;
  config.Fluids = true;
  config.Vegetation = true;
  config.Decoration = true;
  config.Structures = true;
  config.LavaPools = true;
  config.FirePatch = true;
  return std::make_unique<UComposableWorldGenerator>(ctx, config);
}

std::unique_ptr<IWorldGenPipeline> CreateFull(WorldGenContext ctx)
{
  ComposableWorldGenConfig config;
  config.TerrainMode = ComposableTerrainMode::NoiseHeightmap;
  config.HeightPreset = HeightPreset::Overworld;
  config.UseBiomeSurface = true;
  config.Fluids = true;
  config.Caves = true;
  config.Ores = true;
  config.Vegetation = true;
  config.Decoration = true;
  config.Structures = true;
  config.LavaPools = true;
  config.FirePatch = true;
  return std::make_unique<UComposableWorldGenerator>(ctx, config);
}

std::unique_ptr<IWorldGenPipeline> CreateBetaRetro(WorldGenContext ctx)
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
  return std::make_unique<UComposableWorldGenerator>(ctx, config);
}

std::unique_ptr<IWorldGenPipeline> CreateIndevRetro(WorldGenContext ctx)
{
  ComposableWorldGenConfig config;
  config.TerrainMode = ComposableTerrainMode::IndevRetro;
  config.Fluids = ctx.Settings.FillWater;
  return std::make_unique<UComposableWorldGenerator>(ctx, config);
}

constexpr WorldGeneratorDescriptor kDescriptors[] = {
    {ProceduralGenerator::Flat,
     "Flat",
     "Flat grass platform for creative builds.",
     kFeatureFlatSurfaceY,
     ApplyFlatDefaults,
     CreateFlat},
    {ProceduralGenerator::Heightmap,
     "Heightmap",
     "Legacy hash hills with optional water.",
     kFeatureFluids | kFeatureVertical,
     ApplyHeightmapDefaults,
     CreateHeightmap},
    {ProceduralGenerator::Overworld,
     "Overworld",
     "Smooth noise terrain with oceans and spawn island.",
     kFeatureFluids | kFeatureVertical | kFeatureTuning,
     ApplyOverworldDefaults,
     CreateOverworld},
    {ProceduralGenerator::Hills,
     "Hills",
     "Rolling hills with stronger height variation.",
     kFeatureFluids | kFeatureVertical | kFeatureTuning,
     ApplyHillsDefaults,
     CreateHills},
    {ProceduralGenerator::Mountains,
     "Mountains",
     "Taller peaks with stone caps at high elevation.",
     kFeatureFluids | kFeatureVertical | kFeatureTuning,
     ApplyMountainsDefaults,
     CreateMountains},
    {ProceduralGenerator::OverworldBiomes,
     "Overworld (Biomes)",
     "Biome-aware terrain with trees, decor and structures.",
     kFeatureBiomes | kFeatureTrees | kFeatureStructures | kFeatureFluids |
         kFeatureVertical | kFeatureTuning,
     ApplyBiomesDefaults,
     CreateBiomes},
    {ProceduralGenerator::OverworldFull,
     "Overworld (Full)",
     "Biomes, caves, ores, vegetation, decor and structures.",
     kFeatureBiomes | kFeatureTrees | kFeatureStructures | kFeatureCaves |
         kFeatureOres | kFeatureFluids | kFeatureVertical | kFeatureTuning,
     ApplyFullDefaults,
     CreateFull},
    {ProceduralGenerator::BetaRetro,
     "Beta Retro",
     "Classic beta-style cliffs with biomes and sparse decor.",
     kFeatureBiomes | kFeatureTrees | kFeatureStructures | kFeatureFluids |
         kFeatureVertical | kFeatureTuning,
     ApplyBetaRetroDefaults,
     CreateBetaRetro},
    {ProceduralGenerator::IndevRetro,
     "Indev Retro",
     "Early heightmap hills with rare floating islands.",
     kFeatureFluids | kFeatureVertical,
     ApplyIndevRetroDefaults,
     CreateIndevRetro},
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
  return 0;
}

std::unique_ptr<IWorldGenPipeline> UWorldGeneratorRegistry::Create(
    WorldGenContext ctx)
{
  const WorldGeneratorDescriptor *descriptor = Find(ctx.Settings.Generator);
  if (!descriptor)
  {
    descriptor = &kDescriptors[5];
  }
  return descriptor->Create(ctx);
}

} // namespace cutum
