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
#include "WorldGen/Features/ObjectFeatureConfig.h"
#include "WorldGen/Features/ObjectFeaturePlacer.h"
#include "WorldGen/Sampling/BiomeSampler.h"
#include "WorldGen/Sampling/BiomeSource.h"

#include <chrono>
#include <algorithm>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <memory>
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

static void TestConfiguredGroundCoverEcology()
{
  bool hasMeadowGrass = false;
  bool hasFern = false;
  bool hasWaterEdgePapyrus = false;
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
    }
  }
  FluidTest::Expect(hasMeadowGrass, kTestName,
                    "meadow grass uses an ecological placement profile");
  FluidTest::Expect(hasFern, kTestName,
                    "fern placement is limited to damp conditions");
  FluidTest::Expect(hasWaterEdgePapyrus, kTestName,
                    "papyrus profile uses near-water and moisture constraints");
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
  for (int cx = -2; cx <= 2; ++cx)
  {
    for (int cz = -2; cz <= 2; ++cz)
    {
      chunkOrder.emplace_back(cx, 0, cz);
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
      request.columnOrigin = glm::ivec2(8, -8);
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

  const int tree_logs = CountBlocksNamed(forwardWorld, registry, "tree_log");
  const int tree_leaves = CountBlocksNamed(forwardWorld, registry, "tree_leaves");
  const int species_logs = CountBlocksNamed(forwardWorld, registry, "mtg_tree");
  const int species_leaves = CountBlocksNamed(forwardWorld, registry, "mtg_leaves");
  FluidTest::Expect(tree_logs > 0 || tree_leaves > 0 || species_logs > 0 ||
                        species_leaves > 0,
                    kTestName, "chunk populator placed tree blocks");
  FluidTest::Expect(species_logs > 0 || species_leaves > 0, kTestName,
                    "procedural tree strategy emits species blocks");
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
      "height": {"base_offset": 8.0, "amplitude_mul": 1.0},
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

int main()
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
  FluidTest::Expect(cutum::UWorldGenPack::LoadPackId("default"), kTestName,
                    "worldgen pack load failed");
  FluidTest::Expect(cutum::UWorldGenPack::AquifersConfig().Loaded, kTestName,
                    "aquifer configuration loads from the active worldgen pack");
  TestConfiguredGroundCoverEcology();

  const fs::path pack_root = repo_root / "resource_packs" / "minetest_default_16";
  const auto definitions = LoadPackDefinitions(pack_root);
  cutum::UBlockRegistry registry(nullptr, definitions);
  constexpr const char *kOwnerPack = "minetest_default_16";

  cutum::UObjectLibrary objects;
  cutum::ResourcePackManifest manifest =
      *cutum::UResourcePack::LoadManifest(pack_root);
  objects.LoadMerged(repo_root / "objects", {manifest}, registry);
  objects.RebindBlockIds(registry);
  FluidTest::Expect(objects.Get("tree_pine") != nullptr, kTestName,
                    "tree_pine available after LoadMerged");

  cutum::UPipelineChunkPopulator populator(registry, &objects, kOwnerPack);
  TestChunkPopulatorPlacesTrees(populator, registry, objects);
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
