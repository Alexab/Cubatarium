#pragma once
#include "Blocks/BlockRegistry.h"
#include "Blocks/BlockDefinitionStorage.h"
#include "WorldGen/Core/WorldGenContentPin.h"
#include <algorithm>
#include <array>
#include <cstdint>

namespace cutum
{
class UObjectLibrary;

struct PipelineWeakShellLayout
{
  std::array<size_t,6> requests{}; // registry tag,definitions,pack,biomes,features,refs
  size_t per_thread_upper{};
  bool supported{};
};
// Default application make_shared factories only. This is the whole retained
// coallocation after T's destructor,not sizeof(shared_ptr) or dynamic payload.
inline PipelineWeakShellLayout GetPipelineWeakShellLayout() noexcept
{
  PipelineWeakShellLayout out;
#if defined(_MSVC_STL_VERSION) && _ITERATOR_DEBUG_LEVEL == 0
  out.requests={sizeof(std::_Ref_count_obj2<const unsigned char>),
    sizeof(std::_Ref_count_obj2<BlockDefinitionCatalog>),
    sizeof(std::_Ref_count_obj2<WorldGenPack>),
    sizeof(std::_Ref_count_obj2<UBiomeDefinitionRegistry>),
    sizeof(std::_Ref_count_obj2<ObjectFeatureConfig>),
    sizeof(std::_Ref_count_obj2<WorldGenRefsCatalog>)};
  for(auto n:out.requests)out.per_thread_upper+=n;
  out.supported=true;
#endif
  return out;
}
struct PipelineWeakShellEnvelope
{
  size_t requested_upper{};
  bool supported{},overflow{};
};
inline PipelineWeakShellEnvelope BoundPipelineWeakShells(size_t threads,
    bool thread_domain_known,bool default_owner_profile) noexcept
{
  const auto layout=GetPipelineWeakShellLayout();
  PipelineWeakShellEnvelope out;
  out.supported=layout.supported && thread_domain_known && default_owner_profile;
  if(threads && layout.per_thread_upper>SIZE_MAX/threads){out.overflow=true;out.requested_upper=SIZE_MAX;}
  else out.requested_upper=threads*layout.per_thread_upper;
  return out;
}
struct PipelineWeakShellCensus
{
  size_t occupied_slots{},distinct_owners{},expired_owners{},expired_request_upper{};
  bool supported{};
};

// Actual TLS cache identity;no new runtime owner or history is introduced.
struct PipelineOwnerKey
{
  std::weak_ptr<const void> registry;
  std::weak_ptr<const BlockDefinitionCatalog> definitions;
  std::weak_ptr<const WorldGenPack> pack;
  std::weak_ptr<const UBiomeDefinitionRegistry> biomes;
  std::weak_ptr<const ObjectFeatureConfig> features;
  std::weak_ptr<const WorldGenRefsCatalog> refs;
  UObjectLibrary *objects{nullptr};

  template<class T> static bool Same(const std::weak_ptr<T> &previous,const std::shared_ptr<T> &current) {
    return !previous.owner_before(current) && !current.owner_before(previous) && previous.lock().get()==current.get();
  }
  bool Matches(const UBlockRegistry &block_registry,UObjectLibrary *prefabs,const WorldGenContentSnapshot &content) const {
    return objects==prefabs && Same(registry,block_registry.GetLifetimeIdentity()) &&
        Same(definitions,block_registry.GetDefinitionsCatalogSnapshot()) && Same(pack,content.Pack) &&
        Same(biomes,content.Biomes) && Same(features,content.Features) && Same(refs,content.Refs);
  }
  void Assign(const UBlockRegistry &block_registry,UObjectLibrary *prefabs,const WorldGenContentSnapshot &content) {
    registry=block_registry.GetLifetimeIdentity();definitions=block_registry.GetDefinitionsCatalogSnapshot();
    pack=content.Pack;biomes=content.Biomes;features=content.Features;refs=content.Refs;objects=prefabs;
  }
  // Owner-thread,quiescent inspection. Temporary weak copies neither allocate
  // nor lock/retain the payload. Live controls already belong to E11's retained
  // graph and get no second charge here. External alias/deleter/allocator
  // backings require their own profile;their type cannot be discovered safely.
  PipelineWeakShellCensus InspectWeakShells(bool default_owner_profile) const noexcept {
    const auto layout=GetPipelineWeakShellLayout();
    PipelineWeakShellCensus out;out.supported=layout.supported && default_owner_profile;
    const std::array<std::weak_ptr<const void>,6> slots={registry,definitions,pack,biomes,features,refs};
    const std::weak_ptr<const void> empty;
    for(size_t i=0;i<slots.size();++i){
      if(!slots[i].owner_before(empty) && !empty.owner_before(slots[i]))continue;
      ++out.occupied_slots;
      bool seen=false;
      for(size_t j=0;j<i;++j)if(!slots[i].owner_before(slots[j]) && !slots[j].owner_before(slots[i])){seen=true;break;}
      if(seen)continue;
      ++out.distinct_owners;
      if(!slots[i].expired())continue;
      ++out.expired_owners;
      size_t width=layout.requests[i];
      for(size_t j=i+1;j<slots.size();++j)if(!slots[i].owner_before(slots[j]) && !slots[j].owner_before(slots[i]))
        width=std::max(width,layout.requests[j]);
      out.expired_request_upper+=width;
    }
    return out;
  }
};
} // namespace cutum
