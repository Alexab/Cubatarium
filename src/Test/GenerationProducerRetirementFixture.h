#pragma once
#include "World/Streaming/ChunkGenerationRetirement.h"
#include "WorldGen/Core/AllocatorOwnerCensus.h"
#include <nlohmann/json.hpp>
#include <condition_variable>
#include <stdexcept>

namespace cutum {
struct GenerationRetirementGate {
  std::mutex mutex;std::condition_variable cv;bool entered{},release{},cancelled{},release_on_cancel{};
  size_t destroyed{};bool scheduler_before_populator{true};
  std::weak_ptr<UConcurrentAllocationCountingResource> control;
  void Release(){std::lock_guard lock(mutex);release=true;cv.notify_all();}
  void Wait(){std::unique_lock lock(mutex);if(!cv.wait_for(lock,std::chrono::seconds(10),[&]{return entered;}))throw std::runtime_error("generation retirement worker gate deadline");}
  ChunkPopulateResult Populate(const ChunkPopulateRequest &r){
    {std::unique_lock lock(mutex);entered=true;cv.notify_all();
      const auto deadline=std::chrono::steady_clock::now()+std::chrono::seconds(10);
      while(!release && !(release_on_cancel && r.shouldCancel && r.shouldCancel())){
        if(std::chrono::steady_clock::now()>=deadline)throw std::runtime_error("generation retirement release deadline");
        cv.wait_for(lock,std::chrono::milliseconds(1));
      }}
    cancelled=r.shouldCancel && r.shouldCancel();
    ChunkPopulateResult out;out.coord=r.chunkCoord;out.token=r.token;
    out.buffer.SetBlock(r.chunkCoord*CHUNK_SIZE,8);return out;
  }
  void Destroyed(){++destroyed;scheduler_before_populator=scheduler_before_populator && control.expired();}
};
struct GenerationGateRelease {GenerationRetirementGate &gate;~GenerationGateRelease(){gate.Release();}};
struct GenerationProducerRetirementProbe {
  static nlohmann::json Run(){
    using Json=nlohmann::json;
    const auto require=[](bool value,const char *message){if(!value)throw std::runtime_error(message);};
    struct Populator final: IUChunkPopulator {
      GenerationRetirementGate &gate;explicit Populator(GenerationRetirementGate &g):gate(g){}
      ~Populator(){gate.Destroyed();}
      ChunkPopulateResult Populate(const ChunkPopulateRequest &r)override{return gate.Populate(r);}
    };
    Json rows=Json::array();double worst_busy_ms=0;
    for(int cycle=0;cycle<8;++cycle){
      GenerationRetirementGate gate;UChunkGenerationRegistry tokens;
      auto populator=std::make_unique<Populator>(gate);
      auto scheduler=std::make_unique<UChunkLoadScheduler>(*populator,tokens);
      GenerationGateRelease cleanup{gate};bool requested=false;
      gate.control=scheduler->ControlMemory;
      std::weak_ptr<UBufferAllocationBudget> budget=scheduler->BufferBudget;
      std::weak_ptr<std::atomic<size_t>> counter=scheduler->RetainedResults;
      ProceduralSettings settings;settings.MaxHeight=16;const glm::ivec3 coord{cycle,0,0};
      require(scheduler->RequestLoad(coord,1,settings) && scheduler->StartQueuedGenerationWorkers(1)==1,"busy actual generation admitted");gate.Wait();
      auto *old_scheduler=scheduler.get();auto *old_populator=populator.get();
      const auto begin=std::chrono::steady_clock::now();
      for(int retry=0;retry<64;++retry){
        require(!TryRetireChunkGeneration(scheduler,populator,requested) && requested &&
          scheduler.get()==old_scheduler && populator.get()==old_populator && !gate.destroyed,"busy retries retain one visible producer pair");
        AllocatorOwnerCensus incomplete(16);incomplete.Scheduler(*scheduler);
        require(!incomplete.Result(true).supported,"busy worker without its source view cannot be certified");
      }
      const double busy_ms=std::chrono::duration<double,std::milli>(std::chrono::steady_clock::now()-begin).count();
      worst_busy_ms=std::max(worst_busy_ms,busy_ms);
      require(busy_ms<500 && !gate.control.expired() && !budget.expired() && !counter.expired(),"64 busy polls do not wait on active task or lose owners");
      gate.Release();require(AllocatorOwnerCensus::WaitSchedulerIdle(*scheduler,std::chrono::seconds(10)),"cancelled task and closure retire");
      require(gate.cancelled,"cancellation token remains usable until worker retirement");
      require(TryRetireChunkGeneration(scheduler,populator,requested) && !scheduler && !populator && gate.destroyed==1 &&
        gate.scheduler_before_populator && gate.control.expired() && budget.expired() && counter.expired(),"idle retirement frees all producer payloads in order");
      rows.push_back({{"cycle",cycle},{"busy_retries",64},{"busy_poll_ms",busy_ms},{"pair_destroyed",1},{"all_payloads_retired",true},{"cancellation_callback_valid",true}});
    }
    GenerationRetirementGate next;next.release=true;UChunkGenerationRegistry tokens;
    auto populator=std::make_unique<Populator>(next);auto scheduler=std::make_unique<UChunkLoadScheduler>(*populator,tokens);
    GenerationGateRelease cleanup{next};next.control=scheduler->ControlMemory;ProceduralSettings settings;settings.MaxHeight=16;
    require(scheduler->RequestLoad({0,0,99},1,settings) && scheduler->StartQueuedGenerationWorkers(1)==1 &&
      AllocatorOwnerCensus::WaitSchedulerIdle(*scheduler,std::chrono::seconds(10)),"successor actual worker completes");
    UBlockWorld world;scheduler->Tick(world,1,0);
    require(scheduler->IsCommitted({0,0,99}) && world.GetBlock({0,0,99*CHUNK_SIZE})==8 &&
      !scheduler->BufferReservedBytes() && !scheduler->GetRetainedGenerationResultCount() && !scheduler->GetControlAllocationSnapshot().live_bytes,"successor commits exact requested terrain without cohort debt");
    scheduler.reset();populator.reset();require(next.destroyed==1 && next.scheduler_before_populator,"successor normal owners retire in order");
    return {{"schema","generation-producer-retirement.v1"},{"status","PASS"},{"cycles",rows},{"max_visible_pairs",1},
      {"busy_retries",512},{"worst_busy_poll_ms",worst_busy_ms},{"exact_successor_committed",true},{"cohort_debt0",true},
      {"scheduler_before_populator",true},{"scope","Actual production retirement helper and generation jobs. Caller retains same source graph until pool idle including closure retirement;not all runtime pools/process-exit/CRT/GPU/process acceptance."}};
  }
};
inline nlohmann::json RunGenerationProducerRetirementProbe(){return GenerationProducerRetirementProbe::Run();}
}
