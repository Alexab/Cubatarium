#pragma once
#include "WorldGen/Core/PipelineOwnerKey.h"
#include "Test/CatalogOwnerCompositionControls.h"
#include <optional>

namespace pipeline_weak_shell_fixture
{
using namespace cutum;
using Json=nlohmann::json;
struct Graph
{
  std::shared_ptr<UBlockDefinitionStorage> definitions;
  std::optional<UBlockRegistry> registry;
  WorldGenContentSnapshot content;
  PipelineOwnerKey key;
  void Build() {
    definitions=std::make_shared<UBlockDefinitionStorage>();
    BlockDefinition block;block.Name="weak_shell_block";block.DisplayName=std::string(32768,'b');
    definitions->ReplaceAll({{9,block}},{{block.Name,9}});
    registry.emplace(nullptr,definitions);
    auto pack=std::make_shared<WorldGenPack>();pack->Id=std::string(65536,'p');
    pack->BiomeRegistry=std::make_shared<UBiomeDefinitionRegistry>(std::vector<BiomeKey>{std::string(8192,'k')});
    auto features=std::make_shared<ObjectFeatureConfig>();
    ObjectFeatureRule rule;rule.ObjectName=std::string(16384,'f');features->Vegetation.push_back(std::move(rule));
    auto refs=std::make_shared<WorldGenRefsCatalog>();
    (*refs)[std::string(4096,'r')].FallbackSlot=std::string(8192,'s');
    content={pack,pack->BiomeRegistry,features,refs};
    key.Assign(*registry,nullptr,content);
  }
  bool Value()const {
    return registry && key.Matches(*registry,nullptr,content) && content.Pack->Id==std::string(65536,'p') &&
      content.Biomes->Size()==1 && content.Features->Vegetation.front().ObjectName.size()==16384 && content.Refs->size()==1;
  }
  void ReleasePayload() {content={};registry.reset();definitions.reset();}
  void Clear() {ReleasePayload();key={};}
};
inline Json Census(const PipelineWeakShellCensus &c) {
  return {{"occupied_slots",c.occupied_slots},{"distinct_owners",c.distinct_owners},
    {"expired_owners",c.expired_owners},{"expired_request_upper",c.expired_request_upper},{"supported",c.supported}};
}
inline int Run(const char *path)
{
  const auto layout=GetPipelineWeakShellLayout();
  if(!layout.supported)return 2;
  size_t failures=0;
  const auto check=[&](bool value){if(!value)++failures;};
  Graph graph;
  {observation::Scope scope;graph.Build();}
  const auto normal=observation::State;
  check(graph.Value());
  const auto live=graph.key.InspectWeakShells(true);
  check(live.supported && live.occupied_slots==6 && live.distinct_owners==6 && live.expired_owners==0 && live.expired_request_upper==0);
  graph.ReleasePayload();
  const auto shells=graph.key.InspectWeakShells(true);
  check(shells.supported && shells.expired_owners==6 && shells.expired_request_upper==layout.per_thread_upper &&
      observation::State.live==layout.per_thread_upper && observation::State.blocks==6 && normal.live>observation::State.live);
  {
    auto external_alias=graph.key;external_alias.registry=external_alias.pack;
    const auto alias=external_alias.InspectWeakShells(false);
    check(!alias.supported && alias.occupied_slots==6 && alias.distinct_owners==5 && alias.expired_owners==5 &&
        alias.expired_request_upper==layout.per_thread_upper-layout.requests[0]);
  }
  std::array<PipelineOwnerKey,64> aliases;
  for(auto &alias:aliases)alias=graph.key;
  const auto before_inspection=observation::State.attempts;
  // Persistent refusal also covers inspection and weak-only key release.
  observation::State.active=true;observation::State.fail_after=before_inspection;
  bool aliases_deduplicated=true;
  for(const auto &alias:aliases){const auto c=alias.InspectWeakShells(true);
    aliases_deduplicated=aliases_deduplicated && c.expired_request_upper==layout.per_thread_upper && c.expired_owners==6;}
  graph.key={};
  observation::State.active=false;
  const bool no_new=observation::State.attempts==before_inspection;
  check(aliases_deduplicated && no_new && observation::State.live==layout.per_thread_upper && observation::State.blocks==6);
  const bool omitted_shell_control=observation::State.live>layout.per_thread_upper-layout.requests[2];
  check(omitted_shell_control);
  std::thread final_weak([&]{aliases={};});final_weak.join();
  check(observation::State.live==0 && observation::State.blocks==0);
  const bool cross_thread_final_zero=observation::State.live==0 && observation::State.blocks==0;

  // Two independent expired keys need two supplied TLS slots;one is insufficient.
  Graph left,right;
  {observation::Scope scope;left.Build();right.Build();}
  left.ReleasePayload();right.ReleasePayload();
  const auto one=BoundPipelineWeakShells(1,true,true),two=BoundPipelineWeakShells(2,true,true);
  const bool missing_thread_control=observation::State.live>one.requested_upper && observation::State.live==two.requested_upper;
  check(missing_thread_control && observation::State.blocks==12);
  left.Clear();right.Clear();check(observation::State.live==0 && observation::State.blocks==0);

  const auto empty=PipelineOwnerKey{}.InspectWeakShells(true);
  const auto unknown=BoundPipelineWeakShells(0,false,true);
  const auto custom=BoundPipelineWeakShells(1,true,false);
  const auto huge=BoundPipelineWeakShells(SIZE_MAX,true,true);
  check(empty.occupied_slots==0 && empty.expired_request_upper==0 && !unknown.supported && !custom.supported && huge.overflow);
  check(!PipelineOwnerKey{}.InspectWeakShells(false).supported);
  const auto owner_counts=BoundCatalogOwnerSource(catalog_composition_fixture::OwnerCountProfile());
  CatalogCandidateEnvelope retained;retained.supported=true;retained.requested_heap_upper=1234;
  const auto joined=JoinCatalogPipelineWeakShells(retained,owner_counts,true);
  const auto weighted=BoundPipelineWeakShells(owner_counts.thread_upper,true,true);
  check(joined.supported && !joined.overflow && joined.requested_heap_upper==1234+weighted.requested_upper && owner_counts.thread_upper==63);
  retained.requested_heap_upper=SIZE_MAX;
  check(JoinCatalogPipelineWeakShells(retained,owner_counts,true).overflow);
  Json report={{"schema","pipeline-weak-shell.v1"},{"layout_requests",layout.requests},
    {"key_inline_bytes",sizeof(PipelineOwnerKey)},{"per_thread_shell_upper",layout.per_thread_upper},
    {"live",Census(live)},{"expired",Census(shells)},{"normal_peak",normal.peak},
    {"normal_payload_and_shell_retained",normal.live},{"normal_attempts",normal.attempts},
    {"weak_aliases",64},{"aliases_keep_same_six_blocks",aliases_deduplicated},
    {"inspection_and_key_release_allocate0",no_new},{"last_weak_cross_thread_final0",cross_thread_final_zero},
    {"omitted_shell_control",omitted_shell_control},{"missing_thread_control",missing_thread_control},
    {"unknown_custom_and_overflow_rejected",!unknown.supported&&!custom.supported&&huge.overflow},
    {"supplied_threads",owner_counts.thread_upper},{"supplied_thread_weak_upper",weighted.requested_upper},
    {"cases",Json::array()}};
  for(size_t prefix=0;prefix<normal.attempts;++prefix){
    bool refused=false;Graph candidate;
    {observation::Scope scope(prefix);try{candidate.Build();}catch(const std::bad_alloc &){refused=true;}}
    const auto counts=observation::State;candidate.Clear();
    const bool clean=refused && counts.refusals && observation::State.live==0 && observation::State.blocks==0;
    check(clean);
    if(!clean)break;
    {observation::Scope scope;candidate.Build();}
    const bool retry=candidate.Value();candidate.Clear();check(retry && observation::State.live==0 && observation::State.blocks==0);
    report["cases"].push_back({{"prefix",prefix},{"refused",refused},{"clean",clean},{"exact_retry",retry}});
  }
  report["final_bytes"]=observation::State.live;report["final_blocks"]=observation::State.blocks;
  report["failed_cases"]=failures;report["status"]=failures?"FAIL":"PASS";
  report["scope"]="Actual six production PipelineOwnerKey types/default make_shared factories,requested-new coallocations after dynamic payload retirement. Per supplied participating thread,not all OS threads or total heap/RSS. Live owners already in E11;CRT/alignment/upstream,other weak owners,logger/TLS/stack/GPU remain E12.";
  if(path){std::ofstream out(path);out<<report.dump(2)<<'\n';if(!out)return 1;}
  std::cout<<report.dump()<<std::endl;return failures?1:0;
}
} // namespace pipeline_weak_shell_fixture
