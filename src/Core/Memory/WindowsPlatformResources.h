#pragma once

#include "Core/Memory/WindowsCrtIoStorage.h"
#include "Core/Memory/WindowsProcessMemory.h"
#include <algorithm>
#include <array>
#include <cstring>
#include <map>
#include <optional>
#include <string>
#include <vector>

#if defined(_WIN64) && defined(_M_X64)
#include <winternl.h>
#endif

namespace cutum
{
struct WindowsHandleType
{
  std::string type;
  std::size_t entries{};
  std::optional<std::size_t> objects;
};

enum class WindowsFileKind { DiskFile, Directory, Pipe, Character, DiskUnclassified, Unknown };
inline constexpr std::array WindowsFileKindNames{"disk_file", "directory", "pipe", "character", "disk_unclassified", "unknown"};

struct WindowsFileResourceRow
{
  std::size_t entries{}, objects{}, linkedCrtEntries{}, linkedCrtObjects{};
};

struct WindowsFileResources
{
  std::array<WindowsFileResourceRow, WindowsFileKindNames.size()> kinds{};
  std::array<std::optional<WindowsFileKind>, 3> standardStreams;
  WindowsCrtIoStorage crt;
  bool stable{};
};

struct WindowsHandleTable
{
  std::vector<WindowsHandleType> types;
  WindowsFileResources files;
  std::uint32_t before{}, after{};
  std::size_t entries{}, untyped{}, comparisons{};
  bool supported{}, beforeObserved{}, afterObserved{}, stable{}, entryLimitReached{}, comparisonLimitReached{};

  bool Complete() const
  {
    return stable && !untyped && std::all_of(types.begin(), types.end(), [](const auto &type) { return type.objects.has_value(); });
  }
};

struct WindowsPlatformResources
{
  WindowsHandleTable handles;
  std::optional<std::uint32_t> gdi, peakGdi, user, peakUser;
};

#if defined(_WIN64) && defined(_M_X64)
namespace windows_platform_detail
{
constexpr std::size_t HandleLimit = 4096, ComparisonLimit = 65536;
constexpr auto ProcessHandleInformationClass = static_cast<PROCESSINFOCLASS>(51);
using CompareHandles = BOOL (WINAPI *)(HANDLE, HANDLE);
using QueryProcess = decltype(&NtQueryInformationProcess);
using QueryObject = decltype(&NtQueryObject);

// Win64 ProcessHandleInformation is a private OS layout. Verify the
// returned row extent and independently counted cohort on each sample.
struct HandleRow
{
  HANDLE handle;
  ULONG_PTR handles, pointers;
  ULONG access, type, attributes, reserved;
};
static_assert(sizeof(HandleRow) == 40);

inline bool SameEntry(const HandleRow &a, const HandleRow &b) noexcept
{
  return a.handle == b.handle && a.type == b.type && a.access == b.access && a.attributes == b.attributes;
}

inline bool ReadTable(QueryProcess query, std::vector<ULONG_PTR> &buffer, std::vector<HandleRow> &rows)
{
  constexpr auto headerBytes = 2 * sizeof(ULONG_PTR);
  ULONG bytes{};
  if (query(GetCurrentProcess(), ProcessHandleInformationClass, buffer.data(),
           static_cast<ULONG>(buffer.size() * sizeof(buffer.front())), &bytes) < 0)
    return false;
  const auto count = buffer[0];
  if (buffer[1] || count > HandleLimit || bytes != headerBytes + count * sizeof(HandleRow) ||
      bytes > buffer.size() * sizeof(buffer.front()))
    return false;
  rows.resize(count);
  if (count)
    std::memcpy(rows.data(), buffer.data() + 2, count * sizeof(HandleRow));
  const auto address = [](const auto &row) { return reinterpret_cast<std::uintptr_t>(row.handle); };
  std::sort(rows.begin(), rows.end(), [&](const auto &a, const auto &b) { return address(a) < address(b); });
  return std::all_of(rows.begin(), rows.end(), [](const auto &row) { return row.handle && row.type && !row.reserved; }) &&
      std::adjacent_find(rows.begin(), rows.end(), [](const auto &a, const auto &b) { return a.handle == b.handle; }) == rows.end();
}

inline std::string ReadType(QueryObject query, HANDLE handle)
{
  alignas(void *) std::array<std::byte, 512> buffer;
  ULONG bytes{};
  if (query(handle, ObjectTypeInformation, buffer.data(), static_cast<ULONG>(buffer.size()), &bytes) < 0 ||
      bytes < sizeof(PUBLIC_OBJECT_TYPE_INFORMATION) || bytes > buffer.size())
    return {};
  PUBLIC_OBJECT_TYPE_INFORMATION type{};
  std::memcpy(&type, buffer.data(), sizeof(type));
  const auto length = type.TypeName.Length;
  const auto name = reinterpret_cast<std::uintptr_t>(type.TypeName.Buffer);
  const auto base = reinterpret_cast<std::uintptr_t>(buffer.data());
  if (!length || length % sizeof(wchar_t) || length > type.TypeName.MaximumLength ||
      name < base + sizeof(type) || name % alignof(wchar_t) || name - base > bytes || length > bytes - (name - base))
    return {};
  std::string result;
  for (std::size_t i = 0; i < length / sizeof(wchar_t); ++i)
  {
    wchar_t character{};
    std::memcpy(&character, buffer.data() + (name - base) + i * sizeof(character), sizeof(character));
    if (character < L' ' || character > L'~')
      return {};
    result.push_back(static_cast<char>(character));
  }
  return result;
}

struct HandleObjects
{
  std::vector<HANDLE> representatives;
  std::vector<std::size_t> objectForEntry;
};

inline std::optional<HandleObjects> GroupObjects(const std::vector<HANDLE> &handles, CompareHandles compare,
                                                WindowsHandleTable &result)
{
  HandleObjects groups;
  groups.representatives.reserve(handles.size());
  groups.objectForEntry.reserve(handles.size());
  for (const auto handle : handles)
  {
    std::size_t index = 0;
    for (; index < groups.representatives.size(); ++index)
    {
      if (result.comparisons == ComparisonLimit)
      {
        result.comparisonLimitReached = true;
        return std::nullopt;
      }
      ++result.comparisons;
      if (compare(handle, groups.representatives[index]))
        break;
      if (GetLastError() != ERROR_NOT_SAME_OBJECT)
        return std::nullopt;
    }
    if (index == groups.representatives.size())
      groups.representatives.push_back(handle);
    groups.objectForEntry.push_back(index);
  }
  return groups;
}

inline WindowsFileKind ReadFileKind(HANDLE handle) noexcept
{
  switch (GetFileType(handle) & ~FILE_TYPE_REMOTE)
  {
    case FILE_TYPE_PIPE: return WindowsFileKind::Pipe;
    case FILE_TYPE_CHAR: return WindowsFileKind::Character;
    case FILE_TYPE_DISK:
    {
      FILE_STANDARD_INFO info{};
      if (!GetFileInformationByHandleEx(handle, FileStandardInfo, &info, sizeof(info)))
        return WindowsFileKind::DiskUnclassified;
      return info.Directory ? WindowsFileKind::Directory : WindowsFileKind::DiskFile;
    }
    default: return WindowsFileKind::Unknown;
  }
}

inline std::vector<WindowsFileKind> ReadFileKinds(const HandleObjects &objects, WindowsFileResources &result)
{
  std::vector<WindowsFileKind> kinds;
  kinds.reserve(objects.representatives.size());
  for (auto handle : objects.representatives)
  {
    const auto kind = ReadFileKind(handle);
    kinds.push_back(kind);
    ++result.kinds[static_cast<std::size_t>(kind)].objects;
  }
  for (auto object : objects.objectForEntry)
    ++result.kinds[static_cast<std::size_t>(kinds[object])].entries;
  result.stable = true;
  return kinds;
}

#if CUTUM_WINDOWS_STATIC_THREAD_CRT_PROFILE
inline void LinkCrtFiles(const windows_crt_io_detail::Snapshot &snapshot, const std::vector<HANDLE> &handles,
                          const HandleObjects &objects, const std::vector<WindowsFileKind> &kinds,
                          WindowsFileResources &result)
{
  result.crt = windows_crt_io_detail::Summarize(snapshot);
  std::vector<bool> linkedEntries(handles.size()), linkedObjects(kinds.size());
  for (const auto &descriptor : snapshot.descriptors)
  {
    if (!descriptor.HasHandle())
      continue;
    const auto address = static_cast<std::uintptr_t>(descriptor.handle);
    const auto entry = std::lower_bound(handles.begin(), handles.end(), address, [](HANDLE current, auto value) {
      return reinterpret_cast<std::uintptr_t>(current) < value;
    });
    if (entry == handles.end() || reinterpret_cast<std::uintptr_t>(*entry) != address)
      continue;
    const auto index = static_cast<std::size_t>(entry - handles.begin());
    const auto object = objects.objectForEntry[index];
    // freopen can bind a standard FILE slot to a different lowio index.
    for (std::size_t i = 0; i < result.standardStreams.size(); ++i)
      if ((snapshot.streams[i].flags & windows_crt_io_detail::SlotAllocated) &&
          snapshot.streams[i].descriptor == static_cast<int>(descriptor.index))
        result.standardStreams[i] = kinds[object];
    auto &kind = result.kinds[static_cast<std::size_t>(kinds[object])];
    kind.linkedCrtEntries += !linkedEntries[index];
    kind.linkedCrtObjects += !linkedObjects[object];
    linkedEntries[index] = true;
    linkedObjects[object] = true;
    ++result.crt.linkedDescriptors;
    result.crt.linkedStreams += descriptor.streams;
  }
}
#endif

inline WindowsHandleTable ReadHandles()
{
  WindowsHandleTable result;
  const auto ntdll = GetModuleHandleW(L"ntdll.dll");
  const auto kernel = GetModuleHandleW(L"KernelBase.dll");
  if (!ntdll || !kernel)
    return result;
  const auto query = reinterpret_cast<QueryProcess>(GetProcAddress(ntdll, "NtQueryInformationProcess"));
  const auto object = reinterpret_cast<QueryObject>(GetProcAddress(ntdll, "NtQueryObject"));
  const auto compare = reinterpret_cast<CompareHandles>(GetProcAddress(kernel, "CompareObjectHandles"));
  if (!query || !object || !compare)
    return result;
  result.supported = true;
  const auto self = GetCurrentProcess();
  DWORD countBefore{};
  result.beforeObserved = GetProcessHandleCount(self, &countBefore) != 0;
  result.before = countBefore;
  if (!result.beforeObserved || (result.entryLimitReached = countBefore > HandleLimit))
    return result;
  const auto capacity = std::min(HandleLimit, static_cast<std::size_t>(countBefore) + 32);
  std::vector<ULONG_PTR> buffer(2 + capacity * (sizeof(HandleRow) / sizeof(ULONG_PTR)));
  std::vector<HandleRow> before, after;
#if CUTUM_WINDOWS_STATIC_THREAD_CRT_PROFILE
  const auto crtBefore = windows_crt_io_detail::ReadSnapshot();
#endif
  if (!ReadTable(query, buffer, before) || before.size() != countBefore)
    return result;

  std::map<std::string, std::vector<HANDLE>> groups;
  std::size_t untyped{};
  for (const auto &row : before)
  {
    auto type = ReadType(object, row.handle);
    if (type.empty())
      ++untyped;
    else
      groups[std::move(type)].push_back(row.handle);
  }
  std::vector<WindowsHandleType> types;
  std::optional<HandleObjects> fileObjects{HandleObjects{}};
  const std::vector<HANDLE> empty;
  const std::vector<HANDLE> *fileHandles = &empty;
  for (const auto &group : groups)
  {
    auto objects = GroupObjects(group.second, compare, result);
    types.push_back({group.first, group.second.size(), objects ? std::optional<std::size_t>(objects->representatives.size()) :
                                                                 std::nullopt});
    if (group.first == "File")
    {
      fileHandles = &group.second;
      fileObjects = std::move(objects);
    }
  }
  WindowsFileResources files;
  auto kinds = fileObjects ? ReadFileKinds(*fileObjects, files) : std::vector<WindowsFileKind>{};

  const bool afterCaptured = ReadTable(query, buffer, after);
  DWORD countAfter{};
  result.afterObserved = GetProcessHandleCount(self, &countAfter) != 0;
  result.after = countAfter;
  result.stable = afterCaptured && result.afterObserved && countBefore == countAfter &&
      before.size() == after.size() && std::equal(before.begin(), before.end(), after.begin(), SameEntry);
  if (result.stable)
  {
    result.entries = before.size();
    result.untyped = untyped;
    result.types = std::move(types);
#if CUTUM_WINDOWS_STATIC_THREAD_CRT_PROFILE
    const auto crtAfter = windows_crt_io_detail::ReadSnapshot();
    if (fileObjects && !untyped && crtBefore && crtAfter && *crtBefore == *crtAfter)
      LinkCrtFiles(*crtAfter, *fileHandles, *fileObjects, kinds, files);
#endif
    result.files = std::move(files);
  }
  return result;
}
} // namespace windows_platform_detail
#endif

inline WindowsPlatformResources ObserveWindowsPlatformResources()
{
  WindowsPlatformResources result;
#ifdef _WIN32
  const auto gui = [](DWORD kind) -> std::optional<std::uint32_t> {
    SetLastError(ERROR_SUCCESS);
    const auto count = GetGuiResources(GetCurrentProcess(), kind);
    if (!count && GetLastError() != ERROR_SUCCESS)
      return std::nullopt;
    return count;
  };
  result.gdi = gui(GR_GDIOBJECTS);
  result.peakGdi = gui(GR_GDIOBJECTS_PEAK);
  result.user = gui(GR_USEROBJECTS);
  result.peakUser = gui(GR_USEROBJECTS_PEAK);
#endif
#if defined(_WIN64) && defined(_M_X64)
  result.handles = windows_platform_detail::ReadHandles();
#endif
  return result;
}
} // namespace cutum
