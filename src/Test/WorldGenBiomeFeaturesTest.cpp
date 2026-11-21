#include "Test/FluidTestHelpers.h"

#include "WorldGen/Core/WorldGenPack.h"
#include "WorldGen/Sampling/BiomeRegistry.h"
#include "Core/Memory/BufferAllocationBudget.h"

#include <chrono>
#include <cmath>
#include <filesystem>
#include <fstream>
#include <iostream>

namespace fs = std::filesystem;

namespace
{

constexpr const char *kTestName = "worldgen_biome_features_test";

static const char *kBiomeIds[] = {"plains",    "forest",    "desert",
                                  "hills",     "tundra",    "savanna",
                                  "foothills", "scrubland", "cold_steppe"};

static fs::path FindRepoRoot()
{
  fs::path dir = fs::current_path();
  for (int depth = 0; depth < 8; ++depth)
  {
    if (fs::exists(dir / "content" / "worldgen_packs" / "default" /
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

static void TestPackSelectorBudget()
{
  size_t refused=0;
  const std::string long_id(64*1024,'p');
  for(const std::string &id:{std::string("default"),long_id}) {
    auto budget=std::make_shared<cutum::UBufferAllocationBudget>(id.size()>1024 ? 1024 : 1);
    auto peer=std::make_shared<cutum::BufferBudgetJob>(); budget->Reserve(*peer,1);
    const auto before=cutum::UWorldGenPack::GetSnapshot();
    bool denied=false,filesystem_reached=false;
    {
      cutum::UBufferBudgetScope active({budget,std::make_shared<cutum::BufferBudgetJob>()});
      try {(void)cutum::UWorldGenPack::LoadPackId(id);}
      catch(const cutum::UBufferBudgetExceeded &) {denied=true;}
      catch(const fs::filesystem_error &) {filesystem_reached=true;}
    }
    std::cout << "pack_selector_bytes=" << id.size() << " refused=" << denied
        << " filesystem_reached=" << filesystem_reached << " remaining=" << budget->UsedBytes() << std::endl;
    FluidTest::Expect(budget->UsedBytes()==1 && cutum::UWorldGenPack::GetSnapshot()==before,
        kTestName,"selector refusal preserves active catalog and competing credit");
    budget->Release(*peer,1); refused+=denied;
  }
  FluidTest::Expect(refused==2,kTestName,"pack selector path reserves before construction and filesystem work");
  auto budget=std::make_shared<cutum::UBufferAllocationBudget>(1024);
  auto peer=std::make_shared<cutum::BufferBudgetJob>(); budget->Reserve(*peer,1);
  {
    cutum::UBufferBudgetScope active({budget,std::make_shared<cutum::BufferBudgetJob>()});
    const auto before=cutum::UWorldGenPack::GetSnapshot();
    FluidTest::Expect(cutum::UWorldGenPack::LoadPackId("") && cutum::UWorldGenPack::LoadPackId("default") &&
        cutum::UWorldGenPack::GetSnapshot()==before,kTestName,"empty and explicit default selector preserve warm catalog identity");
  }
  FluidTest::Expect(budget->UsedBytes()==1,kTestName,"warm selector scratch fully retires");
  budget->Release(*peer,1);
  std::cout << "pack_selector_refusals=" << refused << " peak=" << budget->PeakBytes() << " final=" << budget->UsedBytes() << std::endl;
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
  const fs::path root =
      fs::temp_directory_path() /
      ("cubatarium_biome_registry_" +
       std::to_string(
           std::chrono::steady_clock::now().time_since_epoch().count()));
  const fs::path biomes = root / "biomes";
  fs::create_directories(biomes);
  {
    std::ofstream pack(root / "pack.json");
    pack << R"({"id":"registry_test","biome_mode":"procedural"})";
    std::ofstream biome(biomes / "crystal_marsh.json");
    biome << R"({
      "schema_version": 3,
      "id": "crystal_marsh",
      "domain": "surface",
      "climate": {"temperature": [0.3, 0.8], "moisture": [0.6, 1.0]},
      "height": {"base_offset": 2, "amplitude_mul": 1.1},
      "weather": {"temperature_offset": -0.12, "precipitation_multiplier": 1.6, "wind_exposure": 0.7},
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
                    kTestName, "valid custom schema-v3 biome pack loads");
  const auto accepted = cutum::UWorldGenPack::GetSnapshot();
  FluidTest::Expect(accepted && accepted->BiomeRegistry, kTestName,
                    "compiled biome registry is included in pack snapshot");
  const auto weather_profile = accepted->Biomes.find("crystal_marsh");
  FluidTest::Expect(
      weather_profile != accepted->Biomes.end() &&
          weather_profile->second.Weather.Defined &&
          std::abs(weather_profile->second.Weather.TemperatureOffset + 0.12f) <
              0.001f &&
          std::abs(weather_profile->second.Weather.PrecipitationMultiplier -
                   1.6f) < 0.001f &&
          std::abs(weather_profile->second.Weather.WindExposure - 0.7f) <
              0.001f,
      kTestName, "schema-v3 weather profile is parsed");
  const cutum::BiomeHandle custom =
      accepted->BiomeRegistry->Find("crystal_marsh");
  FluidTest::Expect(custom.IsValid(), kTestName,
                    "JSON-only biome receives a pack-scoped handle");
  FluidTest::Expect(accepted->BiomeRegistry->Key(custom) &&
                        *accepted->BiomeRegistry->Key(custom) ==
                            "crystal_marsh",
                    kTestName, "handle resolves to its stable biome key");
  FluidTest::Expect(
      cutum::BiomeIdFromString("unregistered_biome") == cutum::BiomeId::Invalid,
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

int main(int argc,char **argv)
{
  const fs::path repo_root = FindRepoRoot();
  std::error_code ec;
  fs::current_path(repo_root, ec);
  FluidTest::Expect(cutum::UWorldGenPack::LoadPackId("default"), kTestName,
                    "worldgen pack load failed");

  if(argc>1 && std::string(argv[1])=="--selector-budget-only") {
    TestPackSelectorBudget(); return 0;
  }

  TestAllBiomesHaveVegetationWeights();
  TestForestTreeWeightApplies();
  TestHillsHasGroundCoverWeight();
  TestPackScopedBiomeHandlesAndFailClosedValidation();

  std::cout << kTestName << ": OK" << std::endl;
  return 0;
}
