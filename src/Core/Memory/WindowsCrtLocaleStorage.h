#pragma once

#include "Core/Memory/WindowsCrtThreadStorage.h"
#if CUTUM_WINDOWS_STATIC_THREAD_CRT_PROFILE
#include <locale.h>
extern "C" __crt_locale_data __acrt_initial_locale_data;
extern "C" __crt_multibyte_data __acrt_initial_multibyte_data;
#endif

namespace cutum
{
namespace windows_crt_locale_profile
{
inline constexpr std::size_t PairBytes = 16, LocaleBytes = 344, MultibyteBytes = 552;
inline constexpr std::size_t TimeBytes = 704, ConversionBytes = 152;
inline constexpr std::size_t ClassificationBytes = 768, CaseMapBytes = 768, CLabelBytes = 4;
// One copy of the named C locale objects and lookup tables in the image.
inline constexpr std::size_t InitialNamedBytes = PairBytes + LocaleBytes + MultibyteBytes +
    TimeBytes + ConversionBytes + ClassificationBytes + CaseMapBytes + CLabelBytes;
}

struct WindowsCrtLocaleStorage
{
  std::size_t initialNamedBytes{}, snapshotRequestBytes{}, localeRootRequest{}, multibyteRootRequest{};
  bool supported{}, sampled{}, initialLocale{}, initialMultibyte{}, privateMode{};
};

inline WindowsCrtLocaleStorage ObserveWindowsCrtLocale() noexcept
{
  WindowsCrtLocaleStorage result;
#if CUTUM_WINDOWS_STATIC_THREAD_CRT_PROFILE
  using namespace windows_crt_locale_profile;
  static_assert(sizeof(__crt_locale_pointers) == PairBytes);
  static_assert(sizeof(lconv) == ConversionBytes);
  result.supported = true;
  result.initialNamedBytes = InitialNamedBytes;
  const auto mode = _configthreadlocale(0);
  if (mode == -1)
    return result;
  const auto current = _get_current_locale();
  if (!current)
    return result;
  result.privateMode = mode == _ENABLE_PER_THREAD_LOCALE;
  result.snapshotRequestBytes = PairBytes;
  result.initialLocale = current->locinfo == &__acrt_initial_locale_data;
  result.initialMultibyte = current->mbcinfo == &__acrt_initial_multibyte_data;
  const auto heap = GetProcessHeap();
  const auto rootRequest = [heap](const void *root, std::size_t expected) {
    return root && HeapValidate(heap, 0, root) && HeapSize(heap, 0, root) == expected ? expected : 0;
  };
  if (!result.initialLocale)
    result.localeRootRequest = rootRequest(current->locinfo, LocaleBytes);
  if (!result.initialMultibyte)
    result.multibyteRootRequest = rootRequest(current->mbcinfo, MultibyteBytes);
  result.sampled = (result.initialLocale || result.localeRootRequest) &&
      (result.initialMultibyte || result.multibyteRootRequest);
  _free_locale(current);
#endif
  return result;
}
} // namespace cutum
