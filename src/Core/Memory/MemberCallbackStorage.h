#pragma once
#include "Core/Memory/CallbackStorageSource.h"
#include "Core/Memory/RootStorageSource.h"
#include <array>
#include <cstring>
#include <type_traits>
namespace cutum {
enum class MemberCallbackCell:size_t {
  SchedulerDirty,SchedulerColumnDirty,Load,Save,Dirty,Generate,Unload,UnloadColumn,AsyncRequest,Committed,Pending,PendingLight,DeferRelight,RelightColumn,
  DeferMesh,LightRepair,LitPending,SoftHeld,LitCommitted,FaceDebt,FaceDirty,FaceMask,Boundary,GpuProgress,FirstCoverage,MeshColumnDirty,FlowContains,Resident,
  MeshDependency,EnterTelemetry,LightObserver,PrepareLight,PackedSwitch,PendingAction,OperationComplete,Count};
inline constexpr size_t MemberCallbackCells=static_cast<size_t>(MemberCallbackCell::Count);
struct MemberCallbackStorageSnapshot {
  uintptr_t world{};
  RootStorageSnapshot held_root;
  std::array<CallbackStorageSnapshot,MemberCallbackCells> rows{};std::array<const char*,MemberCallbackCells> unknown_types{};uint64_t provided{};bool conflict{},overflow{};
  void Set(MemberCallbackCell cell,CallbackStorageSnapshot source) noexcept {
    const auto i=static_cast<size_t>(cell),bit=uint64_t{1}<<i;
    if(provided&bit){const auto &p=rows[i];if(p.cell!=source.cell || p.owner!=source.owner || p.requests!=source.requests || p.present!=source.present || p.supported!=source.supported)conflict=true;return;}
    rows[i]=source;provided|=bit;
  }
  void Merge(const MemberCallbackStorageSnapshot &other) noexcept {
    if(other.held_root.present){if(held_root.present && (held_root.allocation!=other.held_root.allocation || held_root.requested!=other.held_root.requested))conflict=true;else held_root=other.held_root;}
    conflict=conflict || other.conflict;overflow=overflow || other.overflow;
    for(size_t i=0;i<rows.size();++i)if(other.provided&(uint64_t{1}<<i)){Set(static_cast<MemberCallbackCell>(i),other.rows[i]);if(other.unknown_types[i])unknown_types[i]=other.unknown_types[i];}
  }
  bool Supported(uint64_t expected)const noexcept {
    if(provided!=expected || conflict || overflow)return false;
    for(size_t i=0;i<rows.size();++i)if((expected&(uint64_t{1}<<i)) && !rows[i].supported)return false;return true;
  }
  size_t Requested()const noexcept {size_t sum{};for(const auto &row:rows){if(row.requests>SIZE_MAX-sum)return SIZE_MAX;sum+=row.requests;}return sum;}
};
inline constexpr uint64_t MemberCallbackAll=(uint64_t{1}<<MemberCallbackCells)-1;
struct MemberCallbackStorageEnvelope {size_t requested{},worlds{},cells{},aliases{};bool supported{},overflow{};};
inline MemberCallbackStorageEnvelope JoinMemberCallbackStorage(const std::vector<const MemberCallbackStorageSnapshot*> &views,size_t expected_worlds,bool complete_domain,bool parent_shells_covered) noexcept {
  MemberCallbackStorageEnvelope out;out.supported=complete_domain && parent_shells_covered;
  for(size_t i=0;i<views.size();++i){const auto *s=views[i];if(!s || !s->world || !s->Supported(MemberCallbackAll)){out.supported=false;continue;}
    bool duplicate_world=false;for(size_t j=0;j<i;++j)if(views[j] && views[j]->world==s->world)duplicate_world=true;
    if(!duplicate_world)++out.worlds;
    for(size_t c=0;c<MemberCallbackCells;++c){const auto &row=s->rows[c];if(!row.cell){if(row.present || row.requests)out.supported=false;continue;}
      bool alias=false;
      for(size_t j=0;j<=i;++j)if(const auto *p=views[j])for(size_t d=0;d<(j==i?c:MemberCallbackCells);++d){const auto &prior=p->rows[d];if(prior.cell!=row.cell)continue;
        alias=true;if(d!=c || prior.owner!=row.owner || prior.capture_inline!=row.capture_inline || prior.requests!=row.requests || prior.present!=row.present || prior.supported!=row.supported)out.supported=false;}
      if(alias){++out.aliases;continue;}++out.cells;
      if(row.requests>SIZE_MAX-out.requested){out.requested=SIZE_MAX;out.supported=false;out.overflow=true;}else out.requested+=row.requests;
    }
  }
  if(out.worlds!=expected_worlds)out.supported=false;return out;
}
// Source factories below declare only this/reference captures. For the pinned
// MSVC ABI each is one pointer-sized field; the checked extent has no padding.
// No closure containing owning strings,vectors or smart pointers uses this
// representation profile. Reconstructing the closure never invokes its body.
template<class Capture,class R,class... Args>
inline CallbackStorageSnapshot InspectReferenceCallback(const std::function<R(Args...)> *fn,const Capture &expected,size_t source_pointer_fields,uintptr_t producer,bool history_known) noexcept {
  CallbackStorageSnapshot out;out.capture_inline=sizeof(Capture);out.owner=producer;
  if(fn){out.cell=reinterpret_cast<uintptr_t>(fn);out.present=bool(*fn);}
#if defined(_MSVC_STL_VERSION) && _ITERATOR_DEBUG_LEVEL == 0
  constexpr bool empty=std::is_empty_v<Capture>;
  out.supported=history_known && std::is_trivially_copyable_v<Capture> && (empty || sizeof(Capture)==source_pointer_fields*sizeof(void*));
  if(!out.supported || !fn || !*fn)return out;
  const auto *target=fn->template target<Capture>();
  if(!target){out.supported=false;return out;}
  if constexpr(!empty)if(std::memcmp(target,&expected,sizeof(Capture))){out.supported=false;return out;}
  out.requests=KnownCallbackRequestBytes<Capture>(*fn);
  const auto address=reinterpret_cast<uintptr_t>(target),cell=reinterpret_cast<uintptr_t>(fn);
  const bool internal=address>=cell && address-cell<sizeof(*fn);
  if(internal!=(out.requests==0))out.supported=false;
#endif
  return out;
}
template<class R,class... Args> inline void SetEmptyMemberCallback(MemberCallbackStorageSnapshot &out,MemberCallbackCell cell,const std::function<R(Args...)> *fn,bool known) noexcept {
  CallbackStorageSnapshot row;if(fn)row=InspectEmptyCallback(*fn,known);else row.supported=known;out.Set(cell,row);
#if defined(_MSVC_STL_VERSION)
  if(!row.supported && fn && *fn)out.unknown_types[static_cast<size_t>(cell)]=fn->target_type().raw_name();
#endif
}
template<class Capture,class R,class... Args> inline void SetReferenceMemberCallback(MemberCallbackStorageSnapshot &out,MemberCallbackCell cell,const std::function<R(Args...)> *fn,const Capture &expected,size_t words,uintptr_t producer,bool known) noexcept {
  const auto row=InspectReferenceCallback(fn,expected,words,producer,known);out.Set(cell,row);
#if defined(_MSVC_STL_VERSION)
  if(!row.supported && fn && *fn)out.unknown_types[static_cast<size_t>(cell)]=fn->target_type().raw_name();
#endif
}
} // namespace cutum
