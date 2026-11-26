#pragma once
#include "WorldGen/Core/PipelineRetainedStorage.h"
#include "Core/Memory/CallbackStorageSource.h"
namespace cutum {
class UCore;class UApplication;
struct WorldOwnedStorageLayout {
  PipelineStorageLayout generation;
  std::array<size_t,4> lighting{}; // Flat,CPU Full,GPU Full,embedded CPU overlap
  size_t world_inline{},block_world_inline{},function_inline{};
  bool supported{};
  bool Same(const WorldOwnedStorageLayout &b)const noexcept{return generation.Same(b.generation) && lighting==b.lighting && world_inline==b.world_inline && block_world_inline==b.block_world_inline && function_inline==b.function_inline && supported==b.supported;}
};
WorldOwnedStorageLayout GetWorldOwnedStorageLayout() noexcept;
struct WorldOwnedStorageSnapshot {
  WorldOwnedStorageLayout layout;PipelineStorageSnapshot generation;
  std::array<CallbackStorageSnapshot,10> callbacks{};
  uintptr_t world{},lighting{};size_t lighting_requests{},lighting_kind{};
  bool supported{},overflow{},borrowed_context_matches{};
  size_t Total()const noexcept {
    PipelineStorageEnvelope out;out.supported=supported;AddPipelineStorage(out,generation,false);
    PipelineStorageAdd(out.additional_requests,lighting_requests,out);
    // The generator's callback is already included in its W10 callback cell.
    for(size_t i=1;i<callbacks.size();++i)PipelineStorageAdd(out.additional_requests,callbacks[i].requests,out);
    return out.additional_requests;
  }
};
struct WorldOwnedStorageEnvelope {size_t requested{},worlds{};bool supported{},overflow{};};
// The caller keeps this source graph stable; packets retain no source owner.
inline WorldOwnedStorageEnvelope JoinWorldOwnedStorage(const std::vector<const WorldOwnedStorageSnapshot*> &views,size_t expected_worlds,bool complete_domain,bool parent_shells_covered) noexcept {
  WorldOwnedStorageEnvelope out;out.supported=complete_domain && parent_shells_covered;const WorldOwnedStorageSnapshot *profile=nullptr;
  for(size_t i=0;i<views.size();++i){const auto *s=views[i];
    if(!s || !s->supported || s->overflow || !s->world || !s->layout.supported){out.supported=false;continue;}
    if(!profile)profile=s;else if(!profile->layout.Same(s->layout))out.supported=false;
    bool alias=false;
    for(size_t j=0;j<i;++j)if(const auto *p=views[j]){
      if(p->world==s->world){alias=true;
        if(p->generation.pipeline!=s->generation.pipeline || p->generation.requests!=s->generation.requests || p->lighting!=s->lighting || p->lighting_requests!=s->lighting_requests)out.supported=false;
        for(size_t c=0;c<s->callbacks.size();++c)if(p->callbacks[c].cell!=s->callbacks[c].cell || p->callbacks[c].owner!=s->callbacks[c].owner || p->callbacks[c].requests!=s->callbacks[c].requests || p->callbacks[c].present!=s->callbacks[c].present)out.supported=false;
      }else if((s->generation.pipeline && p->generation.pipeline==s->generation.pipeline) || (s->lighting && p->lighting==s->lighting))out.supported=false;
    }
    if(alias)continue;++out.worlds;const auto n=s->Total();
    if(n>SIZE_MAX-out.requested){out.overflow=true;out.supported=false;out.requested=SIZE_MAX;}else out.requested+=n;
  }
  if(out.worlds!=expected_worlds)out.supported=false;return out;
}
} // namespace cutum
