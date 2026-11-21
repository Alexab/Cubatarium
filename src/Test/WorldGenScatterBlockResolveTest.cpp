#include "Test/FluidTestHelpers.h"

#include "Blocks/BlockRegistry.h"
#include "ResourcePacks/BlockMergeRegistry.h"
#include "ResourcePacks/ResourcePack.h"
#include "WorldGen/Core/WorldGenBlockResolver.h"
#include "Core/Memory/BufferAllocationBudget.h"

#include <filesystem>
#include <glm/glm.hpp>
#include <iostream>
#include <memory>
#include <string>

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
  if(argc>1 && std::string(argv[1])=="--mutating-name-budget-only") {
    TestMutatingRegistryNameBudget(); return 0;
  }
  TestMergeRegistryLookupDoesNotCreateSyntheticBlocks();
  TestScatterResolveUsesPackLookupOnly();
  TestRealPackScatterBlocksResolve();
  std::cout << kTestName << ": OK" << std::endl;
  return 0;
}
