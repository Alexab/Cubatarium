#pragma once
#include "Test/OrdinaryNewObservation.h"
#include "Blocks/BlockDefinitionStorage.h"
#include <nlohmann/json.hpp>
#include <optional>
#include <windows.h>

static int RunMergeStagingOwnershipFixture(const char *reportPath,bool matrix)
{
  using namespace cutum;using Json=nlohmann::json;
  const auto parent=fs::temp_directory_path()/("cuba_merge_"+std::to_string(GetCurrentProcessId()));fs::create_directories(parent);
  std::vector<ResourcePackManifest> packs;
  const char *ids[]={"base","skip","override","duplicate"};
  for(int i=0;i<4;++i){ResourcePackManifest p;p.Id=ids[i];p.Root=parent/ids[i];p.EffectivePriority=i;
    p.License=std::string(65537,'l');p.Name=std::string(8193,'n');p.Depends={std::string(4097,'d')};
    p.MergeMode=i==2?PackMergeMode::Override:i==3?PackMergeMode::Duplicate:PackMergeMode::SkipExisting;
    fs::create_directories(p.Root/"blocks");
    for(int b=0;b<5;++b){const std::string name=b?std::string(ids[i])+"_"+std::to_string(b):"shared";
      Json data={{"name",name},{"displayName",std::string(257,char('a'+i))},{"id",kPackBlockIdMin+100+i*10+b},
        {"hardness",float(i+1)},{"types",Json::array({"solid",std::string(129,'t')})},
        {"dig",{{"groups",{{"pickaxe",i+2}}}}},{"textures",Json::array({"one","two","three","four","five","six"})}};
      std::ofstream out(p.Root/"blocks"/(name+".json"));out<<data.dump();}
    packs.push_back(std::move(p));
  }
  const auto snapshot=[&](const UBlockMergeRegistry &merge){
    UBlockDefinitionStorage storage;merge.PopulateBlockDefinitionStorage(storage);const auto catalog=storage.GetCatalogSnapshot();Json out=Json::object();
    for(const auto &desc:merge.GetCubeDescriptors()){
      const auto found=catalog->ById.find(desc.Id);if(found==catalog->ById.end())return Json();const auto &d=found->second;
      out[desc.Name]={{"id",desc.Id},{"name",d.Name},{"display",d.DisplayName},{"hardness",d.Hardness},{"types",d.Types},{"dig",d.DigGroups},{"stems",desc.Stems}};
    }return out;
  };
  const auto oracle=[&](const UBlockMergeRegistry &merge){
    if(merge.GetNameToId().size()!=18 || merge.GetCubeDescriptors().size()!=18)return false;
    const auto data=snapshot(merge);
    for(int i=0;i<4;++i)for(int b=0;b<5;++b){if(b==0 && i<2)continue;
      std::string name=b?std::string(ids[i])+"_"+std::to_string(b):"shared";if(i==3)name="duplicate::"+name;
      if(!data.contains(name))return false;const auto &d=data[name];
      if(d["id"]!=kPackBlockIdMin+100+i*10+b || d["name"]!=name || d["display"]!=std::string(257,char('a'+i)) || d["hardness"]!=float(i+1) || d["types"]!=Json::array({"solid",std::string(129,'t')}) || d["dig"]["pickaxe"]!=i+2 || d["stems"]!=Json::array({"one","two","three","four","five","six"}))return false;
    }return true;
  };
  // Keep persistent numeric ostream/CRT initialization outside catalog epoch.
  {UBlockMergeRegistry warm;warm.Rebuild(packs,nullptr,16);}std::cout<<size_t{0}<<std::endl;
  std::optional<UBlockMergeRegistry> merge;
  {observation::Scope scope;merge.emplace();merge->Rebuild(packs,nullptr,16);}
  const auto normal=observation::State;bool ok=oracle(*merge);const auto normalized=snapshot(*merge);const auto fingerprint=merge->ComputeCatalogFingerprint();
  const auto oldId=packs[0].Id;packs[0].Id="changed_caller";
  const bool owns=merge->ComputeCatalogFingerprint()==fingerprint && snapshot(*merge)==normalized;packs[0].Id=oldId;
  merge.reset();if(!owns || observation::State.live || observation::State.blocks)ok=false;
  Json report={{"schema","merge-staging-ownership.v1"},{"normal_peak",normal.peak},{"normal_retained",normal.live},{"normal_attempts",normal.attempts},{"normalized",normalized},{"fingerprint",fingerprint},{"retained_catalog_owns_data",owns},{"cases",Json::array()}};
  size_t failed=ok?0:1,completedWithRefusal=0;
  if(matrix && ok)for(size_t i=0;i<normal.attempts;++i){bool threw=false;
    {observation::Scope scope(i);try{merge.emplace();merge->Rebuild(packs,nullptr,16);}catch(const std::bad_alloc &){threw=true;}}
    const auto counts=observation::State;merge.reset();const bool cleaned=observation::State.live==0 && observation::State.blocks==0;
    if(!counts.refusals || !cleaned)++failed;if(counts.refusals && !threw)++completedWithRefusal;
    report["cases"].push_back({{"prefix",i},{"refusals",counts.refusals},{"threw",threw},{"cleaned",cleaned}});
    if(!cleaned)break;
    {observation::Scope scope;merge.emplace();merge->Rebuild(packs,nullptr,16);}if(!oracle(*merge) || snapshot(*merge)!=normalized)++failed;merge.reset();if(observation::State.live || observation::State.blocks)++failed;
  }
  report["completed_after_observed_refusal"]=completedWithRefusal;report["failed_cases"]=failed;report["status"]=failed?"FAIL":"PASS";
  report["scope"]="Actual four-pack Rebuild with raw missing texture stems and no placeholder/GPU;skip/override/duplicate,stable IDs and all fixture fields. Allocation refusals release fresh target and retry;mutable Rebuild rollback is not asserted. Scoped observed peak,not general source/process bound.";
  const auto resolved=fs::weakly_canonical(parent),temp=fs::weakly_canonical(fs::temp_directory_path());if(resolved.parent_path()!=temp || resolved!=temp/parent.filename())return 1;fs::remove_all(resolved);
  if(reportPath){std::ofstream out(reportPath);out<<report.dump(2)<<'\n';}std::cout<<report.dump()<<std::endl;return failed?1:0;
}
