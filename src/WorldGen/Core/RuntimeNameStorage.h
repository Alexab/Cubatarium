#pragma once
#include "Core/Environment.h"
#include "Core/Memory/NameStorageSource.h"
#include "Core/Memory/AllocatorSourcePacket.h"
#include "ResourcePacks/BlockMergeRegistry.h"

namespace cutum {
struct RuntimeNameStorageSnapshot {
  NameStorageSourceLayout layout;
  size_t flags{},flag_capacity{},environment_vector{},environment_names{},merge_name{},merge_capacity{};
  uintptr_t environment_cell{},merge_cell{};bool supported{},overflow{};
  size_t Total()const noexcept {NameStorageEnvelope e;e.supported=supported;NameStorageAdd(e,environment_vector);NameStorageAdd(e,environment_names);NameStorageAdd(e,merge_name);return e.requested;}
};
inline NameStorageSourceLayout GetRuntimeNameStorageLayout() noexcept {
  NameStorageSourceLayout out;out.flag_entry=sizeof(detail::EnvironmentFlagEntry);out.environment_inline=EnvironmentFlagCacheInlineBytes();
  out.merge_inline=sizeof(std::string);out.string_sso=std::string{}.capacity();out.string_max=std::string{}.max_size();
  out.flag_max=detail::EnvironmentFlagCache{}.max_size();
#if defined(_MSVC_STL_VERSION) && _ITERATOR_DEBUG_LEVEL == 0
  out.supported=true;out.large_threshold=std::_Big_allocation_threshold;out.large_slack=std::_Non_user_size;
#endif
  return out;
}
inline RuntimeNameStorageSnapshot InspectCallingThreadRuntimeNames() noexcept {
  RuntimeNameStorageSnapshot out;out.layout=GetRuntimeNameStorageLayout();out.supported=out.layout.supported;
  const auto &cache=detail::CallingThreadEnvironmentFlagCache();const auto &name=UBlockMergeRegistry::CallingThreadTypeNameForStorage();
  out.flags=cache.size();out.flag_capacity=cache.capacity();out.merge_capacity=name.capacity();
  out.environment_cell=reinterpret_cast<uintptr_t>(&cache);out.merge_cell=reinterpret_cast<uintptr_t>(&name);
  NameStorageEnvelope e;e.supported=out.supported;
  out.environment_vector=NameStorageBufferRequest(cache.capacity(),sizeof(detail::EnvironmentFlagEntry),e);
  for(const auto &entry:cache)NameStorageAdd(e,NameStorageBufferRequest(NameStorageExternalBytes(entry.name),1,e));
  out.environment_names=e.requested;e.requested=0;
  out.merge_name=NameStorageBufferRequest(NameStorageExternalBytes(name),1,e);
  NameStorageAdd(e,out.environment_vector);NameStorageAdd(e,out.environment_names);NameStorageAdd(e,out.merge_name);
  out.overflow=e.overflow;out.supported=e.supported;return out;
}
inline NameStorageEnvelope BoundEnvironmentFlagInsertion(const RuntimeNameStorageSnapshot &before,size_t name_length,bool history_known,bool miss_known) noexcept {
  NameStorageEnvelope out;out.supported=before.supported && before.layout.supported && !before.overflow && history_known && miss_known;NameStorageAdd(out,before.Total());
  NameStorageAdd(out,NameStorageStringGrowth(before.layout.string_sso,name_length,before.layout,out));
  if(before.flags>before.flag_capacity || before.flag_capacity>before.layout.flag_max || before.flags>=before.layout.flag_max){out.supported=false;return out;}
  if(before.flags==before.flag_capacity){
    const size_t old=before.flag_capacity,max=before.layout.flag_max;
    const size_t geometric=old>max-old/2?max:old+old/2;
    const size_t capacity=std::max(before.flags+1,geometric);
    NameStorageAdd(out,NameStorageBufferRequest(capacity,before.layout.flag_entry,out));
  }
  return out;
}
inline NameStorageEnvelope BoundMergeNameAssignment(const RuntimeNameStorageSnapshot &before,size_t length,bool history_known,bool found_known,bool found) noexcept {
  NameStorageEnvelope out;out.supported=before.supported && before.layout.supported && !before.overflow && history_known && found_known;NameStorageAdd(out,before.Total());
  if(found)NameStorageAdd(out,NameStorageStringGrowth(before.merge_capacity,length,before.layout,out));return out;
}
struct RuntimeNameStoragePacket {RuntimeNameStorageSnapshot source;uint64_t epoch{};size_t participant{};std::thread::id thread;bool supported{};};
inline void ExportRuntimeNameStorage(RuntimeNameStoragePacket &p,const AllocatorSourcePhase &phase,size_t participant) noexcept {
  p.source=InspectCallingThreadRuntimeNames();p.epoch=phase.epoch;p.participant=participant;p.thread=std::this_thread::get_id();p.supported=phase.IsOpen() && p.source.supported;
}
inline NameStorageEnvelope JoinRuntimeNameStorage(const AllocatorSourcePhase &phase,const std::vector<const RuntimeNameStoragePacket*> &packets,bool complete_domain,bool shells_already_counted) noexcept {
  NameStorageEnvelope out;out.supported=phase.IsOpen() && phase.epoch && phase.participants && complete_domain && shells_already_counted;
  const RuntimeNameStoragePacket *profile=nullptr;size_t participants=0;
  for(size_t i=0;i<packets.size();++i){const auto *p=packets[i];
    if(!p || !p->supported || !p->source.supported || p->source.overflow || !p->source.layout.supported || p->epoch!=phase.epoch || p->participant>=phase.participants || p->thread==std::thread::id{} || !p->source.environment_cell || !p->source.merge_cell){out.supported=false;continue;}
    if(profile && !profile->source.layout.Same(p->source.layout))out.supported=false;if(!profile)profile=p;
    bool alias=false;
    for(size_t j=0;j<i;++j)if(const auto *q=packets[j]){
      if(q->participant==p->participant){alias=true;
        if(q->thread!=p->thread || q->source.environment_cell!=p->source.environment_cell || q->source.merge_cell!=p->source.merge_cell ||
          q->source.environment_vector!=p->source.environment_vector || q->source.environment_names!=p->source.environment_names || q->source.merge_name!=p->source.merge_name ||
          q->source.flags!=p->source.flags || q->source.flag_capacity!=p->source.flag_capacity || q->source.merge_capacity!=p->source.merge_capacity)out.supported=false;
      }else if(q->thread==p->thread || q->source.environment_cell==p->source.environment_cell || q->source.merge_cell==p->source.merge_cell)out.supported=false;
    }
    if(alias)continue;++participants;NameStorageAdd(out,p->source.Total());
  }
  if(participants!=phase.participants)out.supported=false;return out;
}
}
