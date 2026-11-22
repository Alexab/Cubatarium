#include "WorldGen/Core/WorldGenSetsSourceEnvelope.h"
#include <cstddef>
#include <cstdio>
#include <cstdlib>
#include <fstream>
#include <iostream>
#include <new>
#include <nlohmann/json.hpp>

// Isolated single-thread fixture: actual production resolver, ordinary new
// requests only. Inputs/report allocations are outside each observed epoch.
namespace observation
{
struct Counts {size_t epoch{},live{},blocks{},peak{},attempts{},refusals{},max_request{},fail_after{SIZE_MAX},max_single{SIZE_MAX};bool active{};};
inline Counts State;
struct alignas(std::max_align_t) Header {size_t bytes,epoch;};
constexpr size_t Prefix=(sizeof(Header)+15)/16*16;
void *Allocate(size_t bytes) {
  if(bytes>SIZE_MAX-Prefix) throw std::bad_alloc{};
  const size_t epoch=State.active?State.epoch:0;
  if(epoch) {
    State.max_request=std::max(State.max_request,bytes);
    const auto index=State.attempts++;
    if(index>=State.fail_after || bytes>State.max_single) {++State.refusals;throw std::bad_alloc{};}
  }
  auto *memory=static_cast<unsigned char *>(std::malloc(Prefix+std::max(bytes,size_t{1})));
  if(!memory) throw std::bad_alloc{};
  ::new(memory) Header{bytes,epoch};
  if(epoch) {State.live+=bytes;++State.blocks;State.peak=std::max(State.peak,State.live);}
  return memory+Prefix;
}
void Free(void *pointer) noexcept {
  if(!pointer) return;
  auto *memory=static_cast<unsigned char *>(pointer)-Prefix;
  const auto h=*reinterpret_cast<const Header *>(memory);std::free(memory);
  if(h.epoch && h.epoch==State.epoch) {State.live-=h.bytes;--State.blocks;}
}
struct Scope {
  Scope(size_t failure=SIZE_MAX,size_t maxSingle=SIZE_MAX) {
    if(State.active || State.live || State.blocks) std::abort();
    const size_t epoch=State.epoch+1;State={};State.epoch=epoch;State.active=true;
    State.fail_after=failure;State.max_single=maxSingle;
  }
  ~Scope(){State.active=false;}
};
}
void *operator new(size_t n){return observation::Allocate(n);}
void *operator new[](size_t n){return observation::Allocate(n);}
void operator delete(void *p) noexcept {observation::Free(p);}
void operator delete[](void *p) noexcept {observation::Free(p);}
void operator delete(void *p,size_t) noexcept {observation::Free(p);}
void operator delete[](void *p,size_t) noexcept {observation::Free(p);}
void *operator new(size_t n,const std::nothrow_t &) noexcept {try{return observation::Allocate(n);}catch(...){return nullptr;}}
void *operator new[](size_t n,const std::nothrow_t &) noexcept {try{return observation::Allocate(n);}catch(...){return nullptr;}}
void operator delete(void *p,const std::nothrow_t &) noexcept {observation::Free(p);}
void operator delete[](void *p,const std::nothrow_t &) noexcept {observation::Free(p);}

using namespace cutum;
using Json=nlohmann::json;
static bool SameRule(const ObjectFeatureRule &a,const ObjectFeatureRule &b)
{
  // Independent field oracle includes every authored dynamic/scalar field.
  return a.ObjectName==b.ObjectName && a.Biomes==b.Biomes && a.BiomeKeys==b.BiomeKeys &&
    a.Spacing==b.Spacing && a.Weight==b.Weight && a.ChancePerColumn==b.ChancePerColumn &&
    a.SeedOffset==b.SeedOffset && a.PlacementYOffset==b.PlacementYOffset && a.SubBiomes==b.SubBiomes &&
    a.Mode==b.Mode && a.Scatter.BlockName==b.Scatter.BlockName && a.Scatter.Attempts==b.Scatter.Attempts &&
    a.Scatter.Radius==b.Scatter.Radius && a.Scatter.DyOffset==b.Scatter.DyOffset && a.Scatter.MaxPerChunk==b.Scatter.MaxPerChunk &&
    a.Surface.Kind==b.Surface.Kind && a.Surface.NearWaterRadius==b.Surface.NearWaterRadius &&
    a.Tree.HeightMin==b.Tree.HeightMin && a.Tree.HeightMax==b.Tree.HeightMax &&
    a.Tree.CrownRadiusX==b.Tree.CrownRadiusX && a.Tree.CrownRadiusZ==b.Tree.CrownRadiusZ && a.Tree.CrownHeight==b.Tree.CrownHeight &&
    a.Tree.BranchCount==b.Tree.BranchCount && a.Tree.CrownLobes==b.Tree.CrownLobes && a.Tree.LeafDensity==b.Tree.LeafDensity &&
    a.Tree.RootChance==b.Tree.RootChance && a.Tree.CrownIrregularity==b.Tree.CrownIrregularity && a.Tree.CrownShape==b.Tree.CrownShape &&
    a.Tree.TrunkBlock==b.Tree.TrunkBlock && a.Tree.LeavesBlock==b.Tree.LeavesBlock &&
    a.Shrub.HeightMin==b.Shrub.HeightMin && a.Shrub.HeightMax==b.Shrub.HeightMax &&
    a.Shrub.RadiusX==b.Shrub.RadiusX && a.Shrub.RadiusZ==b.Shrub.RadiusZ && a.Shrub.LobeCount==b.Shrub.LobeCount &&
    a.Shrub.LeafDensity==b.Shrub.LeafDensity && a.Shrub.CrownIrregularity==b.Shrub.CrownIrregularity && a.Shrub.AccentChance==b.Shrub.AccentChance &&
    a.Shrub.StemBlock==b.Shrub.StemBlock && a.Shrub.LeavesBlock==b.Shrub.LeavesBlock && a.Shrub.AccentBlock==b.Shrub.AccentBlock &&
    a.Ecology.HasTemperatureRange==b.Ecology.HasTemperatureRange && a.Ecology.HasMoistureRange==b.Ecology.HasMoistureRange &&
    a.Ecology.TemperatureMin==b.Ecology.TemperatureMin && a.Ecology.TemperatureMax==b.Ecology.TemperatureMax &&
    a.Ecology.MoistureMin==b.Ecology.MoistureMin && a.Ecology.MoistureMax==b.Ecology.MoistureMax &&
    a.Ecology.ElevationRef==b.Ecology.ElevationRef && a.Ecology.MinElevation==b.Ecology.MinElevation &&
    a.Ecology.MaxElevation==b.Ecology.MaxElevation && a.Ecology.MaxSlope==b.Ecology.MaxSlope;
}
static bool Verify(const ObjectFeatureConfig &actual,const ObjectFeatureConfig &expected)
{
  const auto pool=[](const auto &a,const auto &b) {
    return a.size()==b.size() && std::equal(a.begin(),a.end(),b.begin(),SameRule);
  };
  return pool(actual.Vegetation,expected.Vegetation) && pool(actual.GroundCover,expected.GroundCover) &&
    pool(actual.Decoration,expected.Decoration) && pool(actual.Structures,expected.Structures) &&
    actual.CaveFeatures.size()==1 && actual.CaveFeatures[0].BiomeKeyName==expected.CaveFeatures[0].BiomeKeyName &&
    actual.CaveFeatures[0].BlockName==expected.CaveFeatures[0].BlockName &&
    actual.CaveFeatures[0].Biome==expected.CaveFeatures[0].Biome && actual.CaveFeatures[0].Kind==expected.CaveFeatures[0].Kind &&
    actual.CaveFeatures[0].Radius==expected.CaveFeatures[0].Radius && actual.CaveFeatures[0].MaxHeight==expected.CaveFeatures[0].MaxHeight &&
    actual.CaveFeatures[0].SeedOffset==expected.CaveFeatures[0].SeedOffset &&
    actual.CaveFeatures[0].Chance==expected.CaveFeatures[0].Chance &&
    actual.VegetationDistribution.RegionalNoiseScale==expected.VegetationDistribution.RegionalNoiseScale &&
    actual.VegetationDistribution.MinDensityMultiplier==expected.VegetationDistribution.MinDensityMultiplier &&
    actual.VegetationDistribution.MaxDensityMultiplier==expected.VegetationDistribution.MaxDensityMultiplier &&
    actual.GroundCoverDistribution.RegionalNoiseScale==expected.GroundCoverDistribution.RegionalNoiseScale &&
    actual.GroundCoverDistribution.MinDensityMultiplier==expected.GroundCoverDistribution.MinDensityMultiplier &&
    actual.GroundCoverDistribution.MaxDensityMultiplier==expected.GroundCoverDistribution.MaxDensityMultiplier &&
    actual.structureCellSize==expected.structureCellSize && actual.structureChancePerCell==expected.structureChancePerCell &&
    actual.structureMinSpacing==expected.structureMinSpacing && !actual.Diagnostics.Snapshot();
}
#include "Test/WorldGenSetsInputEnvelopeFixture.h"
#include "Test/SourceInputStorageCensusFixture.h"
#include "Test/CatalogFileLifetimeFixture.h"
#include "Test/CatalogFilesystemSourceFixture.h"
int main(int argc,char **argv)
{
  if(argc>1 && std::string(argv[1])=="--settings-input") return RunWorldGenSetsInputEnvelopeFixture(argc>2?argv[2]:nullptr);
  if(argc>1 && std::string(argv[1])=="--retained-inputs") return RunSourceInputStorageCensusFixture(argc>2?argv[2]:nullptr);
  if(argc>1 && std::string(argv[1])=="--catalog-files") return RunCatalogFileLifetimeFixture(argc>2?argv[2]:nullptr);
  if(argc>1 && std::string(argv[1])=="--filesystem-source") return RunCatalogFilesystemSourceFixture(argc>2?argv[2]:nullptr);
  ObjectFeatureConfig global;
  ObjectFeatureRule rule;rule.ObjectName="selected";rule.Biomes={BiomeId::Plains};rule.BiomeKeys={"plains"};rule.SubBiomes={SubBiomeIdFromString("none")};
  rule.Spacing=17;rule.Weight=7;rule.ChancePerColumn=13;rule.SeedOffset=29;rule.PlacementYOffset=-4;
  rule.Tree.TrunkBlock=std::string(129,'t');rule.Shrub.AccentBlock=std::string(257,'a');
  rule.Ecology.MinElevation=-47;rule.Ecology.MaxElevation=61;
  global.Vegetation=global.GroundCover=global.Decoration=global.Structures={rule};
  CaveFeatureRule cave;cave.BiomeKeyName=std::string(95,'k');cave.BlockName=std::string(151,'c');cave.Chance=77;global.CaveFeatures={cave};
  global.VegetationDistribution={0.3f,0.4f,0.9f};global.GroundCoverDistribution={0.2f,0.6f,1.4f};
  global.structureCellSize=83;global.structureChancePerCell=19;global.structureMinSpacing=151;
  // Warm read-only enum lookup before observed construction.
  (void)BiomeIdFromString("plains");
  Json report={{"schema","worldgen-sets-construction-source.v1"},{"status","PASS"},{"cases",Json::array()}};
  size_t failures=0;
  for(int schema=1;schema<=3;++schema) for(int mask=0;mask<16;++mask) {
    WorldGenSets sets;sets.SchemaVersion=schema;
    WorldGenObjectEntry entry;entry.ObjectName=rule.ObjectName;entry.Biomes={"plains"};entry.SubBiomes={"none"};
    entry.Spacing=rule.Spacing;entry.Weight=rule.Weight;entry.ChancePerColumn=rule.ChancePerColumn;
    entry.SeedOffset=rule.SeedOffset;entry.PlacementYOffset=rule.PlacementYOffset;
    entry.Tree=rule.Tree;entry.Shrub=rule.Shrub;entry.Ecology=rule.Ecology;
    auto input=global;auto expected=global;
    auto select=[&](int bit,auto &sections,auto &fallback,auto &expect) {
      if(mask&(1<<bit)) {sections["selected"].Entries={entry,WorldGenObjectEntry{}};fallback[0].ObjectName=std::string(256*1024,'z');}
      // Legacy metadata lookup deliberately does not match the replaced name.
      (void)expect;
    };
    select(0,sets.VegetationSections,input.Vegetation,expected.Vegetation);
    select(1,sets.GroundCoverSections,input.GroundCover,expected.GroundCover);
    select(2,sets.DecorationSections,input.Decoration,expected.Decoration);
    select(3,sets.StructureSections,input.Structures,expected.Structures);
    const auto bound=BoundResolvedFeaturesSource(sets,input,2);
    bool ok=bound.supported && !bound.overflow;observation::Counts counts;
    {
      observation::Scope scope(SIZE_MAX,64*1024);
      try {auto resolved=ResolveObjectFeatures(sets,input,2);ok=ok&&Verify(resolved,expected);}
      catch(const std::bad_alloc &) {ok=false;}
      counts=observation::State;
    }
    ok=ok && counts.live==0 && counts.blocks==0 && counts.peak<=bound.requested_heap_upper;
    report["cases"].push_back({{"schema",schema},{"override_mask",mask},{"pass",ok},{"source_upper",bound.requested_heap_upper},
      {"peak",counts.peak},{"max_request",counts.max_request},{"attempts",counts.attempts},{"refusals",counts.refusals},{"final_live",counts.live}});
    if(!ok) ++failures;
    // Every persistent ordinary-new refusal boundary, exact input retry next.
    if(ok && mask==15 && schema==3) for(size_t fail=0;fail<counts.attempts;++fail) {
      bool refused=false;
      {observation::Scope scope(fail);try{auto resolved=ResolveObjectFeatures(sets,input,2);}catch(const std::bad_alloc &){refused=true;}}
      if(!refused || observation::State.live || observation::State.blocks || observation::State.peak>bound.requested_heap_upper) ++failures;
      {observation::Scope scope;auto resolved=ResolveObjectFeatures(sets,input,2);if(!Verify(resolved,expected)) ++failures;}
      if(observation::State.live || observation::State.blocks) ++failures;
      report["persistent_refusal_and_retry_cases"]=fail+1;
    }
  }
  report["additional_cases"]=Json::array();
  const auto run=[&](const char *name,const WorldGenSets &sets,const ObjectFeatureConfig &input,
                     const ObjectFeatureConfig &expected,int profile,size_t ceiling=SIZE_MAX) {
    const auto bound=BoundResolvedFeaturesSource(sets,input,profile);
    bool ok=bound.supported && !bound.overflow;observation::Counts counts;
    {observation::Scope scope(SIZE_MAX,ceiling);
      try {auto resolved=ResolveObjectFeatures(sets,input,profile);ok=ok&&Verify(resolved,expected);}
      catch(const std::bad_alloc &){ok=false;}
      counts=observation::State;
    }
    ok=ok && counts.live==0 && counts.blocks==0 && counts.peak<=bound.requested_heap_upper;
    if(!ok) ++failures;
    report["additional_cases"].push_back({{"name",name},{"pass",ok},{"source_upper",bound.requested_heap_upper},
      {"peak",counts.peak},{"max_request",counts.max_request},{"final_live",counts.live}});
  };
  {
    WorldGenSets sets;WorldGenObjectEntry entry;entry.ObjectName=std::string(4097,'n');
    entry.Biomes={"plains"};for(int i=0;i<31;++i) entry.Biomes.push_back(std::string(257,'b')+std::to_string(i));
    entry.SubBiomes={"woodland","unknown"};
    entry.Tree.TrunkBlock=std::string(8193,'l');entry.Shrub.AccentBlock=std::string(5001,'q');
    sets.VegetationSections["wide"].Entries.assign(129,entry);
    ObjectFeatureRule expectedRule;expectedRule.ObjectName=entry.ObjectName;expectedRule.BiomeKeys=entry.Biomes;
    expectedRule.Biomes={BiomeId::Plains};expectedRule.SubBiomes={SubBiomeIdFromString("woodland"),SubBiomeIdFromString("unknown")};
    expectedRule.Tree=entry.Tree;expectedRule.Shrub=entry.Shrub;
    auto expected=global;expected.Vegetation.assign(129,expectedRule);
    run("wide_vectors_long_keys_profiles",sets,global,expected,2);
  }
  for(int profile=1;profile<=2;++profile) {
    WorldGenSets sets;sets.SchemaVersion=1;WorldGenObjectEntry entry;entry.ObjectName="selected";
    entry.Spacing=37;entry.Weight=11;entry.Tree.TrunkBlock=std::string(5001,'u');
    sets.VegetationSections["legacy"].Entries={entry};
    auto input=global;input.Vegetation[0].Mode=ObjectFeatureRuleMode::ProceduralTree;
    input.Vegetation[0].Tree.LeavesBlock=std::string(10001,'v');input.Vegetation[0].Surface.Kind=SurfaceConstraintKind::Grass;
    input.Vegetation[0].Ecology.MinElevation=-99;
    auto expected=input;ObjectFeatureRule r;r.ObjectName=entry.ObjectName;r.Spacing=37;r.Weight=11;r.Tree=entry.Tree;
    if(profile==2) {r.Mode=input.Vegetation[0].Mode;r.Tree=input.Vegetation[0].Tree;r.Shrub=input.Vegetation[0].Shrub;
      r.Ecology=input.Vegetation[0].Ecology;r.Surface=input.Vegetation[0].Surface;}
    expected.Vegetation={r};
    run(profile==1?"legacy_profile1_preserved":"legacy_profile2_restored",sets,input,expected,profile);
  }
  {
    WorldGenSets sets;sets.SchemaVersion=1;WorldGenObjectEntry entry;entry.Mode=ObjectFeatureRuleMode::ScatterBlocks;
    entry.Scatter.BlockName="stone";entry.Scatter.Attempts=17;entry.Spacing=41;
    sets.DecorationSections["scatter"].Entries={entry};
    auto input=global;auto authored=rule;authored.Mode=ObjectFeatureRuleMode::ScatterBlocks;authored.Scatter.BlockName="stone";
    authored.Tree.TrunkBlock=std::string(7001,'r');input.Decoration.push_back(authored);
    auto expected=input;ObjectFeatureRule r;r.Mode=entry.Mode;r.Scatter=entry.Scatter;r.Spacing=41;
    r.Tree=authored.Tree;r.Shrub=authored.Shrub;r.Ecology=authored.Ecology;r.Surface=authored.Surface;expected.Decoration={r};
    run("legacy_scatter_lookup_and_user_frequency",sets,input,expected,2);
  }
  {
    WorldGenSets sets;sets.VegetationSections["empty"].Entries={WorldGenObjectEntry{}};
    auto input=global;input.Vegetation[0].ObjectName=std::string(256*1024,'x');
    auto expected=global;expected.Vegetation.clear();
    run("nonempty_invalid_pool_suppresses_fallback",sets,input,expected,2,64*1024);
    const auto bound=BoundResolvedFeaturesSource(sets,input,2);
    {observation::Scope scope;void *extra=::operator new(bound.requested_heap_upper+1);::operator delete(extra);}
    const bool detected=observation::State.peak>bound.requested_heap_upper && observation::State.live==0;
    report["unmodeled_allocation_negative_control"]=detected?"PASS":"FAIL";if(!detected) ++failures;
  }
  report["status"]=failures?"FAIL":"PASS";report["failed_cases"]=failures;
  report["scope"]="Actual resolver ordinary-new requests;48 schema/source combinations,unused256KiB global names,64KiB diagnostic single-request ceiling (not production limit),persistent refusal+retry;not parser/file/candidate publication/process cap";
  if(argc>1) {std::ofstream file(argv[1]);file<<report.dump(2)<<'\n';}
  std::cout<<report.dump()<<std::endl;return failures?1:0;
}
