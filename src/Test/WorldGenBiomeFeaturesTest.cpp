#include "Test/FluidTestHelpers.h"

#include "WorldGen/Core/WorldGenPack.h"
#include "WorldGen/Sampling/BiomeRegistry.h"
#include "WorldGen/Core/WorldGenContentPin.h"
#include "WorldGen/Features/ObjectFeatureConfig.h"
#include "Core/Memory/BufferAllocationBudget.h"
#include "Core/Memory/StbiAllocation.h"
#include "ThirdParty/stb_image.h"
#include "Test/StbiAllocationFixture.h"
#include "WorldGen/Core/CatalogJsonSource.h"
#include <sstream>

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
#include "Test/BiomeMapSourceFixture.h"

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
  const auto first_data=first.Pack->BiomeMapImage->Data();
  const auto image_bytes=first.Pack->BiomeMapImage->ByteCount();
  std::vector<std::shared_ptr<const cutum::WorldGenPack>> copies;
  for(int n=0;n<64;++n) {
    auto copy=std::make_shared<cutum::WorldGenPack>(*first.Pack);
    FluidTest::Expect(copy->BiomeMapImage==first.Pack->BiomeMapImage && copy->BiomeMapImage->Data()==first_data,
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
  FluidTest::Expect(!retired_image.expired() && escaped_image->Data()==first_data,kTestName,
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

static void TestFeatureSourceJsonLoading()
{
  const auto path=FindRepoRoot()/"build/desktop-msvc/s4-source-json-feature-fixture.json";
  const auto write=[&](const std::string &text) {
    std::ofstream file(path,std::ios::binary);file << text;
  };
  write("\xEF\xBB\xBF" R"({
    "vegetation":[{"object":"source_tree","biomes":["forest"],"weight":3}],
    "ground_cover":[{"mode":"scatter_blocks","block":"source_grass","biomes":["plains"]}],
    "decoration":[{"object":"source_flower","biomes":["plains"]}],
    "structures":[{"object":"source_house","biomes":["plains"]}],
    "cave_features":[{"biome":"lush_caves","kind":"moss_patch","block":"source_moss"}]
  })");
  FluidTest::Expect(cutum::UObjectFeatureConfigStorage::LoadFromFile(path),kTestName,"BOM feature source loads all five groups");
  const auto old=cutum::UObjectFeatureConfigStorage::GetSnapshot();
  FluidTest::Expect(old->Vegetation.size()==1 && old->Vegetation[0].Weight==3 &&
      old->GroundCover.size()==1 && old->GroundCover[0].Scatter.BlockName=="source_grass" &&
      old->Decoration.size()==1 && old->Structures.size()==1 &&
      old->CaveFeatures.size()==1 && old->CaveFeatures[0].BlockName=="source_moss",kTestName,
      "loaded feature values survive source DOM destruction");
  for(const char *bad:{"null","[]","7","\"root\"","{",R"({"vegetation":[{"mode":7}]})"}) {
    write(bad);
    FluidTest::Expect(!cutum::UObjectFeatureConfigStorage::LoadFromFile(path) &&
        cutum::UObjectFeatureConfigStorage::GetSnapshot()==old,kTestName,
        "non-object/malformed feature source preserves published owner");
  }
  write(R"({"vegetation":null,"ground_cover":{},"decoration":7,"structures":"ignored","cave_features":false})");
  FluidTest::Expect(cutum::UObjectFeatureConfigStorage::LoadFromFile(path),kTestName,"wrong-kind optional groups remain ignored");
  const auto empty=cutum::UObjectFeatureConfigStorage::GetSnapshot();
  FluidTest::Expect(empty!=old && empty->Vegetation.empty() && empty->GroundCover.empty() &&
      empty->Decoration.empty() && empty->Structures.empty() && empty->CaveFeatures.empty() &&
      old->Vegetation[0].ObjectName=="source_tree",kTestName,"replacement does not invalidate retained feature version");
  write("{}");
  FluidTest::Expect(cutum::UObjectFeatureConfigStorage::LoadFromFile(path),kTestName,"absent groups remain valid");
  std::cout << "source_json_feature_groups=5 rejected_preserve=6 optional_kinds=5 missing=5 old_version=1" << std::endl;
}

static void TestStbiAllocationEnvelope()
{
  using namespace cutum;
  const auto before=GetStbiProcessAllocationSnapshot();
  const size_t header=StbiAllocationHeaderBytes();
  auto parent=std::make_shared<UStbiAllocationLedger>();
  std::weak_ptr<UStbiAllocationLedger> parentWeak=parent;
  void *escaped=nullptr;
  {
    UStbiAllocationScope outer(parent);
    auto *original=static_cast<unsigned char *>(StbiAllocate(17));
    FluidTest::Expect(original && reinterpret_cast<uintptr_t>(original)%alignof(std::max_align_t)==0 &&
        (sizeof(void*)!=8 || reinterpret_cast<uintptr_t>(original)%16==0),kTestName,"STBI request prefix preserves malloc alignment");
    for(int i=0;i<17;++i) original[i]=static_cast<unsigned char>(i+1);
    auto child=std::make_shared<UStbiAllocationLedger>(1);
    {
      UStbiAllocationScope inner(child);
      escaped=StbiReallocate(original,100);
      FluidTest::Expect(escaped && InspectStbiAllocation(escaped).owner.get()==parent.get() && !StbiAllocate(1),kTestName,
          "nested realloc keeps original ledger while new nested requests respect child limit");
    }
    for(int i=0;i<17;++i) FluidTest::Expect(static_cast<unsigned char *>(escaped)[i]==i+1,kTestName,"realloc preserves original bytes");
    const auto counts=parent->GetSnapshot();
    FluidTest::Expect(counts.live_request_bytes==header+100 && counts.live_payload_bytes==100 &&
        counts.peak_admitted_request_bytes==2*header+117 && counts.peak_live_request_bytes==header+100 &&
        counts.reallocations==1 && counts.live_allocations==1 &&
        child->GetSnapshot().refusals==1,kTestName,"realloc peak includes simultaneous old/new requests and both prefixes");
  }
  parent.reset();
  FluidTest::Expect(!parentWeak.expired(),kTestName,"escaped output owns original ledger after scope and caller retire");
  auto observed=parentWeak.lock();
  std::thread release([escaped]{StbiFree(escaped);});release.join();
  FluidTest::Expect(observed->GetSnapshot().live_request_bytes==0 && observed->GetSnapshot().live_allocations==0,kTestName,
      "cross-thread free returns original owner credit after physical free");
  observed.reset();FluidTest::Expect(parentWeak.expired(),kTestName,"no global allocation-owner history retains retired ledger payload");
  auto refused=std::make_shared<UStbiAllocationLedger>(2*header+16+32-1);
  {
    UStbiAllocationScope active(refused);
    auto *p=static_cast<unsigned char *>(StbiAllocate(16));p[0]=73;
    FluidTest::Expect(!StbiReallocate(p,32) && p[0]==73 && InspectStbiAllocation(p).payload_bytes==16,kTestName,
        "failed realloc preserves original allocation and bytes");
    StbiFree(p);
    FluidTest::Expect(!StbiAllocate(std::numeric_limits<size_t>::max()),kTestName,"prefix size overflow refuses without allocation");
  }
  FluidTest::Expect(refused->GetSnapshot().live_request_bytes==0 && refused->GetSnapshot().reserved_request_bytes==0 &&
      refused->GetSnapshot().failed_reallocations==1 && refused->GetSnapshot().refusals==2,kTestName,"failed/overflow requests leave no reservations");
#if defined(_WIN64)
  const auto systemFailure=std::make_shared<UStbiAllocationLedger>();
  {
    UStbiAllocationScope active(systemFailure);auto *p=static_cast<unsigned char *>(StbiAllocate(16));p[0]=91;
    // Above the Windows user address space, below the CRT invalid-size limit:
    // reaches realloc's failure path without consuming a real large buffer.
    FluidTest::Expect(!StbiReallocate(p,std::numeric_limits<size_t>::max()/4) && p[0]==91 &&
        InspectStbiAllocation(p).owner.get()==systemFailure.get(),kTestName,"CRT realloc failure reconstructs original prefix/owner/payload");
    FluidTest::Expect(systemFailure->GetSnapshot().system_failures==1 &&
        systemFailure->GetSnapshot().reserved_request_bytes==0,kTestName,"CRT failure returns pending request credit");
    StbiFree(p);
  }
  FluidTest::Expect(systemFailure->GetSnapshot().live_request_bytes==0,kTestName,"CRT rollback output can be freed normally");
#endif

  const auto png=test::MakeStbiAllocationPng();
  const auto check=[&](const unsigned char *pixels,int components) {
    for(int y=0;y<64;++y) for(int x=0;x<64;++x) {
      const auto rgb=test::StbiFixtureRgb(x,y);const size_t offset=(y*64+x)*components;
      for(int c=0;c<3;++c) FluidTest::Expect(pixels[offset+c]==rgb[c],kTestName,"actual PNG decode matches independent RGB source");
      if(components==4) FluidTest::Expect(pixels[offset+3]==255,kTestName,"actual PNG normalized alpha is opaque");
    }
  };
  auto decoder=std::make_shared<UStbiAllocationLedger>();
  unsigned char *pixels=nullptr;int w=0,h=0,c=0;
  {
    UStbiAllocationScope active(decoder);
    pixels=stbi_load_from_memory(png.data(),static_cast<int>(png.size()),&w,&h,&c,4);
    FluidTest::Expect(pixels && w==64 && h==64 && c==3,kTestName,"authored split-IDAT PNG decodes through real allocator hooks");
    check(pixels,4);
    const auto info=InspectStbiAllocation(pixels);const auto counts=decoder->GetSnapshot();
    FluidTest::Expect(info.payload_bytes==64*64*4 && info.request_bytes==64*64*4+header && info.owner.get()==decoder.get() &&
        counts.live_request_bytes==info.request_bytes && counts.live_allocations==1 && counts.reallocations>=2 &&
        counts.peak_admitted_request_bytes>counts.live_request_bytes,kTestName,"actual returned R_i and internal/realloc D_i are distinct measured requests");
  }
  const auto retained=decoder->GetSnapshot();
  std::thread decoderFree([pixels]{stbi_image_free(pixels);});decoderFree.join();
  FluidTest::Expect(decoder->GetSnapshot().live_request_bytes==0,kTestName,"actual decoded output frees outside scope and on another thread");
  size_t allocationFailures=0;
  for(size_t allowed=0;allowed<retained.allocation_attempts;++allowed) {
    auto fault=std::make_shared<UStbiAllocationLedger>(std::numeric_limits<size_t>::max(),allowed);
    {UStbiAllocationScope active(fault);
      auto *result=stbi_load_from_memory(png.data(),static_cast<int>(png.size()),&w,&h,&c,4);
      if(result) {check(result,4);stbi_image_free(result);} else ++allocationFailures;
    }
    const auto counts=fault->GetSnapshot();
    FluidTest::Expect(counts.refusals>0 && counts.live_request_bytes==0 && counts.reserved_request_bytes==0 &&
        counts.live_allocations==0,kTestName,"each real PNG allocation failure cleans original blocks and reservations");
  }
  auto retry=std::make_shared<UStbiAllocationLedger>();
  {
    UStbiAllocationScope active(retry);
    auto *result=stbi_load_from_memory(png.data(),static_cast<int>(png.size()),&w,&h,&c,3);
    FluidTest::Expect(result && InspectStbiAllocation(result).payload_bytes==64*64*3,kTestName,"normal RGB retry after faults retains exact returned allocation");
    check(result,3);stbi_image_free(result);
    auto *wide=stbi_load_16_from_memory(png.data(),static_cast<int>(png.size()),&w,&h,&c,3);
    FluidTest::Expect(wide && InspectStbiAllocation(wide).payload_bytes==64*64*3*sizeof(stbi_us),kTestName,"16-bit conversion uses same allocation ownership");
    stbi_image_free(wide);
    auto *floating=stbi_loadf_from_memory(png.data(),static_cast<int>(png.size()),&w,&h,&c,3);
    FluidTest::Expect(floating && InspectStbiAllocation(floating).payload_bytes==64*64*3*sizeof(float),kTestName,"float conversion uses same aligned allocation ownership");
    stbi_image_free(floating);
    FluidTest::Expect(!stbi_load_from_memory(png.data(),25,&w,&h,&c,4),kTestName,"truncated PNG remains rejected");
  }
  FluidTest::Expect(retry->GetSnapshot().live_request_bytes==0 && retry->GetSnapshot().reserved_request_bytes==0,kTestName,
      "retry/conversion/malformed decoder exits release all credited buffers");
  auto parallel=std::make_shared<UStbiAllocationLedger>();
  std::mutex gate;std::condition_variable ready;size_t readers=0;bool finish=false;
  std::vector<std::thread> workers;
  for(int i=0;i<4;++i) workers.emplace_back([&] {
    UStbiAllocationScope active(parallel);int width=0,height=0,channels=0;
    auto *output=stbi_load_from_memory(png.data(),static_cast<int>(png.size()),&width,&height,&channels,4);
    FluidTest::Expect(output && width==64 && height==64,kTestName,"parallel decoder output is valid");check(output,4);
    {std::unique_lock<std::mutex> lock(gate);++readers;ready.notify_all();ready.wait(lock,[&]{return finish;});}
    stbi_image_free(output);
  });
  {std::unique_lock<std::mutex> lock(gate);ready.wait(lock,[&]{return readers==4;});}
  const auto concurrent=parallel->GetSnapshot();
  FluidTest::Expect(concurrent.live_allocations==4 && concurrent.live_payload_bytes==4*64*64*4 &&
      concurrent.live_request_bytes==4*(64*64*4+header) && concurrent.reserved_request_bytes==0,kTestName,
      "shared ledger observes exactly four retained decoder allocations at barrier");
  {std::lock_guard<std::mutex> lock(gate);finish=true;}ready.notify_all();for(auto &worker:workers) worker.join();
  FluidTest::Expect(parallel->GetSnapshot().live_request_bytes==0 && parallel->GetSnapshot().live_allocations==0,kTestName,
      "parallel decoder free returns all shared requested credits");
  const auto root=FindRepoRoot()/"build/desktop-msvc/s4-stbi-pack-fixture";fs::create_directories(root);
  {std::ofstream image(root/"map.png",std::ios::binary);image.write(reinterpret_cast<const char *>(png.data()),png.size());}
  {std::ofstream pack(root/"pack.json");pack << R"({"id":"stbi_accounting","biome_mode":"image","biome_map_image":"map.png"})";}
  auto packLedger=std::make_shared<UStbiAllocationLedger>();
  {UStbiAllocationScope active(packLedger);FluidTest::Expect(UWorldGenPack::LoadFromDirectory(root.string()),kTestName,"actual image Pack loads through STBI accounting");}
  auto loaded=UWorldGenPack::GetSnapshot();
  FluidTest::Expect(loaded->BiomeMapImage && loaded->BiomeMapImage->ByteCount()==64*64*3 &&
      packLedger->GetSnapshot().live_request_bytes==loaded->BiomeMapImage->DecodedRequestBytes() &&
      packLedger->GetSnapshot().live_allocations==1 && packLedger->GetSnapshot().reallocations>=2,kTestName,
      "Pack adopts exact RGB decoder allocation while internal scratch has retired");
  check(loaded->BiomeMapImage->Data(),3);
  FluidTest::Expect(UWorldGenPack::LoadPackId("default"),kTestName,"restore default Pack through normal loader");
  const auto packBytes=loaded->BiomeMapImage->ByteCount();loaded.reset();
  FluidTest::Expect(packLedger->GetSnapshot().live_request_bytes==0,kTestName,"last Pack owner retires adopted decoder request");
  const auto after=GetStbiProcessAllocationSnapshot();
  FluidTest::Expect(after.live_request_bytes==before.live_request_bytes && after.live_allocations==before.live_allocations &&
      after.reserved_request_bytes==0,kTestName,"all STBI hook paths return process live/request credits to baseline");
  std::cout << "stbi_header_bytes=" << header << " ledger_bytes=" << sizeof(UStbiAllocationLedger)
      << " scope_bytes=" << sizeof(UStbiAllocationScope) << " rgba_retained_request=" << retained.live_request_bytes
      << " decode_admitted_peak=" << retained.peak_admitted_request_bytes << " reallocations=" << retained.reallocations
      << " allocation_failure_cases=" << retained.allocation_attempts << " failed_decodes=" << allocationFailures
      << " pack_decode_peak=" << packLedger->GetSnapshot().peak_admitted_request_bytes
      << " pack_rgb_bytes=" << packBytes
      << " concurrent_retained_request=" << concurrent.live_request_bytes << " final_live_delta=0" << std::endl;
}

static void TestCatalogJsonSourceEnvelope()
{
  using namespace cutum;using Json=nlohmann::json;
  std::vector<std::string> valid={"null","true","-42","1.25e4",R"("text")","[]","{}",
      "\xEF\xBB\xBF{\r\n\"unicode\":\"\\uD83D\\uDE03\",\"zero\":\"\\u0000\"\r\n}",
      R"({"k":[1,{"s":"x"}],"k":[2,3]})"};
  valid.push_back("{\"long\":\""+std::string(256*1024,'x')+"\"}");
  valid.push_back(std::string(512,'[')+"0"+std::string(512,']'));
  std::string wide="[";for(int i=0;i<20000;++i) {if(i) wide+=',';wide+=std::to_string(i);}wide+=']';valid.push_back(std::move(wide));
  for(const auto &text:valid) {
    std::istringstream oldInput(text),input(text);
    const auto expected=Json::parse(oldInput);
    const auto actual=ParseCatalogSourceJson(input,CatalogJsonKind::Pack);
    const auto shape=GetLastCatalogJsonSource();
    FluidTest::Expect(actual==expected && input.rdstate()==oldInput.rdstate(),kTestName,"single-pass catalog parser preserves JSON values and stream flags");
    FluidTest::Expect(shape.success && !shape.overflow && shape.bytes==text.size() &&
        shape.model_supported==kCatalogJsonSourceModelSupported &&
        (shape.model_supported ? shape.requested_heap_upper>=text.size() : shape.requested_heap_upper==0),kTestName,
        "source bytes/host bound or explicit unsupported model are recorded");
  }
  {
    std::istringstream source(R"({"k":[1,{"s":"x"}],"k":[2,3]})");
    (void)ParseCatalogSourceJson(source,CatalogJsonKind::Pack);const auto shape=GetLastCatalogJsonSource();
#if !JSON_DIAGNOSTIC_POSITIONS
    FluidTest::Expect(shape.values==8 && shape.objects==2 && shape.arrays==2 && shape.keys==3 &&
        shape.strings==1 && shape.string_bytes==4 && shape.max_depth==3,kTestName,
        "source envelope includes overwritten duplicate subtree,not only final DOM");
#endif
  }
  const std::vector<std::string> invalid={""," ","{","[1,]","{} []",R"({"a":})",R"("\uD800")","1e10000",std::string("\xFF",1)};
  for(const auto &text:invalid) {
    std::istringstream oldInput(text),input(text);std::string oldError,error;int oldId=0,id=0;
    try {(void)Json::parse(oldInput);} catch(const Json::exception &e) {oldError=e.what();oldId=e.id;}
    try {(void)ParseCatalogSourceJson(input,CatalogJsonKind::Features);} catch(const Json::exception &e) {error=e.what();id=e.id;}
    const auto shape=GetLastCatalogJsonSource();
    FluidTest::Expect(oldId!=0 && id==oldId && error==oldError && input.rdstate()==oldInput.rdstate() &&
        !shape.success && !shape.overflow && shape.model_supported==kCatalogJsonSourceModelSupported,kTestName,
        "malformed/trailing catalog input preserves exact exception and stream state");
  }
  CatalogJsonSourceShape huge;huge.bytes=std::numeric_limits<size_t>::max();huge.success=true;
  huge=BoundCatalogJsonSource(huge);
  FluidTest::Expect(kCatalogJsonSourceModelSupported ? huge.overflow && huge.requested_heap_upper==std::numeric_limits<size_t>::max()
      : !huge.model_supported && huge.requested_heap_upper==0,kTestName,"source bound arithmetic saturates or explicitly declines unsupported model");
  const auto source=GetCatalogJsonSourceSnapshot();
  FluidTest::Expect(source.active==0 && source.failed>=invalid.size() && source.completed>=valid.size(),kTestName,
      "all successful/failed parser scopes retire without source-name history");
  FluidTest::Expect(source.largest_bound_shape.requested_heap_upper==source.largest_requested_heap_upper,kTestName,
      "largest model retains its own numeric input shape rather than joining independent maxima");
  std::cout << "catalog_json_valid=" << valid.size() << " exact_error_cases=" << invalid.size()
      << " duplicate_values=8 deep_levels=512 wide_values=20000 largest_source=" << source.largest_input
      << " largest_source_model=" << source.largest_requested_heap_upper << " shape_inline_bytes=" << sizeof(CatalogJsonSourceShape)
      << " counters_inline_bytes=" << sizeof(CatalogJsonSourceSnapshot) << " active=0" << std::endl;
}

int main(int argc,char **argv)
{
  if(argc>1 && std::string(argv[1])=="--biome-map-source") return RunBiomeMapSourceFixture(argc>2?argv[2]:nullptr);
  const fs::path repo_root = FindRepoRoot();
  std::error_code ec;
  fs::current_path(repo_root, ec);
  FluidTest::Expect(cutum::UWorldGenPack::LoadPackId("default"), kTestName,
                    "worldgen pack load failed");

  if(argc>1 && std::string(argv[1])=="--selector-budget-only") {
    TestPackSelectorBudget(); return 0;
  }
  if(argc>1 && std::string(argv[1])=="--stbi-allocation-only") {TestStbiAllocationEnvelope();return 0;}
  if(argc>1 && std::string(argv[1])=="--catalog-json-source-only") {TestCatalogJsonSourceEnvelope();return 0;}
  if(argc>1 && std::string(argv[1])=="--source-json-load-only") {
    TestFeatureSourceJsonLoading();return 0;
  }
  if(argc>1 && std::string(argv[1])=="--image-owner-only") {
    TestPinnedBiomeImageReload(); return 0;
  }

  TestAllBiomesHaveVegetationWeights();
  TestForestTreeWeightApplies();
  TestHillsHasGroundCoverWeight();
  TestPinnedBiomeImageReload();
  TestPackScopedBiomeHandlesAndFailClosedValidation();
  TestFeatureSourceJsonLoading();
  TestStbiAllocationEnvelope();
  TestCatalogJsonSourceEnvelope();

  std::cout << kTestName << ": OK" << std::endl;
  return 0;
}
