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
#include "WorldGen/Sampling/BiomeSampler.h"
#include "WorldGen/Sampling/BiomeSource.h"

#include <chrono>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <memory>
#include <unordered_map>

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
  cutum::UBlockWorld world;
  world.SetFluidDefinitions(registry.GetDefinitions());
  cutum::ChunkGenerationToken token;
  for (int cx = -2; cx <= 2; ++cx)
  {
    for (int cz = -2; cz <= 2; ++cz)
    {
      cutum::ChunkPopulateRequest request;
      request.chunkCoord = glm::ivec3(cx, 0, cz);
      request.token = token;
      request.settings = settings;
      request.objects = &objects;
      request.columnOrigin = glm::ivec2(8, -8);
      request.hasColumnOrigin = true;
      const cutum::ChunkPopulateResult result = populator.Populate(request);
      result.buffer.ApplyTo(world);
    }
  }

  const int tree_logs = CountBlocksNamed(world, registry, "tree_log");
  const int tree_leaves = CountBlocksNamed(world, registry, "tree_leaves");
  const int species_logs = CountBlocksNamed(world, registry, "mtg_tree");
  const int species_leaves = CountBlocksNamed(world, registry, "mtg_leaves");
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
      if (weights.extensions.Weight(handle) > 0.8f)
      {
        sampled = true;
        break;
      }
    }
  }
  FluidTest::Expect(sampled, kTestName,
                    "climate profile participates in dynamic biome blending");
  FluidTest::Expect(cutum::BiomeKeyForHandle(
                        cutum::DominantBiomeHandle(weights)) == "azure_meadow",
                    kTestName, "dynamic biome can dominate the sampled blend");

  const int with_custom = cutum::RefineSurfaceYWithBiomes(
      0, 0, 64, settings, kSeed, settings.Tuning, {}, &weights);
  cutum::BiomeWeightSet legacy_only = weights;
  legacy_only.extensions.entries.clear();
  float legacy_total = 0.0f;
  for (float weight : legacy_only.weights)
  {
    legacy_total += weight;
  }
  for (float &weight : legacy_only.weights)
  {
    weight /= legacy_total;
  }
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
  TestDataDefinedBiomeAffectsSampling();
  TestImageBiomeSourceStrategy();

  std::cout << kTestName << ": OK" << std::endl;
  return 0;
}
