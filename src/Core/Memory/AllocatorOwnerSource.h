#pragma once
#include "Core/Memory/BufferAllocationBudget.h"
#include "Core/Memory/ScopedSharedAllocation.h"
#include "Core/Memory/StbiAllocation.h"
#include "World/Lighting/SectionLightField.h"
#include <array>
#include <functional>

namespace cutum
{
enum class AllocatorOwnerCell:size_t {ConcurrentResource,StbiLedger,BufferBudget,JobDefault,JobCounted,LightDefault,LightCounted,Count};
constexpr size_t AllocatorOwnerCellCount=static_cast<size_t>(AllocatorOwnerCell::Count);
struct AllocatorOwnerLayout {
  std::array<size_t,AllocatorOwnerCellCount> requests{};
  size_t reserve_callback_bytes{},release_callback_bytes{};
  bool supported{};
};
// Default current MSVC Release source profile only. Count the whole shared
// object/control allocation once,including payload retained by weak owners.
// No live owners/TLS are read or initialized by this layout query.
inline AllocatorOwnerLayout GetAllocatorOwnerLayout() noexcept {
  AllocatorOwnerLayout out;
#if defined(_MSVC_STL_VERSION) && _ITERATOR_DEBUG_LEVEL == 0
  out.supported=true;
  out.requests={sizeof(std::_Ref_count_obj2<UConcurrentAllocationCountingResource>),
    sizeof(std::_Ref_count_obj2<UStbiAllocationLedger>),sizeof(std::_Ref_count_obj2<UBufferAllocationBudget>),
    sizeof(std::_Ref_count_obj2<BufferBudgetJob>),
    sizeof(std::_Ref_count_obj_alloc3<BufferBudgetJob,USharedCountingAllocator<BufferBudgetJob>>),
    sizeof(std::_Ref_count_obj2<SectionLightField>),
    sizeof(std::_Ref_count_obj_alloc3<SectionLightField,USharedCountingAllocator<SectionLightField>>)};
  using Reserve=std::_Func_impl_no_alloc<BufferBudgetReserveCallback,void,size_t>;
  using Release=std::_Func_impl_no_alloc<BufferBudgetReleaseCallback,void,size_t>;
  out.reserve_callback_bytes=std::_Is_large<Reserve>?sizeof(Reserve):0;
  out.release_callback_bytes=std::_Is_large<Release>?sizeof(Release):0;
#endif
  return out;
}
struct AllocatorOwnerDomain {
  // Identity-deduplicated physical blocks,including expired weak-only blocks.
  // Counted job/field blocks already present in E10/field requests go into
  // already_counted. Process ledger and TLS handles are inline W03 terms.
  std::array<size_t,AllocatorOwnerCellCount> blocks{},already_counted{};
  size_t population_callback_pairs{},additional_external_requests{};
  bool owner_graph_known{},source_profile_known{},external_history_known{};
};
struct AllocatorOwnerEnvelope {size_t additional_requests{};bool supported{},overflow{};};
inline AllocatorOwnerEnvelope BoundAllocatorOwners(const AllocatorOwnerLayout &s,const AllocatorOwnerDomain &d) noexcept {
  AllocatorOwnerEnvelope out;out.supported=s.supported && d.owner_graph_known && d.source_profile_known && d.external_history_known;
  const auto add=[&](size_t a,size_t b){if(b>SIZE_MAX-a){out.overflow=true;return SIZE_MAX;}return a+b;};
  const auto mul=[&](size_t a,size_t b){if(b && a>SIZE_MAX/b){out.overflow=true;return SIZE_MAX;}return a*b;};
  for(size_t i=0;i<AllocatorOwnerCellCount;++i){
    if(!s.requests[i] || d.already_counted[i]>d.blocks[i]){out.supported=false;continue;}
    out.additional_requests=add(out.additional_requests,mul(d.blocks[i]-d.already_counted[i],s.requests[i]));
  }
  out.additional_requests=add(out.additional_requests,mul(d.population_callback_pairs,add(s.reserve_callback_bytes,s.release_callback_bytes)));
  out.additional_requests=add(out.additional_requests,d.additional_external_requests);
  if(out.overflow)out.supported=false;
  return out;
}
} // namespace cutum
