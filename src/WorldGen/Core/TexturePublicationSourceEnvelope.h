#pragma once
#include "WorldGen/Core/TextureConstructionComposition.h"

namespace cutum
{
struct TexturePublicationSourceDomain
{
  size_t prior_metadata_heap{},rows{},strings{},text_bytes{},maximum_atlas_bytes{};
  size_t file_references{},max_filename_bytes{},returned_decoder_sum{},max_decoder_peak{};
  size_t callback_calls{},callback_heap_per_call{},legacy_files{},legacy_file_sum{},max_path_units{};
  size_t extra_filesystem_diagnostic_upper{};
  bool supported{true},overflow{},callback_profile_known{true};
};
struct TextureMetadataSourceInspector:detail::CatalogStorageInspector
{
  template<class T>void TextureBases(const T &){}
  template<class T>void BlockStorage(const T &){}
};
inline size_t InspectTextureMetadataSource(const UTextureCubeStorage &storage,bool &overflow)
{TextureMetadataSourceInspector out;storage.InspectStorage(out);overflow=overflow || out.Result.overflow;return detail::MergeCensusUpper(out.Result,overflow);}
inline void AddTexturePublicationRow(TexturePublicationSourceDomain &d,const std::string &name,
    const std::vector<std::string> &stems,size_t atlas_bytes)
{
  detail::CandidateAdd(d.rows,1,d.overflow);detail::CandidateAdd(d.strings,1,d.overflow);detail::CandidateAdd(d.text_bytes,name.size(),d.overflow);
  for(const auto &stem:stems){detail::CandidateAdd(d.strings,1,d.overflow);detail::CandidateAdd(d.text_bytes,stem.size(),d.overflow);}d.maximum_atlas_bytes=std::max(d.maximum_atlas_bytes,atlas_bytes);
}
inline void AddTexturePublicationDecode(TexturePublicationSourceDomain &d,const StbiSourceDomain &source,size_t filename_bytes)
{
  const auto bound=BoundStbiOrdinarySource(source);d.supported=d.supported && bound.supported;d.overflow=d.overflow || bound.overflow;
  detail::CandidateAdd(d.file_references,1,d.overflow);detail::CandidateAdd(d.returned_decoder_sum,bound.returned_request_upper,d.overflow);
  d.max_decoder_peak=std::max(d.max_decoder_peak,bound.admitted_peak_upper);d.max_filename_bytes=std::max(d.max_filename_bytes,filename_bytes);
}
// One directory entry contains at most one legacy descriptor. Raw byte bounds
// cover its returned strings/vector without a second parse of the file.
inline void AddTexturePublicationLegacyFile(TexturePublicationSourceDomain &d,size_t bytes,size_t path_units)
{
  const auto file=BoundLegacyCubeFileSource(bytes,path_units);d.supported=d.supported && file.supported;d.overflow=d.overflow || file.overflow;
  detail::CandidateAdd(d.legacy_files,1,d.overflow);detail::CandidateAdd(d.rows,1,d.overflow);detail::CandidateAdd(d.legacy_file_sum,file.requested_heap_upper,d.overflow);
  size_t values=bytes;detail::CandidateAdd(values,1,d.overflow);detail::CandidateAdd(d.strings,values,d.overflow);detail::CandidateAdd(d.text_bytes,bytes,d.overflow);d.max_path_units=std::max(d.max_path_units,path_units);
}
struct TexturePublicationSourceEnvelope
{
  size_t ordinary_new_upper{},new_decoder_peak_upper{},combined_additional_upper{};
  bool supported{},overflow{};
};
// New preparation only:existing target/base/definition/catalog/returned roots
// belong to entry. Shadow metadata copies borrow GL names;one scratch holds the
// largest atlas,not all row atlases. Callback requested-data cost is an explicit
// supplied profile,including any retained callback results. Driver/CRT/GPU and
// arbitrary window/OS callback memory require their separate E12 terms.
inline TexturePublicationSourceEnvelope BoundTexturePublicationSource(const TexturePublicationSourceDomain &d)
{
  TexturePublicationSourceEnvelope out;out.supported=d.supported && (!d.callback_calls || d.callback_profile_known);out.overflow=d.overflow;
  const auto add=[&](size_t a,size_t b){detail::CandidateAdd(a,b,out.overflow);return a;};
  const auto mul=[&](size_t a,size_t b){if(b && a>SIZE_MAX/b){out.overflow=true;return SIZE_MAX;}return a*b;};
  const auto layout=UTextureCubeStorage::GetPublicationSourceLayout();out.supported=out.supported && layout.supported;
  CpuAtlasSourceDomain cache;cache.batch=true;cache.file_references=d.file_references;cache.max_filename_bytes=d.max_filename_bytes;cache.returned_decoder_sum=d.returned_decoder_sum;cache.max_decoder_peak=d.max_decoder_peak;
  const auto decoded=BoundCpuAtlasSource(cache);out.supported=out.supported && decoded.supported;out.overflow=out.overflow || decoded.overflow;
  const auto node=add(add(layout.row_node_bytes,mul(add(layout.cube_node_bytes,layout.name_node_bytes),4)),512);
  auto upper=add(add(add(65536,layout.stage_bytes),mul(d.prior_metadata_heap,8)),mul(d.rows,node));
  upper=add(add(upper,mul(d.strings,4*sizeof(std::string)+128)),mul(d.text_bytes,32));
  upper=add(add(upper,d.maximum_atlas_bytes),d.maximum_atlas_bytes>=4096?39:0);
  upper=add(add(upper,decoded.ordinary_new_upper),mul(d.callback_calls,d.callback_heap_per_call));
  if(d.legacy_files){const auto scan=BoundCatalogFilesystemSource({d.max_path_units,0,0,false});out.supported=out.supported && scan.supported;out.overflow=out.overflow || scan.overflow;
    upper=add(add(upper,scan.combined_heap_upper),mul(d.legacy_file_sum,8));}
  out.ordinary_new_upper=add(upper,d.extra_filesystem_diagnostic_upper);out.new_decoder_peak_upper=decoded.new_decoder_peak_upper;
  out.combined_additional_upper=add(out.ordinary_new_upper,out.new_decoder_peak_upper);return out;
}
}
