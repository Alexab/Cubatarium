#pragma once
#include "WorldGen/Core/PackCandidateSourceEnvelope.h"
namespace cutum
{
// New standalone finalization result. Borrowed Biomes and any old/escaped
// registry/climate vectors are separate owners; source-pinned builtin table
// has 17 keys and <512 text bytes (independently checked by the fixture).
inline CatalogCandidateEnvelope BoundPackBiomeIndexSource(size_t authored_keys,
    size_t authored_key_bytes)
{
  CatalogCandidateEnvelope result;
  const auto add=[&](size_t a,size_t b){if(b>SIZE_MAX-a){result.overflow=true;return SIZE_MAX;}return a+b;};
  const auto mul=[&](size_t a,size_t b){if(b && a>SIZE_MAX/b){result.overflow=true;return SIZE_MAX;}return a*b;};
  size_t cells=0;
#if defined(_MSVC_STL_VERSION) && _ITERATOR_DEBUG_LEVEL == 0
  result.supported=true;
  result.shared_blocks_bytes=sizeof(std::_Ref_count_obj2<const UBiomeDefinitionRegistry>);
  cells=4*sizeof(BiomeKey)+8*sizeof(BiomeHandle)+
      sizeof(std::_List_node<std::pair<const BiomeKey,BiomeHandle>,void*>)+
      16*2*sizeof(void*)+8*39+256;
#endif
  const auto keys=add(authored_keys,17),text=add(authored_key_bytes,512);
  auto upper=add(16384,result.shared_blocks_bytes);
  upper=add(upper,mul(keys,cells));upper=add(upper,mul(text,32));
  result.requested_heap_upper=upper;return result;
}
}
