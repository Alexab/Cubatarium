#pragma once
#include "WorldGen/Core/MergeSourceEnvelope.h"
#include "WorldGen/Core/PackBiomeIndexSourceEnvelope.h"
#include "WorldGen/Core/BiomeMapSourceEnvelope.h"

namespace cutum
{
struct RetainedCatalogEnvelope
{
  size_t requested_heap_upper{},decoder_request_bytes{};
  bool supported{},overflow{};
};
// Supplied live root graph only,under the current application make_shared/
// default-allocator profile. Inline root shells conservatively stay in upper.
// Arbitrary external owner/deleter backings and expired weak-only shells need
// independent terms;no count of all worlds/threads is inferred here (C06).
inline RetainedCatalogEnvelope BoundRetainedCatalogRequests(const CatalogStorageCensus &c,
    bool default_make_shared_profile=true)
{
  RetainedCatalogEnvelope out;out.supported=c.unordered_model_supported && default_make_shared_profile;
  out.overflow=c.overflow;out.decoder_request_bytes=c.decoder_request_bytes;
  out.requested_heap_upper=detail::MergeCensusUpper(c,out.overflow);
#if defined(_MSVC_STL_VERSION) && _ITERATOR_DEBUG_LEVEL == 0
  static_assert(sizeof(std::_Ref_count_obj2<WorldGenPack>)<=sizeof(WorldGenPack)+64);
  static_assert(sizeof(std::_Ref_count_obj2<UBiomeDefinitionRegistry>)<=sizeof(UBiomeDefinitionRegistry)+64);
  static_assert(sizeof(std::_Ref_count_obj2<ObjectFeatureConfig>)<=sizeof(ObjectFeatureConfig)+64);
  static_assert(sizeof(std::_Ref_count_obj2<WorldGenRefsCatalog>)<=sizeof(WorldGenRefsCatalog)+64);
  static_assert(sizeof(std::_Ref_count_obj2<UObjectLibrary>)<=sizeof(UObjectLibrary)+64);
  static_assert(sizeof(std::_Ref_count_obj2<UBlockDefinitionStorage>)<=sizeof(UBlockDefinitionStorage)+64);
  static_assert(sizeof(std::_Ref_count_obj2<UBlockMergeRegistry>)<=sizeof(UBlockMergeRegistry)+64);
  static_assert(sizeof(std::_Ref_count_obj2<UTextureBaseStorage>)<=sizeof(UTextureBaseStorage)+64);
  static_assert(sizeof(std::_Ref_count_obj2<UTextureCubeStorage>)<=sizeof(UTextureCubeStorage)+64);
#endif
  if(c.shared_owners>SIZE_MAX/64){out.overflow=true;out.requested_heap_upper=SIZE_MAX;}
  else detail::CandidateAdd(out.requested_heap_upper,c.shared_owners*64,out.overflow);
  return out;
}
struct PackDirectorySourceDomain
{
  size_t json_files{},biome_files{},parser_sum{},candidate_sum{},biome_key_bytes_upper{};
  size_t max_path_units{},root_input_bytes{},extra_filesystem_diagnostic_upper{};
  StbiSourceDomain image;
  bool decode_image{},supported{true},overflow{};
};
// Read-only inventory may derive V<=N+1,text<=N without a second JSON parse.
// Existing accumulated pack members are bounded by prior conversion stages;
// outer biome-map nodes/keys/buckets and final registry/publication are added.
inline void AddPackDirectoryDocument(PackDirectorySourceDomain &d,size_t bytes,bool biome)
{
  const auto add=[&](size_t &a,size_t b){detail::CandidateAdd(a,b,d.overflow);};
  const auto mul=[&](size_t a,size_t b){if(b && a>SIZE_MAX/b){d.overflow=true;return SIZE_MAX;}return a*b;};
  add(d.json_files,1);size_t values=bytes;add(values,1);
  add(d.parser_sum,mul(bytes,586));add(d.parser_sum,mul(bytes/32+(bytes%32!=0),16));add(d.parser_sum,16640);
  size_t id=0;if(biome){add(d.biome_files,1);id=bytes;add(id,mul(d.max_path_units,4));add(d.biome_key_bytes_upper,id);}
  const auto candidate=BoundPackCandidateSource({values,bytes,d.overflow},id);
  d.supported=d.supported && candidate.supported;d.overflow=d.overflow || candidate.overflow;add(d.candidate_sum,candidate.requested_heap_upper);
}
struct PackDirectorySourceEnvelope
{
  size_t ordinary_new_upper{},new_decoder_peak_upper{},combined_additional_upper{};
  bool supported{},overflow{};
};
inline PackDirectorySourceEnvelope BoundPackDirectorySource(const PackDirectorySourceDomain &d)
{
  PackDirectorySourceEnvelope out;out.supported=d.supported;out.overflow=d.overflow;
  const auto add=[&](size_t a,size_t b){detail::CandidateAdd(a,b,out.overflow);return a;};
  const auto mul=[&](size_t a,size_t b){if(b && a>SIZE_MAX/b){out.overflow=true;return SIZE_MAX;}return a*b;};
  const auto fs=BoundCatalogFilesystemSource({d.max_path_units,0,0,false});
  const auto index=BoundPackBiomeIndexSource(d.biome_files,d.biome_key_bytes_upper);
  out.supported=out.supported && fs.supported && index.supported;out.overflow=out.overflow || fs.overflow || index.overflow;
  size_t biomeNode=0;
#if defined(_MSVC_STL_VERSION) && _ITERATOR_DEBUG_LEVEL == 0
  biomeNode=sizeof(std::_List_node<decltype(WorldGenPack::Biomes)::value_type,void*>);
#endif
  auto total=add(add(add(65536,d.parser_sum),mul(d.candidate_sum,8)),index.requested_heap_upper);
  total=add(total,mul(d.biome_files,biomeNode+16*2*sizeof(void*)+256));
  total=add(total,mul(d.biome_key_bytes_upper,8));
  total=add(total,fs.combined_heap_upper);
  // Default file/stream construction and concurrent root/entry/optional path
  // strings,logging,final reload-directory copy and shared pack move shell.
  total=add(total,mul(add(d.json_files,16),add(16384,mul(add(add(d.max_path_units,d.root_input_bytes),32),64))));
  total=add(total,d.extra_filesystem_diagnostic_upper);
  if(d.decode_image){const auto image=BoundBiomeMapSource(d.image);out.supported=out.supported && image.supported;
    out.overflow=out.overflow || image.overflow;total=add(total,image.owner_request);out.new_decoder_peak_upper=image.decoder.admitted_peak_upper;}
  out.ordinary_new_upper=total;out.combined_additional_upper=add(total,out.new_decoder_peak_upper);return out;
}
// Phase composition adds only NEW requests to the supplied retained entry.
// R_old is already inside entry;process decoder ledgers mirror that storage.
inline CatalogCandidateEnvelope ComposeCatalogConstruction(const RetainedCatalogEnvelope &entry,
    size_t additional,bool source_supported=true,bool source_overflow=false)
{
  CatalogCandidateEnvelope out;out.supported=entry.supported && source_supported;out.overflow=entry.overflow || source_overflow;
  out.requested_heap_upper=entry.requested_heap_upper;detail::CandidateAdd(out.requested_heap_upper,additional,out.overflow);return out;
}

struct CatalogFileSourceDomain
{
  CatalogCandidateKind kind{CatalogCandidateKind::Features};
  size_t bytes{},path_units{},manifest_root_units{},extra_filesystem_diagnostic_upper{};
};
// Real default file read+managed DOM+conversion/publication,with input path,
// old publications and external source owners supplied separately as entry.
inline CatalogCandidateEnvelope BoundCatalogFileSource(const CatalogFileSourceDomain &d)
{
  CatalogCandidateEnvelope out;
  const auto add=[&](size_t a,size_t b){detail::CandidateAdd(a,b,out.overflow);return a;};
  const auto mul=[&](size_t a,size_t b){if(b && a>SIZE_MAX/b){out.overflow=true;return SIZE_MAX;}return a*b;};
  const auto conversion=BoundCatalogCandidateSource(d.kind,{add(d.bytes,1),d.bytes,out.overflow},d.manifest_root_units);
  out.supported=conversion.supported;out.overflow=out.overflow || conversion.overflow;
  auto total=add(add(mul(d.bytes,586),mul(d.bytes/32+(d.bytes%32!=0),16)),16640);
  total=add(total,conversion.requested_heap_upper);
  total=add(total,add(16384,mul(add(d.path_units,32),64)));
  out.requested_heap_upper=add(total,d.extra_filesystem_diagnostic_upper);return out;
}
struct DefinitionDirectorySourceDomain
{
  size_t files{},file_source_sum{},max_path_units{},extra_filesystem_diagnostic_upper{};
  bool supported{true},overflow{};
};
inline CatalogCandidateEnvelope BoundDefinitionDirectorySource(const DefinitionDirectorySourceDomain &d)
{
  CatalogCandidateEnvelope out;out.supported=d.supported;out.overflow=d.overflow;
  const auto add=[&](size_t a,size_t b){detail::CandidateAdd(a,b,out.overflow);return a;};
  const auto mul=[&](size_t a,size_t b){if(b && a>SIZE_MAX/b){out.overflow=true;return SIZE_MAX;}return a*b;};
  size_t nodes=0;
#if defined(_MSVC_STL_VERSION) && _ITERATOR_DEBUG_LEVEL == 0
  nodes=sizeof(std::_List_node<decltype(BlockDefinitionCatalog::ById)::value_type,void*>)+
      sizeof(std::_List_node<decltype(BlockDefinitionCatalog::NameToId)::value_type,void*>);
#endif
  const auto fs=BoundCatalogFilesystemSource({d.max_path_units,0,0,false});out.supported=out.supported && fs.supported;out.overflow=out.overflow || fs.overflow;
  out.requested_heap_upper=add(add(add(add(65536,mul(d.file_source_sum,8)),
      mul(d.files,add(nodes,32*2*sizeof(void*)+256))),fs.combined_heap_upper),d.extra_filesystem_diagnostic_upper);
  return out;
}
} // namespace cutum
