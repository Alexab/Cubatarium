#pragma once
#include "WorldGen/Core/CatalogStorageCensus.h"
#include "WorldGen/Core/CatalogCandidateSourceEnvelope.h"
namespace cutum
{
// Temporary inspection is outside append observation. No clone of old blocks.
inline CatalogCandidateEnvelope MeasureResourceBlockPayload(
    const std::vector<ResourcePackBlock> &blocks)
{
  detail::CatalogStorageInspector inspector;
  for(const auto &block:blocks){inspector.Definition(block.Definition);for(const auto &s:block.TextureStems)inspector.Text(s);}
  const auto &c=inspector.Result;CatalogCandidateEnvelope result;result.supported=c.unordered_model_supported;result.overflow=c.overflow;
  const auto add=[&](size_t n){detail::CandidateAdd(result.requested_heap_upper,n,result.overflow);};
  add(c.known_storage_bytes);add(c.msvc_unordered_extra_bytes);
  size_t backings=c.strings;detail::CandidateAdd(backings,c.vector_allocations,result.overflow);detail::CandidateAdd(backings,c.tables,result.overflow);
  if(backings>SIZE_MAX/39){result.overflow=true;result.requested_heap_upper=SIZE_MAX;}else add(backings*39);
  return result;
}
// Additional ordinary-new requests during actual append only. Existing block
// array/payload and incoming parsed payload remain separate live source terms.
inline CatalogCandidateEnvelope BoundResourceBlockAppend(size_t size,size_t capacity,
    const CatalogCandidateEnvelope &prior_payload)
{
  CatalogCandidateEnvelope result;result.supported=prior_payload.supported;result.overflow=prior_payload.overflow;
  const auto add=[&](size_t a,size_t b){detail::CandidateAdd(a,b,result.overflow);return a;};
  const auto mul=[&](size_t a,size_t b){if(b && a>SIZE_MAX/b){result.overflow=true;return SIZE_MAX;}return a*b;};
  if(size>capacity){result.overflow=true;return result;}
  size_t array=0;
  if(size==capacity){const auto next=std::max(add(size,1),add(capacity,capacity/2));array=add(mul(next,sizeof(ResourcePackBlock)),39);}
  result.requested_heap_upper=add(add(16384,array),mul(prior_payload.requested_heap_upper,2));
  return result;
}
}
