#pragma once

#include <cstddef>
#include <cstdint>
#ifdef _WIN32
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#include <psapi.h>
#endif

namespace cutum
{
struct WindowsProcessMemory
{
  std::size_t workingSet{}, peakWorkingSet{}, privateCommit{}, peakPrivateCommit{};
  std::size_t pagedPoolQuota{}, peakPagedPoolQuota{}, nonpagedPoolQuota{}, peakNonpagedPoolQuota{};
  std::size_t privateWorkingSet{};
  std::uint64_t sharedCommit{};
  bool observed{}, extendedObserved{};
};

// PSAPI reports memory and pool quota for the current process. Pool values
// describe the charge attributed to the process by Windows.
inline WindowsProcessMemory ReadWindowsProcessMemory(bool includeShared = false) noexcept
{
  WindowsProcessMemory result;
#ifdef _WIN32
  const auto self = GetCurrentProcess();
  PROCESS_MEMORY_COUNTERS_EX counters{};
#ifdef _WIN64
  if (includeShared)
  {
    // The documented 64-bit EX2 layout extends EX with two counters. Keep
    // the request available with SDK headers that omit the EX2 typedef.
    struct ExtendedCounters
    {
      PROCESS_MEMORY_COUNTERS_EX base;
      SIZE_T privateWorkingSet;
      ULONG64 sharedCommit;
    } extended{{}, SIZE_MAX, UINT64_MAX};
    static_assert(sizeof(ExtendedCounters) == 96);
    extended.base.cb = sizeof(extended);
    result.extendedObserved = GetProcessMemoryInfo(self,
        reinterpret_cast<PROCESS_MEMORY_COUNTERS *>(&extended), sizeof(extended)) != 0 &&
        extended.base.cb == sizeof(extended) && extended.privateWorkingSet != SIZE_MAX &&
        extended.sharedCommit != UINT64_MAX;
    if (result.extendedObserved)
    {
      counters = extended.base;
      result.privateWorkingSet = extended.privateWorkingSet;
      result.sharedCommit = extended.sharedCommit;
    }
  }
#else
  (void)includeShared;
#endif
  if (result.extendedObserved)
    result.observed = true;
  else
  {
    counters.cb = sizeof(counters);
    result.observed = GetProcessMemoryInfo(self,
        reinterpret_cast<PROCESS_MEMORY_COUNTERS *>(&counters), sizeof(counters)) != 0;
  }
  if (result.observed)
  {
    result.workingSet = counters.WorkingSetSize;
    result.peakWorkingSet = counters.PeakWorkingSetSize;
    result.privateCommit = counters.PrivateUsage;
    result.peakPrivateCommit = counters.PeakPagefileUsage;
    result.pagedPoolQuota = counters.QuotaPagedPoolUsage;
    result.peakPagedPoolQuota = counters.QuotaPeakPagedPoolUsage;
    result.nonpagedPoolQuota = counters.QuotaNonPagedPoolUsage;
    result.peakNonpagedPoolQuota = counters.QuotaPeakNonPagedPoolUsage;
  }
#else
  (void)includeShared;
#endif
  return result;
}
} // namespace cutum
