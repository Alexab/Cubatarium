#pragma once
#include "WorldGen/Core/CatalogConstructionComposition.h"
#include "Core/Memory/StbiSourceEnvelope.h"
#include <climits>

namespace cutum
{
inline CatalogCandidateEnvelope BoundLegacyBaseDirectorySource(size_t entries,size_t path_units,
    size_t extra_filesystem_diagnostic_upper=0)
{
  CatalogCandidateEnvelope out;
  const auto add=[&](size_t a,size_t b){detail::CandidateAdd(a,b,out.overflow);return a;};
  const auto mul=[&](size_t a,size_t b){if(b && a>SIZE_MAX/b){out.overflow=true;return SIZE_MAX;}return a*b;};
  size_t node=0;
#if defined(_MSVC_STL_VERSION) && _ITERATOR_DEBUG_LEVEL == 0
  out.supported=true;node=sizeof(std::_Tree_node<std::map<std::string,UTextureBase>::value_type,void*>);
#endif
  const auto fs=BoundCatalogFilesystemSource({path_units,0,0,false});out.supported=out.supported && fs.supported;out.overflow=out.overflow || fs.overflow;
  out.requested_heap_upper=add(add(add(65536,fs.combined_heap_upper),mul(entries,add(node*4+256,mul(add(path_units,32),64)))),extra_filesystem_diagnostic_upper);return out;
}
inline CatalogCandidateEnvelope BoundLegacyCubeCandidateSource(const CatalogCandidateInput &d)
{
  CatalogCandidateEnvelope out;out.supported=kCatalogMsvcUnorderedModel;out.overflow=d.overflow;
  const auto add=[&](size_t a,size_t b){detail::CandidateAdd(a,b,out.overflow);return a;};
  const auto mul=[&](size_t a,size_t b){if(b && a>SIZE_MAX/b){out.overflow=true;return SIZE_MAX;}return a*b;};
  out.requested_heap_upper=add(add(16384,mul(d.values,4*sizeof(std::string)+256)),mul(d.text_bytes,32));return out;
}
inline CatalogCandidateEnvelope BoundLegacyCubeFileSource(size_t bytes,size_t path_units)
{
  CatalogCandidateEnvelope out;
  const auto add=[&](size_t a,size_t b){detail::CandidateAdd(a,b,out.overflow);return a;};
  const auto mul=[&](size_t a,size_t b){if(b && a>SIZE_MAX/b){out.overflow=true;return SIZE_MAX;}return a*b;};
  const auto candidate=BoundLegacyCubeCandidateSource({add(bytes,1),bytes,out.overflow});out.supported=candidate.supported;out.overflow=out.overflow || candidate.overflow;
  out.requested_heap_upper=add(add(add(mul(bytes,586),mul(bytes/32+(bytes%32!=0),16)),candidate.requested_heap_upper),
      add(32768,mul(add(path_units,32),64)));return out;
}
struct CpuAtlasSourceDomain
{
  int first_width{},first_height{},strip_frames{};size_t face_references{};
  size_t file_references{},max_filename_bytes{},prior_cache_entries{},prior_cache_buckets{};
  size_t returned_decoder_sum{},max_decoder_peak{};
  bool batch{},supported{true},overflow{};
};
inline void AddCpuAtlasDecodedSource(CpuAtlasSourceDomain &d,const StbiSourceDomain &source,size_t filename_bytes)
{
  const auto b=BoundStbiOrdinarySource(source);d.supported=d.supported && b.supported;d.overflow=d.overflow || b.overflow;
  detail::CandidateAdd(d.file_references,1,d.overflow);detail::CandidateAdd(d.returned_decoder_sum,b.returned_request_upper,d.overflow);
  d.max_decoder_peak=std::max(d.max_decoder_peak,b.admitted_peak_upper);d.max_filename_bytes=std::max(d.max_filename_bytes,filename_bytes);
}
struct CpuAtlasSourceEnvelope
{
  size_t atlas_bytes{},ordinary_new_upper{},new_decoder_peak_upper{},combined_additional_upper{};
  bool supported{},overflow{};
};
// Supplied source registrations,existing cache owners and old/returned atlases
// are retained entry terms. New cached pixels coexist with at most one active
// decoder:sum(returned request bounds)+max(single decode admission),not a sum
// of observed phase peaks. Potential file occurrences conservatively include
// cache hits/repeated filenames;actual census deduplicates their one owner.
inline CpuAtlasSourceEnvelope BoundCpuAtlasSource(const CpuAtlasSourceDomain &d)
{
  CpuAtlasSourceEnvelope out;out.supported=d.supported;out.overflow=d.overflow;
  const auto add=[&](size_t a,size_t b){detail::CandidateAdd(a,b,out.overflow);return a;};
  const auto mul=[&](size_t a,size_t b){if(b && a>SIZE_MAX/b){out.overflow=true;return SIZE_MAX;}return a*b;};
  const auto layout=UTextureCubeStorage::GetAtlasDecodeSourceLayout();out.supported=out.supported && layout.supported;
  if(d.first_width>0 && d.first_height>0 && d.face_references){
    const bool strip=d.strip_frames>1 && d.face_references==6;
    const auto frames=strip?static_cast<size_t>(d.strip_frames):std::max(size_t{1},d.face_references/6);
    if(frames<=INT_MAX){const auto height=strip?static_cast<size_t>(d.first_height)/frames:static_cast<size_t>(d.first_height);
      if(height && d.first_width<=INT_MAX/6 && height<=static_cast<size_t>(INT_MAX)/frames)
        out.atlas_bytes=mul(mul(mul(static_cast<size_t>(d.first_width),6),mul(height,frames)),4);
    }
  }
  auto upper=add(16384,add(out.atlas_bytes,out.atlas_bytes>=4096?39:0));
  upper=add(upper,mul(add(d.file_references,1),layout.control_request_upper));
  if(d.batch){upper=add(upper,mul(d.file_references,add(layout.cache_node_bytes+256,mul(add(d.max_filename_bytes,1),8))));
    const auto buckets=add(add(d.prior_cache_entries,d.prior_cache_buckets),add(d.file_references,8));upper=add(upper,mul(buckets,32*sizeof(void*)));}
  out.ordinary_new_upper=upper;out.new_decoder_peak_upper=add(d.returned_decoder_sum,d.max_decoder_peak);
  out.combined_additional_upper=add(upper,out.new_decoder_peak_upper);return out;
}
}
