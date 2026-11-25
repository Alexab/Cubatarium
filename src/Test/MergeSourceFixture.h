#pragma once
#include "WorldGen/Core/MergeSourceEnvelope.h"
#include "Core/UiPump.h"
static cutum::MergeSourceDomain ReadMergeSourceDomain(const std::vector<cutum::ResourcePackManifest> &packs,
    const cutum::UBlockMergeRegistry &target,const std::shared_ptr<cutum::UPlaceholderTextureCache> &cache,int tile)
{
  using namespace cutum;MergeSourceDomain d;d.packs=packs.size();d.prior=InspectMergeTargetInput(target);d.placeholder=bool(cache);d.tile_size=tile;if(cache)d.cache=MeasurePlaceholderSourceInput(*cache);
  const auto manifests=MeasureMergeManifests(packs);d.manifest_heap_upper=manifests.requested_heap_upper;d.overflow=manifests.overflow;d.supported=manifests.supported;
  const auto add=[&](size_t &a,size_t n){detail::CandidateAdd(a,n,d.overflow);};
  const auto mul=[&](size_t a,size_t b){if(b && a>SIZE_MAX/b){d.overflow=true;return SIZE_MAX;}return a*b;};
  const auto parser=[&](size_t n){add(d.json_parser_sum,mul(n,586));add(d.json_parser_sum,mul(n/32+(n%32!=0),16));add(d.json_parser_sum,16640);};
  const auto read=[](const fs::path &path){std::ifstream in(path,std::ios::binary);return std::string(std::istreambuf_iterator<char>(in),{});};
  for(const auto &p:packs){d.id_max_bytes=std::max(d.id_max_bytes,p.Id.size());d.root_max_units=std::max(d.root_max_units,p.Root.native().size());d.entry_max_units=std::max(d.entry_max_units,(p.Root/"blocks").native().size());
    if(fs::exists(p.Root/"blocks"))for(const auto &entry:fs::directory_iterator(p.Root/"blocks")){
      d.entry_max_units=std::max(d.entry_max_units,entry.path().native().size());if(entry.path().extension()!=".json" || !entry.is_regular_file())continue;
      const auto bytes=read(entry.path());add(d.block_files,1);add(d.file_reads,1);parser(bytes.size());d.block_max_bytes=std::max(d.block_max_bytes,bytes.size());
      size_t values=bytes.size();add(values,1);const auto row=BoundCatalogCandidateSource(CatalogCandidateKind::Block,{values,bytes.size(),false});add(d.block_candidate_sum,row.requested_heap_upper);d.supported=d.supported && row.supported;d.overflow=d.overflow || row.overflow;
    }
    for(bool json:{true,false}){const auto path=p.Root/(json?"texture_overrides.json":"texture_overrides.yaml");if(!fs::exists(path))continue;const auto bytes=read(path);add(d.file_reads,1);d.override_max_bytes=std::max(d.override_max_bytes,bytes.size());
      CatalogCandidateEnvelope row;if(json){parser(bytes.size());size_t values=bytes.size();add(values,1);row=BoundTextureOverrideJsonSource({values,bytes.size(),false});}
      else row=BoundTextureOverrideYamlSource(MeasureTextureOverrideYamlInput(bytes));add(d.override_candidate_sum,row.requested_heap_upper);d.supported=d.supported && row.supported;d.overflow=d.overflow || row.overflow;
    }
  }return d;
}
static int RunMergeSourceFixture(const char *reportPath)
{
  using namespace cutum;using Json=nlohmann::json;
  const auto parent=fs::temp_directory_path()/("cuba_merge_source_"+std::to_string(GetCurrentProcessId()));fs::create_directories(parent);
  std::vector<ResourcePackManifest> packs;const char *ids[]={"base","skip","override","duplicate"};
  for(int i=0;i<4;++i){ResourcePackManifest p;p.Id=ids[i];p.Root=parent/ids[i];p.EffectivePriority=i;p.License=std::string(32769,'l');p.MergeMode=i==2?PackMergeMode::Override:i==3?PackMergeMode::Duplicate:PackMergeMode::SkipExisting;
    fs::create_directories(p.Root/"blocks");for(int b=0;b<2;++b){const auto name=b?std::string(ids[i])+"_unique":"shared";std::ofstream out(p.Root/"blocks"/(name+".json"));out<<Json{{"name",name},{"id",kPackBlockIdMin+100+i*10+b},{"hardness",float(i+1)},{"dig",{{"groups",{{"pickaxe",i+1}}}}},{"types",Json::array({std::string(129,'t')})},{"textures",Json::array({"missing","missing","missing","missing","missing","missing"})}}.dump();}packs.push_back(std::move(p));}
  {std::ofstream out(packs[0].Root/"texture_overrides.json");out<<R"({"base_unique":{"stem":"json_top","faces":["top"]}})";}
  {std::ofstream out(packs[1].Root/"texture_overrides.yaml");out<<"skip_unique:\n faces: [bottom]\n stem: yaml_bottom\n";}
  UBlockingWorkPump::Clear();{UBlockMergeRegistry warm;warm.Rebuild(packs,nullptr,16);UPlaceholderTextureCache pixels(parent/"warm_cache",16,glm::vec3(0.2f));(void)pixels.GetOrCreateStem("warm",0,16);}std::cout<<size_t{0}<<std::endl;std::cerr<<size_t{0}<<std::endl;
  const auto verify=[&](const UBlockMergeRegistry &registry,bool placeholder,bool overlay){
    if(registry.GetCubeDescriptors().size()!=(overlay?7:6))return false;
    std::map<std::string,BlockId> expected={{"base_unique",kPackBlockIdMin+101},{"skip_unique",kPackBlockIdMin+111},{"override_unique",kPackBlockIdMin+121},{"shared",kPackBlockIdMin+120},{"duplicate::shared",kPackBlockIdMin+130},{"duplicate::duplicate_unique",kPackBlockIdMin+131}};
    for(const auto &row:registry.GetCubeDescriptors()){if(row.Name=="overlay_fixture"){if(!overlay) return false;continue;}const auto it=expected.find(row.Name);if(it==expected.end() || it->second!=row.Id)return false;
      for(int face=0;face<6;++face){const auto &s=row.Stems[face];if(placeholder){if(s.rfind("__ph_",0)!=0)return false;}else {const auto wanted=row.Name=="base_unique" && face==4?"json_top":row.Name=="skip_unique" && face==5?"yaml_bottom":"missing";if(s!=wanted)return false;}}}return true;
  };
  Json report={{"schema","merge-composed-source.v1"},{"cases",Json::array()}};size_t failures=0,prefixes=0;
  for(bool placeholder:{false,true}){
    std::shared_ptr<UPlaceholderTextureCache> cache;if(placeholder)cache=std::make_shared<UPlaceholderTextureCache>(parent/"cache_normal",16,glm::vec3(0.2f),3);
    UBlockMergeRegistry empty;const auto domain=ReadMergeSourceDomain(packs,empty,cache,16);const auto bound=BoundMergeSource(domain);std::optional<UBlockMergeRegistry> target;
    WarningCounterBuffer warnings;{ScopedWarningCapture capture(warnings);{observation::Scope scope;target.emplace();target->Rebuild(packs,cache,16);}}
    const auto normal=observation::State;const bool semantic=verify(*target,placeholder,false);target.reset();cache.reset();
    const bool ok=semantic && bound.supported && !bound.overflow && normal.peak<=bound.additional_heap_upper && !observation::State.live && !observation::State.blocks;if(!ok)++failures;
    report["cases"].push_back({{"placeholder",placeholder},{"pass",ok},{"peak",normal.peak},{"upper",bound.additional_heap_upper},{"rows",bound.rows_upper},{"memo_keys",bound.memo_keys_upper},{"manifest_heap",domain.manifest_heap_upper},{"json_parser",domain.json_parser_sum},{"block_candidates",domain.block_candidate_sum},{"overrides",domain.override_candidate_sum},{"attempts",normal.attempts}});
    if(ok)for(size_t i=0;i<normal.attempts;++i){if(placeholder)cache=std::make_shared<UPlaceholderTextureCache>(parent/("cache_"+std::to_string(i)),16,glm::vec3(0.2f),3);
      {ScopedWarningCapture capture(warnings);{observation::Scope scope(i);try{target.emplace();target->Rebuild(packs,cache,16);}catch(const std::bad_alloc &){}}}
      const auto counts=observation::State;target.reset();cache.reset();if(!counts.refusals || observation::State.live || observation::State.blocks || counts.peak>bound.additional_heap_upper){++failures;break;}
      if(placeholder)cache=std::make_shared<UPlaceholderTextureCache>(parent/("cache_"+std::to_string(i)),16,glm::vec3(0.2f),3);
      {ScopedWarningCapture capture(warnings);{observation::Scope scope;target.emplace();target->Rebuild(packs,cache,16);}}if(!verify(*target,placeholder,false))++failures;target.reset();cache.reset();if(observation::State.live || observation::State.blocks){++failures;break;}++prefixes;
    }
  }
  // One epoch holds an old definition snapshot while current storage changes.
  std::optional<UBlockMergeRegistry> target;std::unordered_map<BlockId,BlockDefinition> escaped;
  {observation::Scope scope;target.emplace();BlockDefinition overlay;overlay.Name="overlay_fixture";overlay.Physics=BlockPhysicsProfile::Solid();target->RegisterRuntimeBlock(overlay,{});target->Rebuild(packs,nullptr,16);UBlockDefinitionStorage copy;target->PopulateBlockDefinitionStorage(copy);escaped=copy.GetCatalogSnapshot()->ById;}
  const auto prior=InspectMergeTargetInput(*target);detail::CatalogStorageInspector old;old.Map(escaped);for(const auto &entry:escaped)old.Definition(entry.second);bool overflow=false;const auto escapedUpper=detail::MergeCensusUpper(old.Result,overflow);
  size_t entryUpper=prior.heap_upper;detail::CandidateAdd(entryUpper,escapedUpper,overflow);const auto entryObserved=observation::State.live;
  {std::ofstream out(packs[0].Root/"blocks/base_unique.json");out<<Json{{"name","base_unique"},{"id",kPackBlockIdMin+101},{"hardness",78.0f},{"textures",Json::array({"missing","missing","missing","missing","missing","missing"})}}.dump();}
  const auto warmDomain=ReadMergeSourceDomain(packs,*target,nullptr,16);const auto warmBound=BoundMergeSource(warmDomain);
  observation::State.peak=observation::State.live;observation::State.attempts=0;observation::State.active=true;target->Rebuild(packs,nullptr,16);observation::State.active=false;
  const auto combinedPeak=observation::State.peak;size_t composed=entryUpper;detail::CandidateAdd(composed,warmBound.additional_heap_upper,overflow);
  UBlockDefinitionStorage current;target->PopulateBlockDefinitionStorage(current);const auto now=current.GetCatalogSnapshot();
  const bool preserved=escaped.at(kPackBlockIdMin+101).Hardness==1.0f && now->ById.at(kPackBlockIdMin+101).Hardness==78.0f;
  if(!preserved || !verify(*target,false,true) || overflow || warmBound.overflow || entryObserved>entryUpper || combinedPeak>composed)++failures;
  target.reset();{auto release=std::move(escaped);}if(observation::State.live || observation::State.blocks)++failures;
  report["prior_current"]={{"old_snapshot_preserved",preserved},{"entry_observed",entryObserved},{"entry_upper",entryUpper},{"combined_peak",combinedPeak},{"combined_upper",composed}};
  MergeSourceDomain huge;huge.prior.supported=true;huge.packs=huge.block_files=huge.manifest_heap_upper=SIZE_MAX;const auto saturated=BoundMergeSource(huge);if(!saturated.overflow)++failures;
  UBlockMergeRegistry negativeTarget;const auto upper=BoundMergeSource(ReadMergeSourceDomain(packs,negativeTarget,nullptr,16)).additional_heap_upper;
  {observation::Scope scope;auto *extra=::operator new(upper+1);::operator delete(extra);}const bool negative=observation::State.peak>upper && !observation::State.live;if(!negative)++failures;
  report["refusal_and_retry_cases"]=prefixes;report["overflow_rejected"]=saturated.overflow;report["negative_control"]=negative;report["failed_cases"]=failures;report["status"]=failures?"FAIL":"PASS";
  report["scope"]="Stable default MSVC file/stream profile,no work-pump callback. Complete Rebuild additional ordinary-new terms;prior/shared/escaped owners added separately. No filesystem diagnostic faults injected;their accepted explicit source allowance remains caller-supplied. Not process/GPU/CRT cap.";
  const auto resolved=fs::weakly_canonical(parent),temp=fs::weakly_canonical(fs::temp_directory_path());if(resolved.parent_path()!=temp || resolved!=temp/parent.filename())return 1;fs::remove_all(resolved);
  if(reportPath){std::ofstream out(reportPath);out<<report.dump(2)<<'\n';}std::cout<<report.dump()<<std::endl;return failures?1:0;
}
