#include "Test/FluidTestHelpers.h"

#include "Blocks/BlockRegistry.h"
#include "ResourcePacks/ResourcePack.h"
#include "World/Chunks/ChunkBuffer.h"
#include "World/Chunks/ChunkGenerationToken.h"
#include "World/Core/BlockWorld.h"
#include "World/Objects/ObjectLibrary.h"
#include "WorldGen/Core/IUChunkPopulator.h"
#include "WorldGen/Core/ProceduralSettings.h"
#include "WorldGen/Core/WorldGenPack.h"
#include "WorldGen/Core/WorldGenRefs.h"
#include "WorldGen/Core/WorldGenSets.h"
#include "WorldGen/Features/ObjectFeatureConfig.h"
#include "WorldGen/Features/ObjectFeaturePlacer.h"
#include "WorldGen/Sampling/BiomeSampler.h"
#include "WorldGen/Sampling/BiomeSource.h"

#include <algorithm>
#include <array>
#include <chrono>
#include <cmath>
#include <filesystem>
#include <fstream>
#include <iostream>
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

constexpr const char *kTestName = "worldgen_vegetation_placement_integration_test";
constexpr uint32_t kSeed = 410021u;

static fs::path FindRepoRoot()
{
  fs::path dir = fs::current_path();
  for (int depth = 0; depth < 8; ++depth)
  {
    if (fs::exists(dir / "content" / "worldgen_refs.json") &&
        fs::exists(dir / "resource_packs" / "minetest_default_16" / "pack.json"))
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
  FluidTest::Expect(manifest.has_value(), kTestName, "pack manifest load failed");
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
      hasFern = rule.Ecology.HasMoistureRange &&
                rule.Ecology.MoistureMin >= 0.6f;
    }
    else if (rule.Scatter.BlockName == "mtg_papyrus")
    {
      hasWaterEdgePapyrus =
          rule.Surface.Kind == cutum::SurfaceConstraintKind::NearWater &&
          rule.Surface.NearWaterRadius == 7 &&
          rule.Ecology.HasMoistureRange;
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
  FluidTest::Expect(absolute.AllowsElevation(51, 63) &&
                        !absolute.AllowsElevation(60, 63),
                    kTestName,
                    "legacy absolute ecology ranges keep their original meaning");

  const fs::path legacyConfigPath =
      fs::temp_directory_path() / "cubatarium_legacy_feature_elevation.json";
  {
    std::ofstream legacyConfig(legacyConfigPath, std::ios::trunc);
    legacyConfig << R"({"ground_cover":[{"mode":"scatter_blocks","block":"legacy_test_plant","biomes":["plains"],"ecology":{"elevation":[45,54]}}]})";
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
            rule.Ecology.ElevationRef == cutum::FeatureEcologyProfile::
                                            ElevationReference::AbsoluteY &&
            rule.Ecology.MinElevation == 45 &&
            rule.Ecology.MaxElevation == 54 &&
            rule.Ecology.AllowsElevation(51, 63) &&
            !rule.Ecology.AllowsElevation(60, 63);
      }
    }
  }
  cutum::UObjectFeatureConfigStorage::LoadFromFile(
      repo_root / "content" / "object_features.json");
  std::error_code removeError;
  fs::remove(legacyConfigPath, removeError);
  FluidTest::Expect(legacyArrayLoaded, kTestName,
                    "legacy ecology elevation arrays remain absolute Y ranges");
}

static void TestChunkPopulatorPlacesTrees(cutum::UPipelineChunkPopulator &populator,
                                          cutum::UBlockRegistry &registry,
                                          cutum::UObjectLibrary &objects)
{
  FluidTest::Expect(objects.Get("tree_pine") != nullptr, kTestName,
                    "tree_pine prefab loaded");
  FluidTest::Expect(
      cutum::UObjectFeatureConfigStorage::Get().Vegetation.size() > 0, kTestName,
      "vegetation rules loaded");

  const cutum::ProceduralSettings settings = MakeVegetationSettings();
  std::vector<glm::ivec3> chunkOrder;
  // Seed 410021's generated spawn is in chunk (2, 1); sample around that
  // location because its origin window at (0, 0) is water and has no trees.
  constexpr int kSpawnChunkX = 2;
  constexpr int kSpawnChunkZ = 1;
  for (int cx = -2; cx <= 2; ++cx)
  {
    for (int chunk_y = 2; chunk_y <= 9; ++chunk_y)
    {
      for (int cz = -2; cz <= 2; ++cz)
      {
        // Vegetation grows around Y=40..140. Populate those vertical sections
        // so worldgen-owned tree voxels are applied to their owning chunks.
        chunkOrder.emplace_back(kSpawnChunkX + cx, chunk_y,
                                kSpawnChunkZ + cz);
      }
    }
  }
  cutum::ChunkGenerationToken token;
  const auto populate = [&](cutum::UBlockWorld &world,
                            const std::vector<glm::ivec3> &order)
  {
    world.SetFluidDefinitions(registry.GetDefinitions());
    for (const glm::ivec3 &chunkCoord : order)
    {
      cutum::ChunkPopulateRequest request;
      request.chunkCoord = chunkCoord;
      request.token = token;
      request.settings = settings;
      request.objects = &objects;
      request.columnOrigin = glm::ivec2(
          kSpawnChunkX * cutum::CHUNK_SIZE + 8,
          kSpawnChunkZ * cutum::CHUNK_SIZE + 8);
      request.hasColumnOrigin = true;
      const cutum::ChunkPopulateResult result = populator.Populate(request);
      result.buffer.ApplyTo(world);
    }
  };

  const auto snapshot = [](cutum::UBlockWorld &world)
  {
    std::vector<std::tuple<int, int, int, cutum::BlockId>> blocks;
    world.ForEachBlock(
        [&blocks](glm::ivec3 pos, cutum::BlockId id)
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
  FluidTest::Expect(forwardBlocks == reverseBlocks, kTestName,
                    "generated chunk area is independent of chunk apply order");

  static constexpr std::array<const char *, 16> treeBlockNames = {
      "tree_log",          "mtg_tree",       "mtg_pine_tree",
      "mtg_aspen_tree",    "mtg_acacia_tree", "mtg_jungletree",
      "tree_leaves",       "mtg_leaves",     "leaves_spruce",
      "leaves_spruce_light", "leaves_jungle",  "leaves_jungle_light",
      "mtg_aspen_leaves",  "mtg_acacia_leaves", "mtg_jungleleaves",
      "mtg_pine_needles"};
  int tree_blocks = 0;
  for (const char *name : treeBlockNames)
  {
    tree_blocks += CountBlocksNamed(forwardWorld, registry, name);
  }
  FluidTest::Expect(tree_blocks > 0, kTestName,
                    "chunk populator placed blocks from a tree species");
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
  FluidTest::Expect(placedGrass >= 55, kTestName,
                    "full-cell jitter preserves authored flora candidate density");
}

static void TestProceduralVegetationStrategies(cutum::UBlockRegistry &registry)
{
  cutum::ProceduralSettings settings = MakeVegetationSettings();
  settings.SeaLevel = 48;
  settings.MaxHeight = 128;
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
  tree.Tree.BranchCount = 8;
  tree.Tree.CrownLobes = 0;
  tree.Tree.RootChance = 0.0f;
  tree.Tree.CrownIrregularity = 0.28f;
  tree.Tree.TrunkBlock = "mtg_tree";
  tree.Tree.LeavesBlock = "mtg_leaves";
  const auto treeStrategy = cutum::ObjectFeaturePlacementStrategyRegistry::Find(
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
          tree_extent = std::max(
              tree_extent,
              std::max(std::abs(position.x), std::abs(position.z)));
        }
      });
  FluidTest::Expect(tree_extent >= 5, kTestName,
                    "wide profile produces outward branch and leaf silhouettes");

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
  const cutum::BlockId pine_trunk =
      registry.GetIdByTypeName("mtg_pine_tree");
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

  cutum::ObjectFeatureRule shrub;
  shrub.Mode = cutum::ObjectFeatureRuleMode::ProceduralShrub;
  shrub.Shrub.HeightMin = 2;
  shrub.Shrub.HeightMax = 3;
  shrub.Shrub.RadiusX = 2;
  shrub.Shrub.RadiusZ = 3;
  shrub.Shrub.LobeCount = 4;
  shrub.Shrub.StemBlock = "mtg_bush_stem";
  shrub.Shrub.LeavesBlock = "mtg_bush_leaves";
  const auto shrubStrategy = cutum::ObjectFeaturePlacementStrategyRegistry::Find(
      cutum::ObjectFeatureRuleMode::ProceduralShrub);
  FluidTest::Expect(shrubStrategy != nullptr, kTestName,
                    "procedural shrub strategy is registered");
  FluidTest::Expect(shrubStrategy->Place(ctx, 12, 0, 51, shrub), kTestName,
                    "procedural shrub strategy places its profile");
  FluidTest::Expect(CountBlocksNamed(world, registry, "mtg_bush_leaves") > 0,
                    kTestName, "procedural shrub writes a leafy silhouette");
}

static void TestWorldGenSetsSchemaV2Migration()
{
  using json = nlohmann::json;
  json legacy;
  legacy["schema_version"] = 1;
  legacy["objects"]["vegetation"]["default"]["entries"] = json::array(
      {{{"object", "tree_procedural_oak"},
        {"weight", 5},
        {"spacing", 39},
        {"seed_offset", 4012},
        {"biomes", {"forest", "plains"}}}});
  cutum::WorldGenSets sets;
  std::string error;
  FluidTest::Expect(cutum::ParseWorldGenSets(legacy, sets, error), kTestName,
                    "legacy worldgen_sets schema loads");
  FluidTest::Expect(sets.SchemaVersion == 1, kTestName,
                    "legacy schema version is preserved before migration");

  const cutum::ObjectFeatureConfig &authored =
      cutum::UObjectFeatureConfigStorage::Get();
  cutum::UpgradeLegacyWorldGenSetMetadata(sets, authored, 2);
  FluidTest::Expect(sets.SchemaVersion == 2, kTestName,
                    "profile-2 world upgrades worldgen_sets schema");
  const cutum::ObjectFeatureConfig resolved =
      cutum::ResolveObjectFeatures(sets, authored, 2);
  const auto tree = std::find_if(
      resolved.Vegetation.begin(), resolved.Vegetation.end(),
      [](const cutum::ObjectFeatureRule &rule)
      { return rule.ObjectName == "tree_procedural_oak"; });
  FluidTest::Expect(tree != resolved.Vegetation.end() &&
                        tree->Mode ==
                            cutum::ObjectFeatureRuleMode::ProceduralTree &&
                        tree->Tree.CrownLobes == 4 && tree->Weight == 5 &&
                        tree->Spacing == 39,
                    kTestName,
                    "migration restores authored mode and profile, preserving saved frequency");

  json saved;
  cutum::WriteWorldGenSets(saved, sets);
  FluidTest::Expect(saved["worldgen_sets"]["schema_version"] == 2 &&
                        saved["worldgen_sets"]["objects"]["vegetation"]
                                ["default"]["entries"][0]["mode"] ==
                            "procedural_tree" &&
                        saved["worldgen_sets"]["objects"]["vegetation"]
                                ["default"]["entries"][0].contains(
                                    "tree_profile"),
                    kTestName,
                    "schema-v2 save retains procedural mode and tree profile");
}

static void TestDataDefinedBiomeAffectsSampling()
{
  const fs::path root = fs::temp_directory_path() /
                        ("cubatarium_worldgen_biome_" +
                         std::to_string(std::chrono::steady_clock::now()
                                            .time_since_epoch()
                                            .count()));
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
  FluidTest::Expect(handle.IsValid(), kTestName,
                    "data-defined biome is registered without a C++ classifier");
  cutum::WorldGenTuning tuning;
  cutum::UBiomeSampler sampler(kSeed, tuning, 2);
  FluidTest::Expect(sampler.At3DKey(17, 24, -31, 80, 48, 128) ==
                        "crystal_caves",
                    kTestName,
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
  for (const cutum::BiomeId builtin : {
           cutum::BiomeId::Plains, cutum::BiomeId::Forest,
           cutum::BiomeId::Desert, cutum::BiomeId::Hills,
           cutum::BiomeId::Tundra, cutum::BiomeId::Savanna,
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
  FluidTest::Expect(std::all_of(
                        weights.entries.begin(), weights.entries.end(),
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
              << weights.Weight(handle)
              << " base_offset="
              << (definition ? definition->Height.baseOffsetBlocks : -999.0f)
              << " unified=" << settings.Tuning.useUnifiedHeightField
              << " sea=" << settings.SeaLevel
              << " min=" << settings.MinHeight
              << " max=" << settings.MaxHeight
              << " with=" << with_custom
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
                    kTestName, "image source routes surface 3D keys to image map");
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
  FluidTest::Expect(stone != cutum::BLOCK_AIR &&
                        mossVisual != cutum::BLOCK_AIR,
                    kTestName, "cave strategy test blocks resolve from the pack");
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

  const auto biomeAt = [](int, int, int, int) {
    return cutum::BiomeId::LushCaves;
  };
  const auto biomeKeyAt = [](int, int, int, int) {
    return cutum::BiomeKey("lush_caves");
  };
  FluidTest::Expect(cutum::TryPlaceCaveFeatures(ctx, 0, 0, 20, biomeAt,
                                                biomeKeyAt),
                    kTestName, "registered cave strategy places a moss floor");
  const cutum::BlockId placedBlock = world.GetBlock(glm::ivec3(0, 8, 0));
  const std::string placedMessage =
      "cave feature writes its resolved block through sink (expected=" +
      std::to_string(static_cast<int>(mossVisual)) + ", actual=" +
      std::to_string(static_cast<int>(placedBlock)) + ")";
  FluidTest::Expect(
      placedBlock == mossVisual, kTestName, placedMessage.c_str());
  FluidTest::Expect(world.GetBlock(glm::ivec3(0, 5, 0)) == cutum::BLOCK_AIR,
                    kTestName,
                    "unresolved cave rock aliases do not treat air as a floor");
  FluidTest::Expect(
      cutum::CaveFeaturePlacementStrategyRegistry::Find(
          cutum::CaveFeatureKind::MossPatch) != nullptr &&
          cutum::CaveFeaturePlacementStrategyRegistry::Find(
              cutum::CaveFeatureKind::Stalagmite) != nullptr &&
          cutum::CaveFeaturePlacementStrategyRegistry::Find(
              cutum::CaveFeatureKind::AmethystCluster) != nullptr,
      kTestName, "built-in cave feature strategies are registered");
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
  const auto strategy = cutum::ObjectFeaturePlacementStrategyRegistry::Find(
      cutum::ObjectFeatureRuleMode::ScatterBlocks);
  FluidTest::Expect(strategy != nullptr, kTestName,
                    "scatter placement strategy is registered");
  FluidTest::Expect(strategy->Place(ctx, 0, 0, 51, rule), kTestName,
                    "near-water scatter placement is accepted");
  FluidTest::Expect(world.GetBlock(glm::ivec3(0, 52, 0)) == papyrus,
                    kTestName, "near-water scatter writes the plant block");
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
  const std::vector<std::pair<cutum::BlockId, int>> ranks = {
      {trunk, 3}, {leaves, 1}};
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

} // namespace

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
  TestWorldGenSetsSchemaV2Migration();
  FluidTest::Expect(cutum::UWorldGenPack::LoadPackId("default"), kTestName,
                    "worldgen pack load failed");
  FluidTest::Expect(cutum::UWorldGenPack::AquifersConfig().Loaded, kTestName,
                    "aquifer configuration loads from the active worldgen pack");
  if (argc > 1 && std::string(argv[1]) == "--biome-profile-only")
  {
    TestDataDefinedBiomeAffectsSampling();
    std::cout << kTestName << ": biome profile check OK" << std::endl;
    return 0;
  }
  TestConfiguredGroundCoverEcology(repo_root);

  const fs::path pack_root = repo_root / "resource_packs" / "minetest_default_16";
  const auto definitions = LoadPackDefinitions(pack_root);
  cutum::UBlockRegistry registry(nullptr, definitions);
  constexpr const char *kOwnerPack = "minetest_default_16";
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
  if (argc > 1 && std::string(argv[1]) == "--procedural-vegetation-only")
  {
    TestProceduralVegetationStrategies(registry);
    std::cout << kTestName << ": procedural vegetation check OK" << std::endl;
    return 0;
  }
  FluidTest::Expect(objects.Get("tree_pine") != nullptr, kTestName,
                    "tree_pine available after LoadMerged");

  cutum::UPipelineChunkPopulator populator(registry, &objects, kOwnerPack);
  TestSpatialFloraCandidateDensity(registry);
  TestChunkPopulatorPlacesTrees(populator, registry, objects);
  TestProceduralVegetationStrategies(registry);
  FluidTest::Expect(
      cutum::ObjectFeaturePlacementStrategyRegistry::Find(
          cutum::ObjectFeatureRuleMode::Object) != nullptr &&
          cutum::ObjectFeaturePlacementStrategyRegistry::Find(
              cutum::ObjectFeatureRuleMode::ScatterBlocks) != nullptr &&
          cutum::ObjectFeaturePlacementStrategyRegistry::Find(
              cutum::ObjectFeatureRuleMode::ProceduralTree) != nullptr,
      kTestName, "built-in feature placement strategies are registered");
  TestScatterSurfaceConstraint(registry);
  TestCrossChunkTreeConflictStable(registry);
  TestCaveFeaturePlacementStrategies(registry);
  TestDataDefinedBiomeAffectsSampling();
  TestImageBiomeSourceStrategy();

  std::cout << kTestName << ": OK" << std::endl;
  return 0;
}
