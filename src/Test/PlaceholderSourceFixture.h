#pragma once
#include "WorldGen/Core/PlaceholderSourceEnvelope.h"
static int RunPlaceholderSourceFixture(const char *reportPath)
{
  using namespace cutum;using Json=nlohmann::json;
  const auto parent=fs::temp_directory_path()/("cuba_ph_source_"+std::to_string(GetCurrentProcessId()));fs::create_directories(parent);
  {UPlaceholderTextureCache warm(parent/"warm",16,glm::vec3(0.2f));(void)warm.GetOrCreateStem("warm",0,16);}
  const std::string collisionA="collision_asset_119dd336068b3c23",collisionB="collision_asset_b85df5af9754de83";
  const auto valid=[](const UPlaceholderTextureCache &cache,const std::string &stem,int size){
    UTextureBaseStorage storage;cache.RegisterAll(storage);const auto it=storage.GetBaseTextures().find(stem);if(it==storage.GetBaseTextures().end())return false;
    const auto *p=it->second.GetPixelData();if(!p || p->Width!=size || p->Height!=size || p->Rgba.size()!=size_t(size)*size*4)return false;
    if(size==16){uint64_t hash=14695981039346656037ull;for(auto b:p->Rgba)hash=(hash^b)*1099511628211ull;return hash==0xfbc66fccbb979734ull;}return true;
  };
  Json report={{"schema","placeholder-source-envelope.v1"},{"cases",Json::array()}};size_t failures=0,prefixes=0;
  const auto run=[&](const char *label,size_t seeds,size_t limit,const std::string &key,int size,bool disk,bool hit,bool collision){
    const auto prepare=[&](const fs::path &folder){
      if(disk){UPlaceholderTextureCache stored(folder,16,glm::vec3(0.2f),limit);(void)stored.GetOrCreateStem(key,0,size);}
      auto cache=std::make_unique<UPlaceholderTextureCache>(folder,16,glm::vec3(0.2f),limit);
      for(size_t i=0;i<seeds;++i)(void)cache->GetOrCreateStem(collision?collisionA:"seed_"+std::to_string(i),0,16);
      if(hit)(void)cache->GetOrCreateStem(key,0,size);return cache;
    };
    auto cache=prepare(parent/(std::string(label)+"_normal"));const auto input=MeasurePlaceholderSourceInput(*cache);const auto bound=BoundPlaceholderSource(input,key.size(),size);
    std::optional<std::string> stem;
    {observation::Scope scope;stem.emplace(cache->GetOrCreateStem(key,0,size));}const auto normal=observation::State;
    const bool semantic=valid(*cache,*stem,size) && cache->GetEntryCount()<=limit && (!collision || *stem=="__ph_e61552fa");cache.reset();stem.reset();
    const bool ok=semantic && input.supported && !input.overflow && bound.supported && !bound.overflow && normal.peak<=bound.requested_heap_upper && !observation::State.live && !observation::State.blocks;
    if(!ok)++failures;report["cases"].push_back({{"name",label},{"pass",ok},{"prior_structural_upper",input.structural_heap_upper},{"prior_pixel_upper",input.pixel_heap_upper},{"path_units",input.path_units},{"peak",normal.peak},{"upper",bound.requested_heap_upper},{"attempts",normal.attempts}});
    if(ok)for(size_t i=0;i<normal.attempts;++i){cache=prepare(parent/(std::string(label)+"_"+std::to_string(i)));bool refused=false;
      {observation::Scope scope(i);try{stem.emplace(cache->GetOrCreateStem(key,0,size));}catch(const std::bad_alloc &){refused=true;}}
      const auto counts=observation::State;stem.reset();if(!refused || counts.peak>bound.requested_heap_upper)++failures;
      stem.emplace(cache->GetOrCreateStem(key,0,size));if(!valid(*cache,*stem,size) || cache->GetEntryCount()>limit)++failures;
      cache.reset();stem.reset();if(observation::State.live || observation::State.blocks)++failures;++prefixes;
    }
  };
  run("miss16",0,2,"candidate",16,false,false,false);
  run("hit16",0,2,"candidate",16,false,true,false);
  run("disk16",0,2,"candidate",16,true,false,false);
  run("evict16",1,1,"candidate",16,false,false,false);
  run("long_key",0,2,std::string(8193,'k'),16,false,false,false);
  run("wide65_rehash",64,128,"candidate",16,false,false,false);
  run("tile129",0,2,"candidate",129,false,false,false);
  run("collision_eviction",1,1,collisionB,16,false,false,true);
  bool shared=false,retired=false,oneCopiedTile=false;size_t sharedCopyPeak=0,sharedCopyUpper=0;
  {const auto folder=parent/"shared_stem";UPlaceholderTextureCache cache(folder,16,glm::vec3(0.2f),2);
    const auto a=cache.GetOrCreateStem(collisionA,0,16),b=cache.GetOrCreateStem(collisionB,0,16);
    const auto input=MeasurePlaceholderSourceInput(cache);const auto bound=BoundPlaceholderRegistration(input);
    auto transferred=std::make_unique<UTextureBaseStorage>();{observation::Scope scope;cache.RegisterAll(*transferred);}sharedCopyPeak=observation::State.peak;sharedCopyUpper=bound.requested_heap_upper;
    oneCopiedTile=cache.GetEntryCount()==2 && input.pixel_count==1 && transferred->GetBaseTextures().size()==1 && sharedCopyPeak<=sharedCopyUpper;
    (void)cache.GetOrCreateStem("third",0,16);shared=a==b && a=="__ph_e61552fa" && valid(cache,b,16) && fs::exists(folder/(b+".rgba"));
    (void)cache.GetOrCreateStem("fourth",0,16);UTextureBaseStorage storage;cache.RegisterAll(storage);retired=storage.GetBaseTextures().count(b)==0 && !fs::exists(folder/(b+".rgba"));
    const auto *copied=transferred->GetBaseTextures().begin()->second.GetPixelData();oneCopiedTile=oneCopiedTile && copied && copied->Rgba.size()==1024;transferred.reset();if(observation::State.live || observation::State.blocks)++failures;}
  if(!shared || !retired || !oneCopiedTile)++failures;
  report["registration_cases"]=Json::array();size_t transferPrefixes=0;
  for(size_t seeds:{size_t{0},size_t{3},size_t{65}}){
    auto cache=std::make_unique<UPlaceholderTextureCache>(parent/("register"+std::to_string(seeds)),16,glm::vec3(0.2f),128);
    for(size_t i=0;i<seeds;++i)(void)cache->GetOrCreateStem("registered_"+std::to_string(i),0,16);
    UTextureBaseStorage oracle;cache->RegisterAll(oracle);const auto input=MeasurePlaceholderSourceInput(*cache);const auto bound=BoundPlaceholderRegistration(input);
    const auto matches=[&](const UTextureBaseStorage &storage){const auto &a=storage.GetBaseTextures(),&b=oracle.GetBaseTextures();if(a.size()!=b.size())return false;
      for(const auto &[key,expected]:b){const auto it=a.find(key);if(it==a.end())return false;const auto *p=it->second.GetPixelData(),*q=expected.GetPixelData();if(!p || !q || p->Width!=q->Width || p->Height!=q->Height || p->Rgba!=q->Rgba)return false;}return true;};
    const auto prepare=[&]{auto result=std::make_unique<UTextureBaseStorage>();if(!oracle.GetBaseTextures().empty()){TexturePixelData old;old.Width=1;old.Height=1;old.Rgba={0,0,0,0};result->RegisterPixels(oracle.GetBaseTextures().begin()->first,std::move(old));}return result;};
    auto target=prepare();{observation::Scope scope;cache->RegisterAll(*target);}const auto normal=observation::State;const bool same=matches(*target);target.reset();
    const bool ok=same && bound.supported && !bound.overflow && normal.peak<=bound.requested_heap_upper && !observation::State.live && !observation::State.blocks;
    if(!ok)++failures;report["registration_cases"].push_back({{"tiles",seeds},{"pass",ok},{"peak",normal.peak},{"upper",bound.requested_heap_upper},{"attempts",normal.attempts}});
    if(ok)for(size_t i=0;i<normal.attempts;++i){target=prepare();bool refused=false;
      {observation::Scope scope(i);try{cache->RegisterAll(*target);}catch(const std::bad_alloc &){refused=true;}}
      if(!refused || observation::State.peak>bound.requested_heap_upper)++failures;
      cache->RegisterAll(*target);if(!matches(*target))++failures;target.reset();if(observation::State.live || observation::State.blocks)++failures;++transferPrefixes;
    }
    target=prepare();{observation::Scope scope;cache->RegisterAll(*target);}cache.reset();if(!matches(*target))++failures;target.reset();if(observation::State.live || observation::State.blocks)++failures;
  }
  const auto overflow=BoundPlaceholderSource({SIZE_MAX,SIZE_MAX,SIZE_MAX,true,false},SIZE_MAX,16);if(!overflow.overflow)++failures;
  const auto transferOverflow=BoundPlaceholderRegistration({SIZE_MAX,SIZE_MAX,0,true,false,SIZE_MAX});if(!transferOverflow.overflow)++failures;
  const auto upper=BoundPlaceholderSource({0,0,0,true,false},0,16).requested_heap_upper;
  {observation::Scope scope;auto *extra=::operator new(upper+1);::operator delete(extra);}const bool negative=observation::State.peak>upper && !observation::State.live;if(!negative)++failures;
  const auto transferUpper=BoundPlaceholderRegistration({0,0,0,true,false}).requested_heap_upper;
  {observation::Scope scope;auto *extra=::operator new(transferUpper+1);::operator delete(extra);}const bool transferNegative=observation::State.peak>transferUpper && !observation::State.live;if(!transferNegative)++failures;
  report["collision_registration_copies_one_tile"]=oneCopiedTile;report["shared_registration_peak"]=sharedCopyPeak;report["shared_registration_upper"]=sharedCopyUpper;
  report["actual_hash_collision_shared_stem"]=shared;report["last_reference_retires_payload"]=retired;report["persistent_prefix_and_retry_cases"]=prefixes;report["registration_prefix_and_retry_cases"]=transferPrefixes;report["overflow_rejected"]=overflow.overflow && transferOverflow.overflow;report["negative_control"]=negative && transferNegative;report["failed_cases"]=failures;report["status"]=failures?"FAIL":"PASS";
  report["scope"]="One real cache operation ordinary-new requests;prior structural/pixel backing and caller root separate. Standard streams/default allocators;CRT/OS/GPU/sort-stack and merge-global diagnostic strings outside. Input inspection scratch outside observation. No arbitrary asset cap.";
  const auto resolved=fs::weakly_canonical(parent),temp=fs::weakly_canonical(fs::temp_directory_path());if(resolved.parent_path()!=temp || resolved!=temp/parent.filename())return 1;fs::remove_all(resolved);
  if(reportPath){std::ofstream out(reportPath);out<<report.dump(2)<<'\n';}std::cout<<report.dump()<<std::endl;return failures?1:0;
}
