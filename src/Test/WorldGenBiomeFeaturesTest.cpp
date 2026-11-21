#include "Test/FluidTestHelpers.h"

#include "WorldGen/Core/WorldGenPack.h"
#include "WorldGen/Sampling/BiomeRegistry.h"
#include "WorldGen/Core/WorldGenContentPin.h"
#include "Core/Memory/BufferAllocationBudget.h"

#include <chrono>
#include <cmath>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <array>
#include <atomic>
#include <condition_variable>
#include <mutex>
#include <thread>

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

static void TestPinnedBiomeImageReload()
{
  const auto root=fs::temp_directory_path()/(
      "cubatarium_biome_image_"+std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()));
  fs::create_directories(root);
  const auto author=[&](const std::array<unsigned char,6> &pixels,int scale,int width,int height) {
    std::ofstream pack(root/"pack.json");
    pack << "{\"id\":\"same_image_fixture\",\"biome_mode\":\"image\",\"biome_map_image\":\"map.ppm\",\"biome_map_block_scale\":" << scale << "}";
    std::ofstream image(root/"map.ppm",std::ios::binary);
    image << "P6\n" << width << ' ' << height << "\n255\n";
    image.write(reinterpret_cast<const char *>(pixels.data()),pixels.size());
  };
  author({0x2d,0x8a,0x3f,0xe8,0xd4,0x6a},4,2,1); // forest, desert
  FluidTest::Expect(cutum::UWorldGenPack::LoadFromDirectory(root.string()),kTestName,"first authored image pack loads");
  auto first=cutum::CaptureWorldGenContentSnapshot();
  const auto samples=[&](const cutum::WorldGenContentSnapshot &content) {
    cutum::UWorldGenContentPinScope pin(content);
    return std::array<cutum::BiomeId,8>{cutum::UWorldGenPack::BiomeAtImage(0,0),cutum::UWorldGenPack::BiomeAtImage(4,0),
        cutum::UWorldGenPack::BiomeAtImage(-4,-4),cutum::UWorldGenPack::BiomeAtImage(8,4),
        cutum::UWorldGenPack::BiomeAtImage(2,0),cutum::UWorldGenPack::BiomeAtImage(0,2),
        cutum::UWorldGenPack::BiomeAtImage(0,-2),cutum::UWorldGenPack::BiomeAtImage(-1,-1)};
  };
  const std::array<cutum::BiomeId,8> first_expected={cutum::BiomeId::Forest,cutum::BiomeId::Desert,
      cutum::BiomeId::Desert,cutum::BiomeId::Forest,cutum::BiomeId::Forest,cutum::BiomeId::Forest,
      cutum::BiomeId::Forest,cutum::BiomeId::Forest};
  FluidTest::Expect(samples(first)==first_expected,kTestName,"authored image colors scale and negative wrapping have independent expected biome values");
  author({0x7a,0x7a,0x7a,0xd8,0xe8,0xf0},2,1,2); // hills, tundra
  FluidTest::Expect(cutum::UWorldGenPack::ReloadActive(),kTestName,"same-name replacement image reloads");
  const auto second=cutum::CaptureWorldGenContentSnapshot();
  const bool first_preserved=samples(first)==first_expected;
  const std::array<cutum::BiomeId,8> second_expected={cutum::BiomeId::Hills,cutum::BiomeId::Hills,
      cutum::BiomeId::Hills,cutum::BiomeId::Hills,cutum::BiomeId::Hills,cutum::BiomeId::Tundra,
      cutum::BiomeId::Tundra,cutum::BiomeId::Hills};
  const bool second_correct=samples(second)==second_expected;
  std::cout << "biome_image_same_name_old_preserved=" << first_preserved
      << " new_values_correct=" << second_correct << std::endl;
  FluidTest::Expect(first_preserved && second_correct,kTestName,
      "pinned image pixels and dimensions belong to their own catalog snapshot across same-name reload");
  FluidTest::Expect(first.Pack->BiomeMapImage && first.Pack->BiomeMapImage->Width==2 && first.Pack->BiomeMapImage->Height==1 &&
      second.Pack->BiomeMapImage && second.Pack->BiomeMapImage->Width==1 && second.Pack->BiomeMapImage->Height==2 &&
      first.Pack->BiomeMapImage!=second.Pack->BiomeMapImage,kTestName,"image dimensions and immutable payload identity are publication-owned");
  const auto first_data=first.Pack->BiomeMapImage->Pixels.data();
  const auto image_bytes=first.Pack->BiomeMapImage->Pixels.capacity();
  std::vector<std::shared_ptr<const cutum::WorldGenPack>> copies;
  for(int n=0;n<64;++n) {
    auto copy=std::make_shared<cutum::WorldGenPack>(*first.Pack);
    FluidTest::Expect(copy->BiomeMapImage==first.Pack->BiomeMapImage && copy->BiomeMapImage->Pixels.data()==first_data,
        kTestName,"pack copies share exact immutable decoded payload instead of copying pixels");
    copies.push_back(std::move(copy));
  }
  {
    std::ofstream invalid(root/"pack.json"); invalid << "{";
  }
  FluidTest::Expect(!cutum::UWorldGenPack::ReloadActive() && cutum::UWorldGenPack::GetSnapshot()==second.Pack &&
      samples(first)==first_expected && samples(second)==second_expected,kTestName,
      "failed JSON reload keeps published snapshot and all old image payloads unchanged");
  FluidTest::Expect(cutum::UWorldGenPack::LoadPackId("default"),kTestName,"default restored after image ownership fixture");
  auto procedural=cutum::CaptureWorldGenContentSnapshot();
  const auto all_plains=[](const auto &values) {
    for(const auto value:values) if(value!=cutum::BiomeId::Plains) return false;
    return true;
  };
  FluidTest::Expect(!procedural.Pack->BiomeMapImage && all_plains(samples(procedural)) &&
      samples(first)==first_expected && samples(second)==second_expected,kTestName,
      "procedural publication has no stale global image while old pinned images remain available");
  author({0x7a,0x7a,0x7a,0xd8,0xe8,0xf0},2,1,2);
  {
    std::ofstream corrupt(root/"map.ppm",std::ios::binary); corrupt << "broken image";
  }
  FluidTest::Expect(cutum::UWorldGenPack::LoadFromDirectory(root.string()),kTestName,
      "failed image decode preserves existing procedural fallback policy");
  auto fallback=cutum::CaptureWorldGenContentSnapshot();
  FluidTest::Expect(fallback.Pack->BiomeMode==cutum::WorldGenBiomeMode::Procedural && !fallback.Pack->BiomeMapImage &&
      all_plains(samples(fallback)) && samples(first)==first_expected && samples(second)==second_expected,kTestName,
      "failed image decode cannot clear an old published image owner");

  std::mutex gate; std::condition_variable changed;
  unsigned phase=0,completed=0;
  std::atomic<unsigned> mismatches{0};
  std::array<std::thread,4> readers;
  for(auto &reader:readers) reader=std::thread([&] {
    for(unsigned round=1;round<=16;++round) {
      {std::unique_lock lock(gate);changed.wait(lock,[&] {return phase>=round;});}
      for(int repeat=0;repeat<64;++repeat) if(samples(first)!=first_expected) ++mismatches;
      {std::lock_guard lock(gate);++completed;}
      changed.notify_all();
    }
  });
  for(unsigned round=1;round<=16;++round) {
    if(round%2) author({0x2d,0x8a,0x3f,0xe8,0xd4,0x6a},4,2,1);
    else author({0x7a,0x7a,0x7a,0xd8,0xe8,0xf0},2,1,2);
    {std::lock_guard lock(gate);phase=round;}
    changed.notify_all();
    FluidTest::Expect(cutum::UWorldGenPack::LoadFromDirectory(root.string()),kTestName,"concurrent reader publication loads");
    {std::unique_lock lock(gate);changed.wait(lock,[&] {return completed==4*round;});}
  }
  for(auto &reader:readers) reader.join();
  FluidTest::Expect(mismatches==0,kTestName,"four readers retain old image values across16 alternating same-name publications");
  FluidTest::Expect(cutum::UWorldGenPack::LoadPackId("default"),kTestName,"procedural catalog restored after reader fixture");
  std::weak_ptr<const cutum::WorldGenPack> retired_pack=first.Pack;
  std::weak_ptr<const cutum::WorldGenBiomeMapImage> retired_image=first.Pack->BiomeMapImage;
  auto escaped_image=first.Pack->BiomeMapImage;
  first={};
  FluidTest::Expect(retired_pack.expired() && !retired_image.expired(),kTestName,
      "escaped payload and pack copies retain pixels without retaining original pack");
  copies.clear();
  FluidTest::Expect(!retired_image.expired() && escaped_image->Pixels.data()==first_data,kTestName,
      "last explicit image owner still owns original allocation after pack copies retire");
  escaped_image.reset();
  FluidTest::Expect(retired_image.expired(),kTestName,"last strong image owner retires old decoded payload");
  std::cout << "biome_image_payload_bytes=" << image_bytes << " shared_pack_copies=64 reader_queries=" << 4*16*64*8
      << " publications=16 mismatches=" << mismatches.load() << " final_image_owner_expired=" << retired_image.expired() << std::endl;
  FluidTest::Expect(fs::equivalent(root.parent_path(),fs::temp_directory_path()) && root.filename().string().find("cubatarium_biome_image_")==0,
      kTestName,"image fixture cleanup stays in its generated temp directory");
  std::error_code ec;fs::remove_all(root,ec);
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
  if(argc>1 && std::string(argv[1])=="--image-owner-only") {
    TestPinnedBiomeImageReload(); return 0;
  }

  TestAllBiomesHaveVegetationWeights();
  TestForestTreeWeightApplies();
  TestHillsHasGroundCoverWeight();
  TestPinnedBiomeImageReload();
  TestPackScopedBiomeHandlesAndFailClosedValidation();

  std::cout << kTestName << ": OK" << std::endl;
  return 0;
}
