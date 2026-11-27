#pragma once

#include "App/Platform/WindowsGpuMemoryReport.h"
#include "Core/Memory/WindowsTlsVectorHistory.h"
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
  std::optional<WindowsModuleTlsStorage> moduleTls;
  std::optional<WindowsReplacedTlsVectors> replacedTlsVectors;
  std::uint64_t elapsedMicros{};
  std::uint32_t processId{};
};

enum class WindowsMemoryDetail { Summary, ModuleImages };

inline WindowsMemoryCheckpoint ReadWindowsMemoryCheckpoint(WindowsMemoryDetail detail = WindowsMemoryDetail::Summary,
                                                            WindowsTlsVectorHistory *history = nullptr)
{
  WindowsMemoryCheckpoint result;
#ifdef _WIN32
  result.processId = GetCurrentProcessId();
  const auto begin = std::chrono::steady_clock::now();
  result.before = ReadWindowsProcessMemory(/*includeShared=*/true);
  if (detail == WindowsMemoryDetail::ModuleImages)
  {
    result.images = ReadWindowsImageMemory(result.addressSpace);
    result.moduleTls = ObserveWindowsModuleTls(*result.images);
    if (history)
      result.replacedTlsVectors = history->Observe(*result.moduleTls);
  }
  else
    result.addressSpace = ReadWindowsAddressSpace();
  result.driver = ReadWindowsGpuMemory();
  result.after = ReadWindowsProcessMemory(/*includeShared=*/true);
  result.elapsedMicros = std::chrono::duration_cast<std::chrono::microseconds>(
      std::chrono::steady_clock::now() - begin).count();
#else
  (void)history;
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

inline nlohmann::json DescribeWindowsModuleTls(const WindowsModuleTlsStorage &tls)
{
  using Json = nlohmann::json;
  const auto storage = [](const WindowsTlsBlockStorage &row) {
    return Json{{"blocks", row.blocks}, {"data_bytes", row.dataBytes}, {"heap_overhead_bytes", row.overheadBytes}};
  };
  auto modules = Json::array();
  for (const auto &module : tls.modules)
    modules.push_back({{"filename", module.filename}, {"template_bytes", module.templateBytes}, {"storage", storage(module.storage)}});
  auto threads = Json::array();
  constexpr std::array threadStates{"UNOBSERVED", "CHANGED", "VECTOR_NOT_IN_HEAP", "VECTOR_TOO_SMALL", "NO_VECTOR", "OBSERVED"};
  constexpr std::array expansionStates{"UNOBSERVED", "ABSENT", "OBSERVED"};
  for (const auto &thread : tls.threads)
    threads.push_back({{"thread_id", thread.id}, {"engine_owner", thread.kind < NativeThreadNames.size() ?
            Json(NativeThreadNames[thread.kind]) : Json(nullptr)}, {"stable", thread.Stable()},
        {"state", threadStates[static_cast<std::size_t>(thread.state)]},
        {"dynamic_expansion", {{"state", expansionStates[static_cast<std::size_t>(thread.dynamicState)]},
                               {"storage", storage(thread.dynamicExpansion)}}},
        {"vector", storage(thread.vector)}, {"module_blocks", storage(thread.blocks)}});
  return {{"schema", "windows-module-tls-storage.v1"},
      {"status", tls.complete ? "PASS" : tls.supported ? "PARTIAL" : "UNSUPPORTED"},
      {"teb_verified", tls.tebVerified}, {"module_list_stable", tls.moduleListStable},
      {"thread_list_stable", tls.threadListStable}, {"heap_walk_finished", tls.heapWalkFinished},
      {"process_threads", tls.processThreads}, {"stable_threads", tls.stableThreads},
      {"threads_with_vector", tls.threadsWithVector}, {"threads_without_vector", tls.threadsWithoutVector},
      {"modules", std::move(modules)}, {"threads", std::move(threads)}};
}

inline nlohmann::json DescribeWindowsReplacedTlsVectors(const WindowsReplacedTlsVectors &row)
{
  return {{"schema", "windows-replaced-tls-vectors.v1"},
      {"status", row.sampled && !row.unconfirmed && !row.limitReached ? "PASS" : "PARTIAL"},
      {"sampled", row.sampled}, {"limit_reached", row.limitReached}, {"tracked_vectors", row.trackedVectors},
      {"observed_replacements", row.observedReplacements}, {"released", row.released}, {"unconfirmed", row.unconfirmed},
      {"retained", {{"blocks", row.retained.blocks}, {"data_bytes", row.retained.dataBytes},
                    {"heap_overhead_bytes", row.retained.overheadBytes}}}};
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
  if (checkpoint.moduleTls)
    report["module_tls"] = DescribeWindowsModuleTls(*checkpoint.moduleTls);
  if (checkpoint.replacedTlsVectors)
    report["replaced_tls_vectors"] = DescribeWindowsReplacedTlsVectors(*checkpoint.replacedTlsVectors);
  return report;
}
} // namespace cutum
