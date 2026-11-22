#include "Core/Memory/StbiAllocation.h"
#define STBI_MALLOC(bytes) cutum::StbiAllocate(bytes)
#define STBI_REALLOC(pointer,bytes) cutum::StbiReallocate(pointer,bytes)
#define STBI_FREE(pointer) cutum::StbiFree(pointer)
#define STB_IMAGE_IMPLEMENTATION
#include "ThirdParty/stb_image.h"
#include "Core/Memory/StbiSourceEnvelope.h"

cutum::StbiDecoderLayout cutum::GetStbiDecoderLayout() noexcept
{
  StbiDecoderLayout result;result.context_inline=sizeof(stbi__context);
  const auto enable=[&](StbiSourceFormat format) {result.format_mask|=1u<<static_cast<unsigned>(format);};
#ifndef STBI_NO_JPEG
  result.jpeg_heap_state=sizeof(stbi__jpeg);enable(StbiSourceFormat::Jpeg);
#endif
#ifndef STBI_NO_PNG
  result.png_inline=sizeof(stbi__png);enable(StbiSourceFormat::Png);
#endif
#ifndef STBI_NO_ZLIB
  result.zlib_inline=sizeof(stbi__zbuf);
#endif
#ifndef STBI_NO_GIF
  result.gif_info_heap_state=sizeof(stbi__gif);result.gif_inline=sizeof(stbi__gif);enable(StbiSourceFormat::Gif);
#endif
#ifndef STBI_NO_BMP
  enable(StbiSourceFormat::Bmp);
#endif
#ifndef STBI_NO_TGA
  enable(StbiSourceFormat::Tga);
#endif
#ifndef STBI_NO_PSD
  enable(StbiSourceFormat::Psd);
#endif
#ifndef STBI_NO_PIC
  enable(StbiSourceFormat::Pic);
#endif
#ifndef STBI_NO_HDR
  enable(StbiSourceFormat::Hdr);
#endif
#ifndef STBI_NO_PNM
  enable(StbiSourceFormat::Pnm);
#endif
  return result;
}

#if defined(CUBA_STBI_SOURCE_ENVELOPE_TEST)
void *cutum::TestStbiConversionRange(unsigned route,void *ownedInput)
{
  if(route==0) return stbi__convert_16_to_8(static_cast<stbi__uint16 *>(ownedInput),32768,32768,4);
  if(route==1) return stbi__convert_8_to_16(static_cast<stbi_uc *>(ownedInput),32768,16384,3);
#if !defined(STBI_NO_PNG) || !defined(STBI_NO_PSD)
  if(route==2) return stbi__convert_format16(static_cast<stbi__uint16 *>(ownedInput),1,4,32768,32768);
  if(route==3) return stbi__convert_format16(static_cast<stbi__uint16 *>(ownedInput),1,4,16384,16384);
#endif
  stbi_image_free(ownedInput);return nullptr;
}
#endif
