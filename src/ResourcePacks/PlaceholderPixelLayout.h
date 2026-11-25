#pragma once
#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <limits>
namespace cutum
{
constexpr bool CheckedRgbaByteCount(size_t width,size_t height,size_t &bytes) noexcept
{
  bytes=0;
  if(!width || !height || width>SIZE_MAX/height || width*height>SIZE_MAX/4)return false;
  bytes=width*height*4;return true;
}
constexpr bool PlaceholderFileShapeFits(int width,int height,size_t bytes) noexcept
{
  size_t expected=0;
  return width>0 && height>0 && width==height &&
      CheckedRgbaByteCount(static_cast<size_t>(width),static_cast<size_t>(height),expected) &&
      expected==bytes && bytes<=(std::numeric_limits<uint32_t>::max)();
}
constexpr bool PlaceholderHeaderMatches(uint32_t width,uint32_t height,uint32_t bytes,int tileSize) noexcept
{
  const auto normalized=tileSize<1?1:tileSize;
  const auto size=static_cast<uint32_t>(normalized);
  return width==size && height==size && PlaceholderFileShapeFits(normalized,normalized,bytes);
}
}
