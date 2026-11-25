#pragma once
#include "WorldGen/Core/CatalogCandidateSourceEnvelope.h"
#include "ResourcePacks/TextureOverrides.h"
#include <string_view>
namespace cutum
{
struct TextureOverrideYamlInput {size_t bytes{},lines{1},commas{};bool overflow{};};
inline TextureOverrideYamlInput MeasureTextureOverrideYamlInput(std::string_view source)
{
  TextureOverrideYamlInput out;out.bytes=source.size();
  for(char c:source){if(c=='\n')detail::CandidateAdd(out.lines,1,out.overflow);if(c==',')detail::CandidateAdd(out.commas,1,out.overflow);}return out;
}
// New conversion result and scratch only;borrowed DOM/istream backing/file
// buffer/prior versions are separate composition terms. Current MSVC Release.
inline CatalogCandidateEnvelope BoundTextureOverrideConversion(size_t elements,size_t text,bool overflow=false)
{
  CatalogCandidateEnvelope out;out.overflow=overflow;
  const auto add=[&](size_t a,size_t b){detail::CandidateAdd(a,b,out.overflow);return a;};
  const auto mul=[&](size_t a,size_t b){if(b && a>SIZE_MAX/b){out.overflow=true;return SIZE_MAX;}return a*b;};
  size_t cells=0;
#if defined(_MSVC_STL_VERSION) && _ITERATOR_DEBUG_LEVEL == 0
  out.supported=true;
  cells=4*sizeof(TextureFaceOverride)+4*8*sizeof(int)+
    sizeof(std::_List_node<TextureOverrideMap::value_type,void*>)+16*2*sizeof(void*)+8*39+256;
#endif
  out.requested_heap_upper=add(add(16384,mul(elements,cells)),mul(text,64));return out;
}
inline CatalogCandidateEnvelope BoundTextureOverrideJsonSource(const CatalogCandidateInput &input)
{return BoundTextureOverrideConversion(input.values,input.text_bytes,input.overflow);}
inline CatalogCandidateEnvelope BoundTextureOverrideYamlSource(const TextureOverrideYamlInput &input)
{
  auto n=input.lines;bool overflow=input.overflow;detail::CandidateAdd(n,input.commas,overflow);detail::CandidateAdd(n,1,overflow);
  return BoundTextureOverrideConversion(n,input.bytes,overflow);
}
}
