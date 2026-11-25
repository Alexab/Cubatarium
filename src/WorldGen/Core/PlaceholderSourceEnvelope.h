#pragma once
#include "WorldGen/Core/CatalogStorageCensus.h"
#include "WorldGen/Core/CatalogCandidateSourceEnvelope.h"
#include "ResourcePacks/PlaceholderPixelLayout.h"
namespace cutum
{
struct PlaceholderSourceInput
{
  size_t structural_heap_upper{},pixel_heap_upper{},path_units{};
  bool supported{},overflow{};
  size_t pixel_count{};
};
inline PlaceholderSourceInput MeasurePlaceholderSourceInput(const UPlaceholderTextureCache &cache)
{
  struct Inspector:detail::CatalogStorageInspector {
    PlaceholderSourceInput Input;
    void Path(const std::filesystem::path &path){Input.path_units=path.native().size();detail::CatalogStorageInspector::Path(path);}
    void Vector(const std::vector<uint8_t> &pixels){
      detail::CandidateAdd(Input.pixel_count,1,Input.overflow);
      detail::CandidateAdd(Input.pixel_heap_upper,pixels.capacity(),Input.overflow);
      if(pixels.capacity()>=4096)detail::CandidateAdd(Input.pixel_heap_upper,39,Input.overflow);
    }
  } out;
  cache.InspectStorage(out);const auto &c=out.Result;auto &input=out.Input;
  input.supported=c.unordered_model_supported;input.overflow=input.overflow || c.overflow;
  const auto add=[&](size_t n){detail::CandidateAdd(input.structural_heap_upper,n,input.overflow);};
  add(c.known_storage_bytes);add(c.msvc_unordered_extra_bytes);add(c.msvc_list_extra_bytes);
  size_t backings=c.strings;detail::CandidateAdd(backings,c.tables,input.overflow);detail::CandidateAdd(backings,c.lists,input.overflow);
  if(backings>SIZE_MAX/39){input.overflow=true;input.structural_heap_upper=SIZE_MAX;}else add(backings*39);
  return input;
}
// Additional ordinary-new requests during one GetOrCreateStem. Original
// structural/pixel backings,caller name/cache object and other versions remain
// separate entry owners. Standard default MSVC Release allocators/load factors.
inline CatalogCandidateEnvelope BoundPlaceholderSource(const PlaceholderSourceInput &input,
    size_t block_name_bytes,int tileSize)
{
  CatalogCandidateEnvelope out;out.supported=input.supported;out.overflow=input.overflow;
  const auto add=[&](size_t a,size_t b){detail::CandidateAdd(a,b,out.overflow);return a;};
  const auto mul=[&](size_t a,size_t b){if(b && a>SIZE_MAX/b){out.overflow=true;return SIZE_MAX;}return a*b;};
  size_t pixels=0;const auto size=static_cast<size_t>(tileSize<1?1:tileSize);
  if(!CheckedRgbaByteCount(size,size,pixels))out.overflow=true;
  // No prior pixel backing is copied. At most one new tile is read/generated;
  // malformed header/length is rejected before its advertised allocation.
  auto upper=add(16384,mul(input.structural_heap_upper,8));
  upper=add(upper,mul(add(add(block_name_bytes,mul(input.path_units,2)),128),64));
  upper=add(upper,add(pixels,39));out.requested_heap_upper=upper;return out;
}
inline CatalogCandidateEnvelope BoundPlaceholderRegistration(const PlaceholderSourceInput &input)
{
  CatalogCandidateEnvelope out;out.supported=input.supported;out.overflow=input.overflow;
  const auto add=[&](size_t a,size_t b){detail::CandidateAdd(a,b,out.overflow);return a;};
  const auto mul=[&](size_t a,size_t b){if(b && a>SIZE_MAX/b){out.overflow=true;return SIZE_MAX;}return a*b;};
  size_t node=0;
#if defined(_MSVC_STL_VERSION) && _ITERATOR_DEBUG_LEVEL == 0
  node=sizeof(std::_Tree_node<std::map<std::string,UTextureBase>::value_type,void*>);
#endif
  // RegisterPixels copies one vector per distinct stem,then moves it through
  // the texture value. Existing cache/target backing is not this new copy.
  out.requested_heap_upper=add(add(add(16384,input.pixel_heap_upper),mul(input.structural_heap_upper,2)),mul(input.pixel_count,add(node,39)));
  return out;
}
}
