#pragma once
#include "WorldGen/Core/PlaceholderSourceEnvelope.h"
#include "WorldGen/Core/TextureOverrideSourceEnvelope.h"
#include "WorldGen/Core/CatalogFilesystemSourceEnvelope.h"
namespace cutum
{
struct MergeRetainedInput
{
  size_t heap_upper{},runtime_rows{},runtime_name_max{},runtime_stem_max{};
  bool supported{},overflow{};
};
namespace detail
{
inline size_t MergeCensusUpper(const CatalogStorageCensus &c,bool &overflow)
{
  auto n=c.known_storage_bytes;overflow=overflow || c.overflow;
  CandidateAdd(n,c.msvc_unordered_extra_bytes,overflow);CandidateAdd(n,c.msvc_list_extra_bytes,overflow);CandidateAdd(n,c.msvc_tree_extra_bytes,overflow);
  size_t backings=c.strings;CandidateAdd(backings,c.vector_allocations,overflow);CandidateAdd(backings,c.tables,overflow);CandidateAdd(backings,c.lists,overflow);CandidateAdd(backings,c.tree_tables,overflow);
  if(backings>SIZE_MAX/39){overflow=true;return SIZE_MAX;}CandidateAdd(n,backings*39,overflow);return n;
}
}
struct MergeTargetSourceInspector:detail::CatalogStorageInspector
{
  MergeRetainedInput Input;
  void Placeholder(const std::shared_ptr<UPlaceholderTextureCache>&){}
  template<class T>void Vector(const T &values){
    detail::CatalogStorageInspector::Vector(values);
    using Overlay=std::pair<BlockDefinition,std::array<std::string,6>>;
    if constexpr(std::is_same_v<typename T::value_type,Overlay>){Input.runtime_rows=values.size();for(const auto &r:values){Input.runtime_name_max=std::max(Input.runtime_name_max,r.first.Name.size());for(const auto &s:r.second)Input.runtime_stem_max=std::max(Input.runtime_stem_max,s.size());}}
  }
};
inline MergeRetainedInput InspectMergeTargetInput(const UBlockMergeRegistry &registry)
{
  MergeTargetSourceInspector out;registry.InspectStorage(out);out.Input.supported=out.Result.unordered_model_supported;out.Input.heap_upper=detail::MergeCensusUpper(out.Result,out.Input.overflow);return out.Input;
}
inline CatalogCandidateEnvelope MeasureMergeManifests(const std::vector<ResourcePackManifest> &packs)
{
  detail::CatalogStorageInspector out;out.Vector(packs);for(const auto &p:packs)out.Manifest(p);
  CatalogCandidateEnvelope result;result.supported=out.Result.unordered_model_supported;result.requested_heap_upper=detail::MergeCensusUpper(out.Result,result.overflow);return result;
}
struct MergeSourceDomain
{
  size_t packs{},block_files{},json_parser_sum{},block_candidate_sum{},override_candidate_sum{};
  size_t block_max_bytes{},override_max_bytes{},id_max_bytes{},root_max_units{},entry_max_units{},file_reads{};
  size_t manifest_heap_upper{},extra_filesystem_diagnostic_upper{};
  MergeRetainedInput prior;
  PlaceholderSourceInput cache;
  int tile_size{16};bool placeholder{},supported{true},overflow{};
};
struct MergeSourceEnvelope
{
  size_t additional_heap_upper{},rows_upper{},memo_keys_upper{},path_units_upper{};
  bool supported{},overflow{};
};
// One Rebuild/default MSVC profile with stable inputs and no unbounded user
// work-pump callback. Additional requests only;all prior/escaped owners remain
// separate entry terms. FS diagnostic source bound can be supplied explicitly.
inline MergeSourceEnvelope BoundMergeSource(const MergeSourceDomain &d)
{
  const auto l=UBlockMergeRegistry::SourceLayout();MergeSourceEnvelope out;
  out.supported=d.supported && d.prior.supported && l.supported && (!d.placeholder || d.cache.supported);
  out.overflow=d.overflow || d.prior.overflow || (d.placeholder && d.cache.overflow);
  const auto add=[&](size_t a,size_t b){detail::CandidateAdd(a,b,out.overflow);return a;};
  const auto mul=[&](size_t a,size_t b){if(b && a>SIZE_MAX/b){out.overflow=true;return SIZE_MAX;}return a*b;};
  const auto n=add(d.block_files,d.prior.runtime_rows);out.rows_upper=n;
  const auto id=std::max(d.id_max_bytes,size_t{7});
  const auto name=std::max(add(add(d.block_max_bytes,id),2),d.prior.runtime_name_max);
  const auto raw=std::max({d.block_max_bytes,d.override_max_bytes,d.prior.runtime_stem_max,size_t{13}});
  out.path_units_upper=std::max(d.entry_max_units,add(add(d.root_max_units,raw),32));
  const auto fs=BoundCatalogFilesystemSource({out.path_units_upper,0,id,false});out.supported=out.supported && fs.supported;out.overflow=out.overflow || fs.overflow;
  auto total=add(add(65536,mul(d.manifest_heap_upper,4)),mul(d.prior.heap_upper,8));
  total=add(total,d.json_parser_sum);total=add(total,mul(add(d.block_candidate_sum,d.override_candidate_sum),16));
  // Simultaneous loaded/pending old+new cells and final maps/IDs/descriptors.
  const auto cells=add(add(mul(add(l.pending,l.block),4),add(add(l.merged_node,l.name_node),l.id_node)),add(mul(l.descriptor,4),4*sizeof(std::string)+3*16*2*sizeof(void*)+16*39+1024));
  total=add(total,mul(n,cells));
  const auto rowText=add(add(name,mul(add(add(raw,id),2),6)),add(mul(id,2),128));
  total=add(total,mul(mul(n,rowText),16));
  out.memo_keys_upper=mul(mul(n,12),d.packs); // two atlas passes,each <=6*N*P keys
  total=add(total,mul(out.memo_keys_upper,add(l.memo_node+16*2*sizeof(void*)+8*39+256,mul(add(add(raw,id),32),4))));
  total=add(total,mul(d.packs,add(l.owner_node+16*2*sizeof(void*)+128,mul(add(id,32),4))));
  total=add(total,mul(d.packs,fs.combined_heap_upper));
  total=add(total,mul(add(d.file_reads,mul(d.packs,2)),add(16384,mul(add(out.path_units_upper,32),64))));
  total=add(total,d.extra_filesystem_diagnostic_upper);
  if(d.placeholder && n){
    const auto calls=mul(n,12);auto cache=d.cache;
    size_t row=0;
#if defined(_MSVC_STL_VERSION) && _ITERATOR_DEBUG_LEVEL == 0
    row=sizeof(std::_List_node<std::pair<const std::string,std::string>,void*>)+sizeof(std::_List_node<std::pair<const std::string,TexturePixelData>,void*>)+sizeof(std::_List_node<std::pair<const std::string,std::list<std::string>::iterator>,void*>)+sizeof(std::_List_node<std::string,void*>)+3*16*2*sizeof(void*)+8*39+256;
#endif
    cache.structural_heap_upper=add(mul(cache.structural_heap_upper,8),add(mul(calls,row),mul(mul(calls,add(name,64)),64)));
    cache.overflow=cache.overflow || out.overflow;
    const auto one=BoundPlaceholderSource(cache,name,d.tile_size);out.overflow=out.overflow || one.overflow;
    size_t pixels=0;const auto tile=static_cast<size_t>(d.tile_size<1?1:d.tile_size);
    if(!CheckedRgbaByteCount(tile,tile,pixels))out.overflow=true;
    // Calls are sequential:retained new cache rows/tiles plus ONE call's
    // transient peak. Do not sum every call's bucket/path/stream scratch.
    const auto retainedNew=add(cache.structural_heap_upper,mul(calls,add(pixels,39)));
    total=add(total,add(retainedNew,one.requested_heap_upper));
  }
  out.additional_heap_upper=total;return out;
}
}
