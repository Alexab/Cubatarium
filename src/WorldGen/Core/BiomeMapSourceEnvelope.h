#pragma once
#include "Core/Memory/StbiSourceEnvelope.h"
#include "WorldGen/Core/WorldGenPack.h"

namespace cutum
{
struct BiomeMapSourceEnvelope
{
  StbiSourceEnvelope decoder;
  size_t owner_request{},retained_request_upper{},combined_peak_upper{};
  bool supported{},overflow{};
};
// Borrowed input path, file/CRT internals, optional ledger and prior/escaped
// images are separate owners. The ordinary owner is allocated AFTER decode;
// its request overlaps returned pixels, not decoder scratch. No vector copy.
inline BiomeMapSourceEnvelope BoundBiomeMapSource(const StbiSourceDomain &domain)
{
  BiomeMapSourceEnvelope result;
  if(domain.requested_components!=3) return result;
  result.decoder=BoundStbiOrdinarySource(domain);result.overflow=result.decoder.overflow;
#if defined(_MSVC_STL_VERSION) && _ITERATOR_DEBUG_LEVEL == 0
  result.supported=result.decoder.supported;
  result.owner_request=sizeof(std::_Ref_count_obj2<WorldGenBiomeMapImage>);
#endif
  if(result.decoder.returned_request_upper>SIZE_MAX-result.owner_request) {
    result.overflow=true;result.retained_request_upper=SIZE_MAX;
  } else result.retained_request_upper=result.decoder.returned_request_upper+result.owner_request;
  result.combined_peak_upper=std::max(result.decoder.admitted_peak_upper,result.retained_request_upper);
  return result;
}
} // namespace cutum
