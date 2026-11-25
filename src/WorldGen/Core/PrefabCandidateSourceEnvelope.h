#pragma once
#include "WorldGen/Core/CatalogCandidateSourceEnvelope.h"
#include "World/Objects/ObjectLibrary.h"

namespace cutum
{
// One prefab candidate plus newly allocated diagnostic-set backing. Borrowed
// DOM, registry, caller strings, prior diagnostic nodes and publication map are
// separate owners. Default MSVC allocators and set max_load_factor == 1.
inline CatalogCandidateEnvelope BoundPrefabCandidateSource(
    const CatalogCandidateInput &input, size_t caller_text_bytes,
    size_t prior_unknown_count=0, size_t prior_unknown_buckets=0)
{
  CatalogCandidateEnvelope result;result.overflow=input.overflow;
  const auto add=[&](size_t a,size_t b){if(b>SIZE_MAX-a){result.overflow=true;return SIZE_MAX;}return a+b;};
  const auto mul=[&](size_t a,size_t b){if(b && a>SIZE_MAX/b){result.overflow=true;return SIZE_MAX;}return a*b;};
  size_t perValue=0;
#if defined(_MSVC_STL_VERSION) && _ITERATOR_DEBUG_LEVEL == 0 && !JSON_DIAGNOSTIC_POSITIONS
  result.supported=true;
  result.shared_blocks_bytes=sizeof(std::_Ref_count_obj2<WorldObjectDefinition>);
  perValue=4*(sizeof(std::string)+sizeof(ObjectVoxel))+
      sizeof(std::_List_node<std::string,void*>)+16*2*sizeof(void*)+8*39+256;
#endif
  auto upper=add(16384,result.shared_blocks_bytes);
  upper=add(upper,mul(input.values,perValue));
  // Name and source-pack strings recur in each admitted unknown-type log key.
  // 32x includes logKey expression temporaries, type/voxel/string growth,
  // filename native/narrow conversion and JSON failure diagnostics.
  upper=add(upper,mul(32,add(input.text_bytes,mul(add(input.values,1),caller_text_bytes))));
  upper=add(upper,mul(prior_unknown_count,32*sizeof(void*)));
  upper=add(upper,mul(prior_unknown_buckets,2*sizeof(void*)));
  result.requested_heap_upper=upper;return result;
}
}
