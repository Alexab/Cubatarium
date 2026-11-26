#pragma once
#include <array>
#include <cstddef>
#include <cstdint>

extern "C" bool CubatariumGlogMessageSourceLayout(size_t *out,size_t count) noexcept;
namespace cutum
{
struct RuntimeMessageSourceDomain
{
  size_t os_threads{},logging_threads{},maximum_depth{},noise_threads{};
  bool thread_domain_known{},callback_and_nesting_known{};
};
struct RuntimeMessageSourceEnvelope
{
  size_t named_static_inline{},named_tls_inline{},message_heap_upper{},noise_heap_upper{};
  size_t requested_heap_upper{};
  bool supported{},overflow{};
};
// Source sizes are compiled in the actual glog implementation. Stream32 and
// formatting16 are the verified default MSVC ordinary-new source profile,
// not CRT heap headers or an arbitrary user sink/locale/allocator guarantee.
inline RuntimeMessageSourceEnvelope BoundRuntimeMessageSource(const std::array<size_t,8> &glog,
    size_t noise_map_inline,size_t noise_peak_upper,bool noise_source_known,const RuntimeMessageSourceDomain &d)
{
  RuntimeMessageSourceEnvelope out;
  out.supported=noise_source_known && d.thread_domain_known && d.callback_and_nesting_known &&
    d.logging_threads<=d.os_threads && d.noise_threads<=d.os_threads &&
    (!d.logging_threads || d.maximum_depth>0) && glog[0] && glog[2]==glog[0] && glog[3]==1;
  const auto add=[&](size_t a,size_t b){if(b>SIZE_MAX-a){out.overflow=true;return SIZE_MAX;}return a+b;};
  const auto mul=[&](size_t a,size_t b){if(b && a>SIZE_MAX/b){out.overflow=true;return SIZE_MAX;}return a*b;};
  out.named_static_inline=add(glog[4],glog[7]);
  out.named_tls_inline=mul(d.os_threads,add(add(glog[2],glog[3]),noise_map_inline));
  // One data object uses raw TLS;additional nested nonfatal messages use heap.
  auto per_thread=mul(d.maximum_depth?d.maximum_depth-1:0,glog[0]);
  per_thread=add(per_thread,mul(d.maximum_depth,32));
  if(d.maximum_depth)per_thread=add(per_thread,16); // one transient prefix formatter state
  out.message_heap_upper=add(64,mul(d.logging_threads,per_thread)); // two static data streams
  out.noise_heap_upper=mul(d.noise_threads,noise_peak_upper);
  out.requested_heap_upper=add(out.message_heap_upper,out.noise_heap_upper);return out;
}
} // namespace cutum
