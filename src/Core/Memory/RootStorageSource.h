#pragma once
#include "Core/Memory/AllocatorSourcePacket.h"
#include <memory>
#include <cstring>
#include <typeinfo>
#include <type_traits>
namespace cutum {
class UWorld;class UCore;class UApplication;class UGeometryEngine;class SectionRenderLightBindings;
enum class RootStorageClass:size_t {World,Core,Application,Geometry,SectionBindings,Count};
inline constexpr size_t RootStorageClasses=static_cast<size_t>(RootStorageClass::Count);
template<class T> inline constexpr RootStorageClass RootStorageKind=RootStorageClass::Count;
template<> inline constexpr RootStorageClass RootStorageKind<UWorld> = RootStorageClass::World;
template<> inline constexpr RootStorageClass RootStorageKind<UCore> = RootStorageClass::Core;
template<> inline constexpr RootStorageClass RootStorageKind<UApplication> = RootStorageClass::Application;
template<> inline constexpr RootStorageClass RootStorageKind<UGeometryEngine> = RootStorageClass::Geometry;
template<> inline constexpr RootStorageClass RootStorageKind<SectionRenderLightBindings> = RootStorageClass::SectionBindings;
#if defined(_WIN64) && defined(_MSVC_STL_VERSION) && _MSVC_STL_VERSION == 145 && _MSVC_STL_UPDATE == 202604L && _ITERATOR_DEBUG_LEVEL == 0
inline constexpr bool RootOwnerRepresentationSupported=true;
#else
inline constexpr bool RootOwnerRepresentationSupported=false;
#endif
inline std::atomic<uint64_t> NextRootStorageEpoch{1};
inline uint64_t NewRootStorageEpoch() noexcept {
  auto value=NextRootStorageEpoch.load(std::memory_order_relaxed);
  while(value){const auto next=value==UINT64_MAX?0:value+1;if(NextRootStorageEpoch.compare_exchange_weak(value,next,std::memory_order_relaxed))return value;}
  return 0; // Exhaustion permanently disables new source phases.
}
struct RootStorageLayout {
  std::array<size_t,RootStorageClasses> objects{},coallocated{};
  size_t epoch_static_inline{sizeof(NextRootStorageEpoch)};bool supported{};
};
RootStorageLayout GetRootStorageLayout() noexcept;
enum class RootStorageOrigin {SharedCoallocation,OrdinaryUnique,Stack,Embedded,Unknown};
struct RootStorageSnapshot {
  RootStorageClass kind{RootStorageClass::Count};RootStorageOrigin origin{RootStorageOrigin::Unknown};
  uintptr_t object{},allocation{},parent{};size_t requested{},inline_bytes{};
  uint64_t epoch{};std::thread::id thread;
  bool supported{},present{},payload_live{},expired_weak{};
};
// The pinned MSVC _Ptr_base contains _Ptr followed by _Rep. Copying its
// representation into integers does not copy a source owner. Validate the
// concrete polymorphic control type before accepting any default factory.
template<class Owner> inline std::array<uintptr_t,2> RootOwnerWords(const Owner &owner) noexcept {
  std::array<uintptr_t,2> out{};
  if constexpr(RootOwnerRepresentationSupported && sizeof(Owner)==sizeof(out))std::memcpy(out.data(),&owner,sizeof(out));return out;
}
template<class T> inline RootStorageSnapshot CaptureRootControl(const std::array<uintptr_t,2> &words,const AllocatorSourcePhase &phase,bool history_known,bool live,bool expired) noexcept {
  RootStorageSnapshot out;out.kind=RootStorageKind<T>;out.origin=RootStorageOrigin::SharedCoallocation;
  out.object=words[0];out.allocation=words[1];out.epoch=phase.epoch;out.thread=std::this_thread::get_id();out.payload_live=live;out.expired_weak=expired;
  out.supported=RootOwnerRepresentationSupported && history_known && phase.IsOpen() && phase.epoch && phase.participants==1 && out.kind!=RootStorageClass::Count;
  if(!words[0] && !words[1])return out;
  out.present=true;
#if defined(_WIN64) && defined(_MSVC_STL_VERSION) && _MSVC_STL_VERSION == 145 && _MSVC_STL_UPDATE == 202604L && _ITERATOR_DEBUG_LEVEL == 0
  if(!out.supported || !words[0] || !words[1]){out.supported=false;return out;}
  using Control=std::_Ref_count_obj2<T>;
  const auto *control=reinterpret_cast<const std::_Ref_count_base*>(words[1]);
  if(typeid(*control)!=typeid(Control)){out.supported=false;return out;}
  const auto *typed=static_cast<const Control*>(control);
  if(words[0]!=reinterpret_cast<uintptr_t>(std::addressof(typed->_Storage._Value))){out.supported=false;return out;}
  out.inline_bytes=sizeof(T);out.requested=sizeof(Control);
  // Reject aliases outside the storage of this exact default-factory object.
  if(words[0]<words[1] || words[0]-words[1]>sizeof(Control) || sizeof(T)>sizeof(Control)-(words[0]-words[1]) || alignof(Control)>__STDCPP_DEFAULT_NEW_ALIGNMENT__)out.supported=false;
#else
  (void)live;(void)expired;
#endif
  return out;
}
template<class T> inline RootStorageSnapshot CaptureSharedRootStorage(const std::shared_ptr<T> &owner,const AllocatorSourcePhase &phase,bool history_known) noexcept {
  const auto words=RootOwnerWords(owner);
  auto out=CaptureRootControl<T>(words,phase,history_known,bool(owner),false);
  if(words[0]!=reinterpret_cast<uintptr_t>(owner.get()))out.supported=false;
  if(out.supported && owner && typeid(*owner)!=typeid(T))out.supported=false;return out;
}
template<class T> inline RootStorageSnapshot CaptureWeakRootStorage(const std::weak_ptr<T> &guard,const AllocatorSourcePhase &phase,bool history_known) noexcept {
  const auto words=RootOwnerWords(guard);const bool expired=guard.expired();
  return CaptureRootControl<T>(words,phase,history_known,!expired,expired && words[1]!=0);
}
template<class T> inline RootStorageSnapshot CaptureDirectRootStorage(const T *object,RootStorageOrigin origin,const AllocatorSourcePhase &phase,bool factory_history_known,uintptr_t parent=0) noexcept {
  RootStorageSnapshot out;out.kind=RootStorageKind<T>;out.origin=origin;out.object=reinterpret_cast<uintptr_t>(object);out.parent=parent;
  out.epoch=phase.epoch;out.thread=std::this_thread::get_id();out.present=object!=nullptr;out.payload_live=out.present;
  out.supported=factory_history_known && phase.IsOpen() && phase.epoch && phase.participants==1 && out.kind!=RootStorageClass::Count &&
    (origin==RootStorageOrigin::OrdinaryUnique || origin==RootStorageOrigin::Stack || origin==RootStorageOrigin::Embedded);
  if(!out.supported)return out;
  if(object){out.inline_bytes=sizeof(T);if(typeid(*object)!=typeid(T))out.supported=false;
    if(origin==RootStorageOrigin::OrdinaryUnique){out.requested=sizeof(T);out.allocation=out.object;}}
  return out;
}
template<class Parent,class Child> inline RootStorageSnapshot CaptureEmbeddedRootStorage(const Parent *parent,const Child *child,const AllocatorSourcePhase &phase,bool history_known) noexcept {
  RootStorageSnapshot out;out.kind=RootStorageKind<Parent>;out.origin=RootStorageOrigin::Embedded;out.object=reinterpret_cast<uintptr_t>(child);out.parent=reinterpret_cast<uintptr_t>(parent);
  out.inline_bytes=sizeof(Child);out.epoch=phase.epoch;out.thread=std::this_thread::get_id();out.present=child!=nullptr;out.payload_live=out.present;
  out.supported=history_known && parent && child && phase.IsOpen() && phase.epoch && phase.participants==1 && out.kind!=RootStorageClass::Count;
  if(out.object<out.parent || out.object-out.parent>sizeof(Parent) || sizeof(Child)>sizeof(Parent)-(out.object-out.parent))out.supported=false;return out;
}
struct RootStorageEnvelope {
  std::array<size_t,RootStorageClasses> blocks{},expired{};
  size_t requested{},stack_inline{},embedded_inline{};bool supported{},overflow{};
};
// Source owners/weak guards stay unchanged and alive through this same open
// phase. Do not cache packets across guard reassignment or final weak release.
inline RootStorageEnvelope JoinRootStorage(const AllocatorSourcePhase &phase,const std::vector<const RootStorageSnapshot*> &views,
    const std::array<size_t,RootStorageClasses> &expected_blocks,bool complete_domain,bool parent_coverage_known) noexcept {
  RootStorageEnvelope out;out.supported=phase.IsOpen() && phase.epoch && phase.participants==1 && complete_domain && parent_coverage_known;
  const auto add=[&](size_t &a,size_t n){if(n>SIZE_MAX-a){a=SIZE_MAX;out.overflow=true;out.supported=false;}else a+=n;};
  for(size_t i=0;i<views.size();++i){const auto *s=views[i];
    if(!s || !s->supported || s->epoch!=phase.epoch || s->thread!=std::this_thread::get_id() || static_cast<size_t>(s->kind)>=RootStorageClasses){out.supported=false;continue;}
    if(!s->present){if(s->object || s->allocation || s->requested)out.supported=false;continue;}
    const auto k=static_cast<size_t>(s->kind);
    if(s->origin==RootStorageOrigin::Embedded){
      bool repeated=false;for(size_t j=0;j<i;++j)if(const auto *p=views[j])if(p->origin==RootStorageOrigin::Embedded && p->parent==s->parent && p->object==s->object){repeated=true;if(p->inline_bytes!=s->inline_bytes || p->kind!=s->kind)out.supported=false;}
      if(repeated)continue;
      bool found=false;for(const auto *p:views)if(p && p->payload_live && p->epoch==phase.epoch && p->present && p->object==s->parent && p->supported && p->object<=s->object &&
        s->object-p->object<=p->inline_bytes && s->inline_bytes<=p->inline_bytes-(s->object-p->object))found=true;
      if(!found || s->requested)out.supported=false;add(out.embedded_inline,s->inline_bytes);continue;
    }
    bool alias=false;for(size_t j=0;j<i;++j)if(const auto *p=views[j]){
      const bool same=s->allocation?p->allocation==s->allocation:!p->allocation && p->object==s->object;
      if(same){alias=true;if(p->kind!=s->kind || p->origin!=s->origin || p->object!=s->object || p->requested!=s->requested || p->inline_bytes!=s->inline_bytes || p->payload_live!=s->payload_live || p->expired_weak!=s->expired_weak)out.supported=false;}
    }
    if(alias)continue;
    if(s->origin==RootStorageOrigin::Stack){if(s->requested)out.supported=false;add(out.stack_inline,s->inline_bytes);continue;}
    if(!s->allocation || !s->requested || !s->object || (s->origin!=RootStorageOrigin::SharedCoallocation && s->origin!=RootStorageOrigin::OrdinaryUnique)){out.supported=false;continue;}
    ++out.blocks[k];if(s->expired_weak)++out.expired[k];add(out.requested,s->requested);
  }
  if(out.blocks!=expected_blocks || !phase.IsOpen())out.supported=false;return out;
}
} // namespace cutum
