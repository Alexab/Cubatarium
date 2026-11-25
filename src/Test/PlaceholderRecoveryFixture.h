#pragma once
#include "Test/OrdinaryNewObservation.h"
#include <windows.h>
#include <nlohmann/json.hpp>
#include <optional>
#include "ResourcePacks/PlaceholderPixelLayout.h"
static int RunPlaceholderRecoveryFixture(const char *reportPath,bool baseline)
{
  using namespace cutum;using Json=nlohmann::json;
  const auto parent=fs::temp_directory_path()/("cuba_ph_recovery_"+std::to_string(GetCurrentProcessId()));fs::create_directories(parent);
  // Warm persistent MSVC stream/hex formatting state outside observation.
  {UPlaceholderTextureCache warm(parent/"warm",16,glm::vec3(0.2f),2);(void)warm.GetOrCreateStem("warm_asset",0,16);}
  const auto good=[&](const UPlaceholderTextureCache &cache,const std::string &stem){UTextureBaseStorage storage;cache.RegisterAll(storage);const auto it=storage.GetBaseTextures().find(stem);if(it==storage.GetBaseTextures().end())return false;const auto *p=it->second.GetPixelData();if(!p || p->Width!=16 || p->Height!=16 || p->Rgba.size()!=1024)return false;uint64_t hash=14695981039346656037ull;for(auto b:p->Rgba)hash=(hash^b)*1099511628211ull;return hash==0xfbc66fccbb979734ull;};
  std::optional<UPlaceholderTextureCache> cache;cache.emplace(parent/"normal",16,glm::vec3(0.2f),2);
  std::string stem;{observation::Scope scope;stem=cache->GetOrCreateStem("candidate_asset",0,16);}const auto normal=observation::State;bool normalGood=good(*cache,stem);cache.reset();
  std::fprintf(stderr,"normal cleanup live=%zu blocks=%zu attempts=%zu\n",observation::State.live,observation::State.blocks,normal.attempts);
  Json report={{"schema","placeholder-recovery.v1"},{"normal_attempts",normal.attempts},{"normal_peak",normal.peak},{"cases",Json::array()}};size_t failures=normalGood?0:1;bool reproduced=false;
  for(size_t i=0;i<normal.attempts;++i){cache.emplace(parent/("prefix"+std::to_string(i)),16,glm::vec3(0.2f),2);bool refused=false;
    {observation::Scope scope(i);try{stem=cache->GetOrCreateStem("candidate_asset",0,16);}catch(const std::bad_alloc &){refused=true;}}
    stem=cache->GetOrCreateStem("candidate_asset",0,16);const bool valid=good(*cache,stem);cache.reset();const bool clean=!observation::State.live && !observation::State.blocks;
    report["cases"].push_back({{"prefix",i},{"refused",refused},{"retry_has_pixels",valid},{"cleaned",clean}});
    if(refused && !valid)reproduced=true;
    if(!baseline && (!refused || !valid || !clean))++failures;
    if(baseline && reproduced)break;
  }
  if(baseline && !reproduced)++failures;
  if(!baseline){
    // Existing hit and capacity-one eviction must remain usable after refusal.
    report["additional_scenes"]=Json::array();
    for(bool hit:{true,false}){
      const std::string label=hit?"hit":"evict";const auto folder=parent/(label+"_normal");
      cache.emplace(folder,16,glm::vec3(0.2f),1);const auto previous=cache->GetOrCreateStem(hit?"candidate_asset":"existing_asset",0,16);
      {observation::Scope scope;stem=cache->GetOrCreateStem("candidate_asset",0,16);}const auto counts=observation::State;
      if(!good(*cache,stem) || cache->GetEntryCount()!=1)++failures;cache.reset();if(observation::State.live || observation::State.blocks)++failures;
      Json cases=Json::array();
      for(size_t i=0;i<counts.attempts;++i){cache.emplace(parent/(label+std::to_string(i)),16,glm::vec3(0.2f),1);const auto old=cache->GetOrCreateStem(hit?"candidate_asset":"existing_asset",0,16);bool refused=false;
        {observation::Scope scope(i);try{stem=cache->GetOrCreateStem("candidate_asset",0,16);}catch(const std::bad_alloc &){refused=true;}}
        const bool prior=good(*cache,old) && cache->GetEntryCount()==1;
        stem=cache->GetOrCreateStem("candidate_asset",0,16);const bool recovered=good(*cache,stem) && cache->GetEntryCount()==1;
        cache.reset();const bool clean=!observation::State.live && !observation::State.blocks;
        if(!refused || !prior || !recovered || !clean)++failures;
        cases.push_back({{"prefix",i},{"refused",refused},{"prior_consistent",prior},{"recovered",recovered},{"cleaned",clean}});
      }
      report["additional_scenes"].push_back({{"name",label},{"attempts",counts.attempts},{"cases",cases}});
    }
    // Derived-file headers must match the requested shape and exact payload.
    report["file_cases"]=Json::array();const auto disk=parent/"disk";
    {UPlaceholderTextureCache initial(disk,16,glm::vec3(0.2f));stem=initial.GetOrCreateStem("disk_asset",0,16);}
    for(int kind=0;kind<5;++kind){const auto file=disk/(stem+".rgba");
      if(kind){std::ofstream out(file,std::ios::binary|std::ios::trunc);uint32_t w=kind==1?UINT32_MAX:16,h=16,n=kind==2?UINT32_MAX:1024;out.write(reinterpret_cast<const char*>(&w),4);out.write(reinterpret_cast<const char*>(&h),4);out.write(reinterpret_cast<const char*>(&n),4);const std::string bytes(kind==3?8:kind==4?1025:0,'x');out.write(bytes.data(),static_cast<std::streamsize>(bytes.size()));}
      cache.emplace(disk,16,glm::vec3(0.2f));{observation::Scope scope(SIZE_MAX,1024*1024);stem=cache->GetOrCreateStem("disk_asset",0,16);}const bool valid=good(*cache,stem);cache.reset();const bool clean=!observation::State.live && !observation::State.blocks;
      if(!valid || !clean)++failures;report["file_cases"].push_back({{"kind",kind},{"valid_pixels",valid},{"cleaned",clean}});
    }
    size_t bytes=0;const bool arithmetic=CheckedRgbaByteCount(32768,32768,bytes) && bytes==uint64_t{4294967296} &&
      !PlaceholderFileShapeFits(32768,32768,bytes) && !CheckedRgbaByteCount(SIZE_MAX,2,bytes) && !CheckedRgbaByteCount(0,1,bytes) &&
      PlaceholderHeaderMatches(16,16,1024,16) && !PlaceholderHeaderMatches(16,16,1023,16);
    if(!arithmetic)++failures;report["checked_arithmetic"]=arithmetic;
    cache.emplace(parent/"large",16,glm::vec3(0.2f));bool refused=false;
    {observation::Scope scope(SIZE_MAX,1024*1024);try{(void)cache->GetOrCreateStem("large_asset",0,32768);}catch(const std::bad_alloc &){refused=true;}}
    const auto large=observation::State;const bool noKey=cache->GetEntryCount()==0;cache.reset();
    const bool largeOk=refused && noKey && large.max_request==uint64_t{4294967296}+39 && !observation::State.live && !observation::State.blocks;
    if(!largeOk)++failures;report["large_request_no_allocation"]={{"pass",largeOk},{"logical_rgba_bytes",uint64_t{4294967296}},{"requested_bytes",large.max_request}};
  }
  report["stale_stem_reproduced"]=reproduced;report["status"]=failures?"FAIL":baseline?"REPRODUCED":"PASS";
  const auto resolved=fs::weakly_canonical(parent),temp=fs::weakly_canonical(fs::temp_directory_path());if(resolved.parent_path()!=temp || resolved!=temp/parent.filename())return 1;fs::remove_all(resolved);
  if(reportPath){std::ofstream out(reportPath);out<<report.dump(2)<<'\n';}std::cout<<report.dump()<<std::endl;return failures?1:0;
}
