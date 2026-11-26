#pragma once
#include "Core/Memory/NameStorageSource.h"
#include "Core/Memory/AllocatorSourcePacket.h"
#include "Render/Engine/RenderThreadState.h"
#include "Render/Mesh/MeshPublicationThreadState.h"
#include "World/Core/WorldThreadState.h"
#include "Items/ItemInspectionSource.h"
#include "Items/ItemSurfaceRenderer.h"
#include "World/Lighting/ChunkLighting.h"
#include <list>

namespace cutum {
enum class EngineThreadCell:size_t {CpuTree,Captures,GpuFrames,PreviewTable,EnvironmentVector,EnvironmentNames,PublicationTrace,Count};
inline constexpr size_t EngineThreadCells=static_cast<size_t>(EngineThreadCell::Count);
struct EngineThreadLayout {
  // Whole actual TLS parents; child containers/pointers are never added again.
  std::array<size_t,6> inline_bytes{};
  // CPU tree node,preview hash node,iterator,GPU witness,celestial body,capture.
  std::array<size_t,6> widths{};
  size_t threshold{},slack{};bool supported{};
  bool Same(const EngineThreadLayout &b)const noexcept{return inline_bytes==b.inline_bytes && widths==b.widths && threshold==b.threshold && slack==b.slack && supported==b.supported;}
};
inline EngineThreadLayout GetEngineThreadLayout() noexcept {
  EngineThreadLayout out;
  out.inline_bytes={sizeof(detail::RenderThreadState),sizeof(detail::WorldThreadState),sizeof(detail::MeshPublicationThreadState),
    sizeof(item_inspection_detail::Current),sizeof(UItemSurfaceDrawAuditScope::Current),sizeof(SyncRelightTransport)};
#if defined(_MSVC_STL_VERSION) && _ITERATOR_DEBUG_LEVEL == 0
  using Tree=detail::CpuBoundLightWitnesses;using Hash=detail::LightPreviewStates;
  static_assert(std::is_same_v<typename std::allocator_traits<Tree::allocator_type>::void_pointer,void*>);
  static_assert(std::is_same_v<typename std::allocator_traits<Hash::allocator_type>::void_pointer,void*>);
  out.widths={sizeof(std::_Tree_node<Tree::value_type,void*>),sizeof(std::_List_node<Hash::value_type,void*>),sizeof(Hash::iterator),
    sizeof(detail::GpuOpaqueDrawStateWitness),sizeof(UWorld::UCelestialBodyVisual),sizeof(std::_Ref_count_obj2<BoundSectionLightCapture>)};
  out.threshold=std::_Big_allocation_threshold;out.slack=std::_Non_user_size;out.supported=true;
#endif
  return out;
}
inline size_t EngineThreadSeparateRequests(size_t count,size_t width,NameStorageEnvelope &out) noexcept {
  if(width && count>SIZE_MAX/width){out.overflow=true;out.supported=false;return SIZE_MAX;}return count*width;
}
struct EngineThreadStorageSnapshot {
  EngineThreadLayout layout;
  std::array<size_t,EngineThreadCells> requests{};
  std::array<uintptr_t,3> cells{};
  size_t cpu_entries{},capture_owners{},binding_handles{},gpu_capacity{},preview_entries{},preview_buckets{},celestial_capacity{};
  bool supported{},overflow{};
  bool Same(const EngineThreadStorageSnapshot &b)const noexcept {
    return layout.Same(b.layout) && requests==b.requests && cells==b.cells && cpu_entries==b.cpu_entries && capture_owners==b.capture_owners &&
      binding_handles==b.binding_handles && gpu_capacity==b.gpu_capacity && preview_entries==b.preview_entries && preview_buckets==b.preview_buckets &&
      celestial_capacity==b.celestial_capacity && supported==b.supported && overflow==b.overflow;
  }
  size_t Total()const noexcept {NameStorageEnvelope e;e.supported=supported;for(auto n:requests)NameStorageAdd(e,n);return e.requested;}
};
// The caller supplies the factory/coverage history for this same stable graph.
// Only source-owned default make_shared captures are a supported profile.
inline EngineThreadStorageSnapshot InspectCallingThreadEngineStorage(bool capture_factory_known,bool binding_roots_supplied) noexcept {
  EngineThreadStorageSnapshot out;out.layout=GetEngineThreadLayout();out.supported=out.layout.supported && capture_factory_known && binding_roots_supplied;
  const auto &r=detail::CallingRenderThreadState();const auto &w=detail::CallingWorldThreadState();const auto &m=detail::CallingMeshPublicationThreadState();
  out.cells={reinterpret_cast<uintptr_t>(&r),reinterpret_cast<uintptr_t>(&w),reinterpret_cast<uintptr_t>(&m)};
  NameStorageEnvelope e;e.supported=out.supported;
  const auto store=[&](EngineThreadCell c,size_t n){out.requests[static_cast<size_t>(c)]=n;};
  if(r.Cpu){
    out.cpu_entries=r.Cpu->size();store(EngineThreadCell::CpuTree,EngineThreadSeparateRequests(out.cpu_entries+1,out.layout.widths[0],e));
    for(auto i=r.Cpu->begin();i!=r.Cpu->end();++i){
      if(i->second.first)++out.binding_handles;
      const auto &capture=i->second.second;if(!capture)continue;
      bool seen=false;
      for(auto j=r.Cpu->begin();j!=i;++j){const auto &prior=j->second.second;if(!prior)continue;
        const bool owner=!capture.owner_before(prior) && !prior.owner_before(capture);
        if(owner){seen=true;if(capture.get()!=prior.get())e.supported=false;}
        else if(capture.get()==prior.get())e.supported=false;
      }
      if(!capture.use_count())e.supported=false;
      if(!seen)++out.capture_owners;
    }
    // make_shared uses direct new _Ref_count_obj2; it has no vector slack.
    store(EngineThreadCell::Captures,EngineThreadSeparateRequests(out.capture_owners,out.layout.widths[5],e));
  }
  out.gpu_capacity=r.GpuFrames.capacity();store(EngineThreadCell::GpuFrames,NameStorageBufferRequest(out.gpu_capacity,out.layout.widths[3],e));
  if(r.Preview){
    out.preview_entries=r.Preview->size();out.preview_buckets=r.Preview->bucket_count();
    NameStorageAdd(e,EngineThreadSeparateRequests(out.preview_entries+1,out.layout.widths[1],e));
    NameStorageAdd(e,NameStorageBufferRequest(out.preview_buckets,2*out.layout.widths[2],e));
    store(EngineThreadCell::PreviewTable,e.requested);e.requested=0;
  }
  if(r.EnvironmentValue){
    const auto &bodies=r.EnvironmentValue->CelestialBodies;out.celestial_capacity=bodies.capacity();
    store(EngineThreadCell::EnvironmentVector,NameStorageBufferRequest(bodies.capacity(),out.layout.widths[4],e));
    for(const auto &body:bodies)NameStorageAdd(e,NameStorageBufferRequest(NameStorageExternalBytes(body.Id),1,e));
    store(EngineThreadCell::EnvironmentNames,e.requested);e.requested=0;
  }
  store(EngineThreadCell::PublicationTrace,NameStorageBufferRequest(NameStorageExternalBytes(m.LastState),1,e));
  for(auto n:out.requests)NameStorageAdd(e,n);out.supported=e.supported;out.overflow=e.overflow;return out;
}
template<class Inspector> void InspectCallingThreadRenderWitnessRoots(Inspector &out){
  const auto &r=detail::CallingRenderThreadState();if(r.Cpu)for(const auto &entry:*r.Cpu)out.Binding(entry.second.first);
}
// Declared maximum new capacities/requests for one source transition. These
// come from the immutable input/history, never from a measured process peak.
struct EngineThreadTransitionDomain {
  size_t cpu_entries{},captures{},gpu_capacity{},preview_entries{},preview_buckets{},celestial_capacity{},name_requests_upper{},trace_requests_upper{};
  bool input_history_known{},factory_known{},binding_roots_supplied{};
};
inline NameStorageEnvelope BoundEngineThreadTransition(const EngineThreadStorageSnapshot &before,const EngineThreadTransitionDomain &d) noexcept {
  NameStorageEnvelope out;out.supported=before.supported && !before.overflow && d.input_history_known && d.factory_known && d.binding_roots_supplied;
  NameStorageAdd(out,before.Total());const auto &l=before.layout;
  if(d.cpu_entries==SIZE_MAX || d.preview_entries==SIZE_MAX || (d.preview_buckets && (d.preview_buckets<8 || (d.preview_buckets&(d.preview_buckets-1)))))out.supported=false;
  if(!out.supported)return out;
  NameStorageAdd(out,EngineThreadSeparateRequests(d.cpu_entries+1,l.widths[0],out));
  // Each node/capture is a distinct request; alignment depends on one node,
  // not the aggregate count. Current supported nodes are smaller than4096.
  if(d.captures && l.widths[5]>SIZE_MAX/d.captures){out.overflow=true;out.supported=false;return out;}
  NameStorageAdd(out,d.captures*l.widths[5]);
  NameStorageAdd(out,NameStorageBufferRequest(d.gpu_capacity,l.widths[3],out));
  NameStorageAdd(out,EngineThreadSeparateRequests(d.preview_entries+1,l.widths[1],out));
  NameStorageAdd(out,NameStorageBufferRequest(d.preview_buckets,2*l.widths[2],out));
  NameStorageAdd(out,NameStorageBufferRequest(d.celestial_capacity,l.widths[4],out));
  NameStorageAdd(out,d.name_requests_upper);NameStorageAdd(out,d.trace_requests_upper);return out;
}
struct EngineThreadStoragePacket {EngineThreadStorageSnapshot source;uint64_t epoch{};size_t participant{};std::thread::id thread;bool supported{};};
inline void ExportEngineThreadStorage(EngineThreadStoragePacket &out,const AllocatorSourcePhase &phase,size_t participant,bool factories,bool bindings) noexcept {
  out.source=InspectCallingThreadEngineStorage(factories,bindings);out.epoch=phase.epoch;out.participant=participant;out.thread=std::this_thread::get_id();out.supported=phase.IsOpen() && out.source.supported;
}
inline NameStorageEnvelope JoinEngineThreadStorage(const AllocatorSourcePhase &phase,const std::vector<const EngineThreadStoragePacket*> &packets,bool complete_domain,bool inline_parents_covered) noexcept {
  NameStorageEnvelope out;out.supported=phase.IsOpen() && phase.epoch && phase.participants && complete_domain && inline_parents_covered;
  size_t participants{};const EngineThreadStoragePacket *profile=nullptr;
  for(size_t i=0;i<packets.size();++i){const auto *p=packets[i];
    if(!p || !p->supported || !p->source.supported || !p->source.layout.supported || p->source.overflow || p->epoch!=phase.epoch || p->participant>=phase.participants || p->thread==std::thread::id{}){out.supported=false;continue;}
    if(!profile)profile=p;else if(!profile->source.layout.Same(p->source.layout))out.supported=false;
    if(std::any_of(p->source.cells.begin(),p->source.cells.end(),[](auto id){return !id;}))out.supported=false;
    bool alias=false;for(size_t j=0;j<i;++j)if(const auto *q=packets[j]){
      if(q->participant==p->participant){alias=true;if(q->thread!=p->thread || !q->source.Same(p->source))out.supported=false;}
      else if(q->thread==p->thread || q->source.cells[0]==p->source.cells[0] || q->source.cells[1]==p->source.cells[1] || q->source.cells[2]==p->source.cells[2])out.supported=false;
    }
    if(alias)continue;++participants;NameStorageAdd(out,p->source.Total());
  }
  if(participants!=phase.participants)out.supported=false;return out;
}
} // namespace cutum
