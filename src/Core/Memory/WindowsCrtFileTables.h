#pragma once

#include "Core/Memory/StlSourceStorage.h"
#include "WindowsCrtFileTableProfile.h"
#include <climits>
#include <cstdint>
#include <cstdio>
#ifdef _WIN32
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#endif

namespace cutum
{

struct WindowsCrtFileTableStorage
{
  size_t streamSlots{}, fileSlotBytes{}, streamPointerTableBytes{}, dynamicFileSlotsUpper{};
  size_t descriptorBlocks{}, descriptorsPerBlock{}, descriptorBytes{}, descriptorBackingUpper{};
  size_t staticsBytes{}, currentRequestedHeapUpper{}, resizePointerTablesUpper{}, requestedHeapUpper{};
  size_t processHandles{};
  bool supported{}, processHandlesObserved{};
};

#if defined(_WIN64) && defined(_M_X64) && defined(_MSC_VER)
namespace windows_crt_file_detail
{
// Layout mirrors for the x64 static UCRT in Windows SDK 10.0.26100.0.
// These types determine storage widths without reading CRT-private objects.
struct FileSlot
{
  union { FILE publicFile; char *cursor; };
  char *base;
  int count;
  long flags, descriptor;
  int inlineChar, bufferSize;
  char *temporaryName;
  CRITICAL_SECTION lock;
};

struct Descriptor
{
  CRITICAL_SECTION lock;
  intptr_t osHandle;
  int64_t bufferPosition;
  unsigned char flags;
  char textMode, pipeLookahead[3];
  uint8_t unicode : 1;
  uint8_t utf8Translations : 1;
  uint8_t dbcsBufferUsed : 1;
  char multibyte[MB_LEN_MAX];
};

inline constexpr size_t StandardStreams = 3;
inline constexpr size_t DescriptorBlocks = 128;
inline constexpr size_t DescriptorsPerBlock = 1 << 6;
inline constexpr size_t MaximumDescriptors = DescriptorBlocks * DescriptorsPerBlock;
} // namespace windows_crt_file_detail
#endif

// Requested payload for one linked CRT instance. Lowio blocks may stay
// allocated after close. The maximum also covers the old and new stdio
// pointer tables coexisting in _recalloc_crt_t during _setmaxstdio.
inline constexpr WindowsCrtFileTableStorage BoundWindowsCrtFileTables(int streamLimit) noexcept
{
  WindowsCrtFileTableStorage result;
#if CUBATARIUM_CRT_FILE_TABLE_PROFILE && defined(_WIN64) && defined(_M_X64) && \
    defined(_MSC_VER) && !defined(_DLL) && !defined(_DEBUG)
  using namespace windows_crt_file_detail;
  if (!StlSourceStorage::SupportedProfile() ||
      streamLimit < static_cast<int>(StandardStreams) ||
      static_cast<size_t>(streamLimit) > MaximumDescriptors)
    return result;

  result.supported = true;
  result.streamSlots = static_cast<size_t>(streamLimit);
  result.fileSlotBytes = sizeof(FileSlot);
  result.streamPointerTableBytes = result.streamSlots * sizeof(FileSlot *);
  result.dynamicFileSlotsUpper = (result.streamSlots - StandardStreams) * sizeof(FileSlot);
  result.descriptorBlocks = DescriptorBlocks;
  result.descriptorsPerBlock = DescriptorsPerBlock;
  result.descriptorBytes = sizeof(Descriptor);
  result.descriptorBackingUpper = MaximumDescriptors * sizeof(Descriptor);
  // _iob, __badioinfo, __pioinfo, __piob and the _nstream/_nhandle/_cflush cells.
  result.staticsBytes = StandardStreams * sizeof(FileSlot) + sizeof(Descriptor) +
      DescriptorBlocks * sizeof(Descriptor *) + sizeof(FileSlot **) + 3 * sizeof(int);
  result.currentRequestedHeapUpper = result.streamPointerTableBytes +
      result.dynamicFileSlotsUpper + result.descriptorBackingUpper;
  result.resizePointerTablesUpper = 2 * MaximumDescriptors * sizeof(FileSlot *);
  result.requestedHeapUpper = (MaximumDescriptors - StandardStreams) * sizeof(FileSlot) +
      result.resizePointerTablesUpper + result.descriptorBackingUpper;
#else
  (void)streamLimit;
#endif
  return result;
}

inline WindowsCrtFileTableStorage InspectWindowsCrtFileTables() noexcept
{
#if defined(_WIN64) && defined(_MSC_VER)
  auto result = BoundWindowsCrtFileTables(_getmaxstdio());
  DWORD handles = 0;
  result.processHandlesObserved = GetProcessHandleCount(GetCurrentProcess(), &handles) != 0;
  result.processHandles = handles;
  return result;
#else
  return {};
#endif
}

} // namespace cutum
