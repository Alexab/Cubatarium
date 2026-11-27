#pragma once

#include "Core/Memory/WindowsCrtFileTables.h"
#include "Core/Memory/WindowsCrtThreadStorage.h"
#include <algorithm>
#include <array>
#include <optional>
#include <vector>

#if CUTUM_WINDOWS_STATIC_THREAD_CRT_PROFILE
extern "C"
{
extern int _nstream, _nhandle;
extern cutum::windows_crt_file_detail::FileSlot **__piob;
extern cutum::windows_crt_file_detail::Descriptor *__pioinfo[cutum::windows_crt_file_detail::DescriptorBlocks];
extern void *__acrt_stdout_buffer, *__acrt_stderr_buffer;
}
#endif

namespace cutum
{
struct WindowsCrtIoStorage
{
  std::size_t stdioSlots{}, dynamicSlots{}, activeStreams{}, streamsWithoutDescriptor{};
  std::size_t lowioBlocks{}, openDescriptors{}, descriptorsWithoutHandle{};
  std::size_t pointerTableBytes{}, dynamicSlotBytes{}, descriptorBlockBytes{};
  std::size_t crtBuffers{}, crtBufferSpan{}, callerBuffers{}, callerBufferSpan{};
  std::size_t temporaryAttachments{}, temporaryBufferSpan{}, retainedTemporaryBuffers{}, retainedTemporaryBytes{};
  std::size_t linkedDescriptors{}, linkedStreams{};
  bool supported{CUTUM_WINDOWS_STATIC_THREAD_CRT_PROFILE != 0}, stable{};
};

#if CUTUM_WINDOWS_STATIC_THREAD_CRT_PROFILE
namespace windows_crt_io_detail
{
using namespace windows_crt_file_detail;
static_assert(sizeof(FileSlot) == 88 && offsetof(FileSlot, flags) == 20 && offsetof(FileSlot, descriptor) == 24);
static_assert(sizeof(Descriptor) == 72 && offsetof(Descriptor, osHandle) == 40 && offsetof(Descriptor, flags) == 56);
constexpr unsigned char DescriptorOpen = 0x01;
constexpr long SlotAllocated = 0x2000, CrtBuffer = 0x0040, CallerBuffer = 0x0080, TemporaryBuffer = 0x0200;
constexpr std::size_t StandardTemporaryBytes = 4096;

struct DescriptorBinding
{
  std::size_t index{}, streams{};
  std::intptr_t handle{};

  bool operator==(const DescriptorBinding &other) const
  {
    return index == other.index && streams == other.streams && handle == other.handle;
  }

  bool HasHandle() const { return handle && handle != -1 && handle != -2; }
};

struct StreamSlot
{
  FileSlot *address{};
  int descriptor{}, bufferSize{};
  char *buffer{};
  long flags{};

  bool operator==(const StreamSlot &other) const
  {
    return address == other.address && descriptor == other.descriptor && bufferSize == other.bufferSize &&
        buffer == other.buffer && flags == other.flags;
  }
};

struct Snapshot
{
  FileSlot **table{};
  std::size_t stdioSlots{}, lowioBlocks{};
  std::array<Descriptor *, DescriptorBlocks> blockPointers;
  std::array<void *, 2> standardTemporary;
  std::vector<StreamSlot> streams;
  std::vector<DescriptorBinding> descriptors;

  bool operator==(const Snapshot &other) const
  {
    return table == other.table && stdioSlots == other.stdioSlots && lowioBlocks == other.lowioBlocks &&
        blockPointers == other.blockPointers && standardTemporary == other.standardTemporary &&
        streams == other.streams && descriptors == other.descriptors;
  }
};

template<class T>
inline bool Copy(const T *source, T *target, std::size_t count = 1) noexcept
{
  SIZE_T bytes{};
  return source && ReadProcessMemory(GetCurrentProcess(), source, target, count * sizeof(T), &bytes) &&
      bytes == count * sizeof(T);
}

// Read the pinned static UCRT without acquiring its internal locks or
// dereferencing a table pointer that another thread could have retired.
inline std::optional<Snapshot> ReadSnapshot()
{
  Snapshot result;
  int stdio{}, lowio{};
  if (!Copy(&_nstream, &stdio) || !Copy(&_nhandle, &lowio) || !Copy(&__piob, &result.table) ||
      stdio < static_cast<int>(StandardStreams) || stdio > static_cast<int>(MaximumDescriptors) ||
      lowio < static_cast<int>(DescriptorsPerBlock) || lowio > static_cast<int>(MaximumDescriptors) ||
      lowio % DescriptorsPerBlock || !Copy(__pioinfo, result.blockPointers.data(), result.blockPointers.size()))
    return {};
  if (!Copy(&__acrt_stdout_buffer, &result.standardTemporary[0]) || !Copy(&__acrt_stderr_buffer, &result.standardTemporary[1]) ||
      (result.standardTemporary[0] && result.standardTemporary[0] == result.standardTemporary[1]))
    return {};
  result.stdioSlots = static_cast<std::size_t>(stdio);
  result.lowioBlocks = static_cast<std::size_t>(lowio) / DescriptorsPerBlock;
  for (std::size_t i = 0; i < result.blockPointers.size(); ++i)
  {
    const auto block = result.blockPointers[i];
    if (static_cast<bool>(block) != (i < result.lowioBlocks))
      return {};
    if (!block)
      continue;
    std::array<Descriptor, DescriptorsPerBlock> entries;
    if (!Copy(block, entries.data(), entries.size()))
      return {};
    for (std::size_t offset = 0; offset < entries.size(); ++offset)
      if (entries[offset].flags & DescriptorOpen)
        result.descriptors.push_back({i * DescriptorsPerBlock + offset, 0, entries[offset].osHandle});
  }

  std::vector<FileSlot *> entries(result.stdioSlots);
  if (!Copy(result.table, entries.data(), entries.size()) ||
      reinterpret_cast<FILE *>(entries[0]) != stdin || reinterpret_cast<FILE *>(entries[1]) != stdout ||
      reinterpret_cast<FILE *>(entries[2]) != stderr)
    return {};
  for (const auto address : entries)
  {
    if (!address)
      continue;
    FileSlot slot{};
    if (!Copy(address, &slot))
      return {};
    auto &stream = result.streams.emplace_back();
    stream.address = address;
    stream.flags = slot.flags & (SlotAllocated | CrtBuffer | CallerBuffer | TemporaryBuffer);
    if (!(stream.flags & SlotAllocated))
      continue;
    stream.descriptor = slot.descriptor;
    if (stream.flags & (CrtBuffer | CallerBuffer | TemporaryBuffer))
    {
      if (!slot.base || slot.bufferSize <= 0)
        return {};
      stream.buffer = slot.base;
      stream.bufferSize = slot.bufferSize;
    }
    if (stream.descriptor >= 0)
    {
      const auto fd = static_cast<std::size_t>(stream.descriptor);
      const auto binding = std::lower_bound(result.descriptors.begin(), result.descriptors.end(), fd,
          [](const auto &row, auto index) { return row.index < index; });
      if (binding != result.descriptors.end() && binding->index == fd)
        ++binding->streams;
    }
  }
  return result;
}

inline WindowsCrtIoStorage Summarize(const Snapshot &snapshot)
{
  WindowsCrtIoStorage result;
  result.stable = true;
  result.stdioSlots = snapshot.stdioSlots;
  result.dynamicSlots = snapshot.streams.size() - StandardStreams;
  result.lowioBlocks = snapshot.lowioBlocks;
  result.openDescriptors = snapshot.descriptors.size();
  result.pointerTableBytes = result.stdioSlots * sizeof(FileSlot *);
  result.dynamicSlotBytes = result.dynamicSlots * sizeof(FileSlot);
  result.descriptorBlockBytes = result.lowioBlocks * DescriptorsPerBlock * sizeof(Descriptor);
  result.retainedTemporaryBuffers = static_cast<std::size_t>(std::count_if(
      snapshot.standardTemporary.begin(), snapshot.standardTemporary.end(), [](auto buffer) { return buffer != nullptr; }));
  result.retainedTemporaryBytes = result.retainedTemporaryBuffers * StandardTemporaryBytes;
  std::size_t streamsWithDescriptor = 0;
  for (const auto &descriptor : snapshot.descriptors)
  {
    result.descriptorsWithoutHandle += !descriptor.HasHandle();
    streamsWithDescriptor += descriptor.streams;
  }
  for (const auto &stream : snapshot.streams)
    if (stream.flags & SlotAllocated)
    {
      ++result.activeStreams;
      // The CRT uses the caller flag for a temporary console attachment.
      if (stream.flags & TemporaryBuffer)
      {
        ++result.temporaryAttachments;
        result.temporaryBufferSpan += stream.bufferSize;
      }
      else if (stream.flags & CrtBuffer)
      {
        ++result.crtBuffers;
        result.crtBufferSpan += stream.bufferSize;
      }
      else if (stream.flags & CallerBuffer)
      {
        ++result.callerBuffers;
        result.callerBufferSpan += stream.bufferSize;
      }
    }
  result.streamsWithoutDescriptor = result.activeStreams - streamsWithDescriptor;
  return result;
}
} // namespace windows_crt_io_detail
#endif
} // namespace cutum
