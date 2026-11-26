#pragma once

#include <cstddef>
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
  bool observed{};
};

// PSAPI reports memory and pool quota for the current process. Pool values
// describe the charge attributed to the process by Windows.
inline WindowsProcessMemory ReadWindowsProcessMemory() noexcept
{
  WindowsProcessMemory result;
#ifdef _WIN32
  PROCESS_MEMORY_COUNTERS_EX counters{};
  counters.cb = sizeof(counters);
  result.observed = GetProcessMemoryInfo(GetCurrentProcess(),
      reinterpret_cast<PROCESS_MEMORY_COUNTERS *>(&counters), sizeof(counters)) != 0;
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
#endif
  return result;
}
} // namespace cutum
