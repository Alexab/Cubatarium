#pragma once
#include "WorldGen/Core/MergeSourceEnvelope.h"
#include "Blocks/BlockDefinitionStorage.h"

namespace cutum
{
struct DefinitionPublicationInput
{
  size_t rows{},copied_heap_upper{};
  bool supported{},overflow{};
};
struct DefinitionPublicationInspector:detail::CatalogStorageInspector
{
  DefinitionPublicationInput Input;
  template<class T>void Text(const T&){}
  template<class T>void Vector(const T&){}
  void Manifest(const ResourcePackManifest&){}
  void Descriptor(const MergedCubeDesc&){}
  template<class T>void Placeholder(const T&){}
  void Definition(const BlockDefinition &value){
    detail::CandidateAdd(Input.rows,1,Input.overflow);detail::CatalogStorageInspector::Definition(value);
  }
  template<class T>void Map(const T &value){
    if constexpr(std::is_same_v<T,std::unordered_map<std::string,BlockId>>){
      detail::CatalogStorageInspector::Map(value);for(const auto &row:value)detail::CatalogStorageInspector::Text(row.first);
    }
  }
};
// Selected definition payload and name-map capacities only. Pending runtime
// definitions may be included conservatively;manifest/texture owners are not.
inline DefinitionPublicationInput InspectDefinitionPublication(const UBlockMergeRegistry &merge)
{
  DefinitionPublicationInspector out;merge.InspectStorage(out);
  out.Input.supported=out.Result.unordered_model_supported;
  out.Input.copied_heap_upper=detail::MergeCensusUpper(out.Result,out.Input.overflow);return out.Input;
}
inline CatalogCandidateEnvelope BoundDefinitionPublication(const DefinitionPublicationInput &input)
{
  CatalogCandidateEnvelope out;out.supported=input.supported;out.overflow=input.overflow;
  const auto add=[&](size_t a,size_t b){detail::CandidateAdd(a,b,out.overflow);return a;};
  const auto mul=[&](size_t a,size_t b){if(b && a>SIZE_MAX/b){out.overflow=true;return SIZE_MAX;}return a*b;};
  size_t node=0;
#if defined(_MSVC_STL_VERSION) && _ITERATOR_DEBUG_LEVEL == 0
  node=sizeof(std::_List_node<std::pair<const BlockId,BlockDefinition>,void*>);
  out.shared_blocks_bytes=sizeof(std::_Ref_count_obj2<BlockDefinitionCatalog>);
#endif
  // ById reserve/nodes and NameToId copy/parameter map moves/default catalog
  // sentinels,plus definition deep copies/old-new string/vector bucket growth.
  out.requested_heap_upper=add(add(add(16384,out.shared_blocks_bytes),mul(input.copied_heap_upper,8)),
      mul(mul(input.rows,4),add(node,16*2*sizeof(void*)+256)));
  return out;
}
struct TexturePublicationDomain
{
  size_t packs{},png_rows{},max_path_units{},id_max_bytes{},extra_filesystem_diagnostic_upper{};
  PlaceholderSourceInput cache;
  bool placeholder{},supported{true},overflow{};
};
// Additional staged texture tree and one sequential scan's scratch. Old target
// and cache remain entry owners;cached pixels are copied once into preparation.
// Stable default MSVC file profile. FS error text has an explicit source term.
inline CatalogCandidateEnvelope BoundTexturePublication(const TexturePublicationDomain &d,bool refresh=true)
{
  CatalogCandidateEnvelope out;out.supported=d.supported && (!d.placeholder || d.cache.supported);out.overflow=d.overflow;
  if(!refresh)return out; // all descriptor stems already present:zero requests
  const auto add=[&](size_t a,size_t b){detail::CandidateAdd(a,b,out.overflow);return a;};
  const auto mul=[&](size_t a,size_t b){if(b && a>SIZE_MAX/b){out.overflow=true;return SIZE_MAX;}return a*b;};
  size_t node=0;
#if defined(_MSVC_STL_VERSION) && _ITERATOR_DEBUG_LEVEL == 0
  node=sizeof(std::_Tree_node<std::map<std::string,UTextureBase>::value_type,void*>);
#else
  out.supported=false;
#endif
  const auto narrow=mul(d.max_path_units,4),key=add(add(d.id_max_bytes,narrow),2);
  auto upper=add(16384,mul(d.png_rows,add(node,mul(add(add(mul(key,2),narrow),128),8))));
  if(d.packs){const auto fs=BoundCatalogFilesystemSource({d.max_path_units,0,d.id_max_bytes,false});
    out.supported=out.supported && fs.supported;out.overflow=out.overflow || fs.overflow;upper=add(upper,fs.combined_heap_upper);}
  if(d.placeholder){const auto pixels=BoundPlaceholderRegistration(d.cache);out.overflow=out.overflow || pixels.overflow;upper=add(upper,pixels.requested_heap_upper);}
  out.requested_heap_upper=add(upper,d.extra_filesystem_diagnostic_upper);return out;
}
} // namespace cutum
