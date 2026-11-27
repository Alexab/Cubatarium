#pragma once

#include "App/Platform/WindowsGpuMemoryReport.h"
#include "Core/Memory/WindowsPlatformResources.h"
#include "Core/Memory/WindowsSlotStorage.h"
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
  std::optional<WindowsSlotStorage> slotStorage;
  std::optional<WindowsPlatformResources> platformResources;
  std::uint64_t elapsedMicros{};
  std::uint32_t processId{};
  std::uint32_t tlsSamplingAttempts{};
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
    // An exiting worker can change the FLS ring between matched checkpoint
    // reads. Resample the pair with the same thread IDs before recording it.
    constexpr std::uint32_t attempts = 3;
    for (std::uint32_t attempt = 1; attempt <= attempts; ++attempt)
    {
      result.tlsSamplingAttempts = attempt;
      result.moduleTls = ObserveWindowsModuleTls(*result.images);
      result.slotStorage = ObserveWindowsSlots(&*result.moduleTls);
      if (result.slotStorage->stable || !result.slotStorage->supported)
        break;
      if (attempt < attempts)
        SwitchToThread();
    }
    if (history)
      result.replacedTlsVectors = history->Observe(*result.moduleTls);
    result.platformResources = ObserveWindowsPlatformResources();
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
      {"paged_pool_quota_bytes", memory.observed ? Json(memory.pagedPoolQuota) : Json(nullptr)},
      {"peak_paged_pool_quota_bytes", memory.observed ? Json(memory.peakPagedPoolQuota) : Json(nullptr)},
      {"nonpaged_pool_quota_bytes", memory.observed ? Json(memory.nonpagedPoolQuota) : Json(nullptr)},
      {"peak_nonpaged_pool_quota_bytes", memory.observed ? Json(memory.peakNonpagedPoolQuota) : Json(nullptr)},
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

inline nlohmann::json DescribeWindowsSlotStorage(const WindowsSlotStorage &row)
{
  using Json = nlohmann::json;
  const auto storage = [](const WindowsTlsBlockStorage &value) {
    return Json{{"blocks", value.blocks}, {"data_bytes", value.dataBytes}, {"heap_overhead_bytes", value.overheadBytes}};
  };
  const auto references = [](const WindowsSlotReferences &value) {
    return Json{{"nonzero_values", value.values}, {"default_heap_references", value.inDefaultHeap},
                {"unmatched_values", value.values - value.inDefaultHeap}};
  };
  return {{"schema", "windows-slot-storage.v1"},
      {"status", row.stable ? "PASS" : row.supported ? "PARTIAL" : "UNSUPPORTED"},
      {"fls_list_observed", row.flsListObserved}, {"heap_walk_finished", row.heapWalkFinished},
      {"process_threads", row.processThreads}, {"active_fls_roots", row.activeFlsRoots},
      {"detached_fls_roots", row.detachedFlsRoots}, {"payload_aliases", row.payloadAliases},
      {"metadata_aliases", row.metadataAliases}, {"inline_tls", references(row.inlineTls)},
      {"expanded_tls", references(row.expandedTls)}, {"fls", references(row.fls)},
      {"expansion_arrays", storage(row.expansionArrays)}, {"fls_headers", storage(row.flsHeaders)},
      {"fls_chunks", storage(row.flsChunks)}, {"distinct_default_heap_payload", storage(row.distinctPayload)}};
}

inline nlohmann::json DescribeWindowsPlatformResources(const WindowsPlatformResources &row)
{
  using Json = nlohmann::json;
  const auto number = [](const auto &value) { return value ? Json(*value) : Json(nullptr); };
  const auto &handles = row.handles;
  const auto &files = handles.files;
  const auto &crt = files.crt;
  auto types = Json::object();
  for (const auto &type : handles.types)
    types[type.type] = {{"entries", type.entries}, {"objects", number(type.objects)}};
  auto kinds = Json::object();
  if (files.stable)
    for (std::size_t i = 0; i < files.kinds.size(); ++i)
    {
      const auto &kind = files.kinds[i];
      kinds[WindowsFileKindNames[i]] = {{"entries", kind.entries}, {"objects", kind.objects},
          {"linked_ucrt_entries", crt.stable ? Json(kind.linkedCrtEntries) : Json(nullptr)},
          {"linked_ucrt_objects", crt.stable ? Json(kind.linkedCrtObjects) : Json(nullptr)}};
    }
  auto standardStreams = Json::object();
  constexpr std::array standardNames{"stdin", "stdout", "stderr"};
  for (std::size_t i = 0; i < standardNames.size(); ++i)
  {
    const auto kind = files.standardStreams[i];
    standardStreams[standardNames[i]] = kind ? Json(WindowsFileKindNames[static_cast<std::size_t>(*kind)]) : Json(nullptr);
  }
  return {{"schema", "windows-platform-resources.v1"},
      {"executive_handles", {
          {"status", handles.Complete() ? "PASS" : handles.supported ? "PARTIAL" : "UNSUPPORTED"},
          {"stable", handles.stable}, {"before", handles.beforeObserved ? Json(handles.before) : Json(nullptr)},
          {"after", handles.afterObserved ? Json(handles.after) : Json(nullptr)},
          {"entries", handles.stable ? Json(handles.entries) : Json(nullptr)},
          {"untyped_entries", handles.stable ? Json(handles.untyped) : Json(nullptr)}, {"types", std::move(types)},
          {"object_comparisons", handles.comparisons}, {"entry_limit_reached", handles.entryLimitReached},
          {"comparison_limit_reached", handles.comparisonLimitReached}}},
      {"file_resources", {
          {"status", files.stable && !handles.untyped ? "PASS" : handles.supported ? "PARTIAL" : "UNSUPPORTED"},
          {"kinds", std::move(kinds)}, {"standard_streams", std::move(standardStreams)},
          {"ucrt", {{"status", crt.stable ? "PASS" : crt.supported ? "PARTIAL" : "UNSUPPORTED"},
                    {"stdio_slots", crt.stdioSlots}, {"allocated_dynamic_slots", crt.dynamicSlots},
                    {"active_streams", crt.activeStreams}, {"streams_without_open_descriptor", crt.streamsWithoutDescriptor},
                    {"lowio_blocks", crt.lowioBlocks}, {"open_descriptors", crt.openDescriptors},
                    {"descriptors_without_os_handle", crt.descriptorsWithoutHandle},
                    {"linked_file_descriptors", crt.linkedDescriptors}, {"linked_file_streams", crt.linkedStreams},
                    {"pointer_table_requested_bytes", crt.pointerTableBytes}, {"dynamic_slots_requested_bytes", crt.dynamicSlotBytes},
                    {"descriptor_blocks_requested_bytes", crt.descriptorBlockBytes},
                    {"crt_buffer_count", crt.crtBuffers}, {"crt_configured_buffer_bytes", crt.crtBufferSpan},
                    {"caller_buffer_count", crt.callerBuffers}, {"caller_configured_buffer_bytes", crt.callerBufferSpan},
                    {"temporary_standard_attachments", crt.temporaryAttachments},
                    {"temporary_standard_configured_bytes", crt.temporaryBufferSpan},
                    {"retained_standard_temporary_buffers", crt.retainedTemporaryBuffers},
                    {"retained_standard_temporary_requested_bytes", crt.retainedTemporaryBytes}}}}},
      {"gui_objects", {{"gdi", number(row.gdi)}, {"peak_gdi", number(row.peakGdi)},
                       {"user", number(row.user)}, {"peak_user", number(row.peakUser)}}}};
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
  if (checkpoint.slotStorage)
  {
    report["slot_storage"] = DescribeWindowsSlotStorage(*checkpoint.slotStorage);
    report["tls_sampling_attempts"] = checkpoint.tlsSamplingAttempts;
  }
  if (checkpoint.platformResources)
    report["platform_resources"] = DescribeWindowsPlatformResources(*checkpoint.platformResources);
  return report;
}
} // namespace cutum
