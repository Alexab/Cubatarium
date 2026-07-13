#include "Test/FluidTestHelpers.h"

#include "WorldGen/Core/WorldGenPack.h"

#include <filesystem>
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

  std::cout << kTestName << ": OK" << std::endl;
  return 0;
}
