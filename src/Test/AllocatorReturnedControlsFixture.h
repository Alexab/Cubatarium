#pragma once
#include "WorldGen/Core/AllocatorOwnerCensus.h"
#include <nlohmann/json.hpp>
#include <stdexcept>

namespace cutum {
struct AllocatorReturnedControlsProbe {
  static nlohmann::json Run(){
    using Json=nlohmann::json;using Kind=AllocatorOwnerCensus::Kind;
    const auto idx=[](Kind kind){return static_cast<size_t>(kind);};
    const auto require=[](bool value,const char *message){if(!value)throw std::runtime_error(message);};
    if(!GetAllocatorOwnerLayout().supported)return {{"schema","allocator-returned-controls.v1"},{"status","UNSUPPORTED"}};
    struct Populator final: IUChunkPopulator {
      ChunkPopulateResult Populate(const ChunkPopulateRequest &request)override{
        ChunkPopulateResult out;out.coord=request.chunkCoord;out.token=request.token;
        out.buffer.SetBlock(request.chunkCoord*CHUNK_SIZE,8);return out;
      }
    } populator;
    UChunkGenerationRegistry tokens;auto scheduler=std::make_unique<UChunkLoadScheduler>(populator,tokens);
    ProceduralSettings settings;settings.MaxHeight=16;
    require(scheduler->RequestLoad({0,0,0},3,settings) && scheduler->StartQueuedGenerationWorkers(1)==1 &&
      AllocatorOwnerCensus::WaitSchedulerIdle(*scheduler,std::chrono::seconds(10)),"returned production completion");
    auto returned=scheduler->Completed.DrainAll();require(returned.size()==1,"returned batch owns actual completion");
    std::weak_ptr<UConcurrentAllocationCountingResource> origin=scheduler->ControlMemory;
    const auto *owners=AllocatorOwnerCensus::BufferOwners(returned[0].result.buffer);require(owners && owners->job,"returned buffer budget context");
    std::weak_ptr<UBufferAllocationBudget> budget=owners->budget;std::weak_ptr<BufferBudgetJob> job=owners->job;
    std::weak_ptr<UChunkLoadScheduler::ResultSlotGuard> guard=returned[0].resultSlot;
    std::weak_ptr<std::atomic<size_t>> counter=returned[0].resultSlot->count;
    const auto widths=AllocatorOwnerCensus::RequestWidths();
    const auto settings_bytes=widths[idx(Kind::SettingsValue)];
    const auto settings_budget_bytes=widths[idx(Kind::SettingsBudget)];
    require(bool(returned[0].request.settings),"returned result owns its immutable request settings");
    std::weak_ptr<const UChunkLoadScheduler::OwnedSettings> settings_owner=returned[0].request.settings;
    std::weak_ptr<UChunkLoadScheduler::SettingsBudgetState> settings_budget=returned[0].request.settings->reservation.budget;
    auto budget_witness=owners->budget;auto counter_witness=returned[0].resultSlot->count;
    const auto resource_history=AllocatorOwnerCensus::RememberResource(origin),budget_history=AllocatorOwnerCensus::RememberBudget(budget);
    const auto job_history=AllocatorOwnerCensus::RememberCountedJob(job,origin),guard_history=AllocatorOwnerCensus::RememberResultGuard(guard,origin);
    const auto counter_history=AllocatorOwnerCensus::RememberResultCounter(counter);
    const auto observe=[&](AllocatorOwnerCensus &out){out.History(resource_history);out.History(budget_history);out.History(job_history,true);out.History(counter_history);out.History(guard_history,true);};
    AllocatorOwnerCensus omitted(64),complete(64);omitted.Scheduler(*scheduler);
    complete.SchedulerWithReturned(*scheduler,returned,origin);const auto owned=complete.Result(true);
    require(!omitted.Result(true).supported && owned.supported && owned.additional_requests==384+settings_bytes &&
      owned.covered_requests==88 && owned.blocks[idx(Kind::SettingsValue)]==1 && owned.blocks[idx(Kind::SettingsBudget)]==1,
      "returned batch required for scheduler retained-slot equality");
    {
      UChunkLoadScheduler::PendingResult alias;alias.resultSlot=returned[0].resultSlot;
      alias.request.settings=returned[0].request.settings;returned.push_back(std::move(alias));
      AllocatorOwnerCensus aliases(64);aliases.SchedulerWithReturned(*scheduler,returned,origin);
      require(aliases.Result(true).supported && aliases.Result(true).additional_requests==384+settings_bytes &&
        aliases.Result(true).covered_requests==88,"caller guard and settings aliases count once");
      returned.pop_back();
      std::vector<UChunkLoadScheduler::PendingResult> foreign(1);
      foreign[0].resultSlot=std::make_shared<UChunkLoadScheduler::ResultSlotGuard>();
      foreign[0].resultSlot->count=std::make_shared<std::atomic<size_t>>(1);
      AllocatorOwnerCensus wrong_counter(64);wrong_counter.SchedulerWithReturned(*scheduler,foreign,origin);
      std::weak_ptr<UConcurrentAllocationCountingResource> missing_origin;
      AllocatorOwnerCensus wrong_origin(64);wrong_origin.SchedulerWithReturned(*scheduler,returned,missing_origin);
      require(!wrong_counter.Result(true).supported && !wrong_origin.Result(true).supported,"unrelated returned counter and missing factory origin rejected");
    }
    scheduler.reset();AllocatorOwnerCensus escaped(64);escaped.ReturnedResults(returned,origin);
    const auto live=escaped.Result(true);require(live.supported && live.additional_requests==336+settings_bytes+settings_budget_bytes &&
      live.covered_requests==88 && !origin.expired() && !job.expired() && counter_witness->load()==1 &&
      !settings_owner.expired() && !settings_budget.expired(),"returned owners and exact request survive scheduler facade");
    returned.clear();require(budget_witness->UsedBytes()==0 && counter_witness->load()==0,"returned last payload releases quota and result credit");
    require(settings_owner.expired() && settings_budget.expired(),"last result retires the immutable request and its settings budget");
    settings_owner.reset();settings_budget.reset();
    budget_witness.reset();counter_witness.reset();
    require(budget.expired() && job.expired() && guard.expired() && counter.expired() && !origin.expired(),"weak controls preserve allocator resource only");
    {
      auto live_origin=origin.lock();AllocatorOwnerCensus no_history(64);no_history.CountedWeakJob(job,live_origin,true);
      AllocatorSourcePhase phase(8101,1);AllocatorSourcePacket packet(64);no_history.ExportInto(packet,phase,0,true);
      require(!packet.supported,"expired control without live-source history is not exported");phase.Close();
    }
    AllocatorOwnerCensus weak(64);observe(weak);const auto shells=weak.Result(true);
    AllocatorSourcePhase phase(8102,1);AllocatorSourcePacket packet(64);weak.ExportInto(packet,phase,0,true);
    const auto joined=JoinAllocatorSourcePackets(phase,{&packet},true);phase.Close();
    require(shells.supported && joined.supported && shells.additional_requests==336 && shells.covered_requests==88 &&
      shells.weak_only[idx(Kind::Budget)]==1 && shells.weak_only[idx(Kind::JobCounted)]==1 &&
      shells.weak_only[idx(Kind::ResultCounter)]==1 && shells.weak_only[idx(Kind::ResultGuard)]==1 &&
      origin.lock()->GetSnapshot().live_bytes==88,"known weak coallocation histories bind actual88 requested control bytes");
    for(int i=0;i<64;++i)observe(weak);require(weak.Result(true).additional_requests==336 && weak.Result(true).covered_requests==88,"weak-history aliases deduplicate");
    AllocatorOwnerCensus late(16);late.History(AllocatorOwnerCensus::RememberCountedJob(job,origin),true);
    require(!late.Result(true).supported,"history cannot be invented after payload expiration");
    guard.reset();require(origin.lock()->GetSnapshot().live_bytes==40,"last weak result guard retires48 bytes");
    job.reset();require(origin.expired(),"last weak job retires allocator payload");
    AllocatorOwnerCensus remainder(64);observe(remainder);const auto remaining=remainder.Result(true);
    AllocatorSourcePhase remaining_phase(8103,1);AllocatorSourcePacket remaining_packet(64);remainder.ExportInto(remaining_packet,remaining_phase,0,true);
    require(remaining.supported && remaining.additional_requests==336 && !remaining.covered_requests &&
      remaining.weak_only[idx(Kind::Resource)]==1 && remaining_packet.supported,"weak allocator shell exported with original coallocation identity");
    remaining_phase.Close();origin.reset();budget.reset();counter.reset();
    AllocatorOwnerCensus empty(64);observe(empty);const auto final=empty.Result(true);
    require(final.supported && !final.additional_requests && !final.covered_requests,"all caller weak roots retire while histories/inspectors stay alive");
    auto first=std::make_shared<UBufferAllocationBudget>(1),second=std::make_shared<UBufferAllocationBudget>(2);
    std::weak_ptr<UBufferAllocationBudget> reassigned=first;const auto old=AllocatorOwnerCensus::RememberBudget(reassigned);reassigned=second;
    AllocatorOwnerCensus mismatch(16);mismatch.History(old);require(!mismatch.Result(true).supported,"live source-slot reassignment rejected");
    return {{"schema","allocator-returned-controls.v1"},{"status","PASS"},{"scheduler_and_caller_additional",owned.additional_requests},
      {"escaped_additional",live.additional_requests},{"escaped_covered",live.covered_requests},{"weak_only",shells.weak_only},
      {"retained_settings_value_bytes",settings_bytes},{"retained_settings_budget_bytes",settings_budget_bytes},
      {"last_result_releases_settings",true},
      {"weak_additional",shells.additional_requests},{"weak_covered",shells.covered_requests},
      {"after_weak_guard_control_requests",40},{"remaining_additional",remaining.additional_requests},{"remaining_covered",remaining.covered_requests},
      {"final_additional",final.additional_requests},{"missing_caller_rejected",true},{"missing_late_reassigned_history_rejected",true},
      {"caller_aliases_and_foreign_counter_controls",true},
      {"expired_packet_history_supported",joined.supported},{"inspector_extends_no_source_lifetime",true},{"quota_and_result_credit0",true},
      {"scope","Actual drained generation completion,buffer/job/result guard after scheduler destruction,and caller-owned weak coallocation histories. Stable weak slots and known factory origin required;no detached-producer/runtime/process verdict."}};
  }
};
inline nlohmann::json RunAllocatorReturnedControlsProbe(){return AllocatorReturnedControlsProbe::Run();}
}
