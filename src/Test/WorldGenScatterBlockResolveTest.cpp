#ifndef NOMINMAX
#define NOMINMAX
#endif
#include "Test/FluidTestHelpers.h"

#include "Blocks/BlockRegistry.h"
#include "ResourcePacks/BlockMergeRegistry.h"
#include "ResourcePacks/ResourcePack.h"
#include "WorldGen/Core/WorldGenBlockResolver.h"
#include "Core/Memory/BufferAllocationBudget.h"
#include "WorldGen/Core/CatalogStorageCensus.h"
#include "Core/Memory/StbiAllocation.h"

#include <algorithm>
#include <filesystem>
#include <fstream>
#include <glm/glm.hpp>
#include <iostream>
#include <memory>
#include <limits>
#include <mutex>
#include <string>
#include <thread>

namespace fs = std::filesystem;

namespace
{

constexpr const char *kTestName = "worldgen_scatter_block_resolve_test";

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

static void TestMergeRegistryLookupDoesNotCreateSyntheticBlocks()
{
  auto merge = std::make_shared<cutum::UBlockMergeRegistry>();
  const size_t descriptors_before = merge->GetCubeDescriptors().size();

  const cutum::BlockId lookup_id =
      merge->LookupBlockName("ghost_block_not_in_pack");
  FluidTest::Expect(lookup_id == cutum::BLOCK_AIR, kTestName,
                    "LookupBlockName returns air for unknown block");
  FluidTest::Expect(merge->GetCubeDescriptors().size() == descriptors_before,
                    kTestName,
                    "LookupBlockName must not grow merge registry");

  const cutum::BlockId resolve_id =
      merge->ResolveName("ghost_block_not_in_pack");
  FluidTest::Expect(resolve_id != cutum::BLOCK_AIR, kTestName,
                    "ResolveName creates synthetic for comparison");
  FluidTest::Expect(merge->GetCubeDescriptors().size() > descriptors_before,
                    kTestName, "ResolveName grows merge registry");
}

static void TestScatterResolveUsesPackLookupOnly()
{
  auto merge = std::make_shared<cutum::UBlockMergeRegistry>();
  cutum::UBlockRegistry registry(nullptr);
  registry.SetMergeRegistry(merge);

  const size_t descriptors_before = merge->GetCubeDescriptors().size();

  const cutum::BlockId scatter_id = cutum::ResolvePackScatterBlockId(
      registry, "minetest_default_16", "ghost_block_not_in_pack");
  FluidTest::Expect(scatter_id == cutum::BLOCK_AIR, kTestName,
                    "scatter resolve returns air for unknown pack block");
  FluidTest::Expect(merge->GetCubeDescriptors().size() == descriptors_before,
                    kTestName,
                    "scatter resolve must not grow merge registry");

  const cutum::BlockId synthetic_id =
      registry.GetIdByTypeName("ghost_block_not_in_pack");
  FluidTest::Expect(synthetic_id != cutum::BLOCK_AIR, kTestName,
                    "GetIdByTypeName still creates synthetic for comparison");
  FluidTest::Expect(merge->GetCubeDescriptors().size() > descriptors_before,
                    kTestName, "GetIdByTypeName grows merge registry");
}

static void TestMutatingRegistryNameBudget()
{
  cutum::UBlockMergeRegistry merge;
  const std::string long_name(64*1024,'n');
  const auto rose=merge.ResolveName("rose");
  const auto long_id=merge.ResolveName(long_name);
  size_t refused=0;
  const auto pressure=[&](const char *label,const std::string &name) {
    auto budget=std::make_shared<cutum::UBufferAllocationBudget>(1024);
    auto peer=std::make_shared<cutum::BufferBudgetJob>(); budget->Reserve(*peer,1);
    const auto before=merge.GetCubeDescriptors().size();
    bool denied=false;
    {
      cutum::UBufferBudgetScope active({budget,std::make_shared<cutum::BufferBudgetJob>()});
      try {(void)merge.ResolveName(name);} catch(const cutum::UBufferBudgetExceeded &) {denied=true;}
    }
    std::cout << "mutating_name_case=" << label << " refused=" << denied
        << " entries_added=" << merge.GetCubeDescriptors().size()-before
        << " remaining=" << budget->UsedBytes() << std::endl;
    FluidTest::Expect(budget->UsedBytes()==1,kTestName,"mutating name unwind returns competing credit");
    if(denied) FluidTest::Expect(merge.GetCubeDescriptors().size()==before,kTestName,"name refusal precedes catalog mutation");
    budget->Release(*peer,1); refused+=denied;
  };
  merge.SetWorldgenOwnerPackId(std::string(64*1024,'p'));
  pressure("owner_prefix","rose");
  merge.SetWorldgenOwnerPackId("");
  pressure("local_fallback","unknown::"+long_name);
  pressure("qualified_synthetic","unknown::"+std::string(64*1024,'s'));
  pressure("unqualified_synthetic",std::string(64*1024,'u'));
  std::cout << "mutating_name_refusals=" << refused << "/4" << std::endl;
  FluidTest::Expect(refused==4,kTestName,"mutating registry derived names reserve before allocation");

  auto budget=std::make_shared<cutum::UBufferAllocationBudget>(1024*1024);
  auto peer=std::make_shared<cutum::BufferBudgetJob>(); budget->Reserve(*peer,1);
  const auto ignored_prefix=std::string(64*1024,'p')+"::rose";
  const auto qualified_long="unknown::"+long_name;
  merge.SetWorldgenOwnerPackId("missing_pack");
  {
    cutum::UBufferBudgetScope active({budget,std::make_shared<cutum::BufferBudgetJob>()});
    FluidTest::Expect(merge.ResolveBlockName("rose")==rose &&
        merge.ResolveName(qualified_long)==long_id,kTestName,"mutating owner miss and qualified local fallback preserve IDs");
    // A long ignored prefix must not create the discarded SplitQualifiedBlockName prefix copy.
    auto full=std::make_shared<cutum::UBufferAllocationBudget>(1);
    auto other=std::make_shared<cutum::BufferBudgetJob>(); full->Reserve(*other,1);
    {
      cutum::UBufferBudgetScope no_heap({full,std::make_shared<cutum::BufferBudgetJob>()});
      FluidTest::Expect(merge.ResolveName(ignored_prefix)==rose &&
          full->RefusalCount()==0,kTestName,"ignored qualified prefix is borrowed without temporary allocation");
    }
    full->Release(*other,1);
    const auto before=merge.GetCubeDescriptors().size();
    const auto synthetic=merge.ResolveName("unknown::new_synthetic");
    FluidTest::Expect(synthetic!=cutum::BLOCK_AIR && merge.ResolveName("unknown::new_synthetic")==synthetic &&
        merge.GetCubeDescriptors().size()==before+1,kTestName,"legacy synthetic creation and repeated resolution remain available");
  }
  FluidTest::Expect(budget->UsedBytes()==1,kTestName,"successful mutating lookup scratch retires completely");
  budget->Release(*peer,1);
  std::cout << "mutating_name_peak=" << budget->PeakBytes() << " final=" << budget->UsedBytes() << std::endl;
}

class WarningCounterBuffer final : public std::streambuf
{
  std::mutex Mutex;
  size_t Lines{0};
  std::streamsize xsputn(const char *text,std::streamsize count) override {
    std::lock_guard lock(Mutex);
    Lines+=std::count(text,text+count,'\n');
    return count;
  }
  int overflow(int ch) override {
    if(traits_type::eq_int_type(ch,traits_type::eof())) return traits_type::not_eof(ch);
    const char byte=traits_type::to_char_type(ch); xsputn(&byte,1); return ch;
  }
public:
  size_t Count() {std::lock_guard lock(Mutex);return Lines;}
};

class ScopedWarningCapture
{
  std::streambuf *Previous;
public:
  explicit ScopedWarningCapture(WarningCounterBuffer &buffer) : Previous(std::cerr.rdbuf(&buffer)) {}
  ~ScopedWarningCapture() {std::cerr.rdbuf(Previous);}
};

static void TestTierAWarningOwnership()
{
  auto first=std::make_shared<cutum::UBlockMergeRegistry>();
  auto second=std::make_shared<cutum::UBlockMergeRegistry>();
  std::weak_ptr<cutum::UBlockMergeRegistry> retired_first=first,retired_second=second;
  std::vector<std::string> aliases;
  for(int n=0;n<32;++n) aliases.push_back(std::string(64*1024,'p')+std::to_string(n)+"::stone");
  WarningCounterBuffer warnings;
  auto budget=std::make_shared<cutum::UBufferAllocationBudget>(1);
  auto peer=std::make_shared<cutum::BufferBudgetJob>(); budget->Reserve(*peer,1);
  size_t first_count=0,second_count=0;
  {
    ScopedWarningCapture capture(warnings);
    cutum::UBufferBudgetScope no_heap({budget,std::make_shared<cutum::BufferBudgetJob>()});
    for(const auto &name:aliases) FluidTest::Expect(first->ResolveName(name)==cutum::BLOCK_AIR,kTestName,"Tier A aliases never create synthetic blocks");
    first_count=warnings.Count();
    for(const auto &name:aliases) FluidTest::Expect(second->ResolveName(name)==cutum::BLOCK_AIR,kTestName,"second registry retains Tier A fail-closed resolution");
    second_count=warnings.Count()-first_count;
  }
  std::cout << "tier_a_aliases=" << aliases.size() << " first_registry_warnings=" << first_count
      << " second_registry_warnings=" << second_count << " remaining=" << budget->UsedBytes() << std::endl;
  FluidTest::Expect(first_count==1 && second_count==1,kTestName,
      "Tier A diagnostics collapse qualified aliases and belong independently to each registry");
  {
    ScopedWarningCapture capture(warnings);
    first->Rebuild({},nullptr,16);
    FluidTest::Expect(first->ResolveName(aliases[0])==cutum::BLOCK_AIR,kTestName,"rebuilt registry still rejects missing Tier A");
  }
  FluidTest::Expect(warnings.Count()==3 && first->GetCubeDescriptors().empty() && second->GetCubeDescriptors().empty(),
      kTestName,"catalog rebuild starts a fresh bounded warning epoch without synthetic entries");
  first.reset();second.reset();
  FluidTest::Expect(retired_first.expired() && retired_second.expired() && budget->UsedBytes()==1 &&
      budget->RefusalCount()==0,kTestName,"warning owner retires with registry and Tier A aliases need no external lookup scratch");
  budget->Release(*peer,1);

  const char *tier_a[]={"bedrock","stone","dirt","grass","sand","sandstone","gravel","snow","clay",
      "ice","hellrock","water","lava","fire","wood","tree_log","tree_bark","tree_leaves"};
  auto shared=std::make_shared<cutum::UBlockMergeRegistry>();
  std::array<std::shared_ptr<cutum::UBlockMergeRegistry>,4> independent;
  for(auto &registry:independent) registry=std::make_shared<cutum::UBlockMergeRegistry>();
  WarningCounterBuffer concurrent;
  {
    ScopedWarningCapture capture(concurrent);
    std::array<std::thread,4> workers;
    for(size_t worker=0;worker<workers.size();++worker) workers[worker]=std::thread([&,worker] {
      for(int repeat=0;repeat<32;++repeat) for(const auto *local:tier_a) {
        const auto name="missing_"+std::to_string(worker)+"_"+std::to_string(repeat)+"::"+local;
        FluidTest::Expect(shared->ResolveName(name)==cutum::BLOCK_AIR &&
            independent[worker]->ResolveName(name)==cutum::BLOCK_AIR,kTestName,"concurrent Tier A diagnostic resolution remains fail closed");
      }
    });
    for(auto &worker:workers) worker.join();
  }
  FluidTest::Expect(concurrent.Count()==18*5 && shared->GetCubeDescriptors().empty(),kTestName,
      "shared and independent registry locks yield exactly one warning per canonical Tier A key per owner");
  for(const auto &registry:independent) FluidTest::Expect(registry->GetCubeDescriptors().empty(),kTestName,"independent warning catalogs remain empty");
  std::cout << "tier_a_rebuild_warnings=" << warnings.Count() << " concurrent_warnings=" << concurrent.Count()
      << " concurrent_resolutions=" << 4*32*18*2 << " final=" << budget->UsedBytes() << std::endl;
}

static void TestRealPackScatterBlocksResolve()
{
  const fs::path repo_root = FindRepoRoot();
  const fs::path pack_root =
      repo_root / "resource_packs" / "minetest_default_16";
  const std::optional<cutum::ResourcePackManifest> manifest =
      cutum::UResourcePack::LoadManifest(pack_root);
  FluidTest::Expect(manifest.has_value(), kTestName, "pack manifest load failed");

  auto placeholder = std::make_shared<cutum::UPlaceholderTextureCache>(
      repo_root / ".placeholder_cache_test", 16, glm::vec3(0.42f, 0.29f, 0.62f));
  auto merge = std::make_shared<cutum::UBlockMergeRegistry>();
  merge->Rebuild({*manifest}, placeholder, 16);
  merge->SetWorldgenOwnerPackId("minetest_default_16");
  const auto lookup_pressure=[&](const std::string &name) {
    auto budget=std::make_shared<cutum::UBufferAllocationBudget>(1024);
    auto peer=std::make_shared<cutum::BufferBudgetJob>(); budget->Reserve(*peer,1);
    bool refused=false;
    {
      cutum::UBufferBudgetScope active({budget,std::make_shared<cutum::BufferBudgetJob>()});
      try {(void)merge->LookupBlockName(name);} catch(const cutum::UBufferBudgetExceeded &) {refused=true;}
    }
    FluidTest::Expect(refused && budget->UsedBytes()==1,kTestName,"native merge derived lookup name refuses before allocation and releases credit");
    budget->Release(*peer,1);
  };
  const auto descriptors=merge->GetCubeDescriptors().size();
  merge->SetWorldgenOwnerPackId(std::string(64*1024,'p'));
  lookup_pressure("rose");
  merge->SetWorldgenOwnerPackId("minetest_default_16");
  lookup_pressure("unknown::"+std::string(64*1024,'n'));
  FluidTest::Expect(merge->LookupBlockName("unknown::rose")==merge->LookupBlockName("rose") &&
      merge->GetCubeDescriptors().size()==descriptors,kTestName,"native qualified miss preserves local fallback and never creates registry entries");

  cutum::UBlockRegistry registry(nullptr);
  registry.SetMergeRegistry(merge);

  for (const char *block_name : {"rose", "mtg_dry_grass_1"})
  {
    const cutum::BlockId block_id = cutum::ResolvePackScatterBlockId(
        registry, "minetest_default_16", block_name);
    const std::string resolve_msg =
        std::string("pack block resolves: ") + block_name;
    FluidTest::Expect(block_id != cutum::BLOCK_AIR, kTestName,
                      resolve_msg.c_str());
    const std::string texture_msg =
        std::string("pack block has descriptor: ") + block_name;
    FluidTest::Expect(registry.HasRenderableTexture(block_id), kTestName,
                      texture_msg.c_str());
  }

  const cutum::BlockId missing_dandelion = cutum::ResolvePackScatterBlockId(
      registry, "minetest_default_16", "dandelion");
  FluidTest::Expect(missing_dandelion == cutum::BLOCK_AIR, kTestName,
                    "dandelion is absent from pack and must not resolve");
}

static void TestAtlasCpuConstruction(bool requireFixed)
{
  auto bases=std::make_shared<cutum::UTextureBaseStorage>();
  cutum::UTextureCubeStorage storage(bases);
  const auto pixel=[](int frame,int face,int x,int y) {
    return std::array<unsigned char,4>{static_cast<unsigned char>(1+32*frame+face),
        static_cast<unsigned char>(5+53*x),static_cast<unsigned char>(7+79*y),255};
  };
  const auto source=[&](int face,int frames) {
    cutum::TexturePixelData data;data.Width=2;data.Height=2*frames;
    for(int f=0;f<frames;++f) for(int y=0;y<2;++y) for(int x=0;x<2;++x) {
      const auto value=pixel(f,face,x,y);data.Rgba.insert(data.Rgba.end(),value.begin(),value.end());
    }
    return data;
  };
  std::vector<std::string> plain,strip;
  for(int f=0;f<2;++f) for(int face=0;face<6;++face) {
    const auto name="atlas_frame_"+std::to_string(f)+"_face_"+std::to_string(face);
    auto data=source(face,1);
    for(size_t i=0;i<data.Rgba.size();i+=4) data.Rgba[i]+=static_cast<unsigned char>(32*f);
    bases->RegisterPixels(name,std::move(data));plain.push_back(name);
    if(f==0) {
      const auto vertical="atlas_strip_"+std::to_string(face);
      bases->RegisterPixels(vertical,source(face,2));strip.push_back(vertical);
    }
  }
  const auto matches=[&](const cutum::UTextureCubeStorage::CubeAtlasPixels &atlas,int frames) {
    if(atlas.Width!=12 || atlas.Height!=2*frames || atlas.Rgba.size()!=size_t(12*2*frames*4)) return false;
    for(int f=0;f<frames;++f) for(int face=0;face<6;++face) for(int y=0;y<2;++y) for(int x=0;x<2;++x) {
      const auto value=pixel(f,face,x,y);
      const size_t offset=((f*2+y)*12+face*2+x)*4;
      for(size_t channel=0;channel<4;++channel) if(atlas.Rgba[offset+channel]!=value[channel]) return false;
    }
    return true;
  };
  auto single=storage.BuildCubeAtlasPixels("single",{plain.begin(),plain.begin()+6},0);
  const auto vertical=storage.BuildCubeAtlasPixels("vertical",strip,2);
  FluidTest::Expect(matches(single,1) && matches(vertical,2),kTestName,"CPU atlas matches independent spatial/frame RGBA patterns");
  single.Rgba[7]^=1;
  FluidTest::Expect(!matches(single,1),kTestName,"independent atlas oracle rejects a changed pixel");
  const auto multi=storage.BuildCubeAtlasPixels("multi",plain,0);
  const bool explicitFrames=matches(multi,2);
  std::cout << "atlas_explicit_frames=" << (explicitFrames?2:1) << " expected=2" << std::endl;
  if(!requireFixed) return;
  FluidTest::Expect(explicitFrames,kTestName,"ordinary twelve-face list retains two animation frames");
  const auto file=FindRepoRoot()/"build/desktop-msvc/s4-atlas-source.ppm";
  {
    const auto data=source(0,1);std::ofstream image(file,std::ios::binary);image << "P6\n2 2\n255\n";
    for(size_t i=0;i<data.Rgba.size();i+=4) image.write(reinterpret_cast<const char *>(data.Rgba.data()+i),3);
  }
  bases->Register("atlas_file",file.string());
  const auto decodeLedger=std::make_shared<cutum::UStbiAllocationLedger>();
  cutum::UTextureCubeStorage::CubeAtlasPixels decoded;
  {cutum::UStbiAllocationScope active(decodeLedger);decoded=storage.BuildCubeAtlasPixels("file",std::vector<std::string>(6,"atlas_file"),0);}
  const auto decodeCounts=decodeLedger->GetSnapshot();
  FluidTest::Expect(decodeCounts.successful_allocations>0 && decodeCounts.live_request_bytes==0 &&
      decodeCounts.reserved_request_bytes==0,kTestName,"CPU atlas adoption retires decoder owner after copied atlas completes");
  FluidTest::Expect(decoded.Width==12 && decoded.Height==2,kTestName,"real STBI decoded file builds CPU atlas without GL");
  for(int face=0;face<6;++face) for(int y=0;y<2;++y) for(int x=0;x<2;++x) {
    const auto expected=pixel(0,0,x,y);const size_t offset=(y*12+face*2+x)*4;
    for(size_t c=0;c<4;++c) FluidTest::Expect(decoded.Rgba[offset+c]==expected[c],kTestName,"all decoded face channels match source pattern");
  }
  cutum::TexturePixelData impossible;impossible.Width=std::numeric_limits<int>::max();impossible.Height=2;impossible.Rgba.resize(4);
  bases->RegisterPixels("atlas_impossible",std::move(impossible));
  cutum::TexturePixelData shortData;shortData.Width=2;shortData.Height=2;shortData.Rgba.resize(3);
  bases->RegisterPixels("atlas_short",std::move(shortData));
  for(const auto &names:{std::vector<std::string>{},std::vector<std::string>(6,"absent"),
      std::vector<std::string>(6,"atlas_impossible"),std::vector<std::string>(6,"atlas_short")})
    FluidTest::Expect(storage.BuildCubeAtlasPixels("invalid",names,0).Rgba.empty(),kTestName,"invalid/missing source rejects before atlas allocation");
  FluidTest::Expect(storage.BuildCubeAtlasPixels("invalid_strip",strip,5).Rgba.empty(),kTestName,"strip with more frames than rows rejects before copying");
  auto missing=std::vector<std::string>{plain.begin(),plain.begin()+6};missing[1]="absent";
  const auto partial=storage.BuildCubeAtlasPixels("partial",missing,0);
  FluidTest::Expect(partial.Width==12 && partial.Height==2,kTestName,"missing secondary face keeps a valid atlas");
  for(int y=0;y<2;++y) for(int x=2;x<4;++x) for(size_t c=0;c<4;++c)
    FluidTest::Expect(partial.Rgba[(y*12+x)*4+c]==0,kTestName,"missing face stays transparent zero");
  cutum::TexturePixelData small;small.Width=1;small.Height=1;small.Rgba={91,92,93,94};
  bases->RegisterPixels("atlas_small",std::move(small));missing[1]="atlas_small";
  const auto clipped=storage.BuildCubeAtlasPixels("clipped",missing,0);
  for(int y=0;y<2;++y) for(int x=2;x<4;++x) for(size_t c=0;c<4;++c)
    FluidTest::Expect(clipped.Rgba[(y*12+x)*4+c]==(y==0 && x==2 ? 91+c : 0),kTestName,
        "smaller source copies only its valid pixel and leaves remaining tile zero");
  std::cout << "atlas_cpu_single=1 strip_frames=2 explicit_frames=2 changed_pixel_rejected=1 stbi_channels=96 invalid_cases=5 missing_face=1 atlas_bytes="
      << multi.Rgba.capacity() << " decoded_admitted_peak=" << decodeCounts.peak_admitted_request_bytes << " decoded_final_live=0" << std::endl;
}

static void TestSourceJsonLoading()
{
  const auto root=FindRepoRoot()/"build/desktop-msvc/s4-source-json-resource-fixture";
  fs::create_directories(root/"blocks");
  const auto write=[](const fs::path &path,const std::string &text) {
    std::ofstream file(path,std::ios::binary);file << text;
  };
  write(root/"pack.json","\xEF\xBB\xBF{\r\n\"id\":\"source_json\",\"name\":\"Source JSON\",\"depends\":[\"base\"],\"ignored\":\""+
      std::string(256*1024,'x')+"\"\r\n}");
  const auto manifest=cutum::UResourcePack::LoadManifest(root);
  FluidTest::Expect(manifest && manifest->Id=="source_json" && manifest->Name=="Source JSON" &&
      manifest->Depends==std::vector<std::string>{"base"},kTestName,"streamed BOM/CRLF manifest retains values after source teardown");
  const std::string block="\xEF\xBB\xBF{\r\n\"name\":\"source_block\",\"id\":548,\"textures\":[\"a\",\"b\",\"c\",\"d\",\"e\",\"f\"],\"ignored\":\""+
      std::string(256*1024,'x')+"\"\r\n}";
  write(root/"blocks/valid.json",block);
  write(root/"blocks/malformed.json","{\"name\":");
  write(root/"blocks/wrong-textures.json",R"({"name":"bad_textures","id":549,"textures":null})");
  auto blocks=cutum::UResourcePack::LoadBlocks(*manifest);
  FluidTest::Expect(blocks.size()==1 && blocks[0].Definition.Name=="source_block" &&
      blocks[0].Definition.Id==548 && blocks[0].TextureStems[0]=="a" && blocks[0].TextureStems[5]=="f",kTestName,
      "streamed block source preserves six stems and skips malformed/invalid files");
  cutum::UBlockDefinitionStorage definitions;
  definitions.Load((root/"blocks").string());
  const auto old=definitions.GetCatalogSnapshot();
  FluidTest::Expect(old->ById.at(548).Name=="source_block" && old->NameToId.at("source_block")==548,kTestName,
      "definition source parses BOM/CRLF without whole-file staging");
  write(root/"blocks/valid.json","{");
  definitions.Load((root/"blocks").string());
  FluidTest::Expect(definitions.GetCatalogSnapshot()->ById.count(548)==0 && old->ById.at(548).Name=="source_block",kTestName,
      "definition reload preserves old reader and existing skip-malformed behavior");
  write(root/"pack.json","{\"id\":");
  FluidTest::Expect(!cutum::UResourcePack::LoadManifest(root),kTestName,"malformed manifest remains rejected");
  std::cout << "source_json_stream_routes=3 bom_crlf=3 long_ignored_bytes=262144 rejected_manifest=1 skipped_block=1 old_definition=1" << std::endl;
}

static void TestResourceCatalogCensus()
{
  const auto root=FindRepoRoot();
  const auto manifest=cutum::UResourcePack::LoadManifest(root/"resource_packs"/"minetest_default_16");
  FluidTest::Expect(manifest.has_value(),kTestName,"resource census real manifest loads");
  auto cache=std::make_shared<cutum::UPlaceholderTextureCache>(root/".resource_census_cache_test",16,glm::vec3(0.4f),3);
  auto merge=std::make_shared<cutum::UBlockMergeRegistry>();merge->Rebuild({*manifest},cache,16);
  merge->SetWorldgenOwnerPackId(std::string(64*1024,'o'));
  for(int n=0;n<9;++n) (void)cache->GetOrCreateStem("census_probe_"+std::to_string(n),n%6,16);
  FluidTest::Expect(cache->GetEntryCount()==3,kTestName,"resource census fixture exercises bounded LRU eviction");
  auto definitions=std::make_shared<cutum::UBlockDefinitionStorage>();merge->PopulateBlockDefinitionStorage(*definitions);
  auto bases=std::make_shared<cutum::UTextureBaseStorage>();
  auto cubes=std::make_shared<cutum::UTextureCubeStorage>(bases);cubes->SetBlockDefinitions(definitions);
  cutum::UBlockRegistry registry(nullptr,definitions);registry.SetMergeRegistry(merge);
  cutum::CatalogResourceRoots resources;
  resources.merge=merge;resources.placeholder=cache;resources.texture_bases=bases;resources.texture_cubes=cubes;resources.block_storage=definitions;
  resources.registry=&registry;
  const auto block_snapshot=definitions->GetCatalogSnapshot();
  const auto before=cutum::MeasureCatalogStorage(nullptr,0,&block_snapshot,1,{},resources);
  cache->RegisterAll(*bases);
  const auto after=cutum::MeasureCatalogStorage(nullptr,0,&block_snapshot,1,{},resources);
  FluidTest::Expect(!after.overflow && after.Objects(cutum::CatalogObjectKind::Merge)==1 &&
      after.Objects(cutum::CatalogObjectKind::Placeholder)==1 && after.Objects(cutum::CatalogObjectKind::TextureBases)==1 &&
      after.Objects(cutum::CatalogObjectKind::TextureCubes)==1 && after.Objects(cutum::CatalogObjectKind::BlockStorage)==1 &&
      after.Objects(cutum::CatalogObjectKind::Blocks)==1 && after.Objects(cutum::CatalogObjectKind::Registry)==1 &&
      after.Objects(cutum::CatalogObjectKind::RegistryIdentity)==1 && after.shared_owners==7,kTestName,
      "merge/direct placeholder and cube/direct base/definition roots deduplicate exact shared objects");
  FluidTest::Expect(after.vector_bytes-before.vector_bytes==3*16*16*4 && after.list_nodes==3 && after.lists==1,
      kTestName,"registered base pixels are independent copied storage and LRU contains only three entries");
  cutum::BlockDefinition overlay;overlay.Name=std::string(64*1024,'r');overlay.Physics=cutum::BlockPhysicsProfile::Solid();
  (void)merge->RegisterRuntimeBlock(overlay,{});
  const auto pending=cutum::MeasureCatalogStorage(nullptr,0,&block_snapshot,1,{},resources);
  FluidTest::Expect(merge->HasRuntimeOverlay() && pending.known_storage_bytes>after.known_storage_bytes &&
      pending.text_bytes>=after.text_bytes+64*1024+1,kTestName,"pending runtime overlay capacity and long owned names are included without flushing");
  const auto repeated=cutum::MeasureCatalogStorage(nullptr,0,&block_snapshot,1,{},resources);
  FluidTest::Expect(repeated.known_storage_bytes==pending.known_storage_bytes && repeated.msvc_tree_extra_bytes==pending.msvc_tree_extra_bytes &&
      repeated.msvc_list_extra_bytes==pending.msvc_list_extra_bytes && cache->GetEntryCount()==3,kTestName,
      "census does not evict/cache new pixels, flush overlays or allocate GPU textures");
  std::cout << "resource_census_known_bytes=" << pending.known_storage_bytes << " unordered_extra=" << pending.msvc_unordered_extra_bytes
      << " tree_extra=" << pending.msvc_tree_extra_bytes << " list_extra=" << pending.msvc_list_extra_bytes
      << " shared_owners=" << pending.shared_owners << " cache_entries=" << cache->GetEntryCount()
      << " pixel_copy_bytes=" << after.vector_bytes-before.vector_bytes << " overlay_pending=1" << std::endl;
}

} // namespace

#include "Test/MergeStagingOwnershipFixture.h"
#include "Test/PlaceholderRecoveryFixture.h"
#include "Test/PlaceholderSourceFixture.h"
#include "Test/MergeDiagnosticOwnershipFixture.h"

int main(int argc,char **argv)
{
  if(argc>1 && std::string(argv[1])=="--merge-diagnostics") return RunMergeDiagnosticOwnershipFixture(argc>2?argv[2]:nullptr,argc>3 && std::string(argv[3])=="baseline");
  if(argc>1 && std::string(argv[1])=="--placeholder-source") return RunPlaceholderSourceFixture(argc>2?argv[2]:nullptr);
  if(argc>1 && std::string(argv[1])=="--placeholder-recovery") return RunPlaceholderRecoveryFixture(argc>2?argv[2]:nullptr,argc>3 && std::string(argv[3])=="baseline");
  if(argc>1 && std::string(argv[1])=="--merge-staging") return RunMergeStagingOwnershipFixture(argc>2?argv[2]:nullptr,argc>3 && std::string(argv[3])=="matrix");
  if(argc>1 && std::string(argv[1])=="--atlas-cpu-baseline-only") {TestAtlasCpuConstruction(false);return 0;}
  if(argc>1 && std::string(argv[1])=="--atlas-cpu-only") {TestAtlasCpuConstruction(true);return 0;}
  if(argc>1 && std::string(argv[1])=="--source-json-load-only") {TestSourceJsonLoading();return 0;}
  if(argc>1 && std::string(argv[1])=="--resource-census-only") {TestResourceCatalogCensus();return 0;}
  if(argc>1 && std::string(argv[1])=="--tier-a-warning-owner-only") {
    TestTierAWarningOwnership(); return 0;
  }
  if(argc>1 && std::string(argv[1])=="--mutating-name-budget-only") {
    TestMutatingRegistryNameBudget(); return 0;
  }
  TestMergeRegistryLookupDoesNotCreateSyntheticBlocks();
  TestScatterResolveUsesPackLookupOnly();
  TestMutatingRegistryNameBudget();
  TestTierAWarningOwnership();
  TestRealPackScatterBlocksResolve();
  TestResourceCatalogCensus();
  TestSourceJsonLoading();
  TestAtlasCpuConstruction(true);
  std::cout << kTestName << ": OK" << std::endl;
  return 0;
}
