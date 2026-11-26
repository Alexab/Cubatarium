#pragma once
#include "Core/Memory/AllocatorOwnerSource.h"
#include "Core/Memory/AllocatorSourcePacket.h"
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
  struct Borrowed {
    const void *slot{};
    std::weak_ptr<const void> (*read)(const void*){};
  };
public:
  enum class Kind:size_t {Resource,Ledger,Budget,JobDefault,JobCounted,LightDefault,LightCounted,
    SettingsBudget,ResultCounter,SettingsValue,ResultGuard,Count};
  static constexpr size_t KindCount=static_cast<size_t>(Kind::Count);
  // The caller's weak slot must stay at a stable address and must not be
  // reassigned to another owner while this history is used. Reset-to-empty
  // removes this root. The history stores no strong/weak owner. For our
  // coallocations the object address stays reserved until the final weak free.
  class SourceHistory {
    friend class AllocatorOwnerCensus;
    Borrowed owner,origin;uintptr_t identity{};Kind kind{};size_t bytes{};bool known{};
  };
  struct Snapshot {
    std::array<size_t,KindCount> blocks{},covered{},weak_only{};
    size_t additional_requests{},covered_requests{},unknown{},aliases{};
    bool supported{},overflow{};
  };
  explicit AllocatorOwnerCensus(size_t reserve=0){Rows.reserve(reserve);}
  static bool WaitSchedulerIdle(UChunkLoadScheduler &scheduler,std::chrono::milliseconds timeout){return scheduler.Pool.WaitIdleFor(timeout);}
  static void EnqueueSchedulerWorkerView(UChunkLoadScheduler &scheduler,std::function<void()> visit){scheduler.Pool.Enqueue(std::move(visit));}
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
  static SourceHistory RememberResource(const std::weak_ptr<UConcurrentAllocationCountingResource> &slot){return Remember(slot,Kind::Resource,NormalBytes<UConcurrentAllocationCountingResource>());}
  static SourceHistory RememberBudget(const std::weak_ptr<UBufferAllocationBudget> &slot){return Remember(slot,Kind::Budget,NormalBytes<UBufferAllocationBudget>());}
  static SourceHistory RememberResultCounter(const std::weak_ptr<std::atomic<size_t>> &slot){return Remember(slot,Kind::ResultCounter,NormalBytes<std::atomic<size_t>>());}
  static SourceHistory RememberCountedJob(const std::weak_ptr<BufferBudgetJob> &slot,const std::weak_ptr<UConcurrentAllocationCountingResource> &origin){return Remember(slot,Kind::JobCounted,CountedBytes<BufferBudgetJob>(),Borrow(origin));}
  static SourceHistory RememberResultGuard(const std::weak_ptr<UChunkLoadScheduler::ResultSlotGuard> &slot,const std::weak_ptr<UConcurrentAllocationCountingResource> &origin){return Remember(slot,Kind::ResultGuard,CountedBytes<UChunkLoadScheduler::ResultSlotGuard>(),Borrow(origin));}
  static SourceHistory RememberResource(std::weak_ptr<UConcurrentAllocationCountingResource> &&)=delete;
  static SourceHistory RememberBudget(std::weak_ptr<UBufferAllocationBudget> &&)=delete;
  static SourceHistory RememberResultCounter(std::weak_ptr<std::atomic<size_t>> &&)=delete;
  static SourceHistory RememberCountedJob(std::weak_ptr<BufferBudgetJob> &&,const std::weak_ptr<UConcurrentAllocationCountingResource> &)=delete;
  static SourceHistory RememberCountedJob(const std::weak_ptr<BufferBudgetJob> &,std::weak_ptr<UConcurrentAllocationCountingResource> &&)=delete;
  static SourceHistory RememberResultGuard(std::weak_ptr<UChunkLoadScheduler::ResultSlotGuard> &&,const std::weak_ptr<UConcurrentAllocationCountingResource> &)=delete;
  static SourceHistory RememberResultGuard(const std::weak_ptr<UChunkLoadScheduler::ResultSlotGuard> &,std::weak_ptr<UConcurrentAllocationCountingResource> &&)=delete;
  void History(const SourceHistory &history,bool covered=false){
    if(!history.known){UnknownOwner();return;}
    if(Empty(history.owner))return;
    const auto live=history.owner.read(history.owner.slot).lock();
    if(live && reinterpret_cast<uintptr_t>(live.get())!=history.identity){UnknownOwner();return;}
    if(history.origin.slot && !history.origin.read(history.origin.slot).lock()){UnknownOwner();return;}
    if(ObserveBorrowed(history.owner,history.kind,history.bytes,covered,history.origin,history.identity) && live && history.kind==Kind::Resource)
      InspectResource(*static_cast<UConcurrentAllocationCountingResource*>(const_cast<void*>(live.get())));
    if(history.origin.slot)WeakResource(history.origin);
  }
  static const BufferBudgetContext *BufferOwners(const UChunkBuffer &buffer){return buffer.Payload?&buffer.Payload->Budget:nullptr;}
  void ReturnedResults(const std::vector<UChunkLoadScheduler::PendingResult> &results,
      const std::weak_ptr<UConcurrentAllocationCountingResource> &origin){
    for(const auto &pending:results){
      CountedWeakOrigin(pending.resultSlot,Kind::ResultGuard,origin,true);
      if(pending.resultSlot)Observe(pending.resultSlot->count,Kind::ResultCounter,NormalBytes<std::atomic<size_t>>(),false);
      if(pending.bufferRetry)Settings(pending.bufferRetry->settings);
      if(const auto *owners=BufferOwners(pending.result.buffer)){CountedWeakOrigin(owners->job,Kind::JobCounted,origin,true);Context(*owners);}
    }
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
  void CallingThread(){
    const auto &source=ActiveBufferBudgetJobSource;
    if(ActiveBufferBudget.job){
      if(source.profile==BufferJobSourceProfile::Default)DefaultJob(ActiveBufferBudget.job);
      else if(source.profile==BufferJobSourceProfile::Counted && source.origin)CountedJob(ActiveBufferBudget.job,*source.origin,source.requests_covered);
      else UnknownOwner();
      if(source.scheduler_settings_slot){
        if(source.profile==BufferJobSourceProfile::Counted)Settings(*static_cast<const std::shared_ptr<const UChunkLoadScheduler::OwnedSettings>*>(source.scheduler_settings_slot));
        else UnknownOwner();
      }
    }
    Resource(ActiveScopedSharedResource);Ledger(detail::ActiveStbiLedger);Context(ActiveBufferBudget);
  }
  void CallingThreadScratch(const UPopulationScratchBudget &scratch,const UChunkManager &manager){
    if(&scratch.Manager!=&manager || scratch.Context.job.owner_before(ActiveBufferBudget.job) ||
        ActiveBufferBudget.job.owner_before(scratch.Context.job) || scratch.Context.budget.owner_before(ActiveBufferBudget.budget) ||
        ActiveBufferBudget.budget.owner_before(scratch.Context.budget)){UnknownOwner();return;}
    Resource(scratch.Metadata);Resource(scratch.Fields);Context(scratch.Context);Manager(manager,&scratch.Fields,true,true);
  }
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
  void Scheduler(const UChunkLoadScheduler &scheduler){SchedulerImpl(scheduler,0,0);}
  void SchedulerWithReturned(const UChunkLoadScheduler &scheduler,const std::vector<UChunkLoadScheduler::PendingResult> &results,
      const std::weak_ptr<UConcurrentAllocationCountingResource> &origin){
    if(origin.owner_before(scheduler.ControlMemory) || scheduler.ControlMemory.owner_before(origin))UnknownOwner();
    size_t distinct=0;
    for(size_t i=0;i<results.size();++i)if(results[i].resultSlot){
      const auto &counter=results[i].resultSlot->count;
      if(counter.owner_before(scheduler.RetainedResults) || scheduler.RetainedResults.owner_before(counter))UnknownOwner();
      bool seen=false;for(size_t j=0;j<i;++j)if(!results[i].resultSlot.owner_before(results[j].resultSlot) && !results[j].resultSlot.owner_before(results[i].resultSlot)){seen=true;break;}
      if(!seen)++distinct;
    }
    SchedulerImpl(scheduler,0,distinct);ReturnedResults(results,origin);
  }
  // All active workers are paused in the supplied source phase. Their own
  // packets must accompany this main-thread view before accepting the union.
  void SchedulerAtWorkerBarrier(const UChunkLoadScheduler &scheduler,size_t worker_views){SchedulerImpl(scheduler,worker_views,0);}
  static std::array<size_t,KindCount> RequestWidths(){
    const auto base=GetAllocatorOwnerLayout();std::array<size_t,KindCount> out{};
    for(size_t i=0;i<base.requests.size();++i)out[i]=base.requests[i];
    out[7]=NormalBytes<UChunkLoadScheduler::SettingsBudgetState>();out[8]=NormalBytes<std::atomic<size_t>>();
    out[9]=NormalBytes<UChunkLoadScheduler::OwnedSettings>();out[10]=CountedBytes<UChunkLoadScheduler::ResultSlotGuard>();return out;
  }
  void ExportInto(AllocatorSourcePacket &packet,const AllocatorSourcePhase &phase,size_t participant,bool complete_domain)const{
    static_assert(KindCount==AllocatorSourceKinds);packet.nodes.clear();packet.epoch=phase.epoch;packet.participant=participant;
    packet.thread=std::this_thread::get_id();packet.widths=RequestWidths();packet.supported=Result(complete_domain).supported && phase.IsOpen();
    for(const auto &row:Rows){
      const auto owner=row.owner.read(row.owner.slot).lock();
      const auto origin=row.origin.slot?row.origin.read(row.origin.slot).lock():std::shared_ptr<const void>{};
      if((!owner && !row.identity) || (row.origin.slot && !origin)){packet.supported=false;continue;}
      const auto identity=owner?reinterpret_cast<uintptr_t>(owner.get()):row.identity;
      if(owner && row.identity && identity!=row.identity){packet.supported=false;continue;}
      packet.nodes.push_back({identity,reinterpret_cast<uintptr_t>(origin.get()),static_cast<size_t>(row.kind),row.bytes,row.covered});
    }
  }
private:
  void Settings(const std::shared_ptr<const UChunkLoadScheduler::OwnedSettings> &slot){
    if(Observe(slot,Kind::SettingsValue,NormalBytes<UChunkLoadScheduler::OwnedSettings>(),false) && slot)
      Observe(slot->reservation.budget,Kind::SettingsBudget,NormalBytes<UChunkLoadScheduler::SettingsBudgetState>(),false);
  }
  void SchedulerImpl(const UChunkLoadScheduler &scheduler,size_t worker_views,size_t returned_views){
    // Establish the stable source boundary before acquiring completion or
    // resource locks. A worker without its own view must not be traversed.
    const auto pool=scheduler.Pool.GetSnapshot();
    if(pool.pending || pool.active!=worker_views){UnknownOwner();return;}
    Resource(scheduler.ControlMemory);Observe(scheduler.BufferBudget,Kind::Budget,NormalBytes<UBufferAllocationBudget>(),false);
    Observe(scheduler.SettingsBudget,Kind::SettingsBudget,NormalBytes<UChunkLoadScheduler::SettingsBudgetState>(),false);
    Observe(scheduler.RetainedResults,Kind::ResultCounter,NormalBytes<std::atomic<size_t>>(),false);
    for(const auto &entry:scheduler.Queue)Settings(entry.settings);
    for(const auto &entry:scheduler.QueuedRequests)Settings(entry.second.settings);
    scheduler.Completed.InspectValues([&](const auto &pending){
      Counted(pending.resultSlot,Kind::ResultGuard,scheduler.ControlMemory,true);
      if(pending.resultSlot)Observe(pending.resultSlot->count,Kind::ResultCounter,NormalBytes<std::atomic<size_t>>(),false);
      if(pending.bufferRetry)Settings(pending.bufferRetry->settings);
      if(pending.result.buffer.Payload){
        CountedJob(pending.result.buffer.Payload->Budget.job,scheduler.ControlMemory,true);Buffer(pending.result.buffer);
      }
    });
    // Active/pending closures and caller-held Tick batches need their own
    // stable owner-thread views;an idle queue snapshot cannot certify them.
    if(scheduler.GetRetainedGenerationResultCount()!=static_cast<size_t>(scheduler.GetCompletedReadyCount())+worker_views+returned_views)UnknownOwner();
  }
public:
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
  struct Row {Borrowed owner,origin;Kind kind{};size_t bytes{};bool covered{},expired{};uintptr_t identity{};};
  std::vector<Row> Rows;size_t Unknown{},Aliases{};bool Overflow{};
  template<class P> static SourceHistory Remember(const P &slot,Kind kind,size_t bytes,Borrowed origin={}){
    SourceHistory out;out.owner=Borrow(slot);out.origin=origin;out.kind=kind;out.bytes=bytes;
    const auto live=out.owner.read(out.owner.slot).lock();
    if(live && bytes && (!origin.slot || origin.read(origin.slot).lock())){out.identity=reinterpret_cast<uintptr_t>(live.get());out.known=true;}
    return out;
  }
  void WeakResource(Borrowed owner){
    if(!ObserveBorrowed(owner,Kind::Resource,NormalBytes<UConcurrentAllocationCountingResource>(),false))return;
    const auto live=owner.read(owner.slot).lock();if(live)InspectResource(*static_cast<UConcurrentAllocationCountingResource*>(const_cast<void*>(live.get())));
  }
  template<class T> void CountedWeakOrigin(const std::shared_ptr<T> &slot,Kind kind,const std::weak_ptr<UConcurrentAllocationCountingResource> &origin,bool covered){
    if(Empty(Borrow(slot)))return;
    if(origin.expired()){UnknownOwner();return;}
    Observe(slot,kind,CountedBytes<std::remove_const_t<T>>(),covered,Borrow(origin));WeakResource(Borrow(origin));
  }
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
  bool ObserveBorrowed(Borrowed owner,Kind kind,size_t bytes,bool covered,Borrowed origin={},uintptr_t identity=0){
    if(Empty(owner))return false;if(!bytes){UnknownOwner();return false;}
    const auto at=Find(owner);
    if(at!=Rows.size()){
      auto &row=Rows[at];if(row.kind!=kind || row.bytes!=bytes || !Same(row.origin,origin))UnknownOwner();
      if(identity){if(row.identity && row.identity!=identity)UnknownOwner();row.identity=identity;}
      row.covered=row.covered || covered;if(Aliases==SIZE_MAX)Overflow=true;else ++Aliases;return false;
    }
    Rows.push_back({owner,origin,kind,bytes,covered,owner.read(owner.slot).expired(),identity});return true;
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
