#pragma once

#include "WindowsCrtFileTableProfile.h"
#include <cstddef>
#include <cstdint>
#ifdef _WIN32
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#endif

#if CUBATARIUM_CRT_FILE_TABLE_PROFILE && defined(_WIN64) && defined(_M_X64) && \
    defined(_MSC_FULL_VER) && _MSC_FULL_VER == 195136260 && !defined(_DLL) && !defined(_DEBUG)
#define CUTUM_WINDOWS_STATIC_THREAD_CRT_PROFILE 1
extern "C" const IMAGE_TLS_DIRECTORY64 _tls_used;
#else
#define CUTUM_WINDOWS_STATIC_THREAD_CRT_PROFILE 0
#endif

namespace cutum
{
namespace windows_crt_thread_profile
{
// Layouts of the x64 static UCRT 10.0.26100.0 and VCRuntime 14.51.36231.
inline constexpr std::size_t UcrtPtdBytes = 968, VcrtPtdBytes = 128, LaunchParameterBytes = 40;
inline constexpr std::size_t ErrnoOffset = 32, LaunchContextOffset = 960;
inline constexpr std::size_t TimeStructBytes = 36, NarrowTimeTextBytes = 26, WideTimeTextBytes = 52;
inline constexpr std::size_t TimeScratchBytes = TimeStructBytes + NarrowTimeTextBytes + WideTimeTextBytes;
} // namespace windows_crt_thread_profile

struct WindowsCrtThreadStorage
{
  std::size_t primaryScopes{}, workerScopes{}, ucrtPtdUpper{}, workerVcrtPtdUpper{};
  std::size_t launchParameterUpper{}, timeScratchUpper{}, mainVcrtStaticBytes{};
  std::size_t tlsRawPerThread{}, tlsZeroFillPerThread{}, tlsTemplatePayload{};
  bool supported{};
};

// For the declared primary thread scopes of the linked image. CRT startup
// payload is accounted for while the worker is registered. TLS describes
// the .exe template width. Loader backing, other images, extra fibers and
// resources reached through CRT pointers have independent lifetimes.
inline WindowsCrtThreadStorage BoundWindowsCrtThreadStorage(std::size_t primaryScopes,
                                                          std::size_t mainScopes) noexcept
{
  WindowsCrtThreadStorage result;
#if CUTUM_WINDOWS_STATIC_THREAD_CRT_PROFILE
  using namespace windows_crt_thread_profile;
  if (mainScopes > 1 || mainScopes > primaryScopes ||
      _tls_used.EndAddressOfRawData < _tls_used.StartAddressOfRawData)
    return result;
  const auto raw = _tls_used.EndAddressOfRawData - _tls_used.StartAddressOfRawData;
  if (raw > SIZE_MAX - _tls_used.SizeOfZeroFill)
    return result;
  const auto tls = raw + _tls_used.SizeOfZeroFill;
  const auto workers = primaryScopes - mainScopes;
  if (primaryScopes > SIZE_MAX / UcrtPtdBytes || workers > SIZE_MAX / VcrtPtdBytes ||
      (tls && primaryScopes > SIZE_MAX / tls))
    return result;
  result.supported = true;
  result.primaryScopes = primaryScopes;
  result.workerScopes = workers;
  result.ucrtPtdUpper = primaryScopes * UcrtPtdBytes;
  result.workerVcrtPtdUpper = workers * VcrtPtdBytes;
  result.launchParameterUpper = workers * LaunchParameterBytes;
  result.timeScratchUpper = primaryScopes * TimeScratchBytes;
  result.mainVcrtStaticBytes = VcrtPtdBytes;
  result.tlsRawPerThread = raw;
  result.tlsZeroFillPerThread = _tls_used.SizeOfZeroFill;
  result.tlsTemplatePayload = primaryScopes * tls;
#else
  (void)primaryScopes;
  (void)mainScopes;
#endif
  return result;
}
} // namespace cutum
