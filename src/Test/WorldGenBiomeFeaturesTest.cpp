#include "Test/FluidTestHelpers.h"

#include "WorldGen/Core/WorldGenPack.h"
#include "WorldGen/Sampling/BiomeRegistry.h"

#include <chrono>
#include <cmath>
#include <filesystem>
#include <fstream>
#include <iostream>

namespace fs = std::filesystem;

namespace
{

constexpr const char *kTestName = "worldgen_biome_features_test";

static const char *kBiomeIds[] = {
    "plains",   "forest",   "desert",    "hills",      "tundra",
    "savanna",  "foothills", "scrubland", "cold_steppe"};

static fs::path FindRepoRoot()
{
  fs::path dir = fs::current_path();
  for (int depth = 0; depth < 8; ++depth)
  {
    if (fs::exists(dir / "content" / "worldgen_packs" / "default" / "pack.json"))
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

static void TestAllBiomesHaveVegetationWeights()
{
  for (const char *biomeId : kBiomeIds)
  {
    const cutum::BiomePackDefinition *def =
        cutum::UWorldGenPack::BiomeDefinitionFor(biomeId);
    FluidTest::Expect(def != nullptr, kTestName, "biome definition missing");
    bool hasVegetationPool = false;
    for (const auto &[key, weight] : def->FeatureWeights)
    {
      (void)key;
      if (weight > 0.0f)
      {
        hasVegetationPool = true;
        break;
      }
    }
    FluidTest::Expect(hasVegetationPool, kTestName,
                      "biome has positive feature weights");
  }
}

static void TestForestTreeWeightApplies()
{
  const float mul =
      cutum::UWorldGenPack::FeatureWeightMultiplier("forest", "tree_pine");
  FluidTest::Expect(mul > 1.0f, kTestName,
                    "forest tree_pine weight multiplier applied");
}

static void TestHillsHasGroundCoverWeight()
{
  const float mul =
      cutum::UWorldGenPack::FeatureWeightMultiplier("hills", "bush_common");
  FluidTest::Expect(mul > 0.5f, kTestName, "hills bush_common weight applied");
}

static void TestPackScopedBiomeHandlesAndFailClosedValidation()
{
  const fs::path root = fs::temp_directory_path() /
                        ("cubatarium_biome_registry_" +
                         std::to_string(std::chrono::steady_clock::now()
                                            .time_since_epoch()
                                            .count()));
  const fs::path biomes = root / "biomes";
  fs::create_directories(biomes);
  {
    std::ofstream pack(root / "pack.json");
    pack << R"({"id":"registry_test","biome_mode":"procedural"})";
    std::ofstream biome(biomes / "crystal_marsh.json");
    biome << R"({
      "schema_version": 2,
      "id": "crystal_marsh",
      "domain": "surface",
      "climate": {"temperature": [0.3, 0.8], "moisture": [0.6, 1.0]},
      "height": {"base_offset": 2, "amplitude_mul": 1.1},
      "palette": {"surface": "grass", "subsurface": "dirt"}
    })";
    std::ofstream caveBiome(biomes / "crystal_caves.json");
    caveBiome << R"({
      "schema_version": 2,
      "id": "crystal_caves",
      "domain": "cave",
      "climate": {"temperature": [0.0, 1.0], "moisture": [0.0, 1.0], "weight": 100.0},
      "features": {"cave_features": {"moss_patch": 0.5}}
    })";
  }

  FluidTest::Expect(cutum::UWorldGenPack::LoadFromDirectory(root.string()),
                    kTestName, "valid custom schema-v2 biome pack loads");
  const auto accepted = cutum::UWorldGenPack::GetSnapshot();
  FluidTest::Expect(accepted && accepted->BiomeRegistry, kTestName,
                    "compiled biome registry is included in pack snapshot");
  const cutum::BiomeHandle custom = accepted->BiomeRegistry->Find("crystal_marsh");
  FluidTest::Expect(custom.IsValid(), kTestName,
                    "JSON-only biome receives a pack-scoped handle");
  FluidTest::Expect(accepted->BiomeRegistry->Key(custom) &&
                        *accepted->BiomeRegistry->Key(custom) == "crystal_marsh",
                    kTestName, "handle resolves to its stable biome key");
  FluidTest::Expect(cutum::BiomeIdFromString("unregistered_biome") ==
                        cutum::BiomeId::Invalid,
                    kTestName, "unknown biome is not silently mapped to plains");
  FluidTest::Expect(std::abs(cutum::UWorldGenPack::FeatureWeightMultiplier(
                                 "crystal_caves", "moss_patch") -
                             0.5f) < 0.001f,
                    kTestName,
                    "cave biome feature weights are retained from JSON");

  {
    std::ofstream invalid(biomes / "bad_biome.json");
    invalid << R"({
      "schema_version": 2,
      "id": "bad_biome",
      "domain": "space",
      "climate": {"temperature": [0.0, 1.0]}
    })";
  }
  FluidTest::Expect(!cutum::UWorldGenPack::LoadFromDirectory(root.string()),
                    kTestName, "invalid biome domain rejects the whole pack");
  FluidTest::Expect(cutum::UWorldGenPack::GetSnapshot() == accepted, kTestName,
                    "failed reload keeps the previously published snapshot");
  std::error_code ec;
  fs::remove_all(root, ec);
}

} // namespace

int main()
{
  const fs::path repo_root = FindRepoRoot();
  std::error_code ec;
  fs::current_path(repo_root, ec);
  FluidTest::Expect(cutum::UWorldGenPack::LoadPackId("default"), kTestName,
                    "worldgen pack load failed");

  TestAllBiomesHaveVegetationWeights();
  TestForestTreeWeightApplies();
  TestHillsHasGroundCoverWeight();
  TestPackScopedBiomeHandlesAndFailClosedValidation();

  std::cout << kTestName << ": OK" << std::endl;
  return 0;
}
