#pragma once

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

struct WindowsHandleTable
{
  std::vector<WindowsHandleType> types;
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

inline std::optional<std::size_t> CountObjects(const std::vector<HANDLE> &handles, CompareHandles compare,
                                               WindowsHandleTable &result)
{
  std::vector<HANDLE> representatives;
  representatives.reserve(handles.size());
  for (const auto handle : handles)
  {
    bool matched = false;
    for (const auto known : representatives)
    {
      if (result.comparisons == ComparisonLimit)
      {
        result.comparisonLimitReached = true;
        return std::nullopt;
      }
      ++result.comparisons;
      if (compare(handle, known))
      {
        matched = true;
        break;
      }
      if (GetLastError() != ERROR_NOT_SAME_OBJECT)
        return std::nullopt;
    }
    if (!matched)
      representatives.push_back(handle);
  }
  return representatives.size();
}

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
  for (const auto &group : groups)
    types.push_back({group.first, group.second.size(), CountObjects(group.second, compare, result)});

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
