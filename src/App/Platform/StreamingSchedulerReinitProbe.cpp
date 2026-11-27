#include "App/Platform/StreamingSchedulerReinitProbe.h"
#include "Core/Progress/IUProgressSink.h"
#include "World/Core/World.h"
#include "World/Streaming/WorldStreaming.h"
#include "Test/GenerationProducerRetirementFixture.h"
#include "Test/PipelineRetainedStorageFixture.h"
#include "Test/RuntimeNameStorageFixture.h"
#include "Test/EngineThreadStorageFixture.h"
#include "Test/WorldOwnedStorageFixture.h"
#include "Test/MemberCallbackStorageFixture.h"
#include "Test/RootStorageCompositionFixture.h"
#include <fstream>

namespace cutum {
struct StreamingSchedulerReinitProbe {
  struct NativeGate final : UPipelineChunkPopulator {
    GenerationRetirementGate &gate;
    NativeGate(UBlockRegistry &registry, GenerationRetirementGate &g)
        : UPipelineChunkPopulator(registry, nullptr, {}), gate(g) {}
    ~NativeGate() { gate.Destroyed(); }
    ChunkPopulateResult Populate(const ChunkPopulateRequest &request) override
    {
      return gate.Populate(request);
    }
  };

  static nlohmann::json CheckWorldReplacement(const std::filesystem::path &root)
  {
    namespace fs = std::filesystem;
    using namespace std::chrono_literals;
    using Clock = std::chrono::steady_clock;
    using Json = nlohmann::json;
    const auto require = [](bool value, const char *message) {
      if (!value) throw std::runtime_error(message);
    };
    GenerationRetirementGate gates[3];
    UWorld world(nullptr, nullptr);
    world.BlockRegistry = std::make_unique<UBlockRegistry>(
        nullptr, std::make_shared<UBlockDefinitionStorage>());
    ProceduralSettings settings;
    settings.Generator = ProceduralGenerator::Flat;
    settings.MinHeight = 0;
    settings.MaxHeight = 15;
    settings.AsyncChunkGeneration = true;
    settings.AsyncChunkIo = false;
    world.SetProceduralSettings(settings, false);
    auto &stream = *world.Streaming;
    fs::create_directory(root);
    Json cycles = Json::array();
    for (int cycle = 0; cycle < 3; ++cycle)
    {
      auto &gate = gates[cycle];
      stream.InitStreamerCallbacks(world);
      stream.ChunkScheduler.reset();
      stream.ChunkPopulator = std::make_unique<NativeGate>(*world.BlockRegistry, gate);
      stream.ChunkScheduler = std::make_unique<UChunkLoadScheduler>(*stream.ChunkPopulator,
                                                                   stream.ChunkGenTokens);
      GenerationGateRelease cleanup{gate};
      auto *old = stream.ChunkScheduler.get();
      auto *old_populator = stream.ChunkPopulator.get();
      gate.control = old->ControlMemory;
      const glm::ivec3 active{cycle, 0, 11};
      const glm::ivec3 queued{cycle, 0, 12};
      require(old->RequestLoad(active, 1, settings) && old->StartQueuedGenerationWorkers(1) == 1 &&
                  old->RequestLoad(queued, 1, settings), "replacement actual active and queued generation");
      gate.Wait();
      const auto parked_at = Clock::now();
      const auto folder = root / ("cycle-" + std::to_string(cycle));
      world.SetWorldFolderPath(folder.string());
      const bool fail_save = cycle == 2;
      auto destination = folder;
      if (fail_save)
      {
        const auto blocker = root / "regular-file";
        std::ofstream(blocker) << "directory creation is denied by the fixture";
        destination = blocker / "world";
      }
      UNullProgressSink progress;
      world.BeginCooperativeSave(destination.string(), false);
      while (!world.TickCooperativeSave(progress, 1))
      {
        require(Clock::now() - parked_at < 5s, "preliminary save completes with a parked generator");
        std::this_thread::yield();
      }
      require(world.GetCooperativeSaveError().empty() != fail_save &&
                  (fail_save || fs::is_regular_file(folder / "world_data.json")),
              "actual preliminary save returns the expected result");
      require(stream.ChunkScheduler.get() == old && stream.ChunkPopulator.get() == old_populator &&
                  !gate.control.expired() && old->GetPendingQueueCount() == 0,
              "save keeps the one cancelled generation producer pair owned");

      const glm::ivec3 marker(1, 2, 3);
      world.BlockWorld.SetBlock(marker, 8);
      const auto poll_until = Clock::now() + 200ms;
      size_t polls = 0;
      do
      {
        if (fail_save)
        {
          world.ResumeAfterSessionSave();
          require(stream.IsChunkSchedulerReplacementPending(), "failed save can resume the old world");
        }
        else
        {
          require(!world.TryAbandonTerrainForWorldReplace(), "world replacement waits for active generation");
          stream.InitStreamerCallbacks(world);
        }
        require(stream.ChunkScheduler.get() == old && stream.ChunkPopulator.get() == old_populator &&
                    !stream.Streamer->IsEnabled() && !gate.destroyed &&
                    world.GetWorldFolderPath() == folder.string() && world.BlockWorld.GetBlock(marker) == 8,
                "parked poll retains the old world and allows only one pair");
        ++polls;
        std::this_thread::sleep_for(5ms);
      } while (Clock::now() < poll_until);

      gate.Release();
      require(AllocatorOwnerCensus::WaitSchedulerIdle(*old, 5s), "old population and captured job retire");
      if (fail_save)
      {
        stream.RetryChunkSchedulerReplacement(world);
        require(world.BlockWorld.GetBlock(marker) == 8, "failed save recovery preserves the old terrain");
      }
      else
      {
        require(world.TryAbandonTerrainForWorldReplace() && !stream.ChunkScheduler && !stream.ChunkPopulator &&
                    world.BlockWorld.GetBlock(marker) == BLOCK_AIR,
                "world replacement retires the old owners before clearing terrain");
        stream.InitStreamerCallbacks(world);
      }
      require(gate.cancelled && gate.destroyed == 1 && gate.scheduler_before_populator && gate.control.expired() &&
                  stream.ChunkScheduler && stream.ChunkPopulator && stream.Streamer->IsEnabled(),
              "the old pair and controls retire exactly once before enabling a successor");
      const glm::ivec3 successor{cycle, 0, 20};
      auto &next = *stream.ChunkScheduler;
      require(next.RequestLoad(successor, 1, settings) && next.StartQueuedGenerationWorkers(1) == 1 &&
                  AllocatorOwnerCensus::WaitSchedulerIdle(next, 5s), "real Flat successor finishes generation");
      next.Tick(world.BlockWorld, 1, 0);
      require(next.IsCommitted(successor) && !next.IsCommitted(active) && !next.IsCommitted(queued) &&
                  !next.GetRetainedGenerationResultCount(), "only the current successor commits");
      cycles.push_back({{"cycle", cycle}, {"save_failed", fail_save}, {"parked_polls", polls},
                        {"held_at_least_ms", 200}, {"old_pair_retired", true}, {"successor_committed", true}});
    }
    return {{"schema", "world-generation-replacement.v1"}, {"status", "PASS"}, {"cycles", cycles}};
  }

  static nlohmann::json Run(const std::filesystem::path &replacement_root){
    using Json=nlohmann::json;Json report;
    const auto require=[](bool value,const char *message){if(!value)throw std::runtime_error(message);};
    report["generation_producer_retirement"]=RunGenerationProducerRetirementProbe();
    {
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
    report["world_generation_replacement"] = CheckWorldReplacement(replacement_root);
    report["pipeline_retained_storage"]=RunPipelineRetainedStorageProbe();
    report["runtime_name_storage"]=RunRuntimeNameStorageProbe();
    report["engine_thread_storage"]=RunEngineThreadStorageProbe();
    report["world_owned_storage"]=RunWorldOwnedStorageProbe();
    report["member_callback_storage"]=RunMemberCallbackStorageProbe();
    report["root_storage_composition"]=RunRootStorageCompositionProbe();
    return report;
  }
};
nlohmann::json RunStreamingSchedulerReinitProbe(const std::filesystem::path &replacement_root)
{
  return StreamingSchedulerReinitProbe::Run(replacement_root);
}
}
