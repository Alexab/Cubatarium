#pragma once
#include "Core/UiPump.h"
#include "Test/OrdinaryNewObservation.h"
#include <nlohmann/json.hpp>
#include <optional>
#include <atomic>
#include <condition_variable>
#include <windows.h>
template<class T,class=void>struct HasMergeWarningFlags:std::false_type{};
template<class T>struct HasMergeWarningFlags<T,std::void_t<decltype(std::declval<const typename T::value_type&>().second.PlaceholderWarned)>>:std::true_type{};
struct MergeWarningInspector:cutum::detail::CatalogStorageInspector
{
  size_t slots{},marked{},value_bytes{};
  template<class T>void Map(const T &values){
    cutum::detail::CatalogStorageInspector::Map(values);
    if constexpr(HasMergeWarningFlags<T>::value){value_bytes=sizeof(typename T::mapped_type);for(const auto &entry:values){slots+=entry.second.PlaceholderWarned.size();marked+=std::count(entry.second.PlaceholderWarned.begin(),entry.second.PlaceholderWarned.end(),true);}}
  }
};
static int RunMergeDiagnosticOwnershipFixture(const char *reportPath,bool baseline)
{
  using namespace cutum;using Json=nlohmann::json;
  const auto parent=fs::temp_directory_path()/("cuba_merge_diag_"+std::to_string(GetCurrentProcessId()));fs::create_directories(parent/"missing/blocks");fs::create_directories(parent/"present/blocks");fs::create_directories(parent/"present/textures/blocks");
  const Json block={{"name","diagnostic_asset"},{"id",kPackBlockIdMin+211},{"textures",Json::array({"missing_stem","missing_stem","missing_stem","missing_stem","missing_stem","missing_stem"})}};
  for(const char *kind:{"missing","present"}){std::ofstream out(parent/kind/"blocks/asset.json");out<<block.dump();}
  {std::ofstream marker(parent/"present/textures/blocks/missing_stem.png");marker<<"file-existence witness only";}
  ResourcePackManifest missing;missing.Id="memo_owner";missing.Root=parent/"missing";ResourcePackManifest present=missing;present.Root=parent/"present";
  // Warm stream formatting and parser state without populating warning history.
  {UPlaceholderTextureCache warm(parent/"warm",16,glm::vec3(0.2f));(void)warm.GetOrCreateStem("warm",0,16);}
  (void)UResourcePack::LoadBlocks(missing);std::cerr<<size_t{0}<<std::endl;UBlockingWorkPump::Clear();
  auto cache=std::make_shared<UPlaceholderTextureCache>(parent/"cache",16,glm::vec3(0.2f),16);
  std::optional<UBlockMergeRegistry> first;
  WarningCounterBuffer warnings;size_t firstWarnings=0,secondWarnings=0,rebuildWarnings=0;observation::Counts normal;MergeWarningInspector flags;
  {ScopedWarningCapture capture(warnings);
    {observation::Scope scope;first.emplace();first->Rebuild({missing},cache,16);}normal=observation::State;firstWarnings=warnings.Count();
    {UBlockMergeRegistry second;second.Rebuild({missing},cache,16);}secondWarnings=warnings.Count()-firstWarnings;
    if(!baseline){first->Rebuild({missing},cache,16);rebuildWarnings=warnings.Count()-firstWarnings-secondWarnings;first->InspectStorage(flags);}
  }
  first.reset();cache.reset();const auto retiredLive=observation::State.live,retiredBlocks=observation::State.blocks;
  Json report={{"schema","merge-diagnostic-ownership.v1"},{"first_warnings",firstWarnings},{"second_warnings",secondWarnings},{"rebuild_warnings",rebuildWarnings},{"retired_live",retiredLive},{"retired_blocks",retiredBlocks}};
  size_t failures=0;
  if(baseline){const bool reproduced=firstWarnings==6 && secondWarnings==0 && retiredLive>0;report["status"]=reproduced?"REPRODUCED":"FAIL";failures=reproduced?0:1;}
  else {
    if(firstWarnings!=6 || secondWarnings!=6 || rebuildWarnings!=6 || retiredLive || retiredBlocks)++failures;
    if(flags.slots!=6 || flags.marked!=6)++failures;
    report["current_warning_slots"]=flags.slots;report["current_marked_faces"]=flags.marked;report["merge_entry_value_bytes"]=flags.value_bytes;report["stage_clock_cell_bytes"]=sizeof(std::chrono::steady_clock::time_point);
    // Runtime overlay makes Rebuild resolve the same pack entry a second time.
    // Its changed placeholder stem must not create another warning history.
    size_t repeatedWarnings=0;
    {auto pixels=std::make_shared<UPlaceholderTextureCache>(parent/"repeat_cache",16,glm::vec3(0.2f),16);UBlockMergeRegistry registry;
      BlockDefinition overlay;overlay.Name="overlay_probe";(void)registry.RegisterRuntimeBlock(overlay,{});
      const auto before=warnings.Count();{ScopedWarningCapture capture(warnings);registry.Rebuild({missing},pixels,16);}repeatedWarnings=warnings.Count()-before;
      MergeWarningInspector current;registry.InspectStorage(current);if(current.slots!=12 || current.marked!=6 || registry.GetCubeDescriptors().size()!=2)++failures;
    }
    report["same_entry_repeat_resolve_warnings"]=repeatedWarnings;if(repeatedWarnings!=6)++failures;
    // These regular files witness only filesystem existence;no image decode or
    // GPU result is attributed to this CPU resolver test.
    std::atomic<size_t> mismatch{0};std::mutex startMutex;std::condition_variable start;size_t ready=0;std::array<std::thread,4> workers;
    for(size_t i=0;i<workers.size();++i)workers[i]=std::thread([&,i]{const auto &pack=i%2?present:missing;
      {std::unique_lock lock(startMutex);if(++ready==4)start.notify_all();else start.wait(lock,[&]{return ready==4;});}
      for(int n=0;n<16;++n){UBlockMergeRegistry registry;registry.Rebuild({pack},nullptr,16);const auto &rows=registry.GetCubeDescriptors();if(rows.size()!=1){++mismatch;continue;}for(const auto &stem:rows[0].Stems)if(stem!=(i%2?"memo_owner/missing_stem":"missing_stem"))++mismatch;}
    });
    for(auto &worker:workers)worker.join();report["parallel_rebuilds"]=64;report["parallel_mismatches"]=mismatch.load();if(mismatch)++failures;
    size_t cleanupFailures=0,retryFailures=0;
    for(size_t i=0;i<normal.attempts;++i){
      cache=std::make_shared<UPlaceholderTextureCache>(parent/("refusal"+std::to_string(i)),16,glm::vec3(0.2f),16);bool refused=false;
      {ScopedWarningCapture capture(warnings);{observation::Scope scope(i);try{first.emplace();first->Rebuild({missing},cache,16);}catch(const std::bad_alloc &){refused=true;}}}
      first.reset();cache.reset();if(!refused || observation::State.live || observation::State.blocks){++cleanupFailures;break;}
      cache=std::make_shared<UPlaceholderTextureCache>(parent/("refusal"+std::to_string(i)),16,glm::vec3(0.2f),16);
      {ScopedWarningCapture capture(warnings);{observation::Scope scope;first.emplace();first->Rebuild({missing},cache,16);}}
      MergeWarningInspector retryFlags;first->InspectStorage(retryFlags);
      if(first->GetCubeDescriptors().size()!=1 || retryFlags.slots!=6 || retryFlags.marked!=6)++retryFailures;
      UTextureBaseStorage pixels;cache->RegisterAll(pixels);if(pixels.GetBaseTextures().size()!=6)++retryFailures;
      first.reset();cache.reset();if(observation::State.live || observation::State.blocks){++cleanupFailures;break;}
    }
    report["persistent_refusal_and_retry_cases"]=normal.attempts;report["cleanup_failures"]=cleanupFailures;report["retry_failures"]=retryFailures;
    failures+=cleanupFailures+retryFailures;
    report["status"]=failures?"FAIL":"PASS";
  }
  report["scope"]="Actual registry warning/memo lifecycle;serial baseline only. Fixed parallel independent registries have same pack ID/different path existence. Pixel/backend acceptance separate;no mutable Rebuild rollback claim.";
  const auto resolved=fs::weakly_canonical(parent),temp=fs::weakly_canonical(fs::temp_directory_path());if(resolved.parent_path()!=temp || resolved!=temp/parent.filename())return 1;fs::remove_all(resolved);
  if(reportPath){std::ofstream out(reportPath);out<<report.dump(2)<<'\n';}std::cout<<report.dump()<<std::endl;return failures?1:0;
}
