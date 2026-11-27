#pragma once

#include "App/Platform/WindowsGpuMemoryReport.h"
#include "Core/Memory/WindowsImageMemory.h"
#include <chrono>
#include <optional>

namespace cutum
{
struct WindowsMemoryCheckpoint
{
  WindowsProcessMemory before, after;
  WindowsAddressSpace addressSpace;
  WindowsGpuMemory driver;
  std::optional<WindowsImageMemory> images;
  std::uint64_t elapsedMicros{};
  std::uint32_t processId{};
};

enum class WindowsMemoryDetail { Summary, ModuleImages };

inline WindowsMemoryCheckpoint ReadWindowsMemoryCheckpoint(WindowsMemoryDetail detail = WindowsMemoryDetail::Summary)
{
  WindowsMemoryCheckpoint result;
#ifdef _WIN32
  result.processId = GetCurrentProcessId();
  const auto begin = std::chrono::steady_clock::now();
  result.before = ReadWindowsProcessMemory(/*includeShared=*/true);
  if (detail == WindowsMemoryDetail::ModuleImages)
    result.images = ReadWindowsImageMemory(result.addressSpace);
  else
    result.addressSpace = ReadWindowsAddressSpace();
  result.driver = ReadWindowsGpuMemory();
  result.after = ReadWindowsProcessMemory(/*includeShared=*/true);
  result.elapsedMicros = std::chrono::duration_cast<std::chrono::microseconds>(
      std::chrono::steady_clock::now() - begin).count();
#else
  if (detail == WindowsMemoryDetail::ModuleImages)
    result.images.emplace();
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

inline nlohmann::json DescribeWindowsImageMemory(const WindowsImageMemory &memory)
{
  using Json = nlohmann::json;
  auto images = Json::array();
  for (const auto &row : memory.images)
  {
    const bool declared = row.tls == WindowsImageTlsState::Declared;
    images.push_back({{"filename", row.filename}, {"loader_module", row.loaderModule}, {"executable", row.executable},
        {"committed_bytes", row.committed}, {"queried_bytes", row.queried},
        {"resident_private_bytes", row.residentPrivate}, {"resident_shareable_bytes", row.residentShareable},
        {"tls", {{"status", declared ? "DECLARED" : row.tls == WindowsImageTlsState::Absent ? "ABSENT" : "UNOBSERVED"},
                 {"raw_template_bytes", declared ? Json(row.tlsRaw) : Json(nullptr)},
                 {"zero_fill_bytes", declared ? Json(row.tlsZeroFill) : Json(nullptr)}}}});
  }
  return {{"schema", "windows-image-memory.v1"},
      {"status", memory.complete ? "PASS" : memory.supported ? "PARTIAL" : "UNSUPPORTED"},
      {"page_bytes", memory.pageBytes}, {"page_limit_reached", memory.pageLimitReached},
      {"loader_modules", memory.loaderModules}, {"loader_without_mapping", memory.loaderWithoutMapping},
      {"stable_loader_list", memory.stableLoaderList}, {"tls_headers_complete", memory.tlsHeadersComplete},
      {"unattributed_committed_bytes", memory.unattributedCommitted}, {"images", std::move(images)}};
}

inline nlohmann::json DescribeWindowsMemoryCheckpoint(const WindowsMemoryCheckpoint &checkpoint)
{
  const bool complete = checkpoint.before.extendedObserved && checkpoint.after.extendedObserved &&
      checkpoint.addressSpace.complete && checkpoint.driver.complete;
  nlohmann::json report{{"schema", "windows-memory-checkpoint.v1"},
      {"status", complete ? "PASS" : checkpoint.processId ? "PARTIAL" : "UNSUPPORTED"},
      {"process_id", checkpoint.processId}, {"elapsed_us", checkpoint.elapsedMicros},
      {"before", DescribeWindowsProcessCommit(checkpoint.before)},
      {"address_space", DescribeWindowsAddressSpace(checkpoint.addressSpace)},
      {"after", DescribeWindowsProcessCommit(checkpoint.after)},
      {"driver_complete", checkpoint.driver.complete}, {"whole_runtime_complete", false}};
  if (checkpoint.images)
    report["module_images"] = DescribeWindowsImageMemory(*checkpoint.images);
  return report;
}
} // namespace cutum
