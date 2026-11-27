#pragma once

#include "App/Platform/WindowsGpuMemoryReport.h"
#include "Core/Memory/WindowsAddressSpace.h"
#include <chrono>

namespace cutum
{
struct WindowsMemoryCheckpoint
{
  WindowsProcessMemory before, after;
  WindowsAddressSpace addressSpace;
  WindowsGpuMemory driver;
  std::uint64_t elapsedMicros{};
  std::uint32_t processId{};
};

inline WindowsMemoryCheckpoint ReadWindowsMemoryCheckpoint()
{
  WindowsMemoryCheckpoint result;
#ifdef _WIN32
  result.processId = GetCurrentProcessId();
  const auto begin = std::chrono::steady_clock::now();
  result.before = ReadWindowsProcessMemory(/*includeShared=*/true);
  result.addressSpace = ReadWindowsAddressSpace();
  result.driver = ReadWindowsGpuMemory();
  result.after = ReadWindowsProcessMemory(/*includeShared=*/true);
  result.elapsedMicros = std::chrono::duration_cast<std::chrono::microseconds>(
      std::chrono::steady_clock::now() - begin).count();
#endif
  return result;
}

inline nlohmann::json DescribeWindowsAddressSpace(const WindowsAddressSpace &space)
{
  return {{"status", space.complete ? "PASS" : space.supported ? "PARTIAL" : "UNSUPPORTED"},
      {"range_bytes", space.requested}, {"inspected_bytes", space.inspected}, {"regions", space.regions},
      {"region_limit_reached", space.limitReached}, {"win32_error", space.error},
      {"free_bytes", space.free}, {"reserved_bytes", space.reserved}, {"committed_bytes", space.Committed()},
      {"private_committed_bytes", space.privateCommitted}, {"mapped_committed_bytes", space.mappedCommitted},
      {"image_committed_bytes", space.imageCommitted}, {"other_committed_bytes", space.otherCommitted},
      {"guard_bytes_within_committed", space.guard}, {"noaccess_bytes_within_committed", space.noAccess}};
}

inline nlohmann::json DescribeWindowsProcessCommit(const WindowsProcessMemory &memory)
{
  using Json = nlohmann::json;
  return {{"observed", memory.observed}, {"extended_observed", memory.extendedObserved},
      {"working_set_bytes", memory.workingSet}, {"peak_working_set_bytes", memory.peakWorkingSet},
      {"private_commit_bytes", memory.privateCommit}, {"peak_private_commit_bytes", memory.peakPrivateCommit},
      {"private_working_set_bytes", memory.extendedObserved ? Json(memory.privateWorkingSet) : Json(nullptr)},
      {"shared_commit_bytes", memory.extendedObserved ? Json(memory.sharedCommit) : Json(nullptr)}};
}

inline nlohmann::json DescribeWindowsMemoryCheckpoint(const WindowsMemoryCheckpoint &checkpoint)
{
  const bool complete = checkpoint.before.extendedObserved && checkpoint.after.extendedObserved &&
      checkpoint.addressSpace.complete && checkpoint.driver.complete;
  return {{"schema", "windows-memory-checkpoint.v1"},
      {"status", complete ? "PASS" : checkpoint.processId ? "PARTIAL" : "UNSUPPORTED"},
      {"process_id", checkpoint.processId}, {"elapsed_us", checkpoint.elapsedMicros},
      {"before", DescribeWindowsProcessCommit(checkpoint.before)},
      {"address_space", DescribeWindowsAddressSpace(checkpoint.addressSpace)},
      {"after", DescribeWindowsProcessCommit(checkpoint.after)},
      {"driver_complete", checkpoint.driver.complete}, {"whole_runtime_complete", false}};
}
} // namespace cutum
