#pragma once
#include "WorldGen/Core/AllocatorOwnerCensus.h"

namespace allocator_census_test {
inline nlohmann::json Run(){
  using namespace cutum;using Json=nlohmann::json;using Kind=AllocatorOwnerCensus::Kind;
  const auto index=[](Kind k){return static_cast<size_t>(k);};
  AllocatorOwnerCensus live(32),weak(32),collision(32),missing(32),unknown(8);
  bool exact{},no_owners{},zero_query{},weak_exact{},final0{},controls{},unknown_hook{};
  AllocatorOwnerCensus::Snapshot counted,expired;
  {
    observation::Scope scope;
    auto control=std::make_shared<UConcurrentAllocationCountingResource>();
    auto fields=std::make_shared<UConcurrentAllocationCountingResource>();
    auto budget=std::make_shared<UBufferAllocationBudget>(1024*1024);
    auto job=std::allocate_shared<BufferBudgetJob>(USharedCountingAllocator<BufferBudgetJob>{control});
    fields->SetBeforeAllocationFn(BufferBudgetReserveCallback{{budget,job}});
    fields->SetAfterDeallocationFn(BufferBudgetReleaseCallback{{budget,job}});
    std::shared_ptr<SectionLightField> field;
    {UScopedSharedAllocationResource active(fields);field=MakeScopedShared<SectionLightField>();}
    std::weak_ptr<SectionLightField> weak_field=field;std::weak_ptr<BufferBudgetJob> weak_job=job;
    std::array<long,5> before{control.use_count(),fields.use_count(),budget.use_count(),job.use_count(),field.use_count()};
    const auto attempts=observation::State.attempts,bytes=observation::State.live;observation::State.fail_after=attempts;
    live.CountedJob(job,control,true);live.CountedField(field,fields,true);
    for(int i=0;i<64;++i){live.CountedField(field,fields,true);live.Resource(control);}
    counted=live.Result(true);
    no_owners=before==std::array<long,5>{control.use_count(),fields.use_count(),budget.use_count(),job.use_count(),field.use_count()};
    zero_query=observation::State.attempts==attempts;
    exact=counted.supported && !counted.overflow && counted.additional_requests==576 && counted.covered_requests==5096 &&
      counted.additional_requests+counted.covered_requests==bytes && counted.blocks[index(Kind::Resource)]==2 &&
      counted.blocks[index(Kind::Budget)]==1 && counted.blocks[index(Kind::JobCounted)]==1 && counted.blocks[index(Kind::LightCounted)]==1;
    collision.DefaultJob(job);collision.CountedJob(job,control,true);
    collision.CountedJob(job,fields,true);
    missing.Resource(fields);
    controls=!live.Result(false).supported && !collision.Result(true).supported && !missing.Result(true).supported;
    observation::State.fail_after=SIZE_MAX;
    field.reset();weak.CountedWeakJob(weak_job,control,true);weak.CountedWeakField(weak_field,fields,true);expired=weak.Result(true);
    weak_exact=expired.supported && expired.weak_only[index(Kind::LightCounted)]==1 &&
      expired.additional_requests==counted.additional_requests && expired.covered_requests==counted.covered_requests;
    // Keep both inspectors alive while all source slots/payloads retire. Their
    // row destructors never read borrowed source addresses or release owners.
    control.reset();fields.reset();budget.reset();job.reset();weak_field.reset();weak_job.reset();
    final0=!observation::State.live && !observation::State.blocks;
  }
  auto custom=std::make_shared<cutum::UConcurrentAllocationCountingResource>();bool called=false;
  custom->SetBeforeAllocationFn([&](size_t){called=true;});unknown.Resource(custom);
  unknown_hook=!unknown.Result(true).supported && !called;custom.reset();
  AllocatorOwnerCensus decoder_tls(8),decoder_output(8),decoder_weak(8),process_only(8);
  bool decoder_graph{},decoder_final0{},default_process_only{};
  const auto decoder_before=GetStbiProcessAllocationSnapshot().live_request_bytes;
  {
    observation::Scope scope;auto ledger=std::make_shared<UStbiAllocationLedger>();std::weak_ptr<UStbiAllocationLedger> probe=ledger;void *p{};
    {UStbiAllocationScope active(ledger);p=StbiAllocate(32);decoder_tls.CallingThread();decoder_tls.StbiOutput(p);}
    ledger.reset();decoder_output.StbiOutput(p);
    const auto live_ledger=decoder_output.Result(true);StbiFree(p);decoder_weak.WeakLedger(probe);
    const auto weak_ledger=decoder_weak.Result(true);
    decoder_graph=decoder_tls.Result(true).blocks[index(Kind::Ledger)]==1 && live_ledger.supported && live_ledger.additional_requests==208 &&
      weak_ledger.supported && weak_ledger.weak_only[index(Kind::Ledger)]==1 && weak_ledger.additional_requests==208;
    probe.reset();decoder_final0=!observation::State.live && !observation::State.blocks &&
      GetStbiProcessAllocationSnapshot().live_request_bytes==decoder_before;
    void *plain=StbiAllocate(16);process_only.StbiOutput(plain);const auto no_heap_ledger=process_only.Result(true);StbiFree(plain);
    default_process_only=no_heap_ledger.supported && !no_heap_ledger.blocks[index(Kind::Ledger)] && !no_heap_ledger.additional_requests &&
      GetStbiProcessAllocationSnapshot().live_request_bytes==decoder_before;
  }
  const bool pass=exact && no_owners && zero_query && weak_exact && final0 && controls && unknown_hook && decoder_graph && decoder_final0 && default_process_only;
  return {{"schema","allocator-owner-census-unit.v1"},{"status",pass?"PASS":"FAIL"},
    {"blocks",counted.blocks},{"covered",counted.covered},{"weak_only",expired.weak_only},
    {"additional_requests",counted.additional_requests},{"covered_requests",counted.covered_requests},{"aliases",counted.aliases},
    {"actual_requests_exact",exact},{"no_owner_count_changes",no_owners},{"reserved_walk_allocates0",zero_query},
    {"weak_only_source_bound",weak_exact},{"source_final0_with_live_inspectors",final0},
    {"incomplete_profile_unbound_job_controls",controls},{"unknown_callback_rejected_without_call",unknown_hook},
    {"decoder_tls_escaped_weak_graph",decoder_graph},{"decoder_final0_with_live_inspectors",decoder_final0},{"default_decoder_uses_inline_process_ledger",default_process_only}};
}
}
