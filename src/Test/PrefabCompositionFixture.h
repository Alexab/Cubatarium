#pragma once
#include "Test/CatalogBoundProfileFixture.h"
#include "App/Platform/Log.h"
#include "Test/OrdinaryNewObservation.h"
#include "World/Objects/ObjectLibrary.h"
#include "WorldGen/Core/PrefabConstructionComposition.h"
#include "ResourcePacks/BlockNameUtil.h"
#include <atomic>
#include <array>
#include <map>
#include <thread>
#include <nlohmann/json.hpp>
#include <filesystem>
#include <fstream>

namespace prefab_composition_fixture
{
namespace fs=std::filesystem;using namespace cutum;using Json=nlohmann::json;
inline void Write(const fs::path &path,const Json &value){fs::create_directories(path.parent_path());std::ofstream out(path);out<<value.dump();}
inline void Define(UBlockDefinitionStorage &storage,BlockId stone,bool leaf=true)
{
  BlockDefinition a;a.Id=stone;a.Name="stone";BlockDefinition b;b.Id=static_cast<BlockId>(stone+1);b.Name="leaf";
  std::unordered_map<BlockId,BlockDefinition> defs{{a.Id,a}};std::unordered_map<std::string,BlockId> names{{a.Name,a.Id}};
  BlockDefinition fixed;fixed.Id=7;fixed.Name="fixed";defs.emplace(fixed.Id,fixed);names.emplace(fixed.Name,fixed.Id);
  if(leaf){defs.emplace(b.Id,b);names.emplace(b.Name,b.Id);}storage.ReplaceAll(std::move(defs),std::move(names));
}
inline Json Object(int offset=0)
{return {{"displayName",std::string(129,'d')},{"tags",Json::array({"fixture"})},{"blocks",Json::array({{{"dx",offset},{"dy",0},{"dz",0},{"type","stone"}}})}};}
inline int Baseline(const char *path)
{
  const auto root=fs::path("build/desktop-msvc/s4-prefab-composition-fixtures/baseline");Write(root/"old/item.json",Object());Write(root/"new/item.json",Object(7));
  CubatariumSetSuppressErrorDialogs(true);auto defs=std::make_shared<UBlockDefinitionStorage>();Define(*defs,101);UBlockRegistry registry(nullptr,defs);
  UObjectLibrary library;library.LoadMerged(root/"old",{},registry);library.LoadMerged(root/"old",{},registry);
  const auto escaped=library.GetShared("item");const auto before=escaped->voxels[0].Id;
  Define(*defs,201);registry.Reload();library.RebindBlockIds(registry);const auto after=escaped->voxels[0].Id;
  const bool mutated=before==101 && after==201 && escaped==library.GetShared("item");
  library.LoadMerged(root/"old",{},registry);bool refused=false;
  const auto newRoot=root/"new";
  {observation::Scope scope(0);try{library.LoadMerged(newRoot,{},registry);}catch(const std::bad_alloc &){refused=true;}}
  const bool lost=refused && !library.GetShared("item");
  Json report={{"schema","prefab-composition-baseline.v1"},{"status",mutated && lost?"REPRODUCED":"FAIL"},{"escaped_id_before",before},{"escaped_id_after",after},{"escaped_definition_mutated",mutated},{"prior_index_lost_under_refusal",lost},{"refusal_prefix",0}};
  if(path){std::ofstream out(path);out<<report.dump(2)<<'\n';}std::cout<<report.dump()<<std::endl;return mutated && lost?0:1;
}
inline Json Payload(const WorldObjectDefinition &v)
{
  const auto xyz=[](const glm::ivec3 &p){return Json::array({p.x,p.y,p.z});};
  Json blocks=Json::array();for(const auto &b:v.voxels)blocks.push_back({{"offset",xyz(b.offset)},{"id",b.Id},{"type",b.Type}});
  return {{"name",v.Name},{"display",v.DisplayName},{"tags",v.Tags},{"origin",static_cast<int>(v.Origin)},
      {"pack",v.SourcePackId},{"hidden",v.Hidden},{"anchor",xyz(v.anchor)},{"min",xyz(v.boundsMin)},
      {"max",xyz(v.boundsMax)},{"mode",static_cast<int>(v.PlacementMode)},{"y",v.PlacementYOffset},{"blocks",blocks}};
}
struct SnapshotInspector
{
  std::map<std::string,std::shared_ptr<const WorldObjectDefinition>> objects;
  std::map<std::string,Json> payloads;size_t diagnostics{};
  template<class T>void Map(const T &v){if constexpr(std::is_same_v<typename T::value_type,std::string>)diagnostics=v.size();}
  void Text(const std::string &){}
  template<class T>void Prefab(const std::shared_ptr<T> &v){if(v){objects.emplace(v->Name,v);payloads.emplace(v->Name,Payload(*v));}}
};
inline SnapshotInspector Snapshot(const UObjectLibrary &v){SnapshotInspector out;v.InspectStorage(out);return out;}
inline bool Same(const UObjectLibrary &v,const SnapshotInspector &before)
{const auto after=Snapshot(v);return after.objects==before.objects && after.payloads==before.payloads && after.diagnostics==before.diagnostics;}
inline Json RichObject(int offset)
{
  auto v=Object(offset);v["tags"]=Json::array({std::string(65,'t'),"fixture"});v["anchor"]=Json::array({3,4,5});
  v["placement"]={{"mode","vertical_plant"},{"y_offset",-3}};
  v["blocks"].push_back({{"dx",offset+2},{"dy",1},{"dz",-1},{"type","leaf"}});return v;
}
struct Inputs
{
  fs::path oldRoot,newRoot,emptyRoot,errorRoot;std::vector<ResourcePackManifest> packs;std::string packKey;
};
inline Inputs Prepare()
{
  Inputs in;const auto root=fs::path("build/desktop-msvc/s4-prefab-composition-fixtures/accepted");
  in.oldRoot=root/"old";in.newRoot=root/"new";in.emptyRoot=root/"empty";in.errorRoot=root/"not_directory";
  Write(in.oldRoot/"item.json",RichObject(0));auto stable=Object(2);stable["blocks"][0]["type"]="fixed";Write(in.oldRoot/"stable.json",stable);
  auto other=RichObject(4);other["blocks"].push_back({{"dx",0},{"dy",0},{"dz",0},{"type",std::string(79,'u')}});Write(in.oldRoot/"other.json",other);
  Write(in.newRoot/"item.json",RichObject(7));Write(in.newRoot/"imported/nested/item.json",RichObject(9));
  Write(in.newRoot/"user/deep/item.json",RichObject(11));auto hidden=RichObject(-5);hidden["visibility"]="hidden";Write(in.newRoot/"hidden.json",hidden);
  Write(in.newRoot/"empty.json",{{"blocks",Json::array()}});
  Write(in.newRoot/"unknown.json",{{"blocks",Json::array({{{"dx",0},{"dy",0},{"dz",0},{"type",std::string(79,'u')}},{{"dx",1},{"dy",0},{"dz",0},{"type",std::string(79,'u')}}})}});
  fs::create_directories(in.newRoot/"user/deep/empty/deeper");fs::create_directories(in.emptyRoot);
  Write(in.newRoot/"ignored/deep/ignored.json",Object());Write(in.newRoot/"uppercase.JSON",Object());
  {std::ofstream out(in.newRoot/"malformed.json");out<<"{\"blocks\":[";}
  {std::ofstream out(in.errorRoot);out<<"regular file";}
  for(int i=0;i<3;++i){ResourcePackManifest p;p.Id=std::string(81,'p');p.Root=root/("pack"+std::to_string(i));
    if(i<2)Write(p.Root/"objects/nested/pack_item.json",RichObject(i==0?17:23));in.packs.push_back(std::move(p));}
  in.packKey=MakeQualifiedBlockName(in.packs[0].Id,"pack_item");return in;
}
inline PrefabDirectoryDomain Inventory(const fs::path &base,const std::vector<ResourcePackManifest> &packs)
{
  PrefabDirectoryDomain d;std::vector<size_t> sizes;
  const auto scan=[&](const fs::path &root,bool recursive,size_t prefix){++d.scans;d.prefix_bytes=std::max(d.prefix_bytes,prefix);d.max_path_units=std::max(d.max_path_units,root.native().size());
    if(!fs::exists(root))return;
    const auto entry=[&](const auto &value,size_t depth){d.max_path_units=std::max(d.max_path_units,value.path().native().size());d.max_depth=std::max(d.max_depth,depth);
      if(value.is_regular_file() && value.path().extension()==".json")sizes.push_back(static_cast<size_t>(fs::file_size(value.path())));};
    if(recursive)for(auto it=fs::recursive_directory_iterator(root);it!=fs::recursive_directory_iterator();++it)entry(*it,static_cast<size_t>(it.depth())+1);
    else for(const auto &value:fs::directory_iterator(root))entry(value,1);
  };
  scan(base,false,0);scan(base/"imported",true,0);scan(base/"user",true,0);for(const auto &p:packs)scan(p.Root/"objects",true,p.Id.size());
  // Collect global maxima first: every candidate can use fallback path/name.
  for(const auto size:sizes)AddPrefabDirectoryFile(d,size);return d;
}
inline bool OldValues(const UObjectLibrary &v,BlockId id)
{const auto a=v.GetShared("item"),b=v.GetShared("stable");return a && b && a->voxels.size()==2 && a->voxels[0].Id==id && a->voxels[1].Id==id+1 && a->voxels[0].offset.x==0 && b->voxels[0].Id==7;}
inline bool NewValues(const UObjectLibrary &v,const Inputs &in,BlockId id,bool leaf)
{
  const auto s=Snapshot(v);const auto a=v.GetShared("item"),b=v.GetShared("hidden"),p=v.GetShared(in.packKey);const auto names=v.ListNames();
  return s.objects.size()==3 && s.diagnostics==(leaf?1:4) && a && b && p && a->Origin==ObjectOrigin::User && a->voxels[0].offset.x==11 &&
      b->Hidden && b->Origin==ObjectOrigin::Builtin && std::find(names.begin(),names.end(),"hidden")==names.end() && names.size()==2 &&
      p->SourcePackId==in.packs[0].Id && p->Origin==ObjectOrigin::ResourcePack && p->voxels[0].offset.x==23 &&
      a->voxels.size()==(leaf?2:1) && a->voxels[0].Id==id && p->voxels[0].Id==id && !v.GetShared("ignored") && !v.GetShared("uppercase") && !v.GetShared("empty");
}
inline int CurrentRoute=-1;inline size_t CurrentPrefix=SIZE_MAX;
inline size_t CheckEpoch(size_t failure=SIZE_MAX)
{if(observation::State.live || observation::State.blocks || observation::State.active)std::fprintf(stderr,"prefab epoch entry route=%d prefix=%zu live=%zu blocks=%zu active=%d\n",CurrentRoute,CurrentPrefix,observation::State.live,observation::State.blocks,int(observation::State.active));return failure;}
template<class Operation>inline observation::Counts ObserveWorker(Operation operation,size_t failure,bool &threw)
{
  observation::Counts counts;
  std::thread worker([&]{
    {observation::Scope scope(CheckEpoch(failure));try{operation();}catch(const std::bad_alloc &){threw=true;}}
    counts=observation::State;
  });worker.join();return counts;
}
inline int Run(const char *path,bool owner_binding=false)
{
  const auto previousTerminate=std::set_terminate([]{const auto &s=observation::State;std::fprintf(stderr,"prefab terminate route=%d prefix=%zu attempts=%zu refusals=%zu live=%zu blocks=%zu active=%d\n",CurrentRoute,CurrentPrefix,s.attempts,s.refusals,s.live,s.blocks,int(s.active));std::fflush(stderr);std::_Exit(86);});
  CubatariumSetSuppressErrorDialogs(true);const auto input=Prepare();auto defs=std::make_shared<UBlockDefinitionStorage>();Define(*defs,101);UBlockRegistry registry(nullptr,defs);
  {UObjectLibrary warm;warm.LoadMerged(input.oldRoot,{},registry);warm.LoadMerged(input.newRoot,input.packs,registry);}
  Json report={{"schema","prefab-construction-composition.v1"},{"phases",Json::array()},{"refusals",Json::array()}};size_t failures=0,total=0;
  const auto domain=Inventory(input.newRoot,input.packs);const auto loadBound=BoundPrefabDirectorySource(domain);
  report["directory_domain"]={{"files",domain.files},{"scans",domain.scans},{"max_path_units",domain.max_path_units},{"depth_including_empty",domain.max_depth},{"prefix_bytes",domain.prefix_bytes}};
  // Selected request graph: library/escaped definitions are observed. Registry,
  // manifests and caller path strings are separately borrowed source inputs.
  std::shared_ptr<UObjectLibrary> library;std::array<std::shared_ptr<const WorldObjectDefinition>,4> escaped{};
  {observation::Scope scope(CheckEpoch());library=std::make_shared<UObjectLibrary>();library->LoadMerged(input.oldRoot,{},registry);}
  escaped[0]=library->GetShared("item");escaped[3]=library->GetShared("stable");const auto original=Payload(*escaped[0]);
  const auto census=[&](){detail::CatalogStorageInspector out;out.Library(library);for(const auto &v:escaped)out.Prefab(v);return out.Result;};
  catalog_bound_profile_fixture::Profile profile(BoundRetainedCatalogRequests(census()));
  const auto phase=[&](const char *name,const CatalogCandidateEnvelope &source,auto operation){
    const auto entry=BoundRetainedCatalogRequests(census());const auto bound=ComposeCatalogConstruction(entry,source.requested_heap_upper,source.supported,source.overflow);
    const auto ownerBound=profile.Phase(entry,source.requested_heap_upper,source.supported,source.overflow);
    const auto observed=observation::State.live;observation::State.peak=observed;observation::State.active=true;
    operation();observation::State.active=false;const auto peak=observation::State.peak;
    const bool ok=bound.supported && !bound.overflow && observed<=entry.requested_heap_upper && peak<=bound.requested_heap_upper &&
      (!owner_binding || (ownerBound.supported && !ownerBound.overflow && peak<=ownerBound.requested_heap_upper));
    if(!ok)++failures;report["phases"].push_back({{"name",name},{"pass",ok},{"entry_observed",observed},{"entry_upper",entry.requested_heap_upper},{"peak",peak},{"upper",bound.requested_heap_upper},{"owner_profile_upper",ownerBound.requested_heap_upper}});
  };
  Define(*defs,201,false);registry.Reload();const auto rebindBound=BoundPrefabRebindSource(InspectPrefabRebindSource(*library));
  phase("rebind_current_library",rebindBound,[&]{library->RebindBlockIds(registry);});escaped[1]=library->GetShared("item");
  const auto rebound=Payload(*escaped[1]);
  const bool rebindOracle=Payload(*escaped[0])==original && escaped[0]!=escaped[1] && escaped[1]->voxels[0].Id==201 && escaped[1]->voxels[1].Id==BLOCK_AIR &&
      escaped[1]->boundsMin==escaped[0]->boundsMin && escaped[1]->boundsMax==escaped[0]->boundsMax && escaped[3]==library->GetShared("stable");
  phase("merged_directory_publication",loadBound,[&]{library->LoadMerged(input.newRoot,input.packs,registry);});escaped[2]=library->GetShared("item");
  const bool graphOracle=rebindOracle && Payload(*escaped[0])==original && Payload(*escaped[1])==rebound && NewValues(*library,input,201,false);
  if(!graphOracle)++failures;report["old_current_oracles"]=graphOracle;
  const auto retained=BoundRetainedCatalogRequests(census());const auto retainedObserved=observation::State.live;
  detail::CatalogStorageInspector single,many;single.Prefab(escaped[0]);for(int i=0;i<64;++i)many.Prefab(escaped[0]);
  const bool dedup=single.Result.known_storage_bytes==many.Result.known_storage_bytes && single.Result.shared_owners==many.Result.shared_owners;
  // Inspectors own weak controls;retire them before clearing the selected graph.
  single={};many={};if(!dedup || retainedObserved>retained.requested_heap_upper)++failures;
  report["retained"]={{"observed",retainedObserved},{"upper",retained.requested_heap_upper},{"alias64_deduplicated",dedup}};
  if(owner_binding){auto checks=profile.Controls(retained);if(!checks["pass"].get<bool>())++failures;report["owner_profile_binding"]=checks;}
  escaped={};library.reset();const bool retired=!observation::State.live && !observation::State.blocks;if(!retired)++failures;
  report["graph_final_zero"]=retired;
  if(!retired){report["remaining_bytes"]=observation::State.live;report["status"]="FAIL";if(path){std::ofstream out(path);out<<report.dump(2)<<'\n';}return 1;}
  // Each route runs all ordinary-new refusal prefixes. Every retry starts
  // from the same old publication;reporting/caller argument creation is outside.
  const std::vector<ResourcePackManifest> noPacks;
  for(int route=0;route<3;++route){
    CurrentRoute=route;CurrentPrefix=SIZE_MAX;
    Define(*defs,101);registry.Reload();auto target=std::make_unique<UObjectLibrary>();target->LoadMerged(input.oldRoot,{},registry);
    if(route==2){Define(*defs,201,false);registry.Reload();}
    const auto source=route==2?BoundPrefabRebindSource(InspectPrefabRebindSource(*target)):route==0?loadBound:BoundPrefabDirectorySource(Inventory(input.emptyRoot,{}));
    const auto operation=[&](){if(route==2)target->RebindBlockIds(registry);else target->LoadMerged(route==0?input.newRoot:input.emptyRoot,route==0?input.packs:noPacks,registry);};
    // Keep input pack vector borrowing identical to production in the tested
    // operation;the empty vector has no heap and no synthetic source copies.
    bool normalThrew=false;const auto normal=ObserveWorker(operation,SIZE_MAX,normalThrew);
    const bool values=route==0?NewValues(*target,input,101,true):route==1?Snapshot(*target).objects.empty():target->GetShared("item")->voxels[0].Id==201 && target->GetShared("item")->voxels[1].Id==BLOCK_AIR;
    target.reset();bool ok=!normalThrew && values && source.supported && !source.overflow && normal.peak<=source.requested_heap_upper && !observation::State.live && !observation::State.blocks;
    size_t checked=0;
    if(ok)for(size_t i=0;i<normal.attempts;++i){
      CurrentPrefix=i;
      Define(*defs,101);registry.Reload();target=std::make_unique<UObjectLibrary>();target->LoadMerged(input.oldRoot,{},registry);
      const auto before=Snapshot(*target);if(route==2){Define(*defs,201,false);registry.Reload();}bool threw=false;
      const auto failed=ObserveWorker(operation,i,threw);const bool preserved=Same(*target,before);
      if(!threw || !failed.refusals || !preserved || failed.peak>source.requested_heap_upper || observation::State.live || observation::State.blocks){ok=false;report["first_failure"]={{"route",route},{"prefix",i},{"threw",threw},{"refusals",failed.refusals},{"preserved",preserved},{"live",observation::State.live},{"blocks",observation::State.blocks}};break;}
      bool retryThrew=false;const auto retryCounts=ObserveWorker(operation,SIZE_MAX,retryThrew);
      const bool retry=route==0?NewValues(*target,input,101,true):route==1?Snapshot(*target).objects.empty():target->GetShared("item")->voxels[0].Id==201 && Payload(*before.objects.at("item"))==before.payloads.at("item") && target->GetShared("stable")==before.objects.at("stable");
      target.reset();if(retryThrew || !retry || retryCounts.peak>source.requested_heap_upper || observation::State.live || observation::State.blocks){ok=false;report["retry_failure"]={{"route",route},{"prefix",i},{"live",observation::State.live},{"blocks",observation::State.blocks}};break;}++checked;
    }
    target.reset();if(!ok)++failures;total+=checked;report["refusals"].push_back({{"route",route},{"pass",ok},{"prefixes",checked},{"attempts",normal.attempts},{"peak",normal.peak},{"upper",source.requested_heap_upper}});
    if(!ok)break;
  }
  if(observation::State.live || observation::State.blocks){
    report["unexpected_retained"]={{"bytes",observation::State.live},{"blocks",observation::State.blocks}};
    report["status"]="FAIL";std::set_terminate(previousTerminate);if(path){std::ofstream out(path);out<<report.dump(2)<<'\n';}return 1;
  }
  // Unchanged definitions retain identity;the already-recorded missing-type
  // diagnostic does not grow. Pure unchanged known-ID rebind allocates zero.
  Define(*defs,101);registry.Reload();UObjectLibrary unchanged;unchanged.LoadMerged(input.oldRoot,{},registry);const auto unchangedBefore=Snapshot(unchanged);
  {observation::Scope scope(CheckEpoch(0));unchanged.RebindBlockIds(registry);}const bool noOp=Same(unchanged,unchangedBefore) && !observation::State.attempts;if(!noOp)++failures;report["unchanged_zero_allocation"]=noOp;
  bool fsThrew=false;try{unchanged.LoadMerged(input.errorRoot,{},registry);}catch(const fs::filesystem_error &){fsThrew=true;}
  const bool fsPreserved=fsThrew && Same(unchanged,unchangedBefore);if(!fsPreserved)++failures;report["filesystem_error_preserves_prior"]=fsPreserved;
  Define(*defs,201,false);registry.Reload();unchanged.RebindBlockIds(registry);const auto missingBefore=Snapshot(unchanged);
  {observation::Scope scope(CheckEpoch(0));unchanged.RebindBlockIds(registry);}
  const bool diagnosticDedup=Same(unchanged,missingBefore) && missingBefore.diagnostics==3 && !observation::State.attempts;
  if(!diagnosticDedup)++failures;report["repeated_missing_type_diagnostics_deduplicated"]=diagnosticDedup;
  Define(*defs,101);registry.Reload();unchanged.LoadMerged(input.oldRoot,{},registry);
  PrefabDirectoryDomain overflow=domain;overflow.scans=SIZE_MAX;PrefabRebindInput bad;bad.supported=true;bad.objects=SIZE_MAX;
  const bool overflowRejected=BoundPrefabDirectorySource(overflow).overflow && BoundPrefabRebindSource(bad).overflow;
  bool negative=false;{observation::Scope scope(0);try{auto *v=::operator new(loadBound.requested_heap_upper+1);::operator delete(v);}catch(const std::bad_alloc &){negative=observation::State.max_request>loadBound.requested_heap_upper;}}
  if(!overflowRejected || !negative)++failures;report["overflow_rejected"]=overflowRejected;report["negative_control"]=negative;
  // No allocation observation is active in this real concurrent-reader check.
  // The baseline never executes this schedule;its mutation was shown serially.
  std::atomic<bool> started{false},done{false},readerOk{true};std::atomic<size_t> reads{0};const auto pinned=unchanged.GetShared("item");const auto pinnedPayload=Payload(*pinned);
  std::thread reader([&]{started.store(true);while(!done.load() || reads.load()<256){const auto current=unchanged.GetShared("item");if(!current || current->voxels.size()!=2 || current->voxels[1].Id!=current->voxels[0].Id+1 || pinned->voxels[0].Id!=101)readerOk.store(false);++reads;}});
  while(!started.load())std::this_thread::yield();for(int i=0;i<32;++i){Define(*defs,static_cast<BlockId>(i%2?101:201));registry.Reload();unchanged.RebindBlockIds(registry);if(i%8==0)unchanged.LoadMerged(input.oldRoot,{},registry);}done.store(true);reader.join();
  const bool concurrent=readerOk.load() && reads.load()>=256 && Payload(*pinned)==pinnedPayload;if(!concurrent)++failures;report["concurrent_reader"]={{"pass",concurrent},{"reads",reads.load()},{"writer_cycles",32}};
  report["refusal_and_exact_retry_cases"]=total;report["failed_cases"]=failures;report["status"]=failures?"FAIL":"PASS";
  report["scope"]="Actual merged directory/rebind source+selected library/escaped-owner graph;borrowed registry/manifests/paths separate. Default MSVC Release allocator,warmed standard streams,ordinary-new requests. C06 all-world/thread/weak-only multiplicities and E12 glog/CRT/OS/stack/GPU/process totals remain open.";
  std::set_terminate(previousTerminate);if(path){std::ofstream out(path);out<<report.dump(2)<<'\n';}std::cout<<report.dump()<<std::endl;return failures?1:0;
}
} // namespace prefab_composition_fixture
