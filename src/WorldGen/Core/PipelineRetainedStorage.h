#pragma once
#include "Core/Memory/AllocatorSourcePacket.h"
#include <array>
#include <algorithm>

namespace cutum {
enum class PipelineStorageCell:size_t {Object,BiomeSource,ContextText,SampleText,SpawnCandidates,Callbacks,DensityCache,CoarseCache,WorldMetadata,WorldMemo,WorldControls,StlAlignment,Count};
constexpr size_t PipelineStorageCells=static_cast<size_t>(PipelineStorageCell::Count);
struct PipelineStorageLayout {
  // Whole object,2 biome strategies,3 function request shapes,spawn entry,
  // inline lake memo,7 empty world tables,memo table,3 controls,resource.
  std::array<size_t,12> widths{};
  std::array<size_t,9> inline_children{}; // overlaps inside whole pipeline
  size_t spawn_raw_upper{},stl_large_threshold{},stl_large_slack{};bool supported{};
  bool Same(const PipelineStorageLayout &b)const{return widths==b.widths && inline_children==b.inline_children && spawn_raw_upper==b.spawn_raw_upper && stl_large_threshold==b.stl_large_threshold && stl_large_slack==b.stl_large_slack && supported==b.supported;}
};
inline size_t PipelineStlAlignmentSlack(size_t bytes) noexcept {
#if defined(_MSVC_STL_VERSION) && _ITERATOR_DEBUG_LEVEL == 0
  return bytes>=std::_Big_allocation_threshold?std::_Non_user_size:0;
#else
  return 0;
#endif
}
struct PipelineStorageSnapshot {
  PipelineStorageLayout layout;
  std::array<size_t,PipelineStorageCells> requests{};
  uintptr_t pipeline{},biome_source{},world{},metadata_allocator{};
  bool initialized{},supported{},idle{true},overflow{};
};
struct PipelineScratchStorage {
  size_t metadata{},memo{},controls{};uintptr_t allocator{};
  bool supported{};
};
// Only the calling thread is observed. No world is constructed on a fresh
// thread. Snapshot contains no strong/weak owner or callback registration.
PipelineStorageSnapshot InspectCallingThreadPipelineStorage() noexcept;

struct PipelineStoragePacket {
  PipelineStorageSnapshot source;uint64_t epoch{};size_t participant{};
  std::thread::id thread;bool supported{};
};
inline void ExportPipelineStorage(PipelineStoragePacket &packet,const AllocatorSourcePhase &phase,size_t participant) noexcept {
  packet.source=InspectCallingThreadPipelineStorage();packet.epoch=phase.epoch;packet.participant=participant;
  packet.thread=std::this_thread::get_id();packet.supported=phase.IsOpen() && packet.source.supported && packet.source.idle;
}
struct PipelineStorageEnvelope {size_t additional_requests{},covered_requests{},participants{};bool supported{},overflow{};};
inline void PipelineStorageAdd(size_t &value,size_t n,PipelineStorageEnvelope &out) noexcept {
  if(n>SIZE_MAX-value){value=SIZE_MAX;out.overflow=true;out.supported=false;}else value+=n;
}
inline void AddPipelineStorage(PipelineStorageEnvelope &out,const PipelineStorageSnapshot &s,bool caches_covered) noexcept {
  out.supported=out.supported && s.supported && s.layout.supported && !s.overflow;
  for(size_t i=0;i<PipelineStorageCells;++i){const bool cache=i==static_cast<size_t>(PipelineStorageCell::DensityCache) || i==static_cast<size_t>(PipelineStorageCell::CoarseCache);
    PipelineStorageAdd(cache && caches_covered?out.covered_requests:out.additional_requests,s.requests[i],out);}
}
inline PipelineStorageEnvelope JoinPipelineStorage(const AllocatorSourcePhase &phase,
    const std::vector<const PipelineStoragePacket*> &packets,bool complete_domain,bool coverage_known,bool allocator_roots_covered,bool caches_covered=true) noexcept {
  PipelineStorageEnvelope out;out.supported=phase.IsOpen() && phase.epoch && complete_domain && coverage_known && allocator_roots_covered && phase.participants>0;
  const PipelineStoragePacket *profile=nullptr;
  for(size_t i=0;i<packets.size();++i){const auto *p=packets[i];
    if(!p || !p->supported || !p->source.idle || !p->source.layout.supported || p->epoch!=phase.epoch || p->participant>=phase.participants || p->thread==std::thread::id{}){out.supported=false;continue;}
    if(!p->source.initialized && (p->source.world || p->source.pipeline || p->source.biome_source || p->source.metadata_allocator ||
      std::any_of(p->source.requests.begin(),p->source.requests.end(),[](size_t n){return n!=0;})))out.supported=false;
    if(p->source.initialized && (!p->source.world || !p->source.metadata_allocator))out.supported=false;
    if(profile && !profile->source.layout.Same(p->source.layout))out.supported=false;
    if(!profile)profile=p;
    bool alias=false;
    for(size_t j=0;j<i;++j)if(const auto *q=packets[j]){
      if(q->participant==p->participant){
        alias=true;if(q->thread!=p->thread || q->source.requests!=p->source.requests ||
          q->source.pipeline!=p->source.pipeline || q->source.world!=p->source.world ||
          q->source.biome_source!=p->source.biome_source || q->source.metadata_allocator!=p->source.metadata_allocator ||
          q->source.initialized!=p->source.initialized || q->source.supported!=p->source.supported || !q->source.layout.Same(p->source.layout))out.supported=false;
      }else if(q->thread==p->thread || (p->source.world && q->source.world==p->source.world) ||
        (p->source.metadata_allocator && q->source.metadata_allocator==p->source.metadata_allocator) ||
        (p->source.pipeline && q->source.pipeline==p->source.pipeline) || (p->source.biome_source && q->source.biome_source==p->source.biome_source))out.supported=false;
    }
    if(alias)continue;++out.participants;AddPipelineStorage(out,p->source,caches_covered);
  }
  if(out.participants!=phase.participants)out.supported=false;
  return out;
}
struct PipelineTransitionDomain {
  size_t fresh_settings_text_upper{},fresh_owner_text_upper{};
  bool source_history_known{},new_world{},coverage_known{},caches_covered{};
};
// Conservative source construction/refresh/spawn upper. Before retains its
// actual capacity history;fresh text bound is supplied from the real input.
// W03 TLS and W07 allocator root coallocations remain separate base terms.
inline PipelineStorageEnvelope BoundPipelineStorageTransition(const PipelineStorageSnapshot &before,const PipelineTransitionDomain &d) noexcept {
  PipelineStorageEnvelope out;out.supported=before.supported && before.layout.supported && d.source_history_known && d.coverage_known;
  AddPipelineStorage(out,before,d.caches_covered);const auto &w=before.layout.widths;
  for(size_t n:{w[0],std::max(w[1],w[2]),w[5],before.layout.spawn_raw_upper})PipelineStorageAdd(out.additional_requests,n,out);
  PipelineStorageAdd(out.additional_requests,d.fresh_settings_text_upper,out);PipelineStorageAdd(out.additional_requests,d.fresh_settings_text_upper,out);
  PipelineStorageAdd(out.additional_requests,d.fresh_owner_text_upper,out);
  // At most3 settings buffers per copy and1 owner buffer;explicit source
  // upper remains valid for input capacities exactly at the large threshold.
  for(int i=0;i<7;++i)PipelineStorageAdd(out.additional_requests,before.layout.stl_large_slack,out);
  if(w[6]>SIZE_MAX/2500){out.supported=false;out.overflow=true;}
  else {PipelineStorageAdd(out.additional_requests,2500*w[6],out);PipelineStorageAdd(out.additional_requests,PipelineStlAlignmentSlack(2500*w[6]),out);}
  PipelineStorageAdd(d.caches_covered?out.covered_requests:out.additional_requests,size_t{81920}+20480,out);
  // Density,coarse values and coarse validity are three STL buffers. Their
  // request alignment slack is outside E06's already counted payload bytes.
  for(int i=0;i<3;++i)PipelineStorageAdd(out.additional_requests,before.layout.stl_large_slack,out);
  // Empty-table reset may briefly hold old and fresh sentinels/buckets.
  PipelineStorageAdd(out.additional_requests,w[8],out);
  if(d.new_world)for(size_t n:{w[9],w[10]})PipelineStorageAdd(out.additional_requests,n,out);
  return out;
}
}
