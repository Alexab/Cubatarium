#pragma once
#include "WorldGen/Core/CatalogJsonSource.h"
#include "WorldGen/Core/WorldGenRefs.h"
#include "WorldGen/Features/ObjectFeatureConfig.h"
#include "ResourcePacks/ResourcePack.h"

namespace cutum
{
enum class CatalogCandidateKind {Features,Refs,Manifest,Block};
struct CatalogCandidateInput
{
  size_t values{},text_bytes{};
  bool overflow{};
};
struct CatalogCandidateEnvelope
{
  size_t requested_heap_upper{},shared_blocks_bytes{};
  bool supported{},overflow{};
};
namespace detail
{
inline void CandidateAdd(size_t &total,size_t n,bool &overflow) {
  if(n>std::numeric_limits<size_t>::max()-total) {overflow=true;total=std::numeric_limits<size_t>::max();}
  else total+=n;
}
}
// Read-only model inspection phase;no copy/second parse of the input DOM.
// Iterative pointer stack is temporary and bounded by the value domain.
inline CatalogCandidateInput MeasureCatalogCandidateInput(const nlohmann::json &root)
{
  CatalogCandidateInput result;std::vector<const nlohmann::json*> pending{&root};
  while(!pending.empty()) {
    const auto *value=pending.back();pending.pop_back();detail::CandidateAdd(result.values,1,result.overflow);
    if(value->is_string()) detail::CandidateAdd(result.text_bytes,value->get_ref<const std::string&>().size(),result.overflow);
    if(value->is_object() || value->is_array()) for(auto it=value->begin();it!=value->end();++it) {
      if(value->is_object()) detail::CandidateAdd(result.text_bytes,it.key().size(),result.overflow);
      pending.push_back(&*it);
    }
  }return result;
}
// DOM-to-candidate stage only. Input DOM/retirement arena,existing publications,
// file/path/stream/CRT/platform/GPU owners are separate composition terms.
// Current default MSVC Release allocators;shared object+control coallocations
// below are INCLUDED,not another downstream additive term.
inline CatalogCandidateEnvelope BoundCatalogCandidateSource(CatalogCandidateKind kind,
    const CatalogCandidateInput &input,size_t manifest_root_units=0)
{
  CatalogCandidateEnvelope result;result.overflow=input.overflow;
  const auto add=[&](size_t a,size_t b) {if(b>SIZE_MAX-a){result.overflow=true;return SIZE_MAX;}return a+b;};
  const auto mul=[&](size_t a,size_t b) {if(b && a>SIZE_MAX/b){result.overflow=true;return SIZE_MAX;}return a*b;};
  size_t cells=0,map_node=0,diagnostic=0;
#if defined(_MSVC_STL_VERSION) && _ITERATOR_DEBUG_LEVEL == 0 && !JSON_DIAGNOSTIC_POSITIONS
  result.supported=true;
  switch(kind) {
  case CatalogCandidateKind::Features:
    result.shared_blocks_bytes=sizeof(std::_Ref_count_obj2<ObjectFeatureConfig>)+sizeof(std::_Ref_count_obj2<ObjectFeatureDiagnosticCatalog>);
    cells=4*(sizeof(ObjectFeatureRule)+sizeof(CaveFeatureRule)+sizeof(BiomeKey)+sizeof(BiomeId)+sizeof(SubBiomeId));
    diagnostic=sizeof(ObjectFeatureRuleCounters)+sizeof(size_t)+sizeof(uint8_t)+sizeof(size_t);break;
  case CatalogCandidateKind::Refs:
    result.shared_blocks_bytes=sizeof(std::_Ref_count_obj2<WorldGenRefsCatalog>);
    cells=4*sizeof(std::string);map_node=sizeof(std::_List_node<WorldGenRefsCatalog::value_type,void*>);break;
  case CatalogCandidateKind::Manifest:cells=4*sizeof(std::string);break;
  case CatalogCandidateKind::Block:
    cells=4*sizeof(std::string);map_node=sizeof(std::_List_node<decltype(BlockDefinition::DigGroups)::value_type,void*>);break;
  default:result.supported=false;return result;
  }
#else
  (void)kind;
#endif
  // <=V admitted vector cells per type,4x live old+new growth. MSVC map
  // sentinel/min buckets fixed below;node+<16 old/new buckets per entry.
  const size_t per_value=add(add(cells,diagnostic),add(map_node?add(map_node,16*2*sizeof(void*)):0,4*39+256));
  size_t bound=add(result.shared_blocks_bytes,16384);
  bound=add(bound,mul(input.values,per_value));
  // At most six block textures may duplicate source text;other families use
  // fewer copies.32x also covers conversion/default/error string overlaps.
  bound=add(bound,mul(input.text_bytes,32));
  // Manifest root copy,filename default ID/name and temporary native/narrow
  // conversions. Fresh canonical copies,not caller's arbitrary spare capacity.
  if(kind==CatalogCandidateKind::Manifest) {
    const auto text=[&](size_t n,size_t width){return add(mul(mul(add(n,32),2),width),39);};
    bound=add(bound,add(mul(text(manifest_root_units,sizeof(wchar_t)),4),mul(text(mul(manifest_root_units,4),1),4)));
  }
  result.requested_heap_upper=bound;return result;
}
} // namespace cutum
