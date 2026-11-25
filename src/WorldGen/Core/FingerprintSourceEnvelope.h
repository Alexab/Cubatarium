#pragma once
#include <cstddef>
#include <cstdint>
#include <limits>
#include <string>
#include <vector>

namespace cutum
{
struct FingerprintSourceEnvelope
{
  size_t payload_bytes_upper{},requested_heap_upper{};
  bool supported{},overflow{};
};
// Additional ordinary-new requests only. Borrowed manifests/map and any prior
// returned strings remain separate owners. Each row uses <64 structural/
// decimal bytes; JSON escaping emits <=6 bytes per raw source byte. One sorted
// pointer vector,one reusable scalar string,one output string and serializer.
inline FingerprintSourceEnvelope BoundFingerprintSource(size_t packs,size_t names,
    size_t raw_text_bytes,size_t largest_text_bytes)
{
  FingerprintSourceEnvelope out;
#if defined(_MSVC_STL_VERSION) && _ITERATOR_DEBUG_LEVEL == 0
  out.supported=true;
#endif
  const auto add=[&](size_t a,size_t b){if(b>SIZE_MAX-a){out.overflow=true;return SIZE_MAX;}return a+b;};
  const auto mul=[&](size_t a,size_t b){if(b && a>SIZE_MAX/b){out.overflow=true;return SIZE_MAX;}return a*b;};
  out.payload_bytes_upper=add(add(32,mul(add(packs,names),64)),mul(raw_text_bytes,6));
  // MSVC growth old+new requests <=4*logical string limit (including aligned
  // allocation slack in fixed term). Scalar old/new growth <=8*(max text+1).
  // Fixed term includes serializer indent backing,output adapter/control,
  // scalar object,hash result,strict UTF8 diagnostic/error text and big align.
  out.requested_heap_upper=add(add(add(16384,mul(names,sizeof(void*))),
      mul(add(out.payload_bytes_upper,1),4)),mul(add(largest_text_bytes,1),8));
  return out;
}
} // namespace cutum
