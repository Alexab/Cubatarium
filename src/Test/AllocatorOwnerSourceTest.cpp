#ifndef NOMINMAX
#define NOMINMAX
#endif
#include "Test/OrdinaryNewObservation.h"
#include "Core/Memory/AllocatorOwnerSource.h"
#include <nlohmann/json.hpp>
#include <fstream>
#include <iostream>

namespace {
using namespace cutum;using Json=nlohmann::json;using Cell=AllocatorOwnerCell;
size_t Index(Cell c){return static_cast<size_t>(c);}
struct Row {size_t bytes{},attempts{};bool exact{},weak_retained{},final0{};};
template<class T,class... Args> Row Default(size_t expected,Args&&...args){
  Row row;{
    observation::Scope scope;auto owner=std::make_shared<T>(std::forward<Args>(args)...);std::weak_ptr<T> weak=owner;
    row.bytes=observation::State.live;row.attempts=observation::State.attempts;row.exact=row.bytes==expected && row.attempts==1;
    owner.reset();row.weak_retained=weak.expired() && observation::State.live==row.bytes;
    weak.reset();row.final0=!observation::State.live && !observation::State.blocks;
  }return row;
}
template<class T> Row Counted(size_t expected){
  auto resource=std::make_shared<UConcurrentAllocationCountingResource>();Row row;{
    observation::Scope scope;auto owner=std::allocate_shared<T>(USharedCountingAllocator<T>{resource});std::weak_ptr<T> weak=owner;
    row.bytes=observation::State.live;row.attempts=observation::State.attempts;
    row.exact=row.bytes==expected && row.attempts==1 && resource->GetSnapshot().live_bytes==expected;
    owner.reset();row.weak_retained=weak.expired() && resource->GetSnapshot().live_bytes==expected && observation::State.live==expected;
    weak.reset();row.final0=!observation::State.live && !observation::State.blocks && !resource->GetSnapshot().live_bytes;
  }return row;
}
struct Chain {size_t attempts{},peak{},additional{},tracked{};bool exact{},callbacks_inline{},weak_graph{},job_weak_keeps_resource{},released{},final0{};};
Chain ResourceChain(const AllocatorOwnerLayout &layout,size_t prefix=SIZE_MAX){
  Chain row;std::weak_ptr<UConcurrentAllocationCountingResource> field_probe,control_probe;
  std::weak_ptr<UBufferAllocationBudget> budget_probe;std::weak_ptr<BufferBudgetJob> job_probe;
  std::weak_ptr<SectionLightField> field_weak;
  {
    observation::Scope observed(prefix);
    try {
      auto control=std::make_shared<UConcurrentAllocationCountingResource>();control_probe=control;
      auto fields=std::make_shared<UConcurrentAllocationCountingResource>();field_probe=fields;
      auto budget=std::make_shared<UBufferAllocationBudget>(1024*1024);budget_probe=budget;
      auto job=std::allocate_shared<BufferBudgetJob>(USharedCountingAllocator<BufferBudgetJob>{control});job_probe=job;
      const auto before_callbacks=observation::State.attempts;
      fields->SetBeforeAllocationFn(BufferBudgetReserveCallback{{budget,job}});
      fields->SetAfterDeallocationFn(BufferBudgetReleaseCallback{{budget,job}});
      row.callbacks_inline=observation::State.attempts==before_callbacks && !layout.reserve_callback_bytes && !layout.release_callback_bytes;
      std::shared_ptr<SectionLightField> field;
      {UScopedSharedAllocationResource scope(fields);field=MakeScopedShared<SectionLightField>();}
      field_weak=field;const auto field_bytes=layout.requests[Index(Cell::LightCounted)];
      const auto job_bytes=layout.requests[Index(Cell::JobCounted)];
      row.tracked=fields->GetSnapshot().live_bytes+control->GetSnapshot().live_bytes;
      AllocatorOwnerDomain domain;domain.blocks[Index(Cell::ConcurrentResource)]=2;domain.blocks[Index(Cell::BufferBudget)]=1;
      domain.blocks[Index(Cell::JobCounted)]=domain.blocks[Index(Cell::LightCounted)]=1;
      domain.already_counted[Index(Cell::JobCounted)]=domain.already_counted[Index(Cell::LightCounted)]=1;
      domain.population_callback_pairs=1;domain.owner_graph_known=domain.source_profile_known=domain.external_history_known=true;
      const auto envelope=BoundAllocatorOwners(layout,domain);row.additional=envelope.additional_requests;
      row.exact=envelope.supported && !envelope.overflow && row.tracked==field_bytes+job_bytes &&
        observation::State.live==row.additional+row.tracked && budget->UsedBytes()==field_bytes && job->reserved==field_bytes;
      control.reset();fields.reset();budget.reset();job.reset();field.reset();
      row.weak_graph=field_weak.expired() && !field_probe.expired() && !control_probe.expired() && !job_probe.expired() && !budget_probe.expired();
      // Last weak field retires its allocator,then callback-held budget/job,
      // then the job allocator's separate control resource.
      field_weak.reset();
      row.job_weak_keeps_resource=field_probe.expired() && job_probe.expired() && budget_probe.expired() && !control_probe.expired();
      job_probe.reset();row.released=control_probe.expired();
    } catch(const std::bad_alloc &) {}
    row.attempts=observation::State.attempts;row.peak=observation::State.peak;
    field_weak.reset();field_probe.reset();control_probe.reset();budget_probe.reset();job_probe.reset();
    row.final0=!observation::State.live && !observation::State.blocks && !ActiveScopedSharedResource;
  }return row;
}
struct Decoder {bool original_owner{},nested_restored{},payload_escaped{},ledger_weak_only{},final0{};size_t ordinary{},malloc_request{};};
Decoder DecoderChain(){
  Decoder row;std::weak_ptr<UStbiAllocationLedger> weak;void *p{};
  const auto before=GetStbiProcessAllocationSnapshot();{
    observation::Scope observed;
    auto ledger=std::make_shared<UStbiAllocationLedger>();weak=ledger;
    {UStbiAllocationScope scope(ledger);p=StbiAllocate(16);}
    if(p){
      const auto saved=ledger.get();ledger.reset();row.payload_escaped=!weak.expired() && !detail::ActiveStbiLedger;
      auto nested=std::make_shared<UStbiAllocationLedger>(0);
      {UStbiAllocationScope scope(nested);p=StbiReallocate(p,32);const auto info=InspectStbiAllocation(p);
        row.original_owner=p && info.owner.get()==saved && nested->GetSnapshot().allocation_attempts==0;
        row.malloc_request=info.request_bytes;
      }
      row.nested_restored=!detail::ActiveStbiLedger;
      StbiFree(p);p=nullptr;row.ordinary=observation::State.live;
      row.ledger_weak_only=weak.expired() && row.ordinary>0;
      nested.reset();weak.reset();
    }
    row.final0=!observation::State.live && !observation::State.blocks &&
      GetStbiProcessAllocationSnapshot().live_request_bytes==before.live_request_bytes;
  }return row;
}
}
int main(int argc,char **argv){
  const auto layout=GetAllocatorOwnerLayout();std::array<Row,AllocatorOwnerCellCount> rows;
  rows[Index(Cell::ConcurrentResource)]=Default<UConcurrentAllocationCountingResource>(layout.requests[Index(Cell::ConcurrentResource)]);
  rows[Index(Cell::StbiLedger)]=Default<UStbiAllocationLedger>(layout.requests[Index(Cell::StbiLedger)]);
  rows[Index(Cell::BufferBudget)]=Default<UBufferAllocationBudget>(layout.requests[Index(Cell::BufferBudget)],1024);
  rows[Index(Cell::JobDefault)]=Default<BufferBudgetJob>(layout.requests[Index(Cell::JobDefault)]);
  rows[Index(Cell::JobCounted)]=Counted<BufferBudgetJob>(layout.requests[Index(Cell::JobCounted)]);
  rows[Index(Cell::LightDefault)]=Default<SectionLightField>(layout.requests[Index(Cell::LightDefault)]);
  rows[Index(Cell::LightCounted)]=Counted<SectionLightField>(layout.requests[Index(Cell::LightCounted)]);
  bool pass=layout.supported;Json measured=Json::array();for(const auto &r:rows){pass=pass && r.exact && r.weak_retained && r.final0;
    measured.push_back({{"bytes",r.bytes},{"attempts",r.attempts},{"exact",r.exact},{"weak_retained",r.weak_retained},{"final0",r.final0}});}
  const auto chain=ResourceChain(layout);pass=pass && chain.exact && chain.callbacks_inline && chain.weak_graph && chain.job_weak_keeps_resource && chain.released && chain.final0;
  Json refusals=Json::array();for(size_t i=0;i<chain.attempts;++i){const auto refused=ResourceChain(layout,i),retry=ResourceChain(layout);
    const bool clean=refused.final0 && refused.attempts==i+1 && retry.exact && retry.final0;pass=pass && clean;
    refusals.push_back({{"prefix",i},{"attempts",refused.attempts},{"clean",clean},{"retry_exact",retry.exact}});}
  const auto decoder=DecoderChain();pass=pass && decoder.original_owner && decoder.nested_restored && decoder.payload_escaped && decoder.ledger_weak_only && decoder.final0;
  AllocatorOwnerDomain domain;domain.owner_graph_known=domain.source_profile_known=domain.external_history_known=true;
  domain.blocks[Index(Cell::ConcurrentResource)]=2;domain.blocks[Index(Cell::BufferBudget)]=1;
  auto unknown=domain;unknown.owner_graph_known=false;auto external=domain;external.external_history_known=false;
  auto profile=domain;profile.source_profile_known=false;auto over=domain;over.already_counted[0]=3;
  auto huge=domain;huge.blocks[0]=SIZE_MAX;auto missing=layout;missing.requests[0]=0;
  bool free_query{};{observation::Scope scope(0);const auto queried=GetAllocatorOwnerLayout();const auto envelope=BoundAllocatorOwners(queried,domain);
    free_query=envelope.supported && !observation::State.attempts;}
  const bool controls=!BoundAllocatorOwners(layout,unknown).supported && !BoundAllocatorOwners(layout,external).supported &&
    !BoundAllocatorOwners(layout,profile).supported && !BoundAllocatorOwners(layout,over).supported &&
    BoundAllocatorOwners(layout,huge).overflow && !BoundAllocatorOwners(missing,domain).supported;
  pass=pass && controls && free_query;
  Json report={{"schema","allocator-owner-source.v1"},{"status",pass?"PASS":"FAIL"},{"requests",layout.requests},
    {"reserve_callback_requests",layout.reserve_callback_bytes},{"release_callback_requests",layout.release_callback_bytes},{"rows",measured},
    {"chain",{{"attempts",chain.attempts},{"peak",chain.peak},{"additional",chain.additional},{"already_counted",chain.tracked},
      {"exact",chain.exact},{"callbacks_inline",chain.callbacks_inline},{"weak_graph",chain.weak_graph},
      {"job_weak_keeps_resource",chain.job_weak_keeps_resource},{"released",chain.released},{"final0",chain.final0}}},
    {"refusal_cases",refusals},{"decoder",{{"original_owner",decoder.original_owner},{"nested_restored",decoder.nested_restored},
      {"payload_escaped",decoder.payload_escaped},{"ledger_weak_only",decoder.ledger_weak_only},{"final0",decoder.final0},{"malloc_request",decoder.malloc_request}}},
    {"query_allocates0",free_query},{"unknown_profile_external_overcoverage_overflow_controls",controls},
    {"scope","Compiled seven default/counted coallocation profiles and actual callback-held ownership. Explicit complete identity-deduplicated physical-block domain required,including expired controls. Job/field requests already counted in E10/E11 deducted once. No global owner count,CRT/upstream/stack/GPU/process or full S6 acceptance."}};
  std::ofstream out(argc>1?argv[1]:"allocator-owner-source.json");out<<report.dump(2)<<'\n';std::cout<<report.dump()<<'\n';return out&&pass?0:1;
}
