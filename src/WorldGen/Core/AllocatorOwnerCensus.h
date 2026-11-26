#pragma once
#include "Core/Memory/AllocatorOwnerSource.h"
#include "WorldGen/Core/PopulationScratchBudget.h"
#include "World/Chunks/ChunkLoadScheduler.h"
#include <vector>

namespace cutum
{
// Synchronous source-profile census. All source slots,callback targets and
// containers must stay stable through the entire walk. Rows borrow addresses;
// they retain neither strong nor weak owners. Result() is a cached snapshot;
// clear the census before inspecting a changed source graph again. Diagnostic
// vector storage is temporary and separate from the reported source requests.
// Unrelated event/render callback graphs and nested field payloads are outside
// this selected allocator-path census and require their own source coverage.
class AllocatorOwnerCensus
{
public:
  enum class Kind:size_t {Resource,Ledger,Budget,JobDefault,JobCounted,LightDefault,LightCounted,
    SettingsBudget,ResultCounter,SettingsValue,ResultGuard,Count};
  static constexpr size_t KindCount=static_cast<size_t>(Kind::Count);
  struct Snapshot {
    std::array<size_t,KindCount> blocks{},covered{},weak_only{};
    size_t additional_requests{},covered_requests{},unknown{},aliases{};
    bool supported{},overflow{};
  };
  explicit AllocatorOwnerCensus(size_t reserve=0){Rows.reserve(reserve);}
  static bool WaitSchedulerIdle(UChunkLoadScheduler &scheduler,std::chrono::milliseconds timeout){return scheduler.Pool.WaitIdleFor(timeout);}
  void Clear(){Rows.clear();Unknown=Aliases=0;Overflow=false;}
  void Resource(const std::shared_ptr<UConcurrentAllocationCountingResource> &slot){
    if(!Observe(slot,Kind::Resource,NormalBytes<UConcurrentAllocationCountingResource>(),false))return;
    if(slot)InspectResource(*slot);else UnknownOwner();
  }
  void Ledger(const std::shared_ptr<UStbiAllocationLedger> &slot){Observe(slot,Kind::Ledger,NormalBytes<UStbiAllocationLedger>(),false);}
  void WeakLedger(const std::weak_ptr<UStbiAllocationLedger> &slot){Observe(slot,Kind::Ledger,NormalBytes<UStbiAllocationLedger>(),false);}
  void Resource(std::shared_ptr<UConcurrentAllocationCountingResource> &&)=delete;
  void Ledger(std::shared_ptr<UStbiAllocationLedger> &&)=delete;
  void DefaultJob(std::shared_ptr<BufferBudgetJob> &&)=delete;
  void Context(BufferBudgetContext &&)=delete;
  void Buffer(UChunkBuffer &&)=delete;
  void DefaultJob(const std::shared_ptr<BufferBudgetJob> &slot){Observe(slot,Kind::JobDefault,NormalBytes<BufferBudgetJob>(),false);}
  void CountedJob(const std::shared_ptr<BufferBudgetJob> &slot,const std::shared_ptr<UConcurrentAllocationCountingResource> &origin,bool covered){
    Counted(slot,Kind::JobCounted,origin,covered);
  }
  void CountedWeakJob(const std::weak_ptr<BufferBudgetJob> &slot,const std::shared_ptr<UConcurrentAllocationCountingResource> &origin,bool covered){
    Counted<BufferBudgetJob>(slot,Kind::JobCounted,origin,covered);
  }
  void CountedWeakField(const std::weak_ptr<SectionLightField> &slot,const std::shared_ptr<UConcurrentAllocationCountingResource> &origin,bool covered){
    Counted<SectionLightField>(slot,Kind::LightCounted,origin,covered);
  }
  void CountedField(const std::shared_ptr<SectionLightField> &slot,const std::shared_ptr<UConcurrentAllocationCountingResource> &origin,bool covered){
    Counted(slot,Kind::LightCounted,origin,covered);
  }
  void Context(const BufferBudgetContext &context){
    Observe(context.budget,Kind::Budget,NormalBytes<UBufferAllocationBudget>(),false);
    const auto owner=Borrow(context.job);if(Empty(owner)){if(context.budget)UnknownOwner();return;}
    if(!context.budget){UnknownOwner();return;}
    const auto at=Find(owner);
    if(at==Rows.size() || (Rows[at].kind!=Kind::JobDefault && Rows[at].kind!=Kind::JobCounted))UnknownOwner();
  }
  void Buffer(const UChunkBuffer &buffer){if(buffer.Payload)Context(buffer.Payload->Budget);}
  void StbiOutput(const void *pointer){if(pointer)Ledger(detail::StbiHeader(pointer)->Owner);}
  void CallingThread(){Resource(ActiveScopedSharedResource);Ledger(detail::ActiveStbiLedger);Context(ActiveBufferBudget);}
  void Scratch(const UPopulationScratchBudget &scratch,const std::shared_ptr<UConcurrentAllocationCountingResource> &job_origin){
    CountedJob(scratch.Context.job,job_origin,true);Resource(scratch.Metadata);Resource(scratch.Fields);Context(scratch.Context);
  }
  // Source contract:manager was populated with the supplied single field
  // allocator profile. nullptr means ordinary main-thread make_shared.
  // Mixed/unknown field construction histories must not use this shortcut.
  void Manager(const UChunkManager &manager,const std::shared_ptr<UConcurrentAllocationCountingResource> *field_origin,
      bool field_requests_covered,bool profile_known){
    if(!profile_known){UnknownOwner();return;}
    Resource(manager.MetadataMemory);
    // The container remains the stable borrowed source after a move. Read its
    // allocator by value only during comparisons/inspection;never save a mirror.
    const Borrowed container{&manager,[](const void *p){return std::weak_ptr<const void>(
      static_cast<const UChunkManager*>(p)->GetMetadataAllocationResource());}};
    if(ObserveBorrowed(container,Kind::Resource,NormalBytes<UConcurrentAllocationCountingResource>(),false)){
      const auto actual=manager.GetMetadataAllocationResource();if(actual)InspectResource(*actual);else UnknownOwner();
    }
    if(const auto *chunk=manager.BeforeChunkAllocation.target<PopulationChunkReserveCallback>()){
      if(chunk->owner){Context(chunk->owner->Context);Resource(chunk->owner->Fields);}else UnknownOwner();
    }else if(manager.BeforeChunkAllocation)UnknownOwner();
    const auto field=[&](const auto &slot){
      if(field_origin)Counted(slot,Kind::LightCounted,*field_origin,field_requests_covered);
      else Observe(slot,Kind::LightDefault,NormalBytes<SectionLightField>(),field_requests_covered);
    };
    for(const auto &entry:manager.LightFields)field(entry.second);
    for(const auto &entry:manager.Chunks)field(entry.second->Light.field);
    for(const auto &chunk:manager.FreeList)if(chunk)field(chunk->Light.field);
  }
  void Scheduler(const UChunkLoadScheduler &scheduler){
    // Establish the stable source boundary before acquiring completion or
    // resource locks. A worker without its own view must not be traversed.
    const auto pool=scheduler.Pool.GetSnapshot();
    if(pool.active || pool.pending){UnknownOwner();return;}
    Resource(scheduler.ControlMemory);Observe(scheduler.BufferBudget,Kind::Budget,NormalBytes<UBufferAllocationBudget>(),false);
    Observe(scheduler.SettingsBudget,Kind::SettingsBudget,NormalBytes<UChunkLoadScheduler::SettingsBudgetState>(),false);
    Observe(scheduler.RetainedResults,Kind::ResultCounter,NormalBytes<std::atomic<size_t>>(),false);
    const auto settings=[&](const auto &slot){
      if(Observe(slot,Kind::SettingsValue,NormalBytes<UChunkLoadScheduler::OwnedSettings>(),false) && slot)
        Observe(slot->reservation.budget,Kind::SettingsBudget,NormalBytes<UChunkLoadScheduler::SettingsBudgetState>(),false);
    };
    for(const auto &entry:scheduler.Queue)settings(entry.settings);
    for(const auto &entry:scheduler.QueuedRequests)settings(entry.second.settings);
    scheduler.Completed.InspectValues([&](const auto &pending){
      Counted(pending.resultSlot,Kind::ResultGuard,scheduler.ControlMemory,true);
      if(pending.resultSlot)Observe(pending.resultSlot->count,Kind::ResultCounter,NormalBytes<std::atomic<size_t>>(),false);
      if(pending.bufferRetry)settings(pending.bufferRetry->settings);
      if(pending.result.buffer.Payload){
        CountedJob(pending.result.buffer.Payload->Budget.job,scheduler.ControlMemory,true);Buffer(pending.result.buffer);
      }
    });
    // Active/pending closures and caller-held Tick batches need their own
    // stable owner-thread views;an idle queue snapshot cannot certify them.
    if(scheduler.GetRetainedGenerationResultCount()!=static_cast<size_t>(scheduler.GetCompletedReadyCount()))UnknownOwner();
  }
  Snapshot Result(bool complete_source_domain)const{
    Snapshot out;out.unknown=Unknown;out.aliases=Aliases;out.overflow=Overflow;
    out.supported=GetAllocatorOwnerLayout().supported && complete_source_domain && !Unknown && !Overflow;
    const auto add=[&](size_t &a,size_t b){if(b>SIZE_MAX-a){a=SIZE_MAX;out.overflow=true;out.supported=false;}else a+=b;};
    for(const auto &row:Rows){const auto i=static_cast<size_t>(row.kind);add(out.blocks[i],1);
      if(row.expired)add(out.weak_only[i],1);
      if(row.covered){add(out.covered[i],1);add(out.covered_requests,row.bytes);}else add(out.additional_requests,row.bytes);
    }return out;
  }
  void UnknownOwner(){if(Unknown==SIZE_MAX)Overflow=true;else ++Unknown;}
private:
  struct Borrowed {
    const void *slot{};
    std::weak_ptr<const void> (*read)(const void*){};
  };
  struct Row {Borrowed owner,origin;Kind kind{};size_t bytes{};bool covered{},expired{};};
  std::vector<Row> Rows;size_t Unknown{},Aliases{};bool Overflow{};
  void InspectResource(UConcurrentAllocationCountingResource &resource){
    // No hook is called. Reentrant resource graphs stop at Observe first.
    std::lock_guard<std::mutex> lock(resource.Mutex);
    const auto *reserve=resource.BeforeAllocation.target<BufferBudgetReserveCallback>();
    const auto *release=resource.AfterDeallocation.target<BufferBudgetReleaseCallback>();
    const auto *metadata=resource.BeforeAllocation.target<PopulationMetadataReserveCallback>();
    if(reserve)Context(reserve->context);
    else if(metadata && metadata->owner){Context(metadata->owner->Context);Resource(metadata->owner->Fields);}
    else if(resource.BeforeAllocation)UnknownOwner();
    if(release)Context(release->context);else if(resource.AfterDeallocation)UnknownOwner();
  }
  template<class P> static Borrowed Borrow(const P &slot){return {&slot,[](const void *p){return std::weak_ptr<const void>(*static_cast<const P*>(p));}};}
  static bool Empty(const Borrowed &p){if(!p.slot)return true;const auto owner=p.read(p.slot);const std::weak_ptr<const void> empty;
    return !owner.owner_before(empty) && !empty.owner_before(owner);}
  static bool Same(const Borrowed &a,const Borrowed &b){if(!a.slot || !b.slot)return a.slot==b.slot;
    const auto left=a.read(a.slot),right=b.read(b.slot);return !left.owner_before(right) && !right.owner_before(left);}
  size_t Find(const Borrowed &p)const{for(size_t i=0;i<Rows.size();++i)if(Same(Rows[i].owner,p))return i;return Rows.size();}
  template<class T> static size_t NormalBytes(){
#if defined(_MSVC_STL_VERSION) && _ITERATOR_DEBUG_LEVEL == 0
    return sizeof(std::_Ref_count_obj2<T>);
#else
    return 0;
#endif
  }
  template<class T> static size_t CountedBytes(){
#if defined(_MSVC_STL_VERSION) && _ITERATOR_DEBUG_LEVEL == 0
    return sizeof(std::_Ref_count_obj_alloc3<T,USharedCountingAllocator<T>>);
#else
    return 0;
#endif
  }
  template<class P> bool Observe(const P &slot,Kind kind,size_t bytes,bool covered,Borrowed origin={}){
    return ObserveBorrowed(Borrow(slot),kind,bytes,covered,origin);
  }
  bool ObserveBorrowed(Borrowed owner,Kind kind,size_t bytes,bool covered,Borrowed origin={}){
    if(Empty(owner))return false;if(!bytes){UnknownOwner();return false;}
    const auto at=Find(owner);
    if(at!=Rows.size()){
      auto &row=Rows[at];if(row.kind!=kind || row.bytes!=bytes || !Same(row.origin,origin))UnknownOwner();
      row.covered=row.covered || covered;if(Aliases==SIZE_MAX)Overflow=true;else ++Aliases;return false;
    }
    Rows.push_back({owner,origin,kind,bytes,covered,owner.read(owner.slot).expired()});return true;
  }
  template<class T> void Counted(const std::shared_ptr<T> &slot,Kind kind,const std::shared_ptr<UConcurrentAllocationCountingResource> &origin,bool covered){
    Counted<std::remove_const_t<T>,std::shared_ptr<T>>(slot,kind,origin,covered);
  }
  template<class T,class P> void Counted(const P &slot,Kind kind,const std::shared_ptr<UConcurrentAllocationCountingResource> &origin,bool covered){
    if(Empty(Borrow(slot)))return;
    if(!origin){UnknownOwner();return;}
    Observe(slot,kind,CountedBytes<T>(),covered,Borrow(origin));Resource(origin);
  }
};
} // namespace cutum
