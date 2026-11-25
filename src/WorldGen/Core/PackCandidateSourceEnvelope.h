#pragma once
#include "WorldGen/Core/CatalogCandidateSourceEnvelope.h"
#include "WorldGen/Core/WorldGenPack.h"
namespace cutum
{
// Fresh per-document pack/biome conversion. Existing target capacities, DOM,
// registry finalization, decoded image and directory/publication owners are
// separate terms. Default MSVC Release allocators.
inline CatalogCandidateEnvelope BoundPackCandidateSource(
    const CatalogCandidateInput &input,size_t biome_id_bytes=0)
{
  CatalogCandidateEnvelope result;result.overflow=input.overflow;
  const auto add=[&](size_t a,size_t b){if(b>SIZE_MAX-a){result.overflow=true;return SIZE_MAX;}return a+b;};
  const auto mul=[&](size_t a,size_t b){if(b && a>SIZE_MAX/b){result.overflow=true;return SIZE_MAX;}return a*b;};
  size_t perValue=0;
#if defined(_MSVC_STL_VERSION) && _ITERATOR_DEBUG_LEVEL == 0 && !JSON_DIAGNOSTIC_POSITIONS
  result.supported=true;result.shared_blocks_bytes=sizeof(std::_Ref_count_obj2<WorldGenPack>);
  perValue=4*(sizeof(PackOreRule)+sizeof(WorldGenStageId))+
      sizeof(std::_List_node<decltype(BiomePackDefinition::FeatureWeights)::value_type,void*>)+
      sizeof(std::_List_node<decltype(BiomePackDefinition::SubBiomes)::value_type,void*>)+
      2*16*2*sizeof(void*)+8*39+256;
#endif
  auto bound=add(16384,result.shared_blocks_bytes);
  bound=add(bound,mul(input.values,perValue));
  // Map key/value copies,rule temporaries and return moves;error messages may
  // contain arbitrary authored pool/slot text. No fixed string-length cap.
  bound=add(bound,mul(32,add(input.text_bytes,biome_id_bytes)));
  result.requested_heap_upper=bound;return result;
}
}
