#pragma once
#include "WorldGen/Core/AllocatorOwnerCensus.h"
#include <nlohmann/json.hpp>
#include <condition_variable>
#include <stdexcept>

namespace cutum {
inline nlohmann::json RunAllocatorOwnerGraphProbe(){
  using Json=nlohmann::json;using Kind=AllocatorOwnerCensus::Kind;
  if(!GetAllocatorOwnerLayout().supported)return {{"schema","allocator-owner-graph.v1"},{"status","UNSUPPORTED"},{"scope","Current compiled MSVC source profile required"}};
  const auto idx=[](Kind k){return static_cast<size_t>(k);};
  const auto require=[](bool value,const char *why){if(!value)throw std::runtime_error(why);};
  struct Populator final: IUChunkPopulator {
    std::mutex mutex;std::condition_variable cv;bool entered{},release{};
    ChunkPopulateResult Populate(const ChunkPopulateRequest &input)override{
      {std::unique_lock lock(mutex);entered=true;cv.notify_all();
        if(!cv.wait_for(lock,std::chrono::seconds(10),[&]{return release;}))std::abort();}
      ChunkPopulateResult value;value.coord=input.chunkCoord;value.token=input.token;
      value.buffer.SetBlock(input.chunkCoord*CHUNK_SIZE,8);return value;
    }
  } populator;
  UChunkGenerationRegistry tokens;UChunkLoadScheduler scheduler(populator,tokens);UBlockWorld world;
  auto &manager=world.GetChunkManager();manager.EnsureChunk({0,0,0});
  manager.EnsureSectionLightField({1,0,0});manager.RemoveChunk({0,0,0});
  AllocatorOwnerCensus queued(64),active(64),completed(64),applied(64);
  ProceduralSettings settings;settings.MaxHeight=16;const glm::ivec3 coord{3,0,0};
  require(scheduler.RequestLoad(coord,7,settings),"allocator graph queue input");
  queued.Manager(manager,nullptr,false,true);queued.Scheduler(scheduler);
  const auto initial=queued.Result(true);require(initial.supported,"allocator queued graph supported");
  require(initial.blocks[idx(Kind::LightDefault)]==2 && initial.blocks[idx(Kind::SettingsValue)]==1,
    "allocator graph includes AIR field,free-list field and shared settings once");
  require(scheduler.StartQueuedGenerationWorkers(1)==1,"allocator graph submitted");
  {std::unique_lock lock(populator.mutex);require(populator.cv.wait_for(lock,std::chrono::seconds(10),[&]{return populator.entered;}),"allocator graph worker entered");}
  active.Scheduler(scheduler);const bool active_rejected=!active.Result(true).supported;
  {std::lock_guard lock(populator.mutex);populator.release=true;populator.cv.notify_all();}
  require(AllocatorOwnerCensus::WaitSchedulerIdle(scheduler,std::chrono::seconds(10)),"allocator graph worker idle");
  completed.Manager(manager,nullptr,false,true);completed.Scheduler(scheduler);
  const auto ready=completed.Result(true);
  require(ready.supported && ready.blocks[idx(Kind::JobCounted)]==1 && ready.blocks[idx(Kind::ResultGuard)]==1,
    "allocator completed buffer retains source job and counted result guard");
  require(ready.covered_requests==scheduler.GetControlAllocationSnapshot().live_bytes,"allocator graph covered bytes equal actual control requests");
  for(int i=0;i<64;++i){completed.Manager(manager,nullptr,false,true);completed.Scheduler(scheduler);}
  const auto aliases=completed.Result(true);require(aliases.additional_requests==ready.additional_requests && aliases.covered_requests==ready.covered_requests,
    "allocator world/scheduler aliases deduplicate physical owners");
  scheduler.Tick(world,1,0);require(scheduler.IsCommitted(coord),"allocator graph exact commit");
  applied.Manager(manager,nullptr,false,true);applied.Scheduler(scheduler);const auto after=applied.Result(true);
  require(after.supported && !after.blocks[idx(Kind::JobCounted)] && !after.blocks[idx(Kind::ResultGuard)] &&
    scheduler.GetControlAllocationSnapshot().live_bytes==0 && scheduler.BufferReservedBytes()==0,
    "allocator graph retires completed job and controls after apply");
  require(active_rejected,"allocator graph refuses missing active worker view");
  auto scratch_control=std::make_shared<UConcurrentAllocationCountingResource>();
  auto scratch_budget=std::make_shared<UBufferAllocationBudget>(1024*1024);
  auto scratch_job=std::allocate_shared<BufferBudgetJob>(USharedCountingAllocator<BufferBudgetJob>{scratch_control});
  UChunkManager scratch_manager;AllocatorOwnerCensus scratch_census(64);AllocatorOwnerCensus::Snapshot scratch_state;
  {
    UPopulationScratchBudget scratch(scratch_manager,{scratch_budget,scratch_job});
    scratch_manager.EnsureChunk({0,0,0});const auto used=scratch_budget->UsedBytes();
    auto fields=scratch.GetFieldAllocationResource();scratch_census.Scratch(scratch,scratch_control);
    scratch_census.Manager(scratch_manager,&fields,true,true);scratch_state=scratch_census.Result(true);
    require(scratch_state.supported && scratch_state.blocks[idx(Kind::Resource)]==3 &&
      scratch_state.blocks[idx(Kind::LightCounted)]==1 && scratch_state.additional_requests==840 &&
      scratch_state.covered_requests==5096 && scratch_budget->UsedBytes()==used,"allocator active scratch source closure");
  }
  scratch_job.reset();require(!scratch_control->GetSnapshot().live_bytes && !scratch_budget->UsedBytes(),"allocator scratch retires credit/control");
  UChunkManager move_source;UChunkManager moved(std::move(move_source));AllocatorOwnerCensus moved_source(16);
  moved_source.Manager(move_source,nullptr,false,true);
  moved_source.Manager(moved,nullptr,false,true);const auto moved_state=moved_source.Result(true);
  require(moved_state.supported && moved_state.blocks[idx(Kind::Resource)]==1 && moved_state.additional_requests==264,
    "moved-from actual allocator resource counted once across source/target");
  return {{"schema","allocator-owner-graph.v1"},{"status","PASS"},{"queued_blocks",initial.blocks},
    {"completed_blocks",ready.blocks},{"applied_blocks",after.blocks},{"queued_additional",initial.additional_requests},
    {"completed_additional",ready.additional_requests},{"completed_covered",ready.covered_requests},{"applied_additional",after.additional_requests},
    {"active_worker_view_missing_rejected",active_rejected},{"aliases64_deduplicated",true},
    {"free_list_and_air_fields",2},{"settings_owner_deduplicated",true},{"job_and_result_retired",true},
    {"scratch_blocks",scratch_state.blocks},{"scratch_additional",scratch_state.additional_requests},
    {"scratch_covered",scratch_state.covered_requests},{"scratch_credit_unchanged_by_query",true},{"scratch_retired",true},
    {"moved_from_allocator_resource_counted",true},
    {"scope","Actual stable manager/free-list/queued/completed/buffer/scheduler source graph. Active worker and foreign/caller/TLS escapes require separate complete source views."}};
}
}
