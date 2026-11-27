#pragma once

#include "Core/Memory/WindowsProcessMemory.h"
#include <algorithm>

namespace cutum
{
struct WindowsAddressSpace
{
  std::uint64_t requested{}, inspected{}, free{}, reserved{};
  std::uint64_t privateCommitted{}, mappedCommitted{}, imageCommitted{}, otherCommitted{};
  std::uint64_t guard{}, noAccess{};
  std::size_t regions{};
  std::uint32_t error{};
  bool supported{}, complete{}, limitReached{};

  std::uint64_t Committed() const
  {
    return privateCommitted + mappedCommitted + imageCommitted + otherCommitted;
  }
};

#ifdef _WIN32
namespace windows_address_space_detail
{
// Count disjoint address spans in the calling process. A sequential walk
// may see concurrent changes. Region type describes the original mapping.
inline WindowsAddressSpace ReadRange(std::uintptr_t begin, std::uintptr_t end,
                                     std::size_t regionLimit = 65536) noexcept
{
  WindowsAddressSpace result;
  result.supported = true;
  if (begin >= end)
  {
    result.error = ERROR_INVALID_PARAMETER;
    return result;
  }
  result.requested = end - begin;
  auto cursor = begin;
  while (cursor < end && result.regions < regionLimit)
  {
    MEMORY_BASIC_INFORMATION region{};
    if (VirtualQuery(reinterpret_cast<const void *>(cursor), &region, sizeof(region)) != sizeof(region))
    {
      result.error = GetLastError();
      break;
    }
    const auto base = reinterpret_cast<std::uintptr_t>(region.BaseAddress);
    if (base > cursor || region.RegionSize > UINTPTR_MAX - base || base + region.RegionSize <= cursor)
    {
      result.error = ERROR_INVALID_DATA;
      break;
    }
    const auto next = std::min(end, base + region.RegionSize);
    const auto bytes = next - cursor;
    switch (region.State)
    {
    case MEM_FREE: result.free += bytes; break;
    case MEM_RESERVE: result.reserved += bytes; break;
    case MEM_COMMIT:
      switch (region.Type)
      {
      case MEM_PRIVATE: result.privateCommitted += bytes; break;
      case MEM_MAPPED: result.mappedCommitted += bytes; break;
      case MEM_IMAGE: result.imageCommitted += bytes; break;
      default: result.otherCommitted += bytes; break;
      }
      if (region.Protect & PAGE_GUARD)
        result.guard += bytes;
      if (region.Protect & PAGE_NOACCESS)
        result.noAccess += bytes;
      break;
    default:
      result.error = ERROR_INVALID_DATA;
      return result;
    }
    ++result.regions;
    result.inspected += bytes;
    cursor = next;
  }
  result.complete = cursor == end;
  result.limitReached = !result.complete && result.regions == regionLimit;
  return result;
}
} // namespace windows_address_space_detail
#endif

inline WindowsAddressSpace ReadWindowsAddressSpace() noexcept
{
#ifdef _WIN32
  SYSTEM_INFO system{};
  GetSystemInfo(&system);
  const auto last = reinterpret_cast<std::uintptr_t>(system.lpMaximumApplicationAddress);
  return windows_address_space_detail::ReadRange(
      reinterpret_cast<std::uintptr_t>(system.lpMinimumApplicationAddress), last + 1);
#else
  return {};
#endif
}
} // namespace cutum
