#include "Test/FluidTestHelpers.h"

#include "Blocks/BlockRegistry.h"
#include "ResourcePacks/ResourcePack.h"
#include "World/Chunks/ChunkBuffer.h"
#include "World/Chunks/ChunkGenerationToken.h"
#include "World/Core/BlockWorld.h"
#include "World/Objects/ObjectLibrary.h"
#include "WorldGen/Core/IUChunkPopulator.h"
#include "WorldGen/Core/IUWorldGenPipeline.h"
#include "WorldGen/Core/ProceduralSettings.h"
#include "WorldGen/Core/WorldGenPack.h"
#include "WorldGen/Core/WorldGenContentPin.h"
#include "WorldGen/Features/OreVeinPlacer.h"
#include "WorldGen/Features/ValleyCarver.h"
#include "WorldGen/Core/WorldGeneratorDescriptor.h"
#include "WorldGen/Core/RuntimeSettingsText.h"
#include "WorldGen/Core/WorldGenRefs.h"
#include "WorldGen/Core/WorldGenSets.h"
#include "WorldGen/Features/ObjectFeatureConfig.h"
#include "WorldGen/Features/ObjectFeaturePlacer.h"
#include "WorldGen/Sampling/BiomeSampler.h"
#include "WorldGen/Sampling/BiomeSource.h"
#include "WorldGen/Sampling/ColumnSample.h"
#include "WorldGen/Pipelines/ComposableWorldGenerator.h"
#include "WorldGen/Stages/WorldGenStages.h"

#include <algorithm>
#include <array>
#include <chrono>
#include <cmath>
#include <filesystem>
#include <thread>
#include <fstream>
#include <iostream>
#include <iterator>
#include <limits>
#include <memory>
#include <nlohmann/json.hpp>
#include <string>
#include <tuple>
#include <unordered_map>
#include <vector>

namespace fs = std::filesystem;

namespace
{

constexpr const char *kTestName =
    "worldgen_vegetation_placement_integration_test";
constexpr uint32_t kSeed = 410021u;

class UFixedSurfacePipeline final : public cutum::IUWorldGenPipeline
{
public:
  UFixedSurfacePipeline(cutum::WorldGenContext context, int surfaceY)
      : IUWorldGenPipeline(context), SurfaceY(surfaceY)
  {
  }

  void GenerateColumn(int worldX, int worldZ) override
  {
    (void)worldX;
    (void)worldZ;
  }

  int SurfaceYAt(int worldX, int worldZ) const override
  {
    (void)worldX;
    (void)worldZ;
    return SurfaceY;
  }

private:
  int SurfaceY;
};

static fs::path FindRepoRoot()
{
  fs::path dir = fs::current_path();
  for (int depth = 0; depth < 8; ++depth)
  {
    if (fs::exists(dir / "content" / "worldgen_refs.json") &&
        fs::exists(dir / "resource_packs" / "minetest_default_16" /
                   "pack.json"))
    {
      return dir;
    }
    if (!dir.has_parent_path())
    {
      break;
    }
    dir = dir.parent_path();
  }
  FluidTest::Expect(false, kTestName, "could not find repository root");
  return fs::current_path();
}

static std::shared_ptr<cutum::UBlockDefinitionStorage>
LoadPackDefinitions(const fs::path &pack_root)
{
  const std::optional<cutum::ResourcePackManifest> manifest =
      cutum::UResourcePack::LoadManifest(pack_root);
  FluidTest::Expect(manifest.has_value(), kTestName,
                    "pack manifest load failed");
  const std::vector<cutum::ResourcePackBlock> blocks =
      cutum::UResourcePack::LoadBlocks(*manifest);
  FluidTest::Expect(!blocks.empty(), kTestName, "pack has no blocks");

  std::unordered_map<cutum::BlockId, cutum::BlockDefinition> by_id;
  std::unordered_map<std::string, cutum::BlockId> name_to_id;
  for (const cutum::ResourcePackBlock &block : blocks)
  {
    by_id[block.Definition.Id] = block.Definition;
    name_to_id[block.Definition.Name] = block.Definition.Id;
  }
  auto storage = std::make_shared<cutum::UBlockDefinitionStorage>();
  storage->ReplaceAll(std::move(by_id), std::move(name_to_id));
  return storage;
}

static cutum::ProceduralSettings MakeVegetationSettings()
{
  cutum::ProceduralSettings settings;
  settings.Generator = cutum::ProceduralGenerator::Overworld;
  settings.TerrainBackendMode = cutum::TerrainBackend::Density3D;
  settings.Seed = kSeed;
  settings.FillWater = true;
  settings.EnableCaves = false;
  settings.EnableTrees = true;
  settings.EnableGroundCover = true;
  settings.EnableDecoration = true;
  settings.EnableStructures = false;
  settings.EnableOres = false;
  cutum::ApplyWorldGenPreset(settings, "balanced");
  return settings;
}

static int CountBlocksNamed(cutum::UBlockWorld &world,
                            cutum::UBlockRegistry &registry,
                            const std::string &name)
{
  const cutum::BlockId id = registry.GetIdByTypeName(name);
  if (id == cutum::BLOCK_AIR)
  {
    return 0;
  }
  int count = 0;
  world.ForEachBlock(
      [&](glm::ivec3, cutum::BlockId block_id)
      {
        if (block_id == id)
        {
          ++count;
        }
      });
  return count;
}

static void TestConfiguredGroundCoverEcology(const fs::path &repo_root)
{
  bool hasMeadowGrass = false;
  bool hasFern = false;
  bool hasWaterEdgePapyrus = false;
  bool hasSeaRelativePapyrusRange = false;
  bool hasSeaRelativeMarramRange = false;
  bool hasSeaRelativeCoralRange = false;
  for (const cutum::ObjectFeatureRule &rule :
       cutum::UObjectFeatureConfigStorage::Get().GroundCover)
  {
    if (rule.Mode != cutum::ObjectFeatureRuleMode::ScatterBlocks)
    {
      continue;
    }
    if (rule.Scatter.BlockName == "mtg_grass_1")
    {
      hasMeadowGrass = rule.Ecology.HasMoistureRange &&
                       rule.Ecology.HasTemperatureRange &&
                       rule.Ecology.MaxSlope > 0.0f;
    }
    else if (rule.Scatter.BlockName == "mtg_fern_1")
    {
      hasFern =
          rule.Ecology.HasMoistureRange && rule.Ecology.MoistureMin >= 0.6f;
    }
    else if (rule.Scatter.BlockName == "mtg_papyrus")
    {
      hasWaterEdgePapyrus =
          rule.Surface.Kind == cutum::SurfaceConstraintKind::NearWater &&
          rule.Surface.NearWaterRadius == 7 && rule.Ecology.HasMoistureRange;
      hasSeaRelativePapyrusRange =
          rule.Ecology.ElevationRef ==
              cutum::FeatureEcologyProfile::ElevationReference::SeaLevel &&
          rule.Ecology.MinElevation == -3 && rule.Ecology.MaxElevation == 10;
    }
    else if (rule.Scatter.BlockName == "mtg_marram_grass_1")
    {
      hasSeaRelativeMarramRange =
          rule.Ecology.ElevationRef ==
              cutum::FeatureEcologyProfile::ElevationReference::SeaLevel &&
          rule.Ecology.MinElevation == -3 && rule.Ecology.MaxElevation == 6;
    }
    else if (rule.Scatter.BlockName == "mtg_coral_cyan")
    {
      hasSeaRelativeCoralRange =
          rule.Ecology.ElevationRef ==
              cutum::FeatureEcologyProfile::ElevationReference::SeaLevel &&
          rule.Ecology.MinElevation == -48 && rule.Ecology.MaxElevation == -4;
    }
  }
  FluidTest::Expect(hasMeadowGrass, kTestName,
                    "meadow grass uses an ecological placement profile");
  FluidTest::Expect(hasFern, kTestName,
                    "fern placement is limited to damp conditions");
  FluidTest::Expect(hasWaterEdgePapyrus, kTestName,
                    "papyrus profile uses near-water and moisture constraints");
  FluidTest::Expect(hasSeaRelativePapyrusRange && hasSeaRelativeMarramRange &&
                        hasSeaRelativeCoralRange,
                    kTestName,
                    "shore and underwater ecology heights are sea-relative");

  cutum::FeatureEcologyProfile seaRelative;
  seaRelative.ElevationRef =
      cutum::FeatureEcologyProfile::ElevationReference::SeaLevel;
  seaRelative.MinElevation = -3;
  seaRelative.MaxElevation = 6;
  FluidTest::Expect(seaRelative.AllowsElevation(60, 63) &&
                        seaRelative.AllowsElevation(69, 63) &&
                        !seaRelative.AllowsElevation(59, 63) &&
                        !seaRelative.AllowsElevation(70, 63),
                    kTestName,
                    "sea-relative ecology follows the active sea level");

  cutum::FeatureEcologyProfile absolute;
  absolute.MinElevation = 45;
  absolute.MaxElevation = 54;
  FluidTest::Expect(
      absolute.AllowsElevation(51, 63) && !absolute.AllowsElevation(60, 63),
      kTestName, "legacy absolute ecology ranges keep their original meaning");

  const fs::path legacyConfigPath =
      fs::temp_directory_path() / "cubatarium_legacy_feature_elevation.json";
  {
    std::ofstream legacyConfig(legacyConfigPath, std::ios::trunc);
    legacyConfig
        << R"({"ground_cover":[{"mode":"scatter_blocks","block":"legacy_test_plant","biomes":["plains"],"ecology":{"elevation":[45,54]}}]})";
  }
  bool legacyArrayLoaded = false;
  if (cutum::UObjectFeatureConfigStorage::LoadFromFile(legacyConfigPath))
  {
    for (const cutum::ObjectFeatureRule &rule :
         cutum::UObjectFeatureConfigStorage::Get().GroundCover)
    {
      if (rule.Scatter.BlockName == "legacy_test_plant")
      {
        legacyArrayLoaded =
            rule.Ecology.ElevationRef ==
                cutum::FeatureEcologyProfile::ElevationReference::AbsoluteY &&
            rule.Ecology.MinElevation == 45 &&
            rule.Ecology.MaxElevation == 54 &&
            rule.Ecology.AllowsElevation(51, 63) &&
            !rule.Ecology.AllowsElevation(60, 63);
      }
    }
  }
  cutum::UObjectFeatureConfigStorage::LoadFromFile(repo_root / "content" /
                                                   "object_features.json");
  std::error_code removeError;
  fs::remove(legacyConfigPath, removeError);
  FluidTest::Expect(legacyArrayLoaded, kTestName,
                    "legacy ecology elevation arrays remain absolute Y ranges");
}

static void TestConfiguredTreeElevationBands()
{
  const std::unordered_map<std::string, int> expectedMaxAboveSea = {
      {"tree_acacia", 52},     {"tree_apple", 56},           {"tree_aspen", 68},
      {"tree_jungle", 50},     {"tree_jungle_emergent", 55}, {"tree_pine", 96},
      {"tree_pine_small", 90}, {"tree_pine_snowy", 105},
  };
  std::unordered_map<std::string, bool> checked;
  for (const auto &expected : expectedMaxAboveSea)
  {
    checked.emplace(expected.first, false);
  }

  for (const cutum::ObjectFeatureRule &rule :
       cutum::UObjectFeatureConfigStorage::Get().Vegetation)
  {
    if (rule.Mode != cutum::ObjectFeatureRuleMode::Object)
    {
      continue;
    }
    const auto expected = expectedMaxAboveSea.find(rule.ObjectName);
    if (expected == expectedMaxAboveSea.end())
    {
      continue;
    }
    const auto reference =
        cutum::FeatureEcologyProfile::ElevationReference::SeaLevel;
    checked[rule.ObjectName] =
        rule.Ecology.ElevationRef == reference &&
        rule.Ecology.MinElevation < 0 &&
        rule.Ecology.MaxElevation == expected->second &&
        rule.Ecology.MaxSlope > 0.0f &&
        rule.Ecology.AllowsElevation(63 + expected->second, 63) &&
        !rule.Ecology.AllowsElevation(64 + expected->second, 63);
  }

  const bool allStaticTreeProfilesPresent =
      std::all_of(checked.begin(), checked.end(),
                  [](const auto &entry) { return entry.second; });
  FluidTest::Expect(allStaticTreeProfilesPresent, kTestName,
                    "all authored tree templates use sea-relative elevation "
                    "and slope limits");
}

static void TestConfiguredNaturalFeatureElevationBands()
{
  const auto hasSeaRelativeProfile = [](const cutum::ObjectFeatureRule &rule,
                                        int expectedMinimum,
                                        int expectedMaximum)
  {
    const cutum::FeatureEcologyProfile &ecology = rule.Ecology;
    return ecology.ElevationRef ==
               cutum::FeatureEcologyProfile::ElevationReference::SeaLevel &&
           ecology.MinElevation == expectedMinimum &&
           ecology.MaxElevation == expectedMaximum &&
           ecology.HasTemperatureRange && ecology.HasMoistureRange &&
           ecology.MaxSlope > 0.0f &&
           ecology.AllowsElevation(63 + expectedMinimum, 63) &&
           ecology.AllowsElevation(63 + expectedMaximum, 63) &&
           !ecology.AllowsElevation(62 + expectedMinimum, 63) &&
           !ecology.AllowsElevation(64 + expectedMaximum, 63);
  };

  bool cactusLimited = false;
  for (const cutum::ObjectFeatureRule &rule :
       cutum::UObjectFeatureConfigStorage::Get().Vegetation)
  {
    if (rule.Mode == cutum::ObjectFeatureRuleMode::Object &&
        rule.ObjectName == "cactus_large")
    {
      cactusLimited = hasSeaRelativeProfile(rule, -5, 40) &&
                      rule.Ecology.TemperatureMin == 0.45f &&
                      rule.Ecology.MoistureMax == 0.42f &&
                      rule.Ecology.MaxSlope == 8.0f;
      break;
    }
  }

  bool flowerScatterLimited = false;
  bool flowerPatchLimited = false;
  bool duplicateFlowerRulePresent = false;
  bool sparsePineBushLimited = false;
  for (const cutum::ObjectFeatureRule &rule :
       cutum::UObjectFeatureConfigStorage::Get().GroundCover)
  {
    if (rule.Mode == cutum::ObjectFeatureRuleMode::ScatterBlocks &&
        rule.Scatter.BlockName == "flower")
    {
      flowerScatterLimited =
          hasSeaRelativeProfile(rule, -3, 42) && rule.Ecology.MaxSlope == 9.0f;
    }
    else if (rule.Mode == cutum::ObjectFeatureRuleMode::Object &&
             rule.ObjectName == "flower_scatter_plains")
    {
      // Removed as a duplicate of the scatter rule (shared seed_offset 4120).
      flowerPatchLimited = false;
      duplicateFlowerRulePresent = true;
    }
    else if (rule.Mode == cutum::ObjectFeatureRuleMode::Object &&
             rule.ObjectName == "bush_pine" && rule.SeedOffset == 4109)
    {
      sparsePineBushLimited = hasSeaRelativeProfile(rule, -3, 80) &&
                              rule.Ecology.TemperatureMax == 0.68f &&
                              rule.Ecology.MoistureMin == 0.22f &&
                              rule.Ecology.MaxSlope == 12.0f;
    }
  }

  FluidTest::Expect(cactusLimited, kTestName,
                    "cactus is restricted to warm, dry, low-slope ground");
  (void)flowerPatchLimited;
  FluidTest::Expect(
      flowerScatterLimited && !duplicateFlowerRulePresent, kTestName,
      "plains flowers use one scatter rule with its ecological band");
  FluidTest::Expect(sparsePineBushLimited, kTestName,
                    "sparse-forest pine bushes use the conifer ecology band");
}

static void
TestChunkPopulatorPlacesTrees(cutum::UPipelineChunkPopulator &populator,
                              cutum::UBlockRegistry &registry,
                              cutum::UObjectLibrary &objects)
{
  FluidTest::Expect(objects.Get("tree_pine") != nullptr, kTestName,
                    "tree_pine prefab loaded");
  FluidTest::Expect(
      cutum::UObjectFeatureConfigStorage::Get().Vegetation.size() > 0,
      kTestName, "vegetation rules loaded");

  cutum::ProceduralSettings settings = MakeVegetationSettings();
  // Profile 3 terrain (smooth noise, gentle shoreline) moved the old seed
  // 410021 forest. Seed 880002 has a dense mixed forest around chunk (5, 9)
  // (254 tree bases in a 5x5 chunk window, verified with tools/forest_metrics).
  settings.Seed = 880002u;
  std::vector<glm::ivec3> chunkOrder;
  constexpr int kSpawnChunkX = 5;
  constexpr int kSpawnChunkZ = 9;
  for (int cx = -2; cx <= 2; ++cx)
  {
    for (int chunk_y = 2; chunk_y <= 9; ++chunk_y)
    {
      for (int cz = -2; cz <= 2; ++cz)
      {
        // Vegetation grows around Y=40..140. Populate those vertical sections
        // so worldgen-owned tree voxels are applied to their owning chunks.
        chunkOrder.emplace_back(kSpawnChunkX + cx, chunk_y, kSpawnChunkZ + cz);
      }
    }
  }
  cutum::ChunkGenerationToken token;
  const auto populate =
      [&](cutum::UBlockWorld &world, const std::vector<glm::ivec3> &order)
  {
    world.SetFluidDefinitions(registry.GetDefinitions());
    for (const glm::ivec3 &chunkCoord : order)
    {
      cutum::ChunkPopulateRequest request;
      request.chunkCoord = chunkCoord;
      request.token = token;
      request.settings = settings;
      request.objects = &objects;
      request.columnOrigin = glm::ivec2(kSpawnChunkX * cutum::CHUNK_SIZE + 8,
                                        kSpawnChunkZ * cutum::CHUNK_SIZE + 8);
      request.hasColumnOrigin = true;
      const cutum::ChunkPopulateResult result = populator.Populate(request);
      result.buffer.ApplyTo(world);
    }
  };

  const auto snapshot = [](cutum::UBlockWorld &world)
  {
    std::vector<std::tuple<int, int, int, cutum::BlockId>> blocks;
    world.ForEachBlock([&blocks](glm::ivec3 pos, cutum::BlockId id)
                       { blocks.emplace_back(pos.x, pos.y, pos.z, id); });
    std::sort(blocks.begin(), blocks.end());
    return blocks;
  };

  cutum::UBlockWorld forwardWorld;
  populate(forwardWorld, chunkOrder);
  std::reverse(chunkOrder.begin(), chunkOrder.end());
  cutum::UBlockWorld reverseWorld;
  populate(reverseWorld, chunkOrder);
  const std::vector<std::tuple<int, int, int, cutum::BlockId>> forwardBlocks =
      snapshot(forwardWorld);
  const std::vector<std::tuple<int, int, int, cutum::BlockId>> reverseBlocks =
      snapshot(reverseWorld);
  if (forwardBlocks != reverseBlocks)
  {
    std::vector<std::tuple<int, int, int, cutum::BlockId>> forwardOnly;
    std::vector<std::tuple<int, int, int, cutum::BlockId>> reverseOnly;
    std::set_difference(forwardBlocks.begin(), forwardBlocks.end(),
                        reverseBlocks.begin(), reverseBlocks.end(),
                        std::back_inserter(forwardOnly));
    std::set_difference(reverseBlocks.begin(), reverseBlocks.end(),
                        forwardBlocks.begin(), forwardBlocks.end(),
                        std::back_inserter(reverseOnly));
    std::cerr << kTestName << ": order diff forward=" << forwardBlocks.size()
              << " reverse=" << reverseBlocks.size()
              << " forward_only=" << forwardOnly.size()
              << " reverse_only=" << reverseOnly.size() << std::endl;
    const auto printFirst =
        [](const char *label,
           const std::vector<std::tuple<int, int, int, cutum::BlockId>> &items)
    {
      for (size_t i = 0; i < std::min<size_t>(items.size(), 12); ++i)
      {
        const auto &[x, y, z, id] = items[i];
        std::cerr << "  " << label << " (" << x << ',' << y << ',' << z
                  << ") id=" << id << std::endl;
      }
    };
    printFirst("forward", forwardOnly);
    printFirst("reverse", reverseOnly);
  }
  FluidTest::Expect(forwardBlocks == reverseBlocks, kTestName,
                    "generated chunk area is independent of chunk apply order");

  static constexpr std::array<const char *, 16> treeBlockNames = {
      "tree_log",         "mtg_tree",
      "mtg_pine_tree",    "mtg_aspen_tree",
      "mtg_acacia_tree",  "mtg_jungletree",
      "tree_leaves",      "mtg_leaves",
      "leaves_spruce",    "leaves_spruce_light",
      "leaves_jungle",    "leaves_jungle_light",
      "mtg_aspen_leaves", "mtg_acacia_leaves",
      "mtg_jungleleaves", "mtg_pine_needles"};
  int tree_blocks = 0;
  for (const char *name : treeBlockNames)
  {
    tree_blocks += CountBlocksNamed(forwardWorld, registry, name);
  }
  FluidTest::Expect(tree_blocks > 0, kTestName,
                    "chunk populator placed blocks from a tree species");
}

static void TestWaterColumnFloorEarlyExit(cutum::UBlockRegistry &registry)
{
  const auto makeContext = [&](cutum::UBlockWorld &world)
  {
    cutum::ProceduralSettings settings = MakeVegetationSettings();
    settings.SeaLevel = 8;
    settings.MinHeight = 0;
    settings.MaxHeight = 64;
    cutum::WorldGenContext ctx(world, registry, settings);
    ctx.WorldgenOwnerPackId = "minetest_default_16";
    ctx.ResolveBlockIds();
    FluidTest::Expect(ctx.Blocks.Stone != cutum::BLOCK_AIR &&
                          ctx.Blocks.Water != cutum::BLOCK_AIR,
                      kTestName, "water-column test resolves stone and water");
    return ctx;
  };

  // Two possible floors: the legacy bottom-up scan retained the upper one.
  cutum::UBlockWorld highestWorld;
  auto highestCtx = makeContext(highestWorld);
  for (int y = 0; y <= 2; ++y)
  {
    highestWorld.SetBlock(glm::ivec3(0, y, 0), highestCtx.Blocks.Stone);
  }
  highestWorld.SetBlock(glm::ivec3(0, 4, 0), highestCtx.Blocks.Stone);
  cutum::SealFluidPermeableDecorInChunk(highestCtx, 0, 0);
  FluidTest::Expect(
      highestWorld.IsAir(glm::ivec3(0, 3, 0)) &&
          highestWorld.GetBlock(glm::ivec3(0, 5, 0)) == highestCtx.Blocks.Water,
      kTestName, "water-column seal retains the highest valid floor");

  // Stone at sea level makes every lower floor invalid. It must not be
  // removed, and the air below it must not turn into water.
  cutum::UBlockWorld blockedWorld;
  auto blockedCtx = makeContext(blockedWorld);
  for (int y = 0; y <= 4; ++y)
  {
    blockedWorld.SetBlock(glm::ivec3(0, y, 0), blockedCtx.Blocks.Stone);
  }
  blockedWorld.SetBlock(glm::ivec3(0, 8, 0), blockedCtx.Blocks.Stone);
  cutum::FluidPermeableSealStats stats;
  cutum::SealFluidPermeableDecorInChunk(blockedCtx, 0, 0, nullptr, &stats);
  FluidTest::Expect(
      blockedWorld.IsAir(glm::ivec3(0, 5, 0)) &&
          blockedWorld.GetBlock(glm::ivec3(0, 8, 0)) == blockedCtx.Blocks.Stone,
      kTestName, "permanent water-column obstruction rejects lower floors");
  FluidTest::Expect(stats.floorGroundChecks < 256 * 8, kTestName,
                    "permanent obstruction avoids full lower-column scans");
}

static void TestSpatialFloraCandidateDensity(cutum::UBlockRegistry &registry)
{
  cutum::ProceduralSettings settings = MakeVegetationSettings();
  settings.SeaLevel = 8;
  settings.MinHeight = 0;
  settings.MaxHeight = 64;
  settings.WorldGenProfileVersion = 2;

  cutum::UBlockWorld world;
  world.SetFluidDefinitions(registry.GetDefinitions());
  cutum::WorldGenContext ctx(world, registry, settings);
  ctx.WorldgenOwnerPackId = "minetest_default_16";
  ctx.ResolveBlockIds();
  FluidTest::Expect(ctx.Blocks.Grass != cutum::BLOCK_AIR, kTestName,
                    "spatial flora density test resolves its ground block");

  constexpr int kRegionSize = 64;
  constexpr int kSurfaceY = 15;
  for (int z = 0; z < kRegionSize; ++z)
  {
    for (int x = 0; x < kRegionSize; ++x)
    {
      world.SetBlock(glm::ivec3(x, kSurfaceY, z), ctx.Blocks.Grass);
    }
  }

  cutum::ObjectFeatureConfig features;
  cutum::ObjectFeatureRule grass;
  grass.Mode = cutum::ObjectFeatureRuleMode::ScatterBlocks;
  grass.Biomes = {cutum::BiomeId::Plains};
  grass.Spacing = 34;
  grass.Weight = 1;
  grass.SeedOffset = 0x41f09a37U;
  grass.Scatter.BlockName = "mtg_grass_1";
  grass.Scatter.Attempts = 1;
  grass.Scatter.Radius = 0;
  features.GroundCover.push_back(grass);
  ctx.ObjectFeatures = &features;
  cutum::RegisterBuiltinObjectFeaturePlacementStrategies();

  for (int z = 0; z < kRegionSize; ++z)
  {
    for (int x = 0; x < kRegionSize; ++x)
    {
      cutum::TryPlaceGroundCoverFeatures(ctx, x, z, kSurfaceY,
                                         cutum::BiomeId::Plains, false);
    }
  }

  const int placedGrass = CountBlocksNamed(world, registry, "mtg_grass_1");
  std::cout << "spatial flora test placements=" << placedGrass << std::endl;
  FluidTest::Expect(
      placedGrass >= 55, kTestName,
      "full-cell jitter preserves authored flora candidate density");
}

struct RegionalFloraRun
{
  std::array<int, 9> TileCounts{};
  std::vector<std::tuple<int, int, int>> Placements;
};

static RegionalFloraRun
RunRegionalFloraFixture(cutum::UBlockRegistry &registry,
                        const cutum::FeatureDistributionProfile &distribution)
{
  cutum::ProceduralSettings settings = MakeVegetationSettings();
  settings.Seed = kSeed;
  settings.SeaLevel = 8;
  settings.MinHeight = 0;
  settings.MaxHeight = 64;
  settings.WorldGenProfileVersion = 2;

  cutum::UBlockWorld world;
  world.SetFluidDefinitions(registry.GetDefinitions());
  cutum::WorldGenContext ctx(world, registry, settings);
  ctx.WorldgenOwnerPackId = "minetest_default_16";
  ctx.ResolveBlockIds();

  constexpr int kRegionSize = 192;
  constexpr int kSurfaceY = 15;
  for (int z = 0; z < kRegionSize; ++z)
  {
    for (int x = 0; x < kRegionSize; ++x)
    {
      world.SetBlock(glm::ivec3(x, kSurfaceY, z), ctx.Blocks.Grass);
    }
  }

  cutum::ObjectFeatureConfig features;
  features.GroundCoverDistribution = distribution;
  cutum::ObjectFeatureRule grass;
  grass.Mode = cutum::ObjectFeatureRuleMode::ScatterBlocks;
  grass.Biomes = {cutum::BiomeId::Plains};
  grass.Spacing = 34;
  grass.Weight = 1;
  grass.SeedOffset = 0x41f09a37U;
  grass.Scatter.BlockName = "mtg_grass_1";
  grass.Scatter.Attempts = 1;
  grass.Scatter.Radius = 0;
  features.GroundCover.push_back(grass);
  ctx.ObjectFeatures = &features;
  cutum::RegisterBuiltinObjectFeaturePlacementStrategies();

  for (int z = 0; z < kRegionSize; ++z)
  {
    for (int x = 0; x < kRegionSize; ++x)
    {
      cutum::TryPlaceGroundCoverFeatures(ctx, x, z, kSurfaceY,
                                         cutum::BiomeId::Plains, false);
    }
  }

  const cutum::BlockId grassId = registry.GetIdByTypeName("mtg_grass_1");
  RegionalFloraRun result;
  world.ForEachBlock(
      [&](glm::ivec3 position, cutum::BlockId id)
      {
        if (id != grassId || position.y != kSurfaceY + 1 || position.x < 0 ||
            position.z < 0 || position.x >= kRegionSize ||
            position.z >= kRegionSize)
        {
          return;
        }
        const int tileX = position.x / 64;
        const int tileZ = position.z / 64;
        ++result.TileCounts[static_cast<size_t>(tileZ * 3 + tileX)];
        result.Placements.emplace_back(position.x, position.y, position.z);
      });
  std::sort(result.Placements.begin(), result.Placements.end());
  return result;
}

static double TileCountVariance(const std::array<int, 9> &counts)
{
  double mean = 0.0;
  for (const int count : counts)
  {
    mean += static_cast<double>(count);
  }
  mean /= static_cast<double>(counts.size());
  double variance = 0.0;
  for (const int count : counts)
  {
    const double delta = static_cast<double>(count) - mean;
    variance += delta * delta;
  }
  return variance / static_cast<double>(counts.size());
}

static void TestRegionalFloraDensityClustering(cutum::UBlockRegistry &registry)
{
  const cutum::FeatureDistributionProfile neutral{};
  cutum::FeatureDistributionProfile clustered;
  clustered.RegionalNoiseScale = 0.008f;
  clustered.MinDensityMultiplier = 0.15f;
  clustered.MaxDensityMultiplier = 1.85f;

  const RegionalFloraRun baseline = RunRegionalFloraFixture(registry, neutral);
  const RegionalFloraRun first = RunRegionalFloraFixture(registry, clustered);
  const RegionalFloraRun repeat = RunRegionalFloraFixture(registry, clustered);
  FluidTest::Expect(first.Placements == repeat.Placements, kTestName,
                    "regional flora clusters repeat for the same seed");
  FluidTest::Expect(TileCountVariance(first.TileCounts) >
                        TileCountVariance(baseline.TileCounts) * 1.2,
                    kTestName,
                    "regional density creates broader ground-cover patches "
                    "than the local field alone");
}

static void
TestBuiltinTreeTemplateLogsConnectToTrunk(const cutum::UObjectLibrary &objects)
{
  static const std::array<const char *, 11> kTreeTemplates = {
      "tree_acacia",
      "tree_apple",
      "tree_aspen",
      "tree_jungle",
      "tree_jungle_emergent",
      "tree_large",
      "tree_pine",
      "tree_pine_small",
      "tree_pine_snowy",
      "tree_pine_snowy_small",
      "tree_small"};

  for (const char *name : kTreeTemplates)
  {
    const cutum::WorldObjectDefinition *object = objects.Get(name);
    const std::string loadedMessage =
        std::string("static tree template loads: ") + name;
    FluidTest::Expect(object != nullptr, kTestName, loadedMessage.c_str());
    if (!object)
    {
      continue;
    }

    std::vector<glm::ivec3> logs;
    for (const cutum::ObjectVoxel &voxel : object->voxels)
    {
      if (voxel.Type == "tree_log")
      {
        logs.push_back(voxel.offset);
      }
    }
    const auto root = std::find(logs.begin(), logs.end(), object->anchor);
    const std::string anchorMessage =
        std::string("static tree has a trunk at its anchor: ") + name;
    FluidTest::Expect(root != logs.end(), kTestName, anchorMessage.c_str());
    if (root == logs.end())
    {
      continue;
    }

    std::vector<bool> reached(logs.size(), false);
    const size_t rootIndex = static_cast<size_t>(root - logs.begin());
    std::vector<size_t> pending{rootIndex};
    reached[rootIndex] = true;
    size_t reachedCount = 0;
    while (!pending.empty())
    {
      const size_t current = pending.back();
      pending.pop_back();
      ++reachedCount;
      for (size_t candidate = 0; candidate < logs.size(); ++candidate)
      {
        if (reached[candidate])
        {
          continue;
        }
        const glm::ivec3 delta = logs[candidate] - logs[current];
        if (std::abs(delta.x) + std::abs(delta.y) + std::abs(delta.z) == 1)
        {
          reached[candidate] = true;
          pending.push_back(candidate);
        }
      }
    }

    const std::string connectivityMessage =
        std::string("static tree logs connect to trunk: ") + name;
    FluidTest::Expect(reachedCount == logs.size(), kTestName,
                      connectivityMessage.c_str());
  }
}

static void
TestStaticPlantPrefabsPlaceOnFlatGround(cutum::UBlockRegistry &registry,
                                        cutum::UObjectLibrary &objects)
{
  cutum::ProceduralSettings settings = MakeVegetationSettings();
  settings.SeaLevel = 63;
  settings.MinHeight = -64;
  settings.MaxHeight = 319;
  settings.WorldGenProfileVersion = 2;

  cutum::UBlockWorld world;
  world.SetFluidDefinitions(registry.GetDefinitions());
  cutum::WorldGenContext ctx(world, registry, settings, &objects);
  ctx.WorldgenOwnerPackId = "minetest_default_16";
  ctx.ResolveBlockIds();

  constexpr int kSurfaceY = 64;
  for (int z = 0; z <= 8; ++z)
  {
    for (int x = 0; x <= 18; ++x)
    {
      world.SetBlock(glm::ivec3(x, kSurfaceY, z), ctx.Blocks.Grass);
    }
  }

  const bool loaded = objects.Get("flower_patch_common") != nullptr &&
                      objects.Get("flower_scatter_plains") != nullptr &&
                      objects.Get("tall_grass_patch") != nullptr;
  FluidTest::Expect(loaded, kTestName,
                    "static flower and grass prefabs load from object content");
  if (!loaded)
  {
    return;
  }

  const bool flowerPatchPlaced = cutum::PlaceObjectAt(
      ctx, "flower_patch_common", glm::ivec3(3, kSurfaceY + 1, 3), kSurfaceY);
  const bool flowerScatterPlaced = cutum::PlaceObjectAt(
      ctx, "flower_scatter_plains", glm::ivec3(9, kSurfaceY + 1, 3), kSurfaceY);
  const bool tallGrassPlaced = cutum::PlaceObjectAt(
      ctx, "tall_grass_patch", glm::ivec3(15, kSurfaceY + 1, 3), kSurfaceY);

  FluidTest::Expect(flowerPatchPlaced, kTestName,
                    "common rose patch places above supported grass");
  FluidTest::Expect(flowerScatterPlaced, kTestName,
                    "plains rose scatter places above supported grass");
  FluidTest::Expect(tallGrassPlaced, kTestName,
                    "non-solid tall-grass prefab places above supported grass");
  FluidTest::Expect(CountBlocksNamed(world, registry, "rose") == 8, kTestName,
                    "both rose prefabs write all configured rose voxels");
  FluidTest::Expect(CountBlocksNamed(world, registry, "tall_grass") == 5,
                    kTestName,
                    "tall-grass prefab writes all configured grass voxels");

  cutum::UBlockWorld unevenWorld;
  unevenWorld.SetFluidDefinitions(registry.GetDefinitions());
  cutum::WorldGenContext unevenCtx(unevenWorld, registry, settings, &objects);
  unevenCtx.WorldgenOwnerPackId = "minetest_default_16";
  unevenCtx.ResolveBlockIds();
  for (int z = 0; z <= 6; ++z)
  {
    for (int x = 0; x <= 6; ++x)
    {
      if (x == 3 && z == 2)
      {
        unevenWorld.SetBlock(glm::ivec3(x, kSurfaceY - 1, z),
                             unevenCtx.Blocks.Grass);
      }
      else
      {
        unevenWorld.SetBlock(glm::ivec3(x, kSurfaceY, z),
                             unevenCtx.Blocks.Grass);
      }
    }
  }
  unevenWorld.SetBlock(glm::ivec3(2, kSurfaceY + 1, 3), unevenCtx.Blocks.Grass);

  const bool unevenFlowerPatchPlaced =
      cutum::PlaceObjectAt(unevenCtx, "flower_patch_common",
                           glm::ivec3(3, kSurfaceY + 1, 3), kSurfaceY);
  FluidTest::Expect(unevenFlowerPatchPlaced, kTestName,
                    "flower patch adapts to one-block steps in terrain");
  const cutum::BlockId rose = registry.GetIdByTypeName("rose");
  const std::array<glm::ivec3, 5> expectedRosePositions = {
      glm::ivec3(2, kSurfaceY + 2, 3), glm::ivec3(3, kSurfaceY, 2),
      glm::ivec3(3, kSurfaceY + 1, 3), glm::ivec3(3, kSurfaceY + 1, 4),
      glm::ivec3(4, kSurfaceY + 1, 3)};
  const bool allUnevenFlowersFollowGround =
      std::all_of(expectedRosePositions.begin(), expectedRosePositions.end(),
                  [&](glm::ivec3 position)
                  { return unevenWorld.GetBlock(position) == rose; });
  FluidTest::Expect(allUnevenFlowersFollowGround, kTestName,
                    "each plant voxel follows its local supporting column");

  constexpr int kRuleFixtureRadius = 64;
  cutum::UBlockWorld defaultWorld;
  defaultWorld.SetFluidDefinitions(registry.GetDefinitions());
  cutum::WorldGenContext defaultCtx(defaultWorld, registry, settings, &objects);
  defaultCtx.WorldgenOwnerPackId = "minetest_default_16";
  defaultCtx.ResolveBlockIds();
  for (int z = -kRuleFixtureRadius - 3; z <= kRuleFixtureRadius + 3; ++z)
  {
    for (int x = -kRuleFixtureRadius - 3; x <= kRuleFixtureRadius + 3; ++x)
    {
      defaultWorld.SetBlock(glm::ivec3(x, kSurfaceY, z),
                            defaultCtx.Blocks.Grass);
    }
  }
  for (int z = -kRuleFixtureRadius; z <= kRuleFixtureRadius; ++z)
  {
    for (int x = -kRuleFixtureRadius; x <= kRuleFixtureRadius; ++x)
    {
      cutum::TryPlaceGroundCoverFeatures(defaultCtx, x, z, kSurfaceY,
                                         cutum::BiomeId::Plains, false);
    }
  }

  const int defaultRoseCount = CountBlocksNamed(defaultWorld, registry, "rose");
  const int defaultTallGrassCount =
      CountBlocksNamed(defaultWorld, registry, "tall_grass");
  std::cout << "default ground-cover pool fixture roses=" << defaultRoseCount
            << " tall_grass=" << defaultTallGrassCount << std::endl;
  FluidTest::Expect(defaultRoseCount > 0, kTestName,
                    "full ground-cover pool can place configured rose prefabs");
  FluidTest::Expect(defaultTallGrassCount > 0, kTestName,
                    "full ground-cover pool can place configured tall grass");
}

static void TestVanillaStyleTreesProfile3(cutum::UBlockRegistry &registry)
{
  cutum::ProceduralSettings settings = MakeVegetationSettings();
  settings.SeaLevel = 48;
  settings.MaxHeight = 128;
  settings.WorldGenProfileVersion = 3;
  cutum::RegisterBuiltinObjectFeaturePlacementStrategies();
  const auto strategy = cutum::UObjectFeaturePlacementStrategyRegistry::Find(
      cutum::ObjectFeatureRuleMode::ProceduralTree);
  FluidTest::Expect(strategy != nullptr, kTestName,
                    "profile 3 tree test finds the strategy");
  const cutum::TreeCrownShape shapes[] = {
      cutum::TreeCrownShape::Round, cutum::TreeCrownShape::Conical,
      cutum::TreeCrownShape::Umbrella, cutum::TreeCrownShape::Columnar};
  for (const cutum::TreeCrownShape shape : shapes)
  {
    cutum::UBlockWorld world;
    world.SetFluidDefinitions(registry.GetDefinitions());
    cutum::WorldGenContext ctx(world, registry, settings);
    ctx.WorldgenOwnerPackId = "minetest_default_16";
    ctx.ResolveBlockIds();
    for (int z = -10; z <= 10; ++z)
    {
      for (int x = -10; x <= 10; ++x)
      {
        world.SetBlock(glm::ivec3(x, 51, z), ctx.Blocks.Grass);
      }
    }
    cutum::ObjectFeatureRule tree;
    tree.Mode = cutum::ObjectFeatureRuleMode::ProceduralTree;
    tree.Tree.HeightMin = 7;
    tree.Tree.HeightMax = 9;
    tree.Tree.CrownRadiusX = 4;
    tree.Tree.CrownRadiusZ = 4;
    tree.Tree.CrownHeight = 5;
    tree.Tree.CrownShape = shape;
    tree.Tree.TrunkBlock = "mtg_tree";
    tree.Tree.LeavesBlock = "mtg_leaves";
    FluidTest::Expect(strategy->Place(ctx, 0, 0, 51, tree), kTestName,
                      "profile 3 tree places");
    const cutum::BlockId trunk = registry.GetIdByTypeName("mtg_tree");
    const cutum::BlockId leaves = registry.GetIdByTypeName("mtg_leaves");
    std::vector<glm::ivec3> logs;
    int leafCount = 0;
    int extent = 0;
    world.ForEachBlock(
        [&](glm::ivec3 position, cutum::BlockId block)
        {
          if (block == trunk)
          {
            logs.push_back(position);
          }
          else if (block == leaves)
          {
            ++leafCount;
            extent = std::max(
                extent, std::max(std::abs(position.x), std::abs(position.z)));
          }
        });
    FluidTest::Expect(!logs.empty() && leafCount >= 8, kTestName,
                      "profile 3 tree has trunk and compact foliage");
    FluidTest::Expect(extent <= 5, kTestName,
                      "profile 3 foliage stays within its radius");
    std::vector<bool> seen(logs.size(), false);
    std::vector<size_t> pending;
    for (size_t i = 0; i < logs.size(); ++i)
    {
      if (logs[i].x == 0 && logs[i].y == 52 && logs[i].z == 0)
      {
        seen[i] = true;
        pending.push_back(i);
      }
    }
    size_t reached = 0;
    while (!pending.empty())
    {
      const size_t cur = pending.back();
      pending.pop_back();
      ++reached;
      for (size_t i = 0; i < logs.size(); ++i)
      {
        const glm::ivec3 d = logs[i] - logs[cur];
        if (!seen[i] && std::abs(d.x) + std::abs(d.y) + std::abs(d.z) == 1)
        {
          seen[i] = true;
          pending.push_back(i);
        }
      }
    }
    FluidTest::Expect(reached == logs.size(), kTestName,
                      "profile 3 trunk is face-connected");
  }
}

static void TestProceduralVegetationStrategies(cutum::UBlockRegistry &registry)
{
  cutum::ProceduralSettings settings = MakeVegetationSettings();
  settings.SeaLevel = 48;
  settings.MaxHeight = 128;
  // Legacy crown/branch shape contract.
  settings.WorldGenProfileVersion = 2;
  cutum::UBlockWorld world;
  world.SetFluidDefinitions(registry.GetDefinitions());
  cutum::WorldGenContext ctx(world, registry, settings);
  ctx.WorldgenOwnerPackId = "minetest_default_16";
  ctx.ResolveBlockIds();
  cutum::RegisterBuiltinObjectFeaturePlacementStrategies();

  const cutum::BlockId grass = ctx.Blocks.Grass;
  FluidTest::Expect(grass != cutum::BLOCK_AIR, kTestName,
                    "procedural flora test ground resolves");
  for (int z = -8; z <= 16; ++z)
  {
    for (int x = -8; x <= 16; ++x)
    {
      world.SetBlock(glm::ivec3(x, 51, z), grass);
    }
  }

  cutum::ObjectFeatureRule tree;
  tree.Mode = cutum::ObjectFeatureRuleMode::ProceduralTree;
  tree.Tree.HeightMin = 7;
  tree.Tree.HeightMax = 9;
  tree.Tree.CrownRadiusX = 4;
  tree.Tree.CrownRadiusZ = 4;
  tree.Tree.CrownHeight = 5;
  tree.Tree.BranchCount = 3;
  tree.Tree.CrownLobes = 0;
  tree.Tree.RootChance = 0.0f;
  tree.Tree.CrownIrregularity = 0.28f;
  tree.Tree.TrunkBlock = "mtg_tree";
  tree.Tree.LeavesBlock = "mtg_leaves";
  const auto treeStrategy =
      cutum::UObjectFeaturePlacementStrategyRegistry::Find(
          cutum::ObjectFeatureRuleMode::ProceduralTree);
  FluidTest::Expect(treeStrategy != nullptr, kTestName,
                    "procedural tree strategy is registered");
  FluidTest::Expect(treeStrategy->Place(ctx, 0, 0, 51, tree), kTestName,
                    "procedural tree strategy places its profile");
  FluidTest::Expect(CountBlocksNamed(world, registry, "mtg_tree") > 0 &&
                        CountBlocksNamed(world, registry, "mtg_leaves") > 0,
                    kTestName,
                    "procedural tree writes species trunk and canopy blocks");
  int tree_extent = 0;
  const cutum::BlockId tree_trunk = registry.GetIdByTypeName("mtg_tree");
  const cutum::BlockId tree_leaves = registry.GetIdByTypeName("mtg_leaves");
  world.ForEachBlock(
      [&](glm::ivec3 position, cutum::BlockId block)
      {
        if (block == tree_trunk || block == tree_leaves)
        {
          tree_extent = std::max(tree_extent, std::max(std::abs(position.x),
                                                       std::abs(position.z)));
        }
      });
  bool oak_has_lateral_wood = false;
  std::vector<glm::ivec3> oak_logs;
  world.ForEachBlock(
      [&](glm::ivec3 position, cutum::BlockId block)
      {
        if (block != tree_trunk)
        {
          return;
        }
        oak_logs.push_back(position);
        if (position.x != 0 || position.z != 0)
        {
          oak_has_lateral_wood = true;
        }
      });
  FluidTest::Expect(tree_extent >= 4 && oak_has_lateral_wood, kTestName,
                    "broadleaf profile grows lateral branches");
  const auto oakRoot = std::find_if(
      oak_logs.begin(), oak_logs.end(), [](const glm::ivec3 &position)
      { return position.x == 0 && position.y == 52 && position.z == 0; });
  std::vector<bool> reachedOakLog(oak_logs.size(), false);
  std::vector<size_t> pendingOakLogs;
  if (oakRoot != oak_logs.end())
  {
    const size_t rootIndex =
        static_cast<size_t>(std::distance(oak_logs.begin(), oakRoot));
    reachedOakLog[rootIndex] = true;
    pendingOakLogs.push_back(rootIndex);
  }
  size_t connectedOakLogCount = 0;
  while (!pendingOakLogs.empty())
  {
    const size_t currentIndex = pendingOakLogs.back();
    pendingOakLogs.pop_back();
    ++connectedOakLogCount;
    for (size_t candidateIndex = 0; candidateIndex < oak_logs.size();
         ++candidateIndex)
    {
      if (reachedOakLog[candidateIndex])
      {
        continue;
      }
      const glm::ivec3 delta =
          oak_logs[candidateIndex] - oak_logs[currentIndex];
      if (std::abs(delta.x) + std::abs(delta.y) + std::abs(delta.z) == 1)
      {
        reachedOakLog[candidateIndex] = true;
        pendingOakLogs.push_back(candidateIndex);
      }
    }
  }
  FluidTest::Expect(
      connectedOakLogCount == oak_logs.size(), kTestName,
      "procedural broadleaf trunk and branches are face-connected");

  cutum::ObjectFeatureRule pine;
  pine.Mode = cutum::ObjectFeatureRuleMode::ProceduralTree;
  pine.Tree.HeightMin = 10;
  pine.Tree.HeightMax = 10;
  pine.Tree.CrownRadiusX = 3;
  pine.Tree.CrownRadiusZ = 3;
  pine.Tree.CrownHeight = 8;
  pine.Tree.BranchCount = 4;
  pine.Tree.CrownLobes = 0;
  pine.Tree.LeafDensity = 1.0f;
  pine.Tree.RootChance = 0.0f;
  pine.Tree.CrownShape = cutum::TreeCrownShape::Conical;
  pine.Tree.TrunkBlock = "mtg_pine_tree";
  pine.Tree.LeavesBlock = "mtg_pine_needles";
  FluidTest::Expect(treeStrategy->Place(ctx, 14, 14, 51, pine), kTestName,
                    "conical tree strategy places a pine profile");
  const cutum::BlockId pine_trunk = registry.GetIdByTypeName("mtg_pine_tree");
  const cutum::BlockId pine_needles =
      registry.GetIdByTypeName("mtg_pine_needles");
  int lowest_pine_foliage_y = std::numeric_limits<int>::max();
  bool pine_has_lateral_bough = false;
  world.ForEachBlock(
      [&](glm::ivec3 position, cutum::BlockId block)
      {
        if (position.x < 8 || position.x > 20 || position.z < 8 ||
            position.z > 20)
        {
          return;
        }
        if (block == pine_needles)
        {
          lowest_pine_foliage_y = std::min(lowest_pine_foliage_y, position.y);
        }
        if (block == pine_trunk && position.y > 51 &&
            (position.x != 14 || position.z != 14))
        {
          pine_has_lateral_bough = true;
        }
      });
  FluidTest::Expect(lowest_pine_foliage_y <= 55, kTestName,
                    "conical pine foliage extends to the lower crown");
  FluidTest::Expect(pine_has_lateral_bough, kTestName,
                    "conical pine has lateral branches in multiple tiers");

  const int trees_before_clipped_tree =
      CountBlocksNamed(world, registry, "mtg_tree");
  for (int y = 55; y <= 63; ++y)
  {
    for (int dz = -1; dz <= 1; ++dz)
    {
      for (int dx = -1; dx <= 1; ++dx)
      {
        if (dx == 0 && dz == 0)
        {
          continue;
        }
        world.SetBlock(glm::ivec3(-6 + dx, y, 8 + dz), grass);
      }
    }
  }
  FluidTest::Expect(
      treeStrategy->Place(ctx, -6, 8, 51, tree), kTestName,
      "tree keeps a viable silhouette when hillside voxels block its canopy");
  FluidTest::Expect(
      CountBlocksNamed(world, registry, "mtg_tree") > trees_before_clipped_tree,
      kTestName,
      "clipped tree retains its central trunk instead of failing placement");

  cutum::ObjectFeatureRule shrub;
  shrub.Mode = cutum::ObjectFeatureRuleMode::ProceduralShrub;
  shrub.Shrub.HeightMin = 2;
  shrub.Shrub.HeightMax = 3;
  shrub.Shrub.RadiusX = 2;
  shrub.Shrub.RadiusZ = 3;
  shrub.Shrub.LobeCount = 4;
  shrub.Shrub.StemBlock = "mtg_bush_stem";
  shrub.Shrub.LeavesBlock = "mtg_bush_leaves";
  const auto shrubStrategy =
      cutum::UObjectFeaturePlacementStrategyRegistry::Find(
          cutum::ObjectFeatureRuleMode::ProceduralShrub);
  FluidTest::Expect(shrubStrategy != nullptr, kTestName,
                    "procedural shrub strategy is registered");
  FluidTest::Expect(shrubStrategy->Place(ctx, 12, 0, 51, shrub), kTestName,
                    "procedural shrub strategy places its profile");
  FluidTest::Expect(CountBlocksNamed(world, registry, "mtg_bush_leaves") > 0,
                    kTestName, "procedural shrub writes a leafy silhouette");

  const int stems_before_clipped_shrub =
      CountBlocksNamed(world, registry, "mtg_bush_stem");
  for (int dz = -1; dz <= 1; ++dz)
  {
    for (int dx = -1; dx <= 1; ++dx)
    {
      if (dx == 0 && dz == 0)
      {
        continue;
      }
      world.SetBlock(glm::ivec3(-6 + dx, 52, -5 + dz), grass);
      world.SetBlock(glm::ivec3(-6 + dx, 53, -5 + dz), grass);
    }
  }
  FluidTest::Expect(
      shrubStrategy->Place(ctx, -6, -5, 51, shrub), kTestName,
      "shrub keeps viable foliage when hillside voxels block leaves");
  FluidTest::Expect(
      CountBlocksNamed(world, registry, "mtg_bush_stem") >
          stems_before_clipped_shrub,
      kTestName, "clipped shrub retains its stem instead of failing placement");
}

static void TestWorldGenSetsSchemaV2Migration()
{
  using json = nlohmann::json;
  json legacy;
  legacy["schema_version"] = 1;
  legacy["objects"]["vegetation"]["default"]["entries"] =
      json::array({{{"object", "tree_procedural_oak"},
                    {"weight", 5},
                    {"spacing", 39},
                    {"seed_offset", 4012},
                    {"biomes", {"forest", "plains"}}},
                   {{"object", "legacy_absolute_height"},
                    {"weight", 1},
                    {"spacing", 48},
                    {"seed_offset", 9001},
                    {"biomes", {"plains"}},
                    {"ecology", {{"elevation", {10, 20}}}}}});
  cutum::WorldGenSets sets;
  std::string error;
  FluidTest::Expect(cutum::ParseWorldGenSets(legacy, sets, error), kTestName,
                    "legacy worldgen_sets schema loads");
  FluidTest::Expect(sets.SchemaVersion == 1, kTestName,
                    "legacy schema version is preserved before migration");

  const cutum::ObjectFeatureConfig &authored =
      cutum::UObjectFeatureConfigStorage::Get();
  cutum::UpgradeLegacyWorldGenSetMetadata(sets, authored, 2);
  FluidTest::Expect(
      sets.SchemaVersion == 3, kTestName,
      "profile-2 world upgrades worldgen_sets to reference-aware schema");
  const cutum::ObjectFeatureConfig resolved =
      cutum::ResolveObjectFeatures(sets, authored, 2);
  const auto tree =
      std::find_if(resolved.Vegetation.begin(), resolved.Vegetation.end(),
                   [](const cutum::ObjectFeatureRule &rule)
                   { return rule.ObjectName == "tree_procedural_oak"; });
  FluidTest::Expect(
      tree != resolved.Vegetation.end() &&
          tree->Mode == cutum::ObjectFeatureRuleMode::ProceduralTree &&
          tree->Tree.CrownLobes == 4 && tree->Weight == 5 &&
          tree->Spacing == 39 &&
          tree->Ecology.ElevationRef ==
              cutum::FeatureEcologyProfile::ElevationReference::SeaLevel &&
          tree->Ecology.MinElevation == -3 && tree->Ecology.MaxElevation == 52,
      kTestName,
      "migration restores authored profile reference and mode, preserving "
      "saved frequency");

  const auto absoluteHeight =
      std::find_if(resolved.Vegetation.begin(), resolved.Vegetation.end(),
                   [](const cutum::ObjectFeatureRule &rule)
                   { return rule.ObjectName == "legacy_absolute_height"; });
  FluidTest::Expect(
      absoluteHeight != resolved.Vegetation.end() &&
          absoluteHeight->Ecology.ElevationRef ==
              cutum::FeatureEcologyProfile::ElevationReference::AbsoluteY &&
          absoluteHeight->Ecology.AllowsElevation(15, 63) &&
          !absoluteHeight->Ecology.AllowsElevation(73, 63),
      kTestName, "legacy elevation arrays continue to mean absolute world Y");

  json saved;
  cutum::WriteWorldGenSets(saved, sets);
  FluidTest::Expect(
      saved["worldgen_sets"]["schema_version"] == 3 &&
          saved["worldgen_sets"]["objects"]["vegetation"]["default"]["entries"]
               [0]["mode"] == "procedural_tree" &&
          saved["worldgen_sets"]["objects"]["vegetation"]["default"]["entries"]
               [0]
                   .contains("tree_profile") &&
          saved["worldgen_sets"]["objects"]["vegetation"]["default"]["entries"]
               [0]["ecology"]["elevation"]["above_sea_level"] ==
              json::array({-3, 52}) &&
          saved["worldgen_sets"]["objects"]["vegetation"]["default"]["entries"]
               [1]["ecology"]["elevation"] == json::array({10, 20}),
      kTestName,
      "schema-v3 save retains strategy, tree profile, and elevation reference");

  cutum::WorldGenSets roundTripped;
  FluidTest::Expect(
      cutum::ParseWorldGenSets(saved["worldgen_sets"], roundTripped, error),
      kTestName, "schema-v3 worldgen_sets parses after save");
  const cutum::ObjectFeatureConfig roundTrippedFeatures =
      cutum::ResolveObjectFeatures(roundTripped, authored, 2);
  const auto roundTrippedTree =
      std::find_if(roundTrippedFeatures.Vegetation.begin(),
                   roundTrippedFeatures.Vegetation.end(),
                   [](const cutum::ObjectFeatureRule &rule)
                   { return rule.ObjectName == "tree_procedural_oak"; });
  FluidTest::Expect(
      roundTrippedTree != roundTrippedFeatures.Vegetation.end() &&
          roundTrippedTree->Ecology.ElevationRef ==
              cutum::FeatureEcologyProfile::ElevationReference::SeaLevel &&
          roundTrippedTree->Ecology.AllowsElevation(63 + 52, 63) &&
          !roundTrippedTree->Ecology.AllowsElevation(63 + 53, 63),
      kTestName,
      "schema-v3 round trip keeps the sea-relative tree height range");
}

static void TestDataDefinedBiomeAffectsSampling()
{
  const fs::path root =
      fs::temp_directory_path() /
      ("cubatarium_worldgen_biome_" +
       std::to_string(
           std::chrono::steady_clock::now().time_since_epoch().count()));
  fs::create_directories(root / "biomes");
  {
    std::ofstream pack(root / "pack.json");
    pack << R"({"id":"data_defined_biome_test"})";
    std::ofstream biome(root / "biomes" / "azure_meadow.json");
    biome << R"({
      "schema_version": 2,
      "id": "azure_meadow",
      "domain": "surface",
      "climate": {"temperature": [0.0, 1.0], "weight": 100.0},
      "height": {"base_offset": 24.0, "amplitude_mul": 1.0},
      "palette": {"surface": "sand", "subsurface": "sandstone"}
    })";
    std::ofstream caveBiome(root / "biomes" / "crystal_caves.json");
    caveBiome << R"({
      "schema_version": 2,
      "id": "crystal_caves",
      "domain": "cave",
      "climate": {"temperature": [0.0, 1.0], "moisture": [0.0, 1.0], "weight": 100.0},
      "features": {"cave_features": {"moss_patch": 0.5}}
    })";
  }
  FluidTest::Expect(cutum::UWorldGenPack::LoadFromDirectory(root.string()),
                    kTestName, "data-defined biome pack loads");
  const auto pack = cutum::UWorldGenPack::GetSnapshot();
  const cutum::BiomeHandle handle = pack->BiomeRegistry->Find("azure_meadow");
  FluidTest::Expect(
      handle.IsValid(), kTestName,
      "data-defined biome is registered without a C++ classifier");
  cutum::WorldGenTuning tuning;
  cutum::UBiomeSampler sampler(kSeed, tuning, 2);
  FluidTest::Expect(
      sampler.At3DKey(17, 24, -31, 80, 48, 128) == "crystal_caves", kTestName,
      "custom cave biome is selected by the 3D biome sampler");

  cutum::ProceduralSettings settings;
  cutum::ApplyWorldGenPreset(settings, "balanced");
  cutum::BiomeWeightSet weights;
  bool sampled = false;
  for (int x = -8; x <= 8 && !sampled; ++x)
  {
    for (int z = -8; z <= 8; ++z)
    {
      weights = cutum::ComputeBiomeWeights(x, z, 64, settings.SeaLevel,
                                           settings.MaxHeight, kSeed,
                                           settings.Tuning);
      if (weights.Weight(handle) > 0.8f)
      {
        sampled = true;
        break;
      }
    }
  }
  FluidTest::Expect(sampled, kTestName,
                    "climate profile participates in dynamic biome blending");
  const cutum::BiomeHandle plainsHandle =
      cutum::BiomeHandleForId(cutum::BiomeId::Plains);
  FluidTest::Expect(plainsHandle.IsValid(), kTestName,
                    "built-in biomes resolve to pack-scoped handles");
  for (const cutum::BiomeId builtin :
       {cutum::BiomeId::Plains, cutum::BiomeId::Forest, cutum::BiomeId::Desert,
        cutum::BiomeId::Hills, cutum::BiomeId::Tundra, cutum::BiomeId::Savanna,
        cutum::BiomeId::Foothills, cutum::BiomeId::Scrubland,
        cutum::BiomeId::ColdSteppe, cutum::BiomeId::Ocean,
        cutum::BiomeId::Beach, cutum::BiomeId::Mountains,
        cutum::BiomeId::SnowySlopes, cutum::BiomeId::JaggedPeaks,
        cutum::BiomeId::LushCaves, cutum::BiomeId::DripstoneCaves,
        cutum::BiomeId::DeepDark})
  {
    FluidTest::Expect(cutum::BiomeHandleForId(builtin).IsValid(), kTestName,
                      "all built-in biome keys exist in the active snapshot");
  }
  FluidTest::Expect(std::all_of(weights.entries.begin(), weights.entries.end(),
                                [](const cutum::BiomeBlendEntry &entry)
                                { return entry.biome.IsValid(); }),
                    kTestName, "biome blends contain handles only");
  FluidTest::Expect(cutum::BiomeKeyForHandle(
                        cutum::DominantBiomeHandle(weights)) == "azure_meadow",
                    kTestName, "dynamic biome can dominate the sampled blend");
  FluidTest::Expect(cutum::DominantBiome(weights) == cutum::BiomeId::Invalid,
                    kTestName,
                    "custom dominant biomes do not silently become plains");

  const int with_custom = cutum::RefineSurfaceYWithBiomes(
      0, 0, 64, settings, kSeed, settings.Tuning, {}, &weights);
  cutum::BiomeWeightSet legacy_only = weights;
  legacy_only.SetWeight(handle, 0.0f);
  legacy_only.Normalize();
  const int without_custom = cutum::RefineSurfaceYWithBiomes(
      0, 0, 64, settings, kSeed, settings.Tuning, {}, &legacy_only);
  if (with_custom < without_custom + 4)
  {
    const auto *definition =
        cutum::UWorldGenPack::BiomeDefinitionFor("azure_meadow");
    std::cerr << "custom biome height diagnostic: weight="
              << weights.Weight(handle) << " base_offset="
              << (definition ? definition->Height.baseOffsetBlocks : -999.0f)
              << " unified=" << settings.Tuning.useUnifiedHeightField
              << " sea=" << settings.SeaLevel << " min=" << settings.MinHeight
              << " max=" << settings.MaxHeight << " with=" << with_custom
              << " without=" << without_custom << std::endl;
  }
  FluidTest::Expect(with_custom >= without_custom + 4, kTestName,
                    "dynamic biome height profile affects terrain shaping");
  std::error_code ec;
  fs::remove_all(root, ec);
}

static void TestImageBiomeSourceStrategy()
{
  FluidTest::Expect(cutum::UWorldGenPack::LoadPackId("image_demo"), kTestName,
                    "image biome pack loads for source strategy test");
  cutum::WorldGenTuning tuning;
  cutum::UBiomeSampler caveSampler(kSeed, tuning, 2);
  cutum::ImageMapBiomeSource source(caveSampler);
  const int x = 36;
  const int z = -20;
  const cutum::BiomeId expected = cutum::UWorldGenPack::BiomeAtImage(x, z);
  const cutum::BiomeWeightSet weights =
      source.SampleSurfaceWeights(x, z, 64, 48, 128);
  FluidTest::Expect(cutum::DominantBiome(weights) == expected, kTestName,
                    "image source returns its map biome as the surface blend");
  FluidTest::Expect(source.Sample3DKey(x, 80, z, 80, 48, 128) ==
                        cutum::BiomeIdToString(expected),
                    kTestName,
                    "image source routes surface 3D keys to image map");
  FluidTest::Expect(source.Sample3DKey(x, 24, z, 80, 48, 128) ==
                        caveSampler.At3DKey(x, 24, z, 80, 48, 128),
                    kTestName, "image source keeps the procedural cave source");
}

static void TestCaveFeaturePlacementStrategies(cutum::UBlockRegistry &registry)
{
  cutum::ProceduralSettings settings = MakeVegetationSettings();
  settings.EnableCaves = true;
  settings.MaxHeight = 128;
  settings.SeaLevel = 48;

  cutum::UBlockWorld world;
  world.SetFluidDefinitions(registry.GetDefinitions());
  cutum::WorldGenContext ctx(world, registry, settings);
  ctx.WorldgenOwnerPackId = "minetest_default_16";
  ctx.ResolveBlockIds();

  const cutum::BlockId stone = registry.GetIdByTypeName("stone");
  const cutum::BlockId mossVisual = cutum::ResolvePackScatterBlockId(
      registry, ctx.WorldgenOwnerPackId, "mtg_grass_1");
  FluidTest::Expect(stone != cutum::BLOCK_AIR && mossVisual != cutum::BLOCK_AIR,
                    kTestName,
                    "cave strategy test blocks resolve from the pack");
  world.SetBlock(glm::ivec3(0, 8, 0), stone);

  cutum::ObjectFeatureConfig features;
  cutum::CaveFeatureRule moss;
  moss.Biome = cutum::BiomeId::LushCaves;
  moss.BiomeKeyName = "lush_caves";
  moss.Kind = cutum::CaveFeatureKind::MossPatch;
  moss.BlockName = "mtg_grass_1";
  moss.Chance = 1;
  moss.Radius = 0;
  features.CaveFeatures.push_back(moss);
  ctx.ObjectFeatures = &features;

  const auto biomeAt = [](int, int, int, int)
  { return cutum::BiomeId::LushCaves; };
  const auto biomeKeyAt = [](int, int, int, int)
  { return cutum::BiomeKey("lush_caves"); };
  FluidTest::Expect(
      cutum::TryPlaceCaveFeatures(ctx, 0, 0, 20, biomeAt, biomeKeyAt),
      kTestName, "registered cave strategy places a moss floor");
  const cutum::BlockId placedBlock = world.GetBlock(glm::ivec3(0, 8, 0));
  const std::string placedMessage =
      "cave feature writes its resolved block through sink (expected=" +
      std::to_string(static_cast<int>(mossVisual)) +
      ", actual=" + std::to_string(static_cast<int>(placedBlock)) + ")";
  FluidTest::Expect(placedBlock == mossVisual, kTestName,
                    placedMessage.c_str());
  FluidTest::Expect(world.GetBlock(glm::ivec3(0, 5, 0)) == cutum::BLOCK_AIR,
                    kTestName,
                    "unresolved cave rock aliases do not treat air as a floor");
  FluidTest::Expect(cutum::UCaveFeaturePlacementStrategyRegistry::Find(
                        cutum::CaveFeatureKind::MossPatch) != nullptr &&
                        cutum::UCaveFeaturePlacementStrategyRegistry::Find(
                            cutum::CaveFeatureKind::Stalagmite) != nullptr &&
                        cutum::UCaveFeaturePlacementStrategyRegistry::Find(
                            cutum::CaveFeatureKind::AmethystCluster) != nullptr,
                    kTestName,
                    "built-in cave feature strategies are registered");
}

static void TestScatterSurfaceConstraint(cutum::UBlockRegistry &registry)
{
  cutum::ProceduralSettings settings = MakeVegetationSettings();
  settings.SeaLevel = 48;
  settings.MaxHeight = 128;
  cutum::UBlockWorld world;
  world.SetFluidDefinitions(registry.GetDefinitions());
  cutum::WorldGenContext ctx(world, registry, settings);
  ctx.WorldgenOwnerPackId = "minetest_default_16";
  ctx.ResolveBlockIds();

  const cutum::BlockId dirt = registry.GetIdByTypeName("dirt");
  const cutum::BlockId water = ctx.Blocks.Water;
  const cutum::BlockId papyrus = cutum::ResolvePackScatterBlockId(
      registry, ctx.WorldgenOwnerPackId, "mtg_papyrus");
  FluidTest::Expect(dirt != cutum::BLOCK_AIR && water != cutum::BLOCK_AIR &&
                        papyrus != cutum::BLOCK_AIR,
                    kTestName, "near-water scatter test blocks resolve");
  world.SetBlock(glm::ivec3(0, 51, 0), dirt);
  world.SetBlock(glm::ivec3(1, 51, 0), water);
  world.SetBlock(glm::ivec3(10, 51, 0), dirt);

  cutum::ObjectFeatureRule rule;
  rule.Mode = cutum::ObjectFeatureRuleMode::ScatterBlocks;
  rule.Scatter.BlockName = "mtg_papyrus";
  rule.Scatter.Attempts = 1;
  rule.Scatter.Radius = 0;
  rule.Surface.Kind = cutum::SurfaceConstraintKind::NearWater;
  rule.Surface.NearWaterRadius = 3;
  const auto strategy = cutum::UObjectFeaturePlacementStrategyRegistry::Find(
      cutum::ObjectFeatureRuleMode::ScatterBlocks);
  FluidTest::Expect(strategy != nullptr, kTestName,
                    "scatter placement strategy is registered");
  FluidTest::Expect(strategy->Place(ctx, 0, 0, 51, rule), kTestName,
                    "near-water scatter placement is accepted");
  FluidTest::Expect(world.GetBlock(glm::ivec3(0, 52, 0)) == papyrus, kTestName,
                    "near-water scatter writes the plant block");
  FluidTest::Expect(!strategy->Place(ctx, 10, 0, 51, rule), kTestName,
                    "near-water scatter placement rejects dry locations");
}

static void TestCrossChunkTreeConflictStable(cutum::UBlockRegistry &registry)
{
  const cutum::BlockId trunk = registry.GetIdByTypeName("mtg_tree");
  const cutum::BlockId leaves = registry.GetIdByTypeName("mtg_leaves");
  FluidTest::Expect(trunk != cutum::BLOCK_AIR && leaves != cutum::BLOCK_AIR,
                    kTestName, "custom tree conflict blocks resolve");

  const glm::ivec3 overlap(16, 8, 8);
  const std::vector<std::pair<cutum::BlockId, int>> ranks = {{trunk, 3},
                                                             {leaves, 1}};
  cutum::UChunkBuffer leftBuffer;
  leftBuffer.SetWorldGenOwnerChunk(glm::ivec3(0, 0, 0), registry, ranks);
  leftBuffer.SetBlock(overlap, trunk);
  cutum::UChunkBuffer rightBuffer;
  rightBuffer.SetWorldGenOwnerChunk(glm::ivec3(1, 0, 0), registry, ranks);
  rightBuffer.SetBlock(overlap, leaves);

  cutum::UBlockWorld leftThenRight;
  leftBuffer.ApplyTo(leftThenRight);
  rightBuffer.ApplyTo(leftThenRight);
  cutum::UBlockWorld rightThenLeft;
  rightBuffer.ApplyTo(rightThenLeft);
  leftBuffer.ApplyTo(rightThenLeft);
  FluidTest::Expect(leftThenRight.GetBlock(overlap) == trunk &&
                        rightThenLeft.GetBlock(overlap) == trunk,
                    kTestName,
                    "cross-chunk procedural tree overlap resolves identically");
}

static void
TestFloraRanksCoverDecorationAndStructures(cutum::UBlockRegistry &registry)
{
  cutum::ObjectFeatureConfig config;
  cutum::ObjectFeatureRule decor;
  decor.Mode = cutum::ObjectFeatureRuleMode::ScatterBlocks;
  decor.Scatter.BlockName = "log_jungle";
  config.Decoration.push_back(decor);
  cutum::ObjectFeatureRule structure;
  structure.Mode = cutum::ObjectFeatureRuleMode::ScatterBlocks;
  structure.Scatter.BlockName = "mtg_mossycobble";
  config.Structures.push_back(structure);

  const auto ranks = cutum::CollectFloraFeatureBlockRanks(
      registry, nullptr, "minetest_default_16", &config);
  const auto findRank = [&ranks](cutum::BlockId id) -> int
  {
    const auto it =
        std::find_if(ranks.begin(), ranks.end(),
                     [id](const std::pair<cutum::BlockId, int> &entry)
                     { return entry.first == id; });
    return it == ranks.end() ? 0 : it->second;
  };

  const cutum::BlockId decor_block = cutum::ResolvePackScatterBlockId(
      registry, "minetest_default_16", "log_jungle");
  const cutum::BlockId structure_block = cutum::ResolvePackScatterBlockId(
      registry, "minetest_default_16", "mtg_mossycobble");
  FluidTest::Expect(decor_block != cutum::BLOCK_AIR &&
                        structure_block != cutum::BLOCK_AIR,
                    kTestName, "decoration/structure blocks resolve");
  FluidTest::Expect(findRank(decor_block) == 1, kTestName,
                    "decoration scatter block carries a cross-chunk rank");
  FluidTest::Expect(findRank(structure_block) == 1, kTestName,
                    "structure scatter block carries a cross-chunk rank");
}

static void TestOceanFallbackSpawnStaysAboveSea(cutum::UBlockRegistry &registry)
{
  cutum::ProceduralSettings settings;
  settings.SeaLevel = 63;
  settings.MinHeight = -64;
  settings.MaxHeight = 320;
  settings.FillWater = true;
  cutum::UBlockWorld world;
  world.SetFluidDefinitions(registry.GetDefinitions());
  cutum::WorldGenContext context(world, registry, settings);
  const cutum::BlockId stone = registry.GetIdByTypeName("stone");
  const cutum::BlockId water = registry.GetIdByTypeName("water");
  FluidTest::Expect(stone != cutum::BLOCK_AIR && water != cutum::BLOCK_AIR,
                    kTestName, "ocean fallback test blocks resolve");
  world.SetBlock(glm::ivec3(0, 34, 0), stone);
  for (int y = 35; y <= settings.SeaLevel; ++y)
  {
    world.SetBlock(glm::ivec3(0, y, 0), water);
  }
  UFixedSurfacePipeline pipeline(context, 34);

  const glm::vec3 spawn = pipeline.ResolvePlayerSpawnPosition(world, registry);
  FluidTest::Expect(spawn.y >= 63.0f + 1.62f + 0.5f, kTestName,
                    "ocean-only fallback does not spawn below the sea surface");
}

} // namespace

static void TestPackDefaultsRespectUserFieldsInProfile3()
{
  cutum::ProceduralSettings user;
  user.WorldGenProfileVersion = 3;
  user.Caves.maxDepthBelowSurface = 20;
  user.Ravines.enabled = false;
  user.Ravines.rarity = 999;
  user.Ravines.aquaticMaxDepth = 2;
  user.Ravines.fillWater = true;
  cutum::ProceduralSettings preserved = user;
  cutum::UWorldGenPack::ApplyPackCaveDefaults(preserved, true);
  cutum::UWorldGenPack::ApplyPackRavineDefaults(preserved, true);
  FluidTest::Expect(
      preserved.Caves.maxDepthBelowSurface == 20 &&
          !preserved.Ravines.enabled && preserved.Ravines.rarity == 999 &&
          preserved.Ravines.aquaticMaxDepth == 2 && preserved.Ravines.fillWater,
      kTestName,
      "profile 3 keeps cave depth and ravine settings from the form");
  FluidTest::Expect(
      preserved.Caves.densityCaveAmplitude != user.Caves.densityCaveAmplitude,
      kTestName, "pack-only cave fields are still applied for profile 3");
  cutum::ProceduralSettings legacy = user;
  legacy.WorldGenProfileVersion = 2;
  cutum::UWorldGenPack::ApplyPackCaveDefaults(legacy, false);
  cutum::UWorldGenPack::ApplyPackRavineDefaults(legacy, false);
  FluidTest::Expect(
      legacy.Caves.maxDepthBelowSurface != 20 && legacy.Ravines.enabled &&
          legacy.Ravines.rarity != 999,
      kTestName,
      "older profiles still take cave and ravine values from the pack");
}

int main(int argc, char **argv)
{
  const fs::path repo_root = FindRepoRoot();
  std::error_code ec;
  fs::current_path(repo_root, ec);

  FluidTest::Expect(cutum::UWorldGenRefs::LoadFromFile(repo_root / "content" /
                                                       "worldgen_refs.json"),
                    kTestName, "worldgen_refs load failed");
  FluidTest::Expect(cutum::UObjectFeatureConfigStorage::LoadFromFile(
                        repo_root / "content" / "object_features.json"),
                    kTestName, "object_features load failed");
  const auto featureConfig = cutum::UObjectFeatureConfigStorage::GetSnapshot();
  FluidTest::Expect(
      featureConfig &&
          std::fabs(featureConfig->VegetationDistribution.RegionalNoiseScale -
                    0.0035f) < 0.00001f &&
          std::fabs(featureConfig->GroundCoverDistribution.RegionalNoiseScale -
                    0.006f) < 0.00001f,
      kTestName, "regional density profiles load from object_features.json");
  const bool allProceduralTreeProfilesHaveBranches =
      featureConfig &&
      std::all_of(
          featureConfig->Vegetation.begin(), featureConfig->Vegetation.end(),
          [](const cutum::ObjectFeatureRule &rule)
          {
            return rule.Mode != cutum::ObjectFeatureRuleMode::ProceduralTree ||
                   rule.Tree.BranchCount > 0;
          });
  FluidTest::Expect(allProceduralTreeProfilesHaveBranches, kTestName,
                    "configured procedural tree species enable branch growth");
  TestWorldGenSetsSchemaV2Migration();
  FluidTest::Expect(cutum::UWorldGenPack::LoadPackId("default"), kTestName,
                    "worldgen pack load failed");
  FluidTest::Expect(
      cutum::UWorldGenPack::AquifersConfig().Loaded, kTestName,
      "aquifer configuration loads from the active worldgen pack");
  TestPackDefaultsRespectUserFieldsInProfile3();
  if (argc > 1 && std::string(argv[1]) == "--population-metrics-only")
  {
    constexpr size_t window=1024;
    const auto initial=cutum::ChunkPopulateDiagnostics::GetSummary();
    FluidTest::Expect(initial.completedCount==0,kTestName,"metrics fixture starts without generation");
    std::vector<double> recent,detail;
    double total=0; uint64_t details=0;
    const size_t count=5*window+37;
    for(size_t i=0;i<count;++i) {
      cutum::ChunkPopulateTiming timing;
      timing.totalMs=i<4*window ? 100000.0 : double(i-4*window);
      timing.detailEnabled=i%3==0;
      timing.mudflowMs=timing.totalMs;
      timing.postStageMs.fill(timing.totalMs);
      total+=timing.totalMs;
      recent.push_back(timing.totalMs);
      if(recent.size()>window) recent.erase(recent.begin());
      if(timing.detailEnabled) {
        ++details; detail.push_back(timing.totalMs);
        if(detail.size()>window) detail.erase(detail.begin());
      }
      cutum::ChunkPopulateDiagnostics::Record(timing);
      if(i%113==0) (void)cutum::ChunkPopulateDiagnostics::GetSummary();
    }
    std::sort(recent.begin(),recent.end()); std::sort(detail.begin(),detail.end());
    auto summary=cutum::ChunkPopulateDiagnostics::GetSummary();
    FluidTest::Expect(summary.total.medianMs==recent[(recent.size()-1)/2] &&
        summary.total.p95Ms==recent[(recent.size()*95+99)/100-1],kTestName,
        "population percentiles use last bounded window after wrap and intermediate reads");
    FluidTest::Expect(summary.completedCount==count && summary.total.count==count &&
        summary.total.totalMs==total && summary.total.maxMs==100000 &&
        summary.detailCount==details && summary.mudflow.count==details &&
        summary.mudflow.medianMs==detail[(detail.size()-1)/2],kTestName,
        "window does not truncate lifetime totals counts or maxima and details have independent window");
    for(const auto &metric:summary.postStages)
      FluidTest::Expect(metric.count==details && metric.p95Ms==detail[(detail.size()*95+99)/100-1],
          kTestName,"every detailed stage uses bounded independent distribution");
    cutum::ChunkPopulateTiming cancelled; cancelled.totalMs=999999;
    cutum::ChunkPopulateDiagnostics::Record(cancelled,true);
    summary=cutum::ChunkPopulateDiagnostics::GetSummary();
    FluidTest::Expect(summary.discardedCount==1 && summary.completedCount==count &&
        summary.total.maxMs==100000 && cutum::ChunkPopulateDiagnostics::GetLast().totalMs==999999,
        kTestName,"discarded timing updates last observation without contaminating accepted metrics");
    std::vector<std::thread> producers;
    for(int worker=0;worker<4;++worker) producers.emplace_back([] {
      cutum::ChunkPopulateTiming timing; timing.totalMs=7;
      for(int n=0;n<8192;++n) cutum::ChunkPopulateDiagnostics::Record(timing);
    });
    for(auto &producer:producers) producer.join();
    summary=cutum::ChunkPopulateDiagnostics::GetSummary();
    FluidTest::Expect(summary.completedCount==count+4*8192 &&
        summary.total.count==count+4*8192 && summary.total.totalMs==total+4*8192*7 &&
        summary.total.medianMs==7 && summary.total.p95Ms==7 && summary.total.maxMs==100000 &&
        summary.total.sampleCount==window && summary.mudflow.sampleCount==window &&
        summary.mudflow.count==details && summary.distributionWindowCapacity==window &&
        summary.sampleStorageBytes==initial.sampleStorageBytes && summary.sampleStorageBytes>0,
        kTestName,"concurrent recording keeps fixed storage and exact lifetime aggregates");
    std::cout << kTestName << ": population metrics bounded-window PASS count=" << count << std::endl;
    std::cout << "population_metrics_storage_bytes=" << summary.sampleStorageBytes
        << " accepted=" << summary.completedCount << " window=" << summary.total.sampleCount << std::endl;
    return 0;
  }
  if (argc > 1 && std::string(argv[1]) == "--algorithm-budget-only")
  {
    const auto definitions=LoadPackDefinitions(repo_root/"resource_packs"/"minetest_default_16");
    cutum::UBlockRegistry registry(nullptr,definitions);
    cutum::UBlockWorld world;
    auto settings=MakeVegetationSettings(); settings.EnableOres=true;
    settings.MinHeight=0; settings.MaxHeight=63;
    cutum::WorldGenContext ctx(world,registry,settings);
    ctx.WorldgenOwnerPackId="minetest_default_16"; ctx.ResolveBlockIds();
    auto pack=std::make_shared<cutum::WorldGenPack>(*cutum::UWorldGenPack::GetSnapshot());
    std::vector<cutum::BiomeKey> cave_keys;
    for(size_t i=0;i<512;++i) {
      auto key="budget_cave_"+std::to_string(i);
      cave_keys.push_back(key);
      pack->Biomes[key].Climate.Enabled=true;
      pack->Biomes["forest"].SubBiomes["ignored_"+std::to_string(i)].NoiseThreshold=-1;
    }
    pack->BiomeRegistry=std::make_shared<cutum::UBiomeDefinitionRegistry>(cave_keys);
    pack->CaveClimateBiomes.clear();
    for(const auto &key:cave_keys) pack->CaveClimateBiomes.push_back(pack->BiomeRegistry->Find(key));
    pack->Ores.Loaded=true;
    pack->Ores.Rules.resize(512);
    for(auto &rule:pack->Ores.Rules) rule.Slot=std::string(8192,'x');
    auto &coal=pack->Ores.Rules.front(); coal.Slot="ore_coal";
    coal.YPeak=24; coal.YSpread=24; coal.VeinSize=24; coal.Rarity=1;
    auto content=cutum::CaptureWorldGenContentSnapshot(); content.Pack=pack; content.Biomes=pack->BiomeRegistry;
    cutum::UWorldGenContentPinScope pin(std::move(content));
    cutum::UBiomeSampler sampler(kSeed,settings.Tuning,3);
    auto quota=std::make_shared<cutum::UBufferAllocationBudget>(1024);
    auto peer=std::make_shared<cutum::BufferBudgetJob>(); quota->Reserve(*peer,1);
    size_t refused=0,cases=0;
    const auto pressure=[&](const char *name,auto operation) {
      bool denied=false;
      {
        cutum::UBufferBudgetScope active({quota,std::make_shared<cutum::BufferBudgetJob>()});
        try {operation();} catch(const cutum::UBufferBudgetExceeded &) {denied=true;}
      }
      std::cout << "algorithm_budget_case=" << name << " refused=" << denied
          << " remaining=" << quota->UsedBytes() << std::endl;
      FluidTest::Expect(quota->UsedBytes()==1,kTestName,"algorithm temporary unwind preserves only competing credit");
      refused+=denied; ++cases;
    };
    pressure("biome_blend",[] {cutum::BiomeBlend blend; for(uint32_t n=0;n<512;++n) blend.AddWeight({n},1);});
    pressure("subbiome_thresholds",[] {(void)cutum::SubBiomeFor(-7,13,cutum::BiomeId::Forest,kSeed);});
    pressure("cave_candidates",[&] {(void)sampler.At3DKey(-7,8,13,40,settings.SeaLevel,settings.MaxHeight);});
    pressure("ore_resolution",[&] {cutum::FillOreVeins(ctx,-7,13,32,kSeed,1);});
    pressure("valley_surface",[&] {
      cutum::ValleyParams params; params.lowlandOnly=true;
      cutum::CarveChunkValleys(ctx,-16,-16,kSeed,params,settings.SeaLevel,1,
          [](int,int){return 10000;});
    });
    quota->Release(*peer,1);
    auto roomy=std::make_shared<cutum::UBufferAllocationBudget>(1024*1024);
    auto roomy_peer=std::make_shared<cutum::BufferBudgetJob>(); roomy->Reserve(*roomy_peer,1);
    std::weak_ptr<cutum::BufferBudgetJob> escaped_job;
    {
      cutum::BiomeBlend escaped;
      {
        auto job=std::make_shared<cutum::BufferBudgetJob>(); escaped_job=job;
        cutum::UBufferBudgetScope active({roomy,job});
        cutum::BiomeBlend local;
        for(uint32_t n=0;n<512;++n) local.AddWeight({n},1);
        escaped=std::move(local);
        local.AddWeight({9999},2);
        FluidTest::Expect(roomy->UsedBytes()==1+sizeof(cutum::BiomeBlendEntry)*
            (escaped.entries.capacity()+local.entries.capacity()),kTestName,
            "moved-from result allocator remains usable and retains original job");
      }
      const size_t owned=escaped.entries.capacity()*sizeof(cutum::BiomeBlendEntry);
      FluidTest::Expect(!escaped_job.expired() && roomy->UsedBytes()==owned+1,kTestName,
          "returned biome blend retains exact allocation credit outside budget scope");
      auto copied=escaped;
      FluidTest::Expect(roomy->UsedBytes()==1+owned+copied.entries.capacity()*sizeof(cutum::BiomeBlendEntry),
          kTestName,"copy outside scope charges original owner and independent storage");
      auto other=std::make_shared<cutum::UBufferAllocationBudget>(1024*1024);
      auto other_peer=std::make_shared<cutum::BufferBudgetJob>(); other->Reserve(*other_peer,1);
      cutum::BiomeBlend target;
      {
        cutum::UBufferBudgetScope active({other,std::make_shared<cutum::BufferBudgetJob>()});
        cutum::BiomeBlend initial; initial.AddWeight({42},7); target=std::move(initial);
      }
      FluidTest::Expect(other->UsedBytes()>1,kTestName,"move target starts with distinct allocation owner");
      target=copied;
      FluidTest::Expect(other->UsedBytes()==1 && target.Weight({42})==1,kTestName,
          "copy assignment retires old owner's storage and adopts original source owner");
      copied=std::move(target);
      FluidTest::Expect(other->UsedBytes()==1 && copied.Weight({511})==1,kTestName,
          "move assignment releases old storage and transfers independently retained result");
      other->Release(*other_peer,1);
      const auto before=roomy->UsedBytes();
      bool grow_refused=false;
      try {escaped.entries.reserve(1024*1024);} catch(const cutum::UBufferBudgetExceeded &) {grow_refused=true;}
      FluidTest::Expect(grow_refused && roomy->UsedBytes()==before && escaped.Weight({511})==1,kTestName,
          "failed reallocation preserves old entries and exact original reservation");
      {
        cutum::UBufferBudgetScope active({roomy,std::make_shared<cutum::BufferBudgetJob>()});
        struct alignas(64) Aligned {char bytes[64];};
        cutum::GenerationVector<Aligned> aligned(16);
        FluidTest::Expect(reinterpret_cast<uintptr_t>(aligned.data())%64==0,kTestName,
            "generation allocator preserves over-alignment");
        struct Throwing {Throwing(){throw 7;}};
        const auto before_throw=roomy->UsedBytes(); bool threw=false;
        try {cutum::GenerationVector<Throwing> values(8);} catch(int) {threw=true;}
        FluidTest::Expect(threw && roomy->UsedBytes()==before_throw,kTestName,
            "element construction failure frees actual allocation before returning credit");
      }
    }
    FluidTest::Expect(escaped_job.expired() && roomy->UsedBytes()==1,kTestName,
        "last escaped result and allocator destruction retire job owner and all payload credit");
    size_t ore_count=0;
    for(bool configured:{true,false}) {
      pack->Ores.Loaded=configured; // private fixture snapshot; no concurrent users
      const auto generate_ores=[&](bool budgeted) {
        cutum::UBlockWorld ore_world;
        cutum::WorldGenContext ore_ctx(ore_world,registry,settings);
        ore_ctx.WorldgenOwnerPackId="minetest_default_16"; ore_ctx.ResolveBlockIds();
        FluidTest::Expect(ore_ctx.Blocks.Stone!=cutum::BLOCK_AIR && ore_ctx.Blocks.OreCoal!=cutum::BLOCK_AIR,
            kTestName,"ore fixture resolves real stone and coal block identities");
        for(int x=-4;x<4;++x) for(int z=-4;z<4;++z) for(int y=0;y<49;++y)
          ore_world.SetBlock({x,y,z},ore_ctx.Blocks.Stone);
        const auto fill=[&] {
          for(int x=-4;x<4;++x) for(int z=-4;z<4;++z)
            cutum::FillOreVeins(ore_ctx,x,z,50,kSeed,2);
        };
        if(budgeted) {
          cutum::UBufferBudgetScope active({roomy,std::make_shared<cutum::BufferBudgetJob>()}); fill();
        } else fill();
        std::vector<cutum::BlockId> blocks;
        for(int x=-4;x<4;++x) for(int z=-4;z<4;++z) for(int y=0;y<49;++y) {
          const auto id=ore_world.GetBlock({x,y,z}); blocks.push_back(id);
          if(budgeted && id!=ore_ctx.Blocks.Stone) ++ore_count;
        }
        return blocks;
      };
      const auto baseline=generate_ores(false);
      FluidTest::Expect(generate_ores(true)==baseline && roomy->UsedBytes()==1,kTestName,
          "configured and default ore rules preserve every voxel under budgeted allocation");
    }
    FluidTest::Expect(ore_count>0,kTestName,"ore parity covers actual replacements of stone");
    auto floor_budget=std::make_shared<cutum::UBufferAllocationBudget>(16);
    {
      cutum::UBufferBudgetScope active({floor_budget,std::make_shared<cutum::BufferBudgetJob>()});
      cutum::ValleyParams params; params.lowlandOnly=true;
      cutum::CarveChunkValleys(ctx,-16,-16,kSeed,params,settings.SeaLevel,1,
          [](int,int){return 10000;});
    }
    FluidTest::Expect(floor_budget->UsedBytes()==0 && floor_budget->PeakBytes()==22*22*sizeof(int) &&
        floor_budget->LimitBytes()==floor_budget->PeakBytes(),kTestName,
        "one valid oversized numerical working set grows sole-job floor and returns all credit");
    std::cout << "algorithm_lifetime_peak=" << roomy->PeakBytes() << " ore_replacements=" << ore_count
        << " valley_single_input_floor=" << floor_budget->LimitBytes() << std::endl;
    roomy->Release(*roomy_peer,1);
    uint64_t cave_hash=1469598103934665603ULL;
    for(int n=0;n<64;++n) {
      const auto key=sampler.At3DKey(n*7-200,8,n*3-100,40,settings.SeaLevel,settings.MaxHeight);
      for(unsigned char c:key) cave_hash=(cave_hash^c)*1099511628211ULL;
    }
    std::cout << "algorithm_cave_parity_hash=" << cave_hash << " cases=" << cases
        << " refused=" << refused << " final=" << quota->UsedBytes() << std::endl;
    FluidTest::Expect(cave_hash==1685353360627598411ULL,kTestName,
        "actual cave selection matches retained pre-fix64-position output");
    FluidTest::Expect(refused==cases && quota->UsedBytes()==0 && roomy->UsedBytes()==0,kTestName,
        "all five actual numeric working vectors reserve shared quota before allocation");
    return 0;
  }
  if (argc > 1 && std::string(argv[1]) == "--settings-text-budget-only")
  {
    auto settings=MakeVegetationSettings();
    settings.SeedText=std::string(64*1024,'s'); settings.WorldGenPackId=std::string(64*1024,'p');
    settings.WorldGenPresetId=std::string(64*1024,'r');
    auto budget=std::make_shared<cutum::UBufferAllocationBudget>(1024);
    auto peer=std::make_shared<cutum::BufferBudgetJob>(); budget->Reserve(*peer,1);
    bool refused=false;
    {
      cutum::UBufferBudgetScope active({budget,std::make_shared<cutum::BufferBudgetJob>()});
      try {cutum::UColumnSampleBuilder sampler(nullptr,nullptr,settings);}
      catch(const cutum::UBufferBudgetExceeded &) {refused=true;}
    }
    std::cout << "settings_text_refused=" << refused << " remaining=" << budget->UsedBytes() << std::endl;
    FluidTest::Expect(refused && budget->UsedBytes()==1,kTestName,
        "temporary sampler settings copy refuses quota before allocation");
    budget->Release(*peer,1);
    auto roomy=std::make_shared<cutum::UBufferAllocationBudget>(4*1024*1024);
    auto roomy_peer=std::make_shared<cutum::BufferBudgetJob>(); roomy->Reserve(*roomy_peer,1);
    {
      cutum::UBufferBudgetScope active({roomy,std::make_shared<cutum::BufferBudgetJob>()});
      for(size_t length:{size_t(0),size_t(1),size_t(15),size_t(16),size_t(17),size_t(31),size_t(32),
          size_t(47),size_t(48),size_t(255),size_t(256),size_t(64*1024)}) {
        settings.SeedText=std::string(length,'s'); settings.WorldGenPackId=std::string(length,'p');
        settings.WorldGenPresetId=std::string(length,'r');
        auto copied=cutum::ProceduralSettings(settings);
        cutum::ProceduralSettings refreshed;
        cutum::ReleaseRuntimeSettingsText(refreshed);
        refreshed=cutum::ProceduralSettings(settings);
        const auto charged=cutum::RuntimeSettingsTextCapacityBytes(settings);
        FluidTest::Expect(cutum::RuntimeSettingsTextCapacityBytes(copied)<=charged &&
            cutum::RuntimeSettingsTextCapacityBytes(refreshed)<=charged,kTestName,
            "supported string copy and fresh-move refresh capacities fit preallocation charge");
        cutum::UColumnSampleBuilder sampler(nullptr,nullptr,settings);
        FluidTest::Expect(roomy->UsedBytes()==1 && sampler.GetRetainedSettingsTextCapacityBytes()==0 &&
            sampler.Build(3,7).SurfaceY==settings.SeaLevel && settings.SeedText.size()==length,
            kTestName,"fresh copy capacity boundaries release quota and preserve caller/numeric snapshot");
      }
    }
    roomy->Release(*roomy_peer,1);
    FluidTest::Expect(roomy->UsedBytes()==0 && roomy->PeakBytes()>64*1024 && roomy->PeakBytes()<=4*1024*1024,
        kTestName,"active temporary text copy has nonzero bounded peak and final release");
    std::cout << "settings_text_peak_bytes=" << roomy->PeakBytes() << " final_bytes=" << roomy->UsedBytes() << std::endl;
    std::cout << kTestName << ": active settings text budget PASS" << std::endl; return 0;
  }
  if (argc > 1 && std::string(argv[1]) == "--population-context-move-only")
  {
    const auto definitions=LoadPackDefinitions(repo_root/"resource_packs"/"minetest_default_16");
    cutum::UBlockRegistry registry(nullptr,definitions); cutum::UBlockWorld world;
    for(size_t n=0;n<cutum::UWorldGeneratorRegistry::Count();++n) {
      auto settings=MakeVegetationSettings();
      settings.Generator=cutum::UWorldGeneratorRegistry::Get(n).Id;
      settings.SeedText=std::string(64*1024,'s'); settings.WorldGenPresetId=std::string(64*1024,'p');
      settings.MinHeight=-48; settings.MaxHeight=127;
      cutum::WorldGenContext context(world,registry,settings);
      const char *seed_storage=context.Settings.SeedText.data();
      const char *preset_storage=context.Settings.WorldGenPresetId.data();
      auto pipeline=cutum::UProceduralWorldGenFactory::Create(std::move(context));
      const auto &actual=pipeline->GetProceduralSettings();
      FluidTest::Expect(actual.SeedText.data()==seed_storage && actual.WorldGenPresetId.data()==preset_storage,
          kTestName,"factory passes owned settings allocations to every pipeline unchanged");
      FluidTest::Expect(actual.Seed==settings.Seed && actual.Generator==settings.Generator &&
          actual.MinHeight==-48 && actual.MaxHeight==127 && actual.SeedText==settings.SeedText &&
          actual.WorldGenPresetId==settings.WorldGenPresetId && actual.WorldGenPackId==settings.WorldGenPackId &&
          &settings!=&actual && settings.SeedText.size()==64*1024,kTestName,
          "context ownership transfer preserves profile numeric values selectors and original caller");
    }
    std::cout << kTestName << ": factory context transfer PASS generators=" << cutum::UWorldGeneratorRegistry::Count() << std::endl;
    return 0;
  }
  if (argc > 1 && std::string(argv[1]) == "--population-memo-only")
  {
    const auto definitions=LoadPackDefinitions(repo_root/"resource_packs"/"minetest_default_16");
    cutum::UBlockRegistry registry(nullptr,definitions);
    cutum::UBlockWorld world;
    auto settings=MakeVegetationSettings();
    cutum::WorldGenContext ctx(world,registry,settings);
    cutum::ComposableWorldGenConfig config;
    config.UseBiomeSurface=true;
    cutum::UComposableWorldGenerator generator(std::move(ctx),config);
    generator.BeginChunkCoarseCache(-16,-16,24);
    const auto coarse_bytes=generator.GetCoarseMemoStorageBytes();
    const auto lake_bytes=generator.GetLakeMemoInlineBytes();
    FluidTest::Expect(coarse_bytes==20480 && lake_bytes==4096,kTestName,
        "coarse halo and inline lake memo have explicit bounded storage");
    for(int n=0;n<600;++n) {
      const int x=n*17-5000,z=n%37-20;
      const int expected=generator.SurfaceYAt(x,z);
      FluidTest::Expect(generator.LakeCenterSurfaceYAt(x,z)==expected &&
          generator.LakeCenterSurfaceYAt(x,z)==expected,kTestName,
          "lake memo preserves direct terrain height across negative coordinates and resets");
      FluidTest::Expect(generator.GetLakeMemoEntryCount()<=256,kTestName,"lake memo entry bound");
      if(n==255 || n==511)
        FluidTest::Expect(generator.GetLakeMemoEntryCount()==256,kTestName,"duplicate lookup at capacity retains memo");
      if(n==256 || n==512)
        FluidTest::Expect(generator.GetLakeMemoEntryCount()==1,kTestName,"new miss at capacity resets memo exactly");
    }
    generator.EndChunkCoarseCache();
    FluidTest::Expect(generator.GetLakeMemoEntryCount()==0,kTestName,"population end retires lake memo entries");
    const int direct=generator.SurfaceYAt(-15,-15);
    for(int cycle=0;cycle<32;++cycle) {
      generator.BeginChunkCoarseCache(-16,-16,24);
      FluidTest::Expect(generator.LakeCenterSurfaceYAt(-15,-15)==direct &&
          generator.GetCoarseMemoStorageBytes()==coarse_bytes,kTestName,"repeated coarse/lake scopes preserve value and capacity");
      generator.BeginChunkCoarseCache(-16,-16,8);
      FluidTest::Expect(generator.GetLakeMemoEntryCount()==0 && generator.GetCoarseMemoStorageBytes()==5120 &&
          generator.LakeCenterSurfaceYAt(-15,-15)==direct,kTestName,"restarting memo safely replaces old callbacks and scope");
      generator.EndChunkCoarseCache();
      FluidTest::Expect(generator.GetCoarseMemoStorageBytes()==0 && generator.GetLakeMemoEntryCount()==0,
          kTestName,"scope end releases coarse allocation and retires lake entries");
    }
    for(int pad:{-1,25,std::numeric_limits<int>::max()}) {
      generator.BeginChunkCoarseCache(-16,-16,pad);
      FluidTest::Expect(generator.GetCoarseMemoStorageBytes()==0 &&
          generator.LakeCenterSurfaceYAt(-15,-15)==direct,kTestName,"unsupported coarse memo geometry keeps exact direct path");
    }
    generator.BeginChunkCoarseCache(std::numeric_limits<int>::min(),0,24);
    FluidTest::Expect(generator.GetCoarseMemoStorageBytes()==0,kTestName,"coarse memo origin overflow is rejected before allocation");
    generator.EndChunkCoarseCache();
    std::cout << kTestName << ": population coarse/lake memo PASS" << std::endl;
    std::cout << "coarse_memo_max_bytes=" << coarse_bytes << " lake_inline_bytes=" << lake_bytes
        << " idle_coarse_bytes=0 idle_lake_entries=0" << std::endl;
    return 0;
  }
  if (argc > 1 && std::string(argv[1]) == "--population-scratch-only")
  {
    const auto definitions=LoadPackDefinitions(repo_root/"resource_packs"/"minetest_default_16");
    cutum::UBlockRegistry registry(nullptr,definitions);
    cutum::UPipelineChunkPopulator populator(registry,nullptr,"minetest_default_16");
    cutum::ChunkPopulateRequest request;
    request.chunkCoord={5,0,9}; request.settings=MakeVegetationSettings();
    request.settings.MinHeight=-48; request.settings.MaxHeight=127;
    request.settings.EnableTrees=false; request.settings.EnableGroundCover=false;
    request.settings.EnableDecoration=false; request.settings.EnableCaves=false;
    request.settings.SeedText=std::string(1024*1024,'s');
    request.settings.WorldGenPresetId=std::string(1024*1024,'r');
    const auto empty=[] {
      const auto state=cutum::ChunkPopulateDiagnostics::GetThreadScratchSnapshot();
      return state.resident_chunks==0 && !state.coarse_cache_active &&
          state.retained_seed_text_capacity_bytes==0 && state.retained_config_text_capacity_bytes==0;
    };
    auto baseline=populator.Populate(request);
    FluidTest::Expect(baseline.buffer.GetOwnedAllocationBytes()>0 && empty(),kTestName,
        "real successful population releases thread scratch cache and large seed display text");
    auto sampling_settings=request.settings;
    sampling_settings.WorldGenPackId=std::string(1024*1024,'p');
    cutum::UColumnSampleBuilder sample_builder(nullptr,nullptr,sampling_settings);
    const auto sample=sample_builder.Build(3,7);
    FluidTest::Expect(sample_builder.GetRetainedSettingsTextCapacityBytes()==0 &&
        request.settings.SeedText.size()==1024*1024 && request.settings.WorldGenPresetId.size()==1024*1024 &&
        request.settings.WorldGenPackId=="default" && sampling_settings.WorldGenPackId.size()==1024*1024 &&
        sample.SurfaceY==request.settings.SeaLevel,kTestName,
        "column sampler retains numeric settings without retaining or changing caller selection text");
    auto quota=std::make_shared<cutum::UBufferAllocationBudget>(128*1024);
    auto competitor=std::make_shared<cutum::BufferBudgetJob>();
    quota->Reserve(*competitor,32*1024);
    bool refused=false;
    try {
      cutum::UBufferBudgetScope scope({quota,std::make_shared<cutum::BufferBudgetJob>()});
      (void)populator.Populate(request);
    } catch(const cutum::UBufferBudgetExceeded &) { refused=true; }
    FluidTest::Expect(refused && quota->UsedBytes()==32*1024 && empty(),kTestName,
        "actual pipeline quota exception releases partial transfer and thread scratch");
    quota->Release(*competitor,32*1024);
    // Large text can now refuse at entry. Keep a separate short-text case
    // proving unwind after actual terrain work has begun.
    auto deep_request=request;
    deep_request.settings.SeedText.clear(); deep_request.settings.WorldGenPresetId="balanced";
    deep_request.settings=cutum::ProceduralSettings(deep_request.settings);
    unsigned terrain_checks=0;
    deep_request.shouldCancel=[&] {++terrain_checks; return false;};
    auto deep_quota=std::make_shared<cutum::UBufferAllocationBudget>(128*1024);
    auto deep_peer=std::make_shared<cutum::BufferBudgetJob>(); deep_quota->Reserve(*deep_peer,32*1024);
    bool deep_refused=false;
    {
      cutum::UBufferBudgetScope active({deep_quota,std::make_shared<cutum::BufferBudgetJob>()});
      try {(void)populator.Populate(deep_request);} catch(const cutum::UBufferBudgetExceeded &) {deep_refused=true;}
    }
    FluidTest::Expect(deep_refused && terrain_checks>0 && deep_quota->UsedBytes()==32*1024 && empty(),
        kTestName,"short-text pipeline refusal still unwinds after actual terrain starts");
    deep_quota->Release(*deep_peer,32*1024);
    std::cout << "deep_terrain_quota_refused=" << deep_refused << " terrain_checks=" << terrain_checks << std::endl;
    auto recovered=populator.Populate(request);
    cutum::UBlockWorld expected,actual;
    baseline.buffer.ApplyTo(expected); recovered.buffer.ApplyTo(actual);
    bool same=expected.CountNonAir()==actual.CountNonAir();
    expected.ForEachBlock([&](glm::ivec3 pos,cutum::BlockId id) {same=same && actual.GetBlock(pos)==id;});
    FluidTest::Expect(same && empty(),kTestName,"real population after quota failure matches baseline");
    unsigned cancel_checks=0;
    request.shouldCancel=[&] {return ++cancel_checks>=3;};
    auto cancelled=populator.Populate(request);
    FluidTest::Expect(cancelled.discarded && cancelled.buffer.GetOwnedAllocationBytes()==0 && empty(),
        kTestName,"partial population cancellation releases scratch and transfer");
    std::cout << kTestName << ": population scratch success/quota/recovery/cancel PASS" << std::endl;
    std::cout << "column_sample_builder_bytes=" << sizeof(cutum::UColumnSampleBuilder)
        << " retained_seed_text_bytes=" << cutum::ChunkPopulateDiagnostics::GetThreadScratchSnapshot().retained_seed_text_capacity_bytes
        << " retained_config_text_bytes=" << cutum::ChunkPopulateDiagnostics::GetThreadScratchSnapshot().retained_config_text_capacity_bytes << std::endl;
    return 0;
  }
  if (argc > 1 && std::string(argv[1]) == "--buffer-boundary-only")
  {
    const auto definitions=LoadPackDefinitions(repo_root/"resource_packs"/"minetest_default_16");
    cutum::UBlockRegistry registry(nullptr,definitions);
    TestCrossChunkTreeConflictStable(registry);
    TestFloraRanksCoverDecorationAndStructures(registry);
    std::cout << kTestName << ": buffer boundary ownership check OK" << std::endl;
    return 0;
  }
  if (argc > 1 && std::string(argv[1]) == "--biome-profile-only")
  {
    TestDataDefinedBiomeAffectsSampling();
    std::cout << kTestName << ": biome profile check OK" << std::endl;
    return 0;
  }
  TestConfiguredGroundCoverEcology(repo_root);
  TestConfiguredTreeElevationBands();
  TestConfiguredNaturalFeatureElevationBands();

  const fs::path pack_root =
      repo_root / "resource_packs" / "minetest_default_16";
  const auto definitions = LoadPackDefinitions(pack_root);
  cutum::UBlockRegistry registry(nullptr, definitions);
  constexpr const char *kOwnerPack = "minetest_default_16";
  TestOceanFallbackSpawnStaysAboveSea(registry);
  if (argc > 1 && std::string(argv[1]) == "--spatial-flora-density-only")
  {
    TestSpatialFloraCandidateDensity(registry);
    std::cout << kTestName << ": spatial flora density check OK" << std::endl;
    return 0;
  }

  cutum::UObjectLibrary objects;
  cutum::ResourcePackManifest manifest =
      *cutum::UResourcePack::LoadManifest(pack_root);
  objects.LoadMerged(repo_root / "objects", {manifest}, registry);
  objects.RebindBlockIds(registry);
  TestBuiltinTreeTemplateLogsConnectToTrunk(objects);
  TestStaticPlantPrefabsPlaceOnFlatGround(registry, objects);
  if (argc > 1 && std::string(argv[1]) == "--static-plant-prefabs-only")
  {
    std::cout << kTestName << ": static plant prefab check OK" << std::endl;
    return 0;
  }
  if (argc > 1 && std::string(argv[1]) == "--procedural-vegetation-only")
  {
    TestProceduralVegetationStrategies(registry);
    TestVanillaStyleTreesProfile3(registry);
    std::cout << kTestName << ": procedural vegetation check OK" << std::endl;
    return 0;
  }
  FluidTest::Expect(objects.Get("tree_pine") != nullptr, kTestName,
                    "tree_pine available after LoadMerged");

  cutum::UPipelineChunkPopulator populator(registry, &objects, kOwnerPack);
  if (argc > 1 && std::string(argv[1]) == "--chunk-order-only")
  {
    TestChunkPopulatorPlacesTrees(populator, registry, objects);
    std::cout << kTestName << ": chunk order check complete" << std::endl;
    return 0;
  }
  TestWaterColumnFloorEarlyExit(registry);
  TestSpatialFloraCandidateDensity(registry);
  TestRegionalFloraDensityClustering(registry);
  TestChunkPopulatorPlacesTrees(populator, registry, objects);
  TestProceduralVegetationStrategies(registry);
  FluidTest::Expect(
      cutum::UObjectFeaturePlacementStrategyRegistry::Find(
          cutum::ObjectFeatureRuleMode::Object) != nullptr &&
          cutum::UObjectFeaturePlacementStrategyRegistry::Find(
              cutum::ObjectFeatureRuleMode::ScatterBlocks) != nullptr &&
          cutum::UObjectFeaturePlacementStrategyRegistry::Find(
              cutum::ObjectFeatureRuleMode::ProceduralTree) != nullptr,
      kTestName, "built-in feature placement strategies are registered");
  TestScatterSurfaceConstraint(registry);
  TestCrossChunkTreeConflictStable(registry);
  TestFloraRanksCoverDecorationAndStructures(registry);
  TestCaveFeaturePlacementStrategies(registry);
  TestDataDefinedBiomeAffectsSampling();
  TestImageBiomeSourceStrategy();

  std::cout << kTestName << ": OK" << std::endl;
  return 0;
}
