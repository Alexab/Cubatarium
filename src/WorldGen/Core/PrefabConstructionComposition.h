#pragma once
#include "WorldGen/Core/CatalogConstructionComposition.h"
#include "WorldGen/Core/PrefabCandidateSourceEnvelope.h"

namespace cutum
{
struct PrefabDirectoryDomain
{
  size_t files{},scans{},values_upper{},parser_sum{},candidate_sum{},max_path_units{},max_depth{},prefix_bytes{};
  size_t extra_filesystem_diagnostic_upper{};
  bool supported{true},overflow{};
};
inline void AddPrefabDirectoryFile(PrefabDirectoryDomain &d,size_t bytes)
{
  const auto add=[&](size_t &a,size_t b){detail::CandidateAdd(a,b,d.overflow);};
  const auto sum=[&](size_t a,size_t b){add(a,b);return a;};
  const auto mul=[&](size_t a,size_t b){if(b && a>SIZE_MAX/b){d.overflow=true;return SIZE_MAX;}return a*b;};
  const auto values=sum(bytes,1),caller=sum(sum(mul(d.max_path_units,8),mul(d.prefix_bytes,2)),64);
  const auto candidate=BoundPrefabCandidateSource({values,bytes,d.overflow},caller,d.values_upper,mul(sum(d.values_upper,1),16));
  d.supported=d.supported && candidate.supported;d.overflow=d.overflow || candidate.overflow;
  add(d.files,1);add(d.values_upper,values);add(d.candidate_sum,candidate.requested_heap_upper);
  add(d.parser_sum,mul(bytes,586));add(d.parser_sum,mul(bytes/32+(bytes%32!=0),16));add(d.parser_sum,16640);
}
// New independent library preparation only. Old library and escaped object
// definitions stay supplied entry owners. All scans use global path/depth
// maxima,including attempted empty subdirectories and missing scan roots.
inline CatalogCandidateEnvelope BoundPrefabDirectorySource(const PrefabDirectoryDomain &d)
{
  CatalogCandidateEnvelope out;out.supported=d.supported;out.overflow=d.overflow;
  const auto add=[&](size_t a,size_t b){detail::CandidateAdd(a,b,out.overflow);return a;};
  const auto mul=[&](size_t a,size_t b){if(b && a>SIZE_MAX/b){out.overflow=true;return SIZE_MAX;}return a*b;};
  size_t row=0;
#if defined(_MSVC_STL_VERSION) && _ITERATOR_DEBUG_LEVEL == 0
  row=sizeof(std::_List_node<std::pair<const std::string,std::shared_ptr<WorldObjectDefinition>>,void*>)+16*2*sizeof(void*)+64+256;
#else
  out.supported=false;
#endif
  const auto fs=BoundCatalogFilesystemSource({d.max_path_units,d.max_depth,d.prefix_bytes,true});out.supported=out.supported && fs.supported;out.overflow=out.overflow || fs.overflow;
  auto upper=add(add(add(65536,d.parser_sum),mul(d.candidate_sum,8)),mul(d.files,row));
  upper=add(upper,mul(d.scans,fs.combined_heap_upper));
  upper=add(upper,mul(add(d.files,d.scans),add(16384,mul(add(add(d.max_path_units,d.prefix_bytes),32),64))));
  out.requested_heap_upper=add(upper,d.extra_filesystem_diagnostic_upper);return out;
}
struct PrefabRebindInput
{
  size_t heap_upper{},objects{},voxels{},name_max{},type_max{},pack_max{};
  bool supported{},overflow{};
};
struct PrefabRebindInspector:detail::CatalogStorageInspector
{
  PrefabRebindInput Input;
  template<class T>void Prefab(const std::shared_ptr<T> &value){
    if(value){detail::CandidateAdd(Input.objects,1,Input.overflow);detail::CandidateAdd(Input.voxels,value->voxels.size(),Input.overflow);
      Input.name_max=std::max(Input.name_max,value->Name.size());Input.pack_max=std::max(Input.pack_max,value->SourcePackId.size());
      for(const auto &v:value->voxels)Input.type_max=std::max(Input.type_max,v.Type.size());}
    detail::CatalogStorageInspector::Prefab(value);
  }
};
inline PrefabRebindInput InspectPrefabRebindSource(const UObjectLibrary &library)
{
  PrefabRebindInspector out;library.InspectStorage(out);out.Input.supported=out.Result.unordered_model_supported;
  out.Input.overflow=out.Input.overflow || out.Result.overflow;
  out.Input.heap_upper=detail::MergeCensusUpper(out.Result,out.Input.overflow);
  if(out.Result.shared_owners>SIZE_MAX/64){out.Input.overflow=true;out.Input.heap_upper=SIZE_MAX;}
  else detail::CandidateAdd(out.Input.heap_upper,out.Result.shared_owners*64,out.Input.overflow);return out.Input;
}
// Production rows own distinct definitions. Registry lookup is borrowed and
// allocation-free;changed definitions clone once per row. New diagnostic keys
// may repeat the longest object/pack name once per voxel,so count that product.
inline CatalogCandidateEnvelope BoundPrefabRebindSource(const PrefabRebindInput &d)
{
  CatalogCandidateEnvelope out;out.supported=d.supported;out.overflow=d.overflow;
  const auto add=[&](size_t a,size_t b){detail::CandidateAdd(a,b,out.overflow);return a;};
  const auto mul=[&](size_t a,size_t b){if(b && a>SIZE_MAX/b){out.overflow=true;return SIZE_MAX;}return a*b;};
  using Pending=std::pair<std::shared_ptr<WorldObjectDefinition>*,std::shared_ptr<WorldObjectDefinition>>;
  size_t node=0;
#if defined(_MSVC_STL_VERSION) && _ITERATOR_DEBUG_LEVEL == 0
  node=sizeof(std::_List_node<std::string,void*>);
#else
  out.supported=false;
#endif
  const auto key=add(add(add(d.name_max,d.type_max),d.pack_max),128);
  out.requested_heap_upper=add(add(add(65536,mul(d.heap_upper,8)),mul(d.objects,sizeof(Pending)*4+128)),
      mul(d.voxels,add(node+16*2*sizeof(void*)+256,mul(key,16))));return out;
}
} // namespace cutum
