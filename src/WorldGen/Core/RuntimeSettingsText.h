#pragma once
#include "WorldGen/Core/ProceduralSettings.h"
#include <limits>

namespace cutum
{
inline size_t AddTextCapacityBytes(size_t total,size_t bytes) noexcept
{
  return bytes>std::numeric_limits<size_t>::max()-total ? std::numeric_limits<size_t>::max() : total+bytes;
}
inline size_t ExternalStringCapacityBytes(const std::string &text) noexcept
{
  const auto data=reinterpret_cast<uintptr_t>(text.data());
  const auto object=reinterpret_cast<uintptr_t>(&text);
  return data>=object && data-object<sizeof(text) ? 0 : text.capacity()+1;
}
inline size_t RuntimeSettingsTextCapacityBytes(const ProceduralSettings &settings) noexcept
{
  size_t total=0;
  for(const auto *text:{&settings.SeedText,&settings.WorldGenPackId,&settings.WorldGenPresetId}) {
    const size_t bytes=ExternalStringCapacityBytes(*text);
    total=AddTextCapacityBytes(total,bytes);
  }
  return total;
}
// Only for private sampling snapshots or an idle worker context. Selectors
// must remain available for factory selection and owner lookup during work.
inline void ReleaseRuntimeSettingsText(ProceduralSettings &settings) noexcept
{
  for(auto *text:{&settings.SeedText,&settings.WorldGenPackId,&settings.WorldGenPresetId})
    std::string{}.swap(*text);
}
} // namespace cutum
