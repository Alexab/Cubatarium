#pragma once
#include "Core/Memory/StbiAllocation.h"
#include <cstdint>

namespace cutum
{
enum class StbiSourceFormat {Png,Jpeg,Bmp,Tga,Psd,Pic,Gif,Hdr,Pnm,Count};
struct StbiDecoderLayout
{
  size_t jpeg_heap_state{},gif_info_heap_state{},context_inline{},png_inline{},zlib_inline{},gif_inline{};
  uint32_t format_mask{};
};
// Private decoder types measured in the same TU as STB_IMAGE_IMPLEMENTATION.
StbiDecoderLayout GetStbiDecoderLayout() noexcept;
#if defined(CUBA_STBI_SOURCE_ENVELOPE_TEST)
// Isolated arithmetic precondition fixture; not compiled into the application.
void *TestStbiConversionRange(unsigned route,void *ownedInput);
#endif
struct StbiSourceDomain
{
  StbiSourceFormat format{StbiSourceFormat::Png};
  int width{},height{},requested_components{4};
  // Source/header domains, not necessarily file length: a truncated PNG can
  // declare more IDAT than it contains; inflated bytes can exceed pixel rows.
  size_t png_idat_capacity_upper{std::numeric_limits<uint32_t>::max()};
  size_t png_expanded_capacity_upper{std::numeric_limits<uint32_t>::max()};
  size_t tga_palette_entries_upper{65535};
};
struct StbiSourceEnvelope
{
  size_t normalized_bytes{},returned_payload_upper{},returned_request_upper{},admitted_peak_upper{};
  bool supported{},overflow{};
};
inline StbiSourceEnvelope BoundStbiOrdinarySource(const StbiSourceDomain &domain)
{
  StbiSourceEnvelope result;
  const auto layout=GetStbiDecoderLayout();
  const auto kind=static_cast<unsigned>(domain.format);
  if(kind>=static_cast<unsigned>(StbiSourceFormat::Count) || !(layout.format_mask&(1u<<kind)) ||
      domain.width<=0 || domain.height<=0 || (domain.requested_components!=3 && domain.requested_components!=4)) return result;
  result.supported=true;
  const auto add=[&](size_t a,size_t b) {
    if(b>std::numeric_limits<size_t>::max()-a) {result.overflow=true;return std::numeric_limits<size_t>::max();}
    return a+b;
  };
  const auto mul=[&](size_t a,size_t b) {
    if(b && a>std::numeric_limits<size_t>::max()/b) {result.overflow=true;return std::numeric_limits<size_t>::max();}
    return a*b;
  };
  const size_t w=domain.width,h=domain.height,p=mul(w,h),prefix=StbiAllocationHeaderBytes();
  result.normalized_bytes=mul(p,static_cast<size_t>(domain.requested_components));
  result.returned_payload_upper=add(result.normalized_bytes,domain.format==StbiSourceFormat::Jpeg?1:0);
  result.returned_request_upper=add(result.returned_payload_upper,prefix);
  // Dispatch may allocate a JPEG probe before rejecting malformed input.
  const size_t probe=add(layout.jpeg_heap_state,prefix);
  size_t peak=0;
  switch(domain.format) {
  case StbiSourceFormat::Jpeg: {
    // <=4 component raw buffers(Q+15),progressive coefficients(2Q+15),
    // four(width+3) rows,output+1,one JPEG state;14 live allocation prefixes.
    const size_t q=mul(add(w,31),add(h,31));
    peak=add(add(add(layout.jpeg_heap_state,mul(q,12)),mul(w,4)),add(result.normalized_bytes,133));
    peak=add(peak,mul(prefix,14));break;
  }
  case StbiSourceFormat::Png:
    // Includes conservative old+new realloc allowance for both IDAT and
    // inflation,interlace pass/final/filter/palette/16->8/conversion overlap.
    peak=add(mul(add(domain.png_idat_capacity_upper,domain.png_expanded_capacity_upper),3),
        add(mul(p,32),add(mul(w,32),mul(prefix,16))));break;
  case StbiSourceFormat::Bmp: peak=add(mul(p,8),mul(prefix,2));break;
  case StbiSourceFormat::Tga:
    peak=add(add(mul(p,8),mul(domain.tga_palette_entries_upper,4)),mul(prefix,3));break;
  case StbiSourceFormat::Psd: peak=add(mul(p,16),mul(prefix,3));break;
  case StbiSourceFormat::Pic: peak=add(mul(p,8),mul(prefix,2));break;
  case StbiSourceFormat::Gif: peak=add(mul(p,12),mul(prefix,4));break;
  case StbiSourceFormat::Hdr: peak=add(add(mul(p,20),mul(w,4)),mul(prefix,3));break;
  case StbiSourceFormat::Pnm: peak=add(mul(p,24),mul(prefix,3));break;
  default: result.supported=false;return result;
  }
  result.admitted_peak_upper=std::max(probe,peak);
  return result;
}
} // namespace cutum
