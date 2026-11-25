#pragma once
#include "WorldGen/Core/PrefabCandidateSourceEnvelope.h"
#include "Blocks/BlockDefinitionStorage.h"
#include "Blocks/BlockRegistry.h"

static int RunPrefabCandidateSourceFixture(const char *reportPath)
{
  auto definitions=std::make_shared<UBlockDefinitionStorage>();
  const std::string type(257,'t');BlockDefinition definition;definition.Id=kPackBlockIdMin+19;definition.Name=type;
  definitions->ReplaceAll({{definition.Id,definition}},{{type,definition.Id}});
  UBlockRegistry registry(nullptr,definitions);
  const auto registrySnapshot=definitions->GetCatalogSnapshot();
  const std::string path="prefab/"+std::string(129,'f')+".json",name(129,'n'),pack(257,'p');
  std::cout<<size_t{0}<<std::endl;std::cerr<<size_t{0}<<std::endl;
  Json report={{"schema","prefab-candidate-source.v1"},{"cases",Json::array()}};
  size_t failures=0,prefixes=0;
  const auto run=[&](const char *label,const Json &input,const std::string &registered,auto oracle,int error=0,size_t prior=0){
    const auto produce=[&](auto &unknown){return std::make_shared<WorldObjectDefinition>(CompilePrefabCatalog(input,registry,path,registered,ObjectOrigin::Imported,pack,unknown));};
    const auto prepare=[&]{std::unordered_set<std::string> set;for(size_t i=0;i<prior;++i)set.insert("prior_unknown_"+std::to_string(i));return set;};
    auto unknown=prepare();const auto bound=BoundPrefabCandidateSource(MeasureCatalogCandidateInput(input),path.size()+registered.size()+pack.size(),unknown.size(),unknown.bucket_count());
    std::shared_ptr<WorldObjectDefinition> result;int actualError=0;
    {observation::Scope scope;try{result=produce(unknown);}catch(const Json::exception &e){actualError=e.id;}}
    const auto normal=observation::State;const bool semantic=actualError==error && (error || (result && oracle(*result,unknown)));
    result.reset();unknown.clear();unknown.rehash(0);
    // Destroy all observed buckets, including the minimal empty-table backing.
    {auto old=std::move(unknown);}
    bool ok=semantic && bound.supported && !bound.overflow && normal.peak<=bound.requested_heap_upper && !observation::State.live && !observation::State.blocks;
    if(!ok)++failures;
    report["cases"].push_back({{"name",label},{"pass",ok},{"peak",normal.peak},{"upper",bound.requested_heap_upper},{"attempts",normal.attempts},{"error",actualError},{"prior_unknown",prior}});
    if(!error && ok)for(size_t i=0;i<normal.attempts;++i){
      auto log=prepare();bool refused=false;
      {observation::Scope scope(i);try{result=produce(log);}catch(const std::bad_alloc &){refused=true;}}
      result.reset();{auto release=std::move(log);}
      if(!refused || observation::State.live || observation::State.blocks || observation::State.peak>bound.requested_heap_upper)++failures;
      auto retryLog=prepare();{observation::Scope scope;result=produce(retryLog);}
      if(!result || !oracle(*result,retryLog))++failures;result.reset();{auto release=std::move(retryLog);}
      if(observation::State.live || observation::State.blocks || definitions->GetCatalogSnapshot()!=registrySnapshot)++failures;
      ++prefixes;
    }
  };
  Json block={{"dx",-2},{"dy",3},{"dz",5},{"type",type}};
  Json authored={{"name","authored"},{"displayName",std::string(5001,'d')},{"tags",Json::array({std::string(257,'g'),42,"plant"})},
    {"origin","pack:custom"},{"visibility","hidden"},{"placement",{{"mode","surface_layer"},{"y_offset",-7}}},
    {"anchor",Json::array({4,-5,6})},{"blocks",Json::array({block})}};
  run("authored_fields",authored,name,[&](const auto &o,const auto &u){return o.Name==name && o.DisplayName==std::string(5001,'d') && o.Tags==std::vector<std::string>{std::string(257,'g'),"plant"} && o.Origin==ObjectOrigin::ResourcePack && o.Hidden && o.SourcePackId==pack && o.PlacementYOffset==-7 && o.PlacementMode==ObjectPlacementMode::SurfaceLayer && o.anchor==glm::ivec3(4,-5,6) && o.voxels.size()==1 && o.voxels[0].offset==glm::ivec3(-2,3,5) && o.voxels[0].Id==definition.Id && o.voxels[0].Type==type && o.boundsMin==glm::ivec3(2,-2,11) && o.boundsMax==o.boundsMin && u.empty();});
  Json defaults={{"blocks",Json::array({block})}};
  run("filename_defaults",defaults,"",[&](const auto &o,const auto &){return o.Name==std::string(129,'f') && o.DisplayName==o.Name && o.Tags==std::vector<std::string>{"misc"} && o.Origin==ObjectOrigin::Imported && !o.Hidden && o.anchor==glm::ivec3(0) && o.PlacementMode==ObjectPlacementMode::Default && o.voxels.size()==1;});
  auto vertical=defaults;vertical["placement"]={{"mode","vertical_plant"}};vertical["origin"]="user";
  run("vertical_user",vertical,name,[](const auto &o,const auto &){return o.PlacementMode==ObjectPlacementMode::VerticalPlant && o.Origin==ObjectOrigin::User;});
  auto wide=defaults;wide["blocks"]=Json::array();
  for(int i=0;i<129;++i){auto b=block;b["dx"]=i;b["dy"]=-i;wide["blocks"].push_back(b);}
  run("129_voxel_growth",wide,name,[](const auto &o,const auto &){return o.voxels.size()==129 && o.boundsMin==glm::ivec3(0,-128,5) && o.boundsMax==glm::ivec3(128,0,5);});
  auto missing=defaults;missing["blocks"]=Json::array();
  for(int i=0;i<17;++i){auto b=block;b["type"]=std::string(129,'u')+std::to_string(i);missing["blocks"].push_back(b);missing["blocks"].push_back(b);}
  run("unknown_duplicates_rehash",missing,name,[](const auto &o,const auto &u){return o.voxels.empty() && u.size()==34;},0,17);
  Json empty={{"blocks",Json::array()}};run("empty_candidate",empty,name,[](const auto &o,const auto &){return o.voxels.empty() && o.boundsMin==glm::ivec3(INT_MAX) && o.boundsMax==glm::ivec3(INT_MIN);});
  run("missing_blocks",Json::object(),name,[](const auto &,const auto &){return false;},403);
  auto invalid=defaults;invalid["blocks"][0]["dx"]="wrong";run("invalid_coordinate",invalid,name,[](const auto &,const auto &){return false;},302);
  run("invalid_root",Json::array(),"",[](const auto &,const auto &){return false;},306);
  const auto overflow=BoundPrefabCandidateSource({SIZE_MAX,SIZE_MAX,false},SIZE_MAX,SIZE_MAX,SIZE_MAX);if(!overflow.overflow)++failures;
  const auto upper=BoundPrefabCandidateSource(MeasureCatalogCandidateInput(defaults),path.size()+name.size()+pack.size()).requested_heap_upper;
  {observation::Scope scope;auto *extra=::operator new(upper+1);::operator delete(extra);}
  const bool negative=observation::State.peak>upper && !observation::State.live;if(!negative)++failures;
  report["overflow_rejected"]=overflow.overflow;report["negative_control"]=negative;report["persistent_prefix_and_retry_cases"]=prefixes;report["failed_cases"]=failures;report["status"]=failures?"FAIL":"PASS";
  report["scope"]="One actual shared production candidate plus newly allocated unknown-log set backing;input DOM/registry/prior diagnostic nodes and publication map separate. No rollback of diagnostic set claimed on allocation failure;caller may keep admitted diagnostics. Default MSVC allocator/load factor. Not directory aggregate/process RSS.";
  if(reportPath){std::ofstream out(reportPath);out<<report.dump(2)<<'\n';}std::cout<<report.dump()<<std::endl;return failures?1:0;
}
