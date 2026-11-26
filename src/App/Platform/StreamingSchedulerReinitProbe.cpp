#include "App/Platform/StreamingSchedulerReinitProbe.h"
#include "World/Core/World.h"
#include "World/Streaming/WorldStreaming.h"
#include "Test/GenerationProducerRetirementFixture.h"
#include "Test/PipelineRetainedStorageFixture.h"
#include "Test/RuntimeNameStorageFixture.h"
#include "Test/EngineThreadStorageFixture.h"
#include "Test/WorldOwnedStorageFixture.h"

namespace cutum {
struct StreamingSchedulerReinitProbe {
  static nlohmann::json Run(){
    using Json=nlohmann::json;Json report;
    const auto require=[](bool value,const char *message){if(!value)throw std::runtime_error(message);};
    report["generation_producer_retirement"]=RunGenerationProducerRetirementProbe();
    {
      struct NativeGate final:UPipelineChunkPopulator {
        GenerationRetirementGate &gate;
        NativeGate(UBlockRegistry &registry,GenerationRetirementGate &g):UPipelineChunkPopulator(registry,nullptr,{}),gate(g){}
        ~NativeGate(){gate.Destroyed();}
        ChunkPopulateResult Populate(const ChunkPopulateRequest &r)override{return gate.Populate(r);}
      };
      Json cycles=Json::array();
      for(int cycle=0;cycle<3;++cycle){
        GenerationRetirementGate gate;UWorld world(nullptr,nullptr);
        world.BlockRegistry=std::make_unique<UBlockRegistry>(nullptr,std::make_shared<UBlockDefinitionStorage>());
        ProceduralSettings settings;settings.MinHeight=0;settings.MaxHeight=15;settings.AsyncChunkGeneration=true;
        world.SetProceduralSettings(settings,false);
        auto &stream=*world.Streaming;stream.InitStreamerCallbacks(world);
        stream.ChunkScheduler.reset();stream.ChunkPopulator=std::make_unique<NativeGate>(*world.BlockRegistry,gate);
        stream.ChunkScheduler=std::make_unique<UChunkLoadScheduler>(*stream.ChunkPopulator,stream.ChunkGenTokens);
        GenerationGateRelease cleanup{gate};gate.control=stream.ChunkScheduler->ControlMemory;
        const glm::ivec3 coord{cycle,0,11};
        require(stream.ChunkScheduler->RequestLoad(coord,1,settings) && stream.ChunkScheduler->StartQueuedGenerationWorkers(1)==1,"native busy generation started");gate.Wait();
        auto *old=stream.ChunkScheduler.get();auto *old_populator=stream.ChunkPopulator.get();
        const auto begin=std::chrono::steady_clock::now();
        for(int retry=0;retry<64;++retry){
          world.EnsureStreamingActiveAfterBackgroundQuiesce();
          stream.RetryChunkSchedulerReplacement(world);
          require(stream.IsChunkSchedulerReplacementPending() && stream.ChunkScheduler.get()==old &&
            stream.ChunkPopulator.get()==old_populator && !gate.destroyed && !stream.Streamer->IsEnabled(),"native repeated resume retains one busy pair and gates admission");
        }
        const double busy_ms=std::chrono::duration<double,std::milli>(std::chrono::steady_clock::now()-begin).count();
        require(busy_ms<500,"native busy resume does not wait for active worker");
        if(cycle==1){
          stream.PauseChunkGeneration(std::chrono::milliseconds(0));stream.RetryChunkSchedulerReplacement(world);
          require(!stream.IsChunkSchedulerReplacementPending() && stream.ChunkScheduler.get()==old && !stream.Streamer->IsEnabled(),"background pause cancels replacement retry without losing producer");
          world.ResumeAfterSessionSave();require(stream.IsChunkSchedulerReplacementPending(),"explicit resume requests pending replacement again");
        }
        std::unique_ptr<UBlockRegistry> saved_registry;
        if(cycle==2){
          saved_registry=std::move(world.BlockRegistry);stream.InitStreamerCallbacks(world);
          require(stream.ChunkScheduler.get()==old && stream.IsChunkSchedulerReplacementPending(),"missing replacement registry keeps still-busy source pair");
        }
        gate.Release();require(AllocatorOwnerCensus::WaitSchedulerIdle(*old,std::chrono::seconds(10)),"native cancelled callable retirement");
        stream.RetryChunkSchedulerReplacement(world);
        require(!stream.IsChunkSchedulerReplacementPending() && gate.destroyed==1 && gate.control.expired() &&
          gate.scheduler_before_populator && gate.cancelled,"native old pair retires before replacement publication");
        if(cycle==2){
          require(!stream.ChunkScheduler && !stream.ChunkPopulator && !stream.Streamer->IsEnabled(),"missing registry leaves retired pair empty without blocking");
          world.BlockRegistry=std::move(saved_registry);stream.InitStreamerCallbacks(world);
        }
        require(stream.ChunkScheduler && stream.ChunkPopulator && stream.ChunkScheduler->MarkDirty &&
          stream.Streamer->IsEnabled(),"native successor callbacks and streamer resume installed");
        cycles.push_back({{"cycle",cycle},{"busy_retries",64},{"busy_poll_ms",busy_ms},{"visible_pairs_upper",1},
          {"old_payloads_retired",true},{"pause_resume",cycle==1},{"absent_registry",cycle==2},{"successor_callbacks",true}});
      }
      GenerationRetirementGate teardown;teardown.release_on_cancel=true;
      {
        UWorld world(nullptr,nullptr);
        world.BlockRegistry=std::make_unique<UBlockRegistry>(nullptr,std::make_shared<UBlockDefinitionStorage>());
        auto &stream=*world.Streaming;
        stream.ChunkPopulator=std::make_unique<NativeGate>(*world.BlockRegistry,teardown);
        stream.ChunkScheduler=std::make_unique<UChunkLoadScheduler>(*stream.ChunkPopulator,stream.ChunkGenTokens);
        teardown.control=stream.ChunkScheduler->ControlMemory;ProceduralSettings settings;settings.MaxHeight=16;
        require(stream.ChunkScheduler->RequestLoad({0,0,12},1,settings) && stream.ChunkScheduler->StartQueuedGenerationWorkers(1)==1,"native destructor busy task started");teardown.Wait();
      }
      require(teardown.cancelled && teardown.destroyed==1 && teardown.scheduler_before_populator && teardown.control.expired(),"normal streaming destructor cancels and joins before populator/token destruction");
      report["streaming_scheduler_reinit"]={{"schema","streaming-scheduler-reinit-native.v1"},{"status","PASS"},{"cycles",cycles},
        {"normal_destruction_joins",true},{"max_visible_pairs",1},{"scope","Actual isolated UWorld session-resume path and production tick retry hook with gated generation. No process-exit/all-pools/whole-process/S6 verdict."}};
    }
    report["pipeline_retained_storage"]=RunPipelineRetainedStorageProbe();
    report["runtime_name_storage"]=RunRuntimeNameStorageProbe();
    report["engine_thread_storage"]=RunEngineThreadStorageProbe();
    report["world_owned_storage"]=RunWorldOwnedStorageProbe();
    return report;
  }
};
nlohmann::json RunStreamingSchedulerReinitProbe(){return StreamingSchedulerReinitProbe::Run();}
}
