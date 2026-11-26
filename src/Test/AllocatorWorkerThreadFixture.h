#pragma once
#include "Test/FluidTestHelpers.h"
#include "WorldGen/Core/AllocatorOwnerCensus.h"
#include "WorldGen/Core/PipelineAllocatorThreadView.h"
#include "WorldGen/Core/PopulationAllocatorObserver.h"
#include "Core/Memory/RuntimeInlineSource.h"
#include "Core/Jobs/JobThreadBudget.h"
#include "Core/Jobs/PipelineAdmission.h"
#include <nlohmann/json.hpp>
#include <condition_variable>
#include <stdexcept>

namespace cutum {
struct AllocatorProbeRefusal {
  void (*set)(bool){};size_t (*attempts)(){};
};
inline nlohmann::json RunAllocatorWorkerThreadProbe(AllocatorProbeRefusal refusal={},bool exclusive_work_slot_domain=false){
  using Json=nlohmann::json;using Kind=AllocatorOwnerCensus::Kind;
  const auto require=[](bool value,const char *message){if(!value)throw std::runtime_error(message);};
  if(!GetAllocatorOwnerLayout().supported)return {{"schema","allocator-worker-threads.v1"},{"status","UNSUPPORTED"}};
  const size_t workers=ComputeWorkerThreadCount(JobPoolKind::ChunkGeneration);
  require(workers>0 && workers<=4,"allocator worker source count");
  struct Bridge {
    std::mutex mutex;std::condition_variable cv;
    std::vector<AllocatorSourcePacket> active,idle,post_task;
    std::vector<PipelineStoragePacket> storage;
    std::vector<bool> passive_ok,passive_fresh,idle_clean,observer_restored;
    AllocatorSourcePhase *active_phase{},*idle_phase{};
    size_t active_ready{},idle_ready{},post_ready{};bool release_active{},release_idle{},release_post{},disabled{};int mode{};
    explicit Bridge(size_t n):active(n),idle(n),post_task(n),storage(n),passive_ok(n),passive_fresh(n),idle_clean(n),observer_restored(n){
      for(auto &p:active)p.nodes.reserve(128);for(auto &p:idle)p.nodes.reserve(128);
      for(auto &p:post_task)p.nodes.reserve(128);
    }
  } bridge(workers);
  struct Wrapper final: IUChunkPopulator {
    UPipelineChunkPopulator production;Bridge &bridge;AllocatorProbeRefusal refusal;
    Wrapper(UBlockRegistry &registry,Bridge &b,AllocatorProbeRefusal r):production(registry,nullptr,""),bridge(b),refusal(r){}
    ChunkPopulateResult Populate(const ChunkPopulateRequest &request)override{
      if(bridge.disabled)return production.Populate(request);
      const size_t slot=static_cast<size_t>(request.chunkCoord.x);AllocatorOwnerCensus passive(128);PipelineAllocatorThreadView fresh;
      bool query_ok=true;if(refusal.set)refusal.set(true);
      try{fresh=InspectPipelineCallingThreadAllocators(passive);}catch(...){query_ok=false;}
      if(refusal.set)refusal.set(false);
      {
        std::lock_guard lock(bridge.mutex);bridge.passive_ok[slot]=query_ok && (!refusal.attempts || refusal.attempts()==0);
        bridge.passive_fresh[slot]=!fresh.initialized;
      }
      struct Observer final: PopulationAllocatorObserver {
        Bridge &bridge;size_t slot;const void *decoded;
        Observer(Bridge &b,size_t s,const void *p):bridge(b),slot(s),decoded(p){}
        void Inspect(const PopulationAllocatorView &view)override{
          AllocatorOwnerCensus census(128);const auto state=InspectPipelineCallingThreadAllocators(census,&view.scratch);
          census.Buffer(view.output);census.StbiOutput(decoded);
          census.ExportInto(bridge.active[slot],*bridge.active_phase,slot+1,state.initialized && state.pipeline_present && state.resident_fields>0);
          std::unique_lock lock(bridge.mutex);++bridge.active_ready;bridge.cv.notify_all();
          if(!bridge.cv.wait_for(lock,std::chrono::seconds(10),[&]{return bridge.release_active;}))std::abort();
          if(bridge.mode==2)throw UBufferBudgetExceeded{};
        }
      };
      bool budget_unwind=false;ChunkPopulateResult result;
      {
        auto ledger=std::make_shared<UStbiAllocationLedger>();
        UStbiAllocationScope decoder_scope(ledger);
        std::unique_ptr<void,decltype(&StbiFree)> decoded(StbiAllocate(32),&StbiFree);
        Observer observer(bridge,slot,decoded.get());PopulationAllocatorObserverScope observing(&observer);
        try{result=production.Populate(request);}catch(const UBufferBudgetExceeded &){budget_unwind=true;}
      }
      AllocatorOwnerCensus idle(128);const auto state=InspectPipelineCallingThreadAllocators(idle);idle.Buffer(result.buffer);
      idle.ExportInto(bridge.idle[slot],*bridge.idle_phase,slot+1,state.initialized && state.pipeline_present);
      {
        std::unique_lock lock(bridge.mutex);bridge.idle_clean[slot]=state.resident_fields==0 && !ActiveScopedSharedResource && !detail::ActiveStbiLedger;
        bridge.observer_restored[slot]=detail::ActivePopulationAllocatorObserver==nullptr;
        ++bridge.idle_ready;bridge.cv.notify_all();
        if(!bridge.cv.wait_for(lock,std::chrono::seconds(10),[&]{return bridge.release_idle;}))std::abort();
      }
      if(budget_unwind)throw UBufferBudgetExceeded{};return result;
    }
  };
  auto definitions=std::make_shared<UBlockDefinitionStorage>();
  std::unordered_map<BlockId,BlockDefinition> material_values;std::unordered_map<std::string,BlockId> material_names;
  BlockId next=8;
  for(const char *name:{"stone","water","bedrock","dirt","grass","sand","sandstone","wood","gravel","lava","fire","ore_coal","ore_iron","snow","clay","ice","hellrock"}){
    BlockDefinition value;value.Name=name;value.Physics=BlockPhysicsProfile::Solid();material_values.emplace(next,value);material_names.emplace(name,next++);
  }
  material_names.emplace("grass_block",material_names.at("grass"));
  definitions->ReplaceAll(std::move(material_values),std::move(material_names));UBlockRegistry registry(nullptr,definitions);
  Wrapper populator(registry,bridge,refusal);UChunkGenerationRegistry tokens;UChunkLoadScheduler scheduler(populator,tokens);UBlockWorld world;
  ProceduralSettings settings;settings.Generator=ProceduralGenerator::Flat;settings.MinHeight=0;settings.MaxHeight=16;
  settings.BedrockTopY=0;settings.SeaLevel=-1;settings.FlatSurfaceY=7;
  settings.EnableTrees=settings.EnableGroundCover=settings.EnableDecoration=settings.EnableStructures=settings.EnableOres=settings.EnableCaves=false;
  settings.Ravines.enabled=false;
  const auto stbi_before=GetStbiProcessAllocationSnapshot().live_request_bytes;
  Json cases=Json::array();bool all_controls=true;size_t fresh_queries=0;
  for(int mode=0;mode<3;++mode){
    AllocatorSourcePhase active_phase(1001+100*mode,workers+1),idle_phase(1002+100*mode,workers+1);
    bridge.active_phase=&active_phase;bridge.idle_phase=&idle_phase;bridge.mode=mode;bridge.disabled=false;
    bridge.active_ready=bridge.idle_ready=0;bridge.release_active=bridge.release_idle=false;
    for(size_t i=0;i<workers;++i)require(scheduler.RequestLoad({int(i),0,mode},3,settings),"allocator worker exact request accepted");
    require(scheduler.StartQueuedGenerationWorkers(int(workers))==int(workers),"allocator entire pool submitted");
    {std::unique_lock lock(bridge.mutex);require(bridge.cv.wait_for(lock,std::chrono::seconds(10),[&]{return bridge.active_ready==workers;}),"allocator active barrier deadline");}
    AllocatorOwnerCensus main(128);main.SchedulerAtWorkerBarrier(scheduler,workers);AllocatorSourcePacket main_packet(128);
    main.ExportInto(main_packet,active_phase,0,true);std::vector<const AllocatorSourcePacket*> packets{&main_packet};
    for(const auto &p:bridge.active)packets.push_back(&p);
    const auto joined=JoinAllocatorSourcePackets(active_phase,packets,true);
    auto missing=packets;missing.pop_back();const bool missing_rejected=!JoinAllocatorSourcePackets(active_phase,missing,true).supported;
    auto aliases=packets;for(int i=0;i<64;++i)aliases.push_back(&bridge.active[0]);const auto dedup=JoinAllocatorSourcePackets(active_phase,aliases,true);
    auto corrupt=bridge.active.back();corrupt.epoch+=1;auto wrong=packets;wrong.back()=&corrupt;
    const bool epoch_rejected=!JoinAllocatorSourcePackets(active_phase,wrong,true).supported;
    corrupt=bridge.active.back();corrupt.widths[0]+=1;wrong=packets;wrong.back()=&corrupt;
    const bool profile_rejected=!JoinAllocatorSourcePackets(active_phase,wrong,true).supported;
    const bool unknown_rejected=!JoinAllocatorSourcePackets(active_phase,packets,false).supported;
    const bool controls=missing_rejected && epoch_rejected && profile_rejected && unknown_rejected && dedup.supported &&
      dedup.additional_requests==joined.additional_requests && dedup.covered_requests==joined.covered_requests;
    const auto active_malloc=GetStbiProcessAllocationSnapshot().live_request_bytes-stbi_before;
    const auto active_control=scheduler.GetControlAllocationSnapshot().live_bytes;
    const auto global_active_slots=UPipelineAdmission::Get().WorkSlotsInUse();
    if(mode==1)for(size_t i=0;i<workers;++i)scheduler.Cancel({int(i),0,mode});
    active_phase.Close();const bool closed_rejected=!JoinAllocatorSourcePackets(active_phase,packets,true).supported;
    {std::lock_guard lock(bridge.mutex);bridge.release_active=true;bridge.cv.notify_all();}
    {std::unique_lock lock(bridge.mutex);require(bridge.cv.wait_for(lock,std::chrono::seconds(10),[&]{return bridge.idle_ready==workers;}),"allocator idle barrier deadline");}
    AllocatorOwnerCensus idle_main(128);idle_main.SchedulerAtWorkerBarrier(scheduler,workers);AllocatorSourcePacket idle_packet(128);
    idle_main.ExportInto(idle_packet,idle_phase,0,true);std::vector<const AllocatorSourcePacket*> idle_packets{&idle_packet};
    for(const auto &p:bridge.idle)idle_packets.push_back(&p);
    const auto after=JoinAllocatorSourcePackets(idle_phase,idle_packets,true);
    bool passive=true,idle_clean=true;for(size_t i=0;i<workers;++i){passive=passive && bridge.passive_ok[i];idle_clean=idle_clean && bridge.idle_clean[i] && bridge.observer_restored[i];if(mode==0)fresh_queries+=bridge.passive_fresh[i];}
    const auto idle_malloc=GetStbiProcessAllocationSnapshot().live_request_bytes-stbi_before;
    idle_phase.Close();{std::lock_guard lock(bridge.mutex);bridge.release_idle=true;bridge.cv.notify_all();}
    require(AllocatorOwnerCensus::WaitSchedulerIdle(scheduler,std::chrono::seconds(10)),"allocator finished workers deadline");
    scheduler.Tick(world,int(workers),0);
    if(mode==2){
      require(scheduler.GetPendingQueueCount()==int(workers),"allocator quota unwind preserves every retry");bridge.disabled=true;
      for(size_t i=0;i<workers;++i){require(scheduler.StartQueuedGenerationWorkers(1)==1 && AllocatorOwnerCensus::WaitSchedulerIdle(scheduler,std::chrono::seconds(10)),"allocator serial exact retry");scheduler.Tick(world,1,0);}
    }
    bool outcomes=true;for(size_t i=0;i<workers;++i)outcomes=outcomes && (scheduler.IsCommitted({int(i),0,mode})==(mode!=1));
    const bool debt0=scheduler.GetRetainedGenerationResultCount()==0 && scheduler.GetControlAllocationSnapshot().live_bytes==0 &&
      scheduler.BufferReservedBytes()==0;
    const auto global_after_slots=UPipelineAdmission::Get().WorkSlotsInUse();
    require(global_active_slots>=workers && (!exclusive_work_slot_domain || !global_after_slots),"allocator work-slot source domain");
    AllocatorSourcePhase post_phase(1003+100*mode,workers+1);AllocatorOwnerCensus post_main(128);post_main.Scheduler(scheduler);
    AllocatorSourcePacket post_packet(128);post_main.ExportInto(post_packet,post_phase,0,true);
    bridge.post_ready=0;bridge.release_post=false;
    AllocatorSourcePhase storage_phase(7001+mode,workers);
    for(size_t i=0;i<workers;++i)AllocatorOwnerCensus::EnqueueSchedulerWorkerView(scheduler,[&,i]{
      AllocatorOwnerCensus census(128);const bool source_clear=!ActiveBufferBudget.budget && !ActiveBufferBudget.job &&
        !ActiveBufferBudgetJobSource.origin && !ActiveBufferBudgetJobSource.scheduler_settings_slot &&
        ActiveBufferBudgetJobSource.profile==BufferJobSourceProfile::Unknown && !detail::ActivePopulationAllocatorObserver;
      PipelineAllocatorThreadView state;bool clean=source_clear;
      if(refusal.set)refusal.set(true);
      try{if(clean){state=InspectPipelineCallingThreadAllocators(census);census.ExportInto(bridge.post_task[i],post_phase,i+1,state.initialized && !state.resident_fields);
        ExportPipelineStorage(bridge.storage[i],storage_phase,i);}}
      catch(...){clean=false;}
      if(refusal.set)refusal.set(false);clean=clean && (!refusal.attempts || !refusal.attempts());
      if(!clean){bridge.post_task[i].supported=false;bridge.storage[i].supported=false;}
      std::unique_lock lock(bridge.mutex);++bridge.post_ready;bridge.cv.notify_all();
      if(!bridge.cv.wait_for(lock,std::chrono::seconds(10),[&]{return bridge.release_post;}))std::abort();
    });
    {std::unique_lock lock(bridge.mutex);require(bridge.cv.wait_for(lock,std::chrono::seconds(10),[&]{return bridge.post_ready==workers;}),"allocator post-task barrier deadline");}
    std::vector<const AllocatorSourcePacket*> post_packets{&post_packet};for(const auto &p:bridge.post_task)post_packets.push_back(&p);
    const auto post=JoinAllocatorSourcePackets(post_phase,post_packets,true);
    std::vector<const PipelineStoragePacket*> storage_packets;bool allocator_coverage=true;
    for(size_t i=0;i<workers;++i){storage_packets.push_back(&bridge.storage[i]);const auto &s=bridge.storage[i].source;
      bool found=false;for(const auto &node:bridge.post_task[i].nodes)if(node.kind==0 && node.object==s.metadata_allocator && node.bytes==s.layout.widths[11])found=true;
      allocator_coverage=allocator_coverage && found && bridge.storage[i].thread==bridge.post_task[i].thread && s.initialized && s.idle;
    }
    const auto retained=JoinPipelineStorage(storage_phase,storage_packets,true,true,allocator_coverage);
    auto absent=storage_packets;absent.pop_back();const bool missing_storage=!JoinPipelineStorage(storage_phase,absent,true,true,true).supported;
    auto repeated=storage_packets;for(int alias=0;alias<64;++alias)repeated.push_back(storage_packets[0]);
    const auto aliased_storage=JoinPipelineStorage(storage_phase,repeated,true,true,true);
    auto corrupted=bridge.storage.back();corrupted.source.layout.widths[0]+=1;auto bad_storage=storage_packets;bad_storage.back()=&corrupted;
    bool storage_controls=missing_storage && !JoinPipelineStorage(storage_phase,bad_storage,true,true,true).supported &&
      !JoinPipelineStorage(storage_phase,storage_packets,false,true,true).supported &&
      !JoinPipelineStorage(storage_phase,storage_packets,true,false,true).supported &&
      !JoinPipelineStorage(storage_phase,storage_packets,true,true,false).supported && aliased_storage.supported &&
      aliased_storage.additional_requests==retained.additional_requests;
    corrupted=bridge.storage.back();corrupted.epoch+=1;
    storage_controls=storage_controls && !JoinPipelineStorage(storage_phase,bad_storage,true,true,true).supported;
    if(workers>1){corrupted=bridge.storage.back();corrupted.thread=bridge.storage.front().thread;
      storage_controls=storage_controls && !JoinPipelineStorage(storage_phase,bad_storage,true,true,true).supported;}
    std::array<size_t,PipelineStorageCells> retained_rows{};
    for(const auto &packet:bridge.storage)for(size_t i=0;i<PipelineStorageCells;++i)retained_rows[i]+=packet.source.requests[i];
    const auto &storage_layout=bridge.storage[0].source.layout;
    require(retained.supported && storage_controls && retained.participants==workers && !retained.covered_requests &&
      retained.additional_requests==workers*(storage_layout.widths[0]+storage_layout.widths[8]+storage_layout.widths[9]+storage_layout.widths[10]),
      "complete idle pipeline/world source requests include whole object and exclude already counted allocator roots");
    storage_phase.Close();const bool storage_closed=!JoinPipelineStorage(storage_phase,storage_packets,true,true,true).supported;
    post_phase.Close();
    {std::lock_guard lock(bridge.mutex);bridge.release_post=true;bridge.cv.notify_all();}
    require(AllocatorOwnerCensus::WaitSchedulerIdle(scheduler,std::chrono::seconds(10)),"allocator post-task probes idle");
    require(post.supported && post.blocks[0]==1+workers && post.blocks[1]==0 && post.blocks[4]==0 && post.blocks[9]==0 &&
      post.covered_requests==0,"allocator post-task TLS descriptors and job owners retired");
    all_controls=all_controls && controls && closed_rejected;
    if(!(joined.supported && after.supported && passive && idle_clean && outcomes && debt0)){
      Json flags=Json::array();for(const auto &p:bridge.active)flags.push_back(p.supported);
      const auto failure=Json{{"allocator_worker_mode",mode},{"active_supported",joined.supported},{"idle_supported",after.supported},
        {"worker_packets",flags},{"main_packet",main_packet.supported},{"passive",passive},{"idle_clean",idle_clean},
        {"outcomes",outcomes},{"debt0",debt0},{"active_blocks",joined.blocks},{"idle_blocks",after.blocks}};
      throw std::runtime_error("allocator worker source view: "+failure.dump());
    }
    require(joined.supported && after.supported && !joined.overflow && !after.overflow && passive && idle_clean && outcomes && debt0,
      "allocator complete source views and terminal outcomes");
    require(joined.blocks[0]==1+2*workers && joined.blocks[1]==workers && joined.blocks[4]==workers && joined.blocks[6]>0 &&
      joined.blocks[9]==workers && joined.blocks[7]==1 && joined.blocks[2]==1 && after.blocks[0]==1+workers &&
      !after.blocks[1] && !after.blocks[6] && active_malloc==workers*64 && !idle_malloc,
      "allocator cross-worker shared roots/private managers/decoder lifetimes");
    cases.push_back({{"mode",mode},{"workers",workers},{"active_blocks",joined.blocks},{"active_covered",joined.covered},
      {"active_additional",joined.additional_requests},{"active_covered_requests",joined.covered_requests},{"active_control_base",active_control},
      {"active_decoder_request_base",active_malloc},{"idle_blocks",after.blocks},{"idle_additional",after.additional_requests},
      {"idle_covered_requests",after.covered_requests},{"idle_decoder_request_base",idle_malloc},{"participant_count",joined.participants},
      {"post_task_blocks",post.blocks},{"post_task_additional",post.additional_requests},{"post_task_covered_requests",post.covered_requests},
      {"post_task_sources_clear",post.supported},
      {"pipeline_storage_requests",retained_rows},{"pipeline_storage_additional",retained.additional_requests},
      {"pipeline_storage_controls",storage_controls && storage_closed},{"pipeline_allocator_coverage",allocator_coverage},
      {"global_work_slots_active",global_active_slots},{"global_work_slots_after",global_after_slots},
      {"exclusive_work_slot_domain",exclusive_work_slot_domain},
      {"all_idle_scopes_restored",idle_clean},{"passive_query_ok",passive},{"outcomes_correct",outcomes},{"debt0",debt0},
      {"missing_epoch_profile_domain_alias_controls",controls},{"closed_phase_rejected",closed_rejected}});
  }
  require(fresh_queries==workers && all_controls,"allocator fresh passive/no-init and union controls");
  return {{"schema","allocator-worker-threads.v1"},{"status","PASS"},{"workers",workers},{"cases",cases},
    {"fresh_no_world_initialization_queries",fresh_queries},{"ordinary_refusal_available",refusal.set!=nullptr},
    {"source_widths",AllocatorOwnerCensus::RequestWidths()},{"pipeline_holder_inline",GetPipelineRuntimeInlineLayout().whole},
    {"pipeline_storage_layout",bridge.storage.front().source.layout.widths},
    {"budget_source_inline",sizeof(ActiveBufferBudgetJobSource)},{"observer_inline",PopulationAllocatorObserverInlineBytes()},
    {"scope","Real pipeline full-pool owner-thread snapshots;only plain identity packets cross the barrier. One open source phase required. Opaque control requests and decoder request base recorded separately;no whole process/CRT/GPU/full S6 verdict."}};
}
}
