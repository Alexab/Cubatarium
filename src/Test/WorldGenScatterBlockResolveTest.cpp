#include "Test/FluidTestHelpers.h"

#include "Blocks/BlockRegistry.h"
#include "ResourcePacks/BlockMergeRegistry.h"
#include "ResourcePacks/ResourcePack.h"
#include "WorldGen/Core/WorldGenBlockResolver.h"
#include "Core/Memory/BufferAllocationBudget.h"

#include <algorithm>
#include <filesystem>
#include <glm/glm.hpp>
#include <iostream>
#include <memory>
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

} // namespace

int main(int argc,char **argv)
{
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
  std::cout << kTestName << ": OK" << std::endl;
  return 0;
}
