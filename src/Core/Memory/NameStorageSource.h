#pragma once
#include <string>
#include <cstdint>
#include <algorithm>
#include <limits>

namespace cutum {
struct NameStorageSourceLayout {
  size_t flag_entry{},environment_inline{},merge_inline{},string_sso{},string_max{},flag_max{},large_threshold{},large_slack{};
  bool supported{};
  bool Same(const NameStorageSourceLayout &b)const{return flag_entry==b.flag_entry && environment_inline==b.environment_inline && merge_inline==b.merge_inline && string_sso==b.string_sso && string_max==b.string_max && flag_max==b.flag_max && large_threshold==b.large_threshold && large_slack==b.large_slack && supported==b.supported;}
};
inline size_t NameStorageExternalBytes(const std::string &s) noexcept {
  const auto data=reinterpret_cast<uintptr_t>(s.data()),object=reinterpret_cast<uintptr_t>(&s);
  return data>=object && data-object<sizeof(s)?0:s.capacity()+1;
}
inline size_t NameStorageAlignmentSlack(size_t bytes) noexcept {
#if defined(_MSVC_STL_VERSION) && _ITERATOR_DEBUG_LEVEL == 0
  return bytes>=std::_Big_allocation_threshold?std::_Non_user_size:0;
#else
  return 0;
#endif
}
struct NameStorageEnvelope {size_t requested{};bool supported{},overflow{};};
inline void NameStorageAdd(NameStorageEnvelope &out,size_t n) noexcept {
  if(n>SIZE_MAX-out.requested){out.requested=SIZE_MAX;out.overflow=true;out.supported=false;}else out.requested+=n;
}
inline size_t NameStorageBufferRequest(size_t count,size_t width,NameStorageEnvelope &out) noexcept {
  if(width && count>SIZE_MAX/width){out.overflow=true;out.supported=false;return SIZE_MAX;}
  const size_t bytes=count*width;
  if(NameStorageAlignmentSlack(bytes)>SIZE_MAX-bytes){out.overflow=true;out.supported=false;return SIZE_MAX;}
  return bytes+NameStorageAlignmentSlack(bytes);
}
inline size_t NameStorageStringGrowth(size_t old_capacity,size_t length,const NameStorageSourceLayout &l,NameStorageEnvelope &out) noexcept {
  if(length>l.string_max || old_capacity>l.string_max){out.supported=false;return 0;}
  if(length<=old_capacity)return 0;
  const size_t masked=length|l.string_sso;
  const size_t geometric=old_capacity>l.string_max-old_capacity/2?l.string_max:old_capacity+old_capacity/2;
  const size_t capacity=masked>l.string_max?l.string_max:std::max(masked,geometric);
  if(capacity==SIZE_MAX){out.overflow=true;out.supported=false;return SIZE_MAX;}
  return NameStorageBufferRequest(capacity+1,1,out);
}
}
