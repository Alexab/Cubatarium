#include "Core/Jobs/JobThreadPool.h"
#include "Core/Memory/WindowsCrtFileTables.h"
#include "Test/AsyncIoStorageFixture.h"
#include "Test/IoCodecAllocationFixture.h"
#include "Test/NativeThreadStackFixture.h"
#include "Test/WindowsAddressSpaceFixture.h"
#include "Test/WindowsImageMemoryFixture.h"
#include "Test/WindowsModuleTlsFixture.h"
#include "Test/WindowsSlotStorageFixture.h"
#include "Test/WindowsTlsVectorHistoryFixture.h"
#include "Test/WindowsCrtThreadFixture.h"
#include "Test/WindowsCrtLocaleFixture.h"
#include "Test/WindowsExecutableTlsFixture.h"
#include "Test/WindowsIoResourceFixture.h"
#include <algorithm>
#include <array>
#include <chrono>
#include <fstream>
#include <iostream>
#include <memory>
#include <nlohmann/json.hpp>
#include <vector>
#ifdef _WIN32
#include <io.h>
#endif

namespace
{
nlohmann::json CheckCrtFileTables(const std::filesystem::path &path)
{
#if defined(_WIN32) && defined(_MSC_VER) && defined(_M_X64)
  using cutum::BoundWindowsCrtFileTables;
  using cutum::InspectWindowsCrtFileTables;
  using cutum::io_codec_fixture::Require;
  const auto before = InspectWindowsCrtFileTables();
  if (!before.supported)
    return {{"supported", false}};

  constexpr int standard_streams = 3;
  constexpr int max_descriptors = 8192;
  static constexpr size_t opened_per_wave = 70;
  const auto minimum = BoundWindowsCrtFileTables(standard_streams);
  const auto maximum = BoundWindowsCrtFileTables(max_descriptors);
  Require(!BoundWindowsCrtFileTables(standard_streams - 1).supported &&
              !BoundWindowsCrtFileTables(max_descriptors + 1).supported &&
              minimum.supported && !minimum.dynamicFileSlotsUpper &&
              maximum.supported && maximum.descriptorBlocks * maximum.descriptorsPerBlock == max_descriptors &&
              maximum.requestedHeapUpper == maximum.currentRequestedHeapUpper + maximum.streamPointerTableBytes &&
              before.requestedHeapUpper == maximum.requestedHeapUpper &&
              before.streamSlots >= standard_streams + opened_per_wave,
          "CRT table bounds cover the valid limits and overlapping stdio pointer tables");

  const auto beforeWrite = cutum::ObserveWindowsPlatformResources();
  if (beforeWrite.handles.files.crt.supported)
    Require(std::fputs("UCRT stderr buffer fixture\n", stderr) >= 0 && std::fflush(stderr) == 0,
            "exercise the CRT standard stream temporary buffering path");
  const auto baseline = cutum::ObserveWindowsPlatformResources();
  const auto &baseFiles = baseline.handles.files;
  if (baseFiles.crt.supported)
  {
    Require(baseFiles.stable && baseFiles.crt.stable, "observe the linked CRT and native File roots before opening readers");
    Require(baseFiles.crt.retainedTemporaryBuffers > 0 &&
        baseFiles.crt.retainedTemporaryBuffers >= beforeWrite.handles.files.crt.retainedTemporaryBuffers &&
        baseFiles.crt.retainedTemporaryBytes == baseFiles.crt.retainedTemporaryBuffers * 4096 &&
        !baseFiles.crt.temporaryAttachments &&
        baseFiles.crt.callerBufferSpan == beforeWrite.handles.files.crt.callerBufferSpan,
        "preserve the temporary standard stream backing after the attachment retires");
  }
  using File = std::unique_ptr<FILE, decltype(&std::fclose)>;
  struct Wave
  {
    std::array<uintptr_t, opened_per_wave> streams{};
    std::array<int, opened_per_wave> descriptors{};
    size_t process_handles{};
    size_t allocated_slots{}, lowio_blocks{};
    nlohmann::json bindings;
  };
  const auto observe = [&] {
    std::array<char, 512> callerBuffer{};
    std::vector<File> readers;
    readers.reserve(opened_per_wave);
    std::array<intptr_t, opened_per_wave> os_handles{};
    Wave wave;
    for (size_t i = 0; i < opened_per_wave; ++i)
    {
      FILE *handle = nullptr;
      const auto error = _wfopen_s(&handle, path.c_str(), L"rb");
      readers.emplace_back(handle, &std::fclose);
      Require(!error && handle, "open independent binary CRT readers");
      wave.streams[i] = reinterpret_cast<uintptr_t>(handle);
      const int fd = _fileno(handle);
      Require(fd >= 0 && fd < max_descriptors, "CRT descriptor lies within lowio bounds");
      wave.descriptors[i] = fd;
      os_handles[i] = _get_osfhandle(fd);
      Require(GetFileType(reinterpret_cast<HANDLE>(os_handles[i])) == FILE_TYPE_DISK,
              "CRT descriptor maps to the open disk file");
    }
    Require(std::fgetc(readers.front().get()) == 'F' &&
        std::setvbuf(readers[1].get(), callerBuffer.data(), _IOFBF, callerBuffer.size()) == 0,
        "activate one ordinary CRT buffer and one caller-provided buffer");
    const auto sort_and_check_distinct = [](auto &values) {
      std::sort(values.begin(), values.end());
      Require(std::adjacent_find(values.begin(), values.end()) == values.end(),
              "concurrent readers own distinct FILE slots, descriptors and Windows handles");
    };
    sort_and_check_distinct(wave.streams);
    sort_and_check_distinct(wave.descriptors);
    sort_and_check_distinct(os_handles);
    const auto live = InspectWindowsCrtFileTables();
    Require(live.streamSlots == before.streamSlots && live.processHandlesObserved &&
                live.processHandles >= opened_per_wave,
            "observe process handle count with concurrent readers and unchanged stream limit");
    wave.process_handles = live.processHandles;
    struct Duplicate
    {
      int fd;
      ~Duplicate() { if (fd >= 0) _close(fd); }
    } alias{_dup(wave.descriptors.front())};
    Require(alias.fd >= 0, "duplicate a live CRT descriptor without adding a FILE stream");
    const auto platform = cutum::ObserveWindowsPlatformResources();
    const auto &files = platform.handles.files;
    const auto &crt = files.crt;
    const auto disk = static_cast<std::size_t>(cutum::WindowsFileKind::DiskFile);
    wave.allocated_slots = crt.dynamicSlots;
    wave.lowio_blocks = crt.lowioBlocks;
    if (baseFiles.crt.supported)
      Require(files.stable && crt.stable && crt.activeStreams == baseFiles.crt.activeStreams + opened_per_wave &&
        crt.openDescriptors == baseFiles.crt.openDescriptors + opened_per_wave + 1 &&
        crt.linkedStreams == baseFiles.crt.linkedStreams + opened_per_wave &&
        crt.linkedDescriptors == baseFiles.crt.linkedDescriptors + opened_per_wave + 1 && crt.lowioBlocks >= 2 &&
        files.kinds[disk].linkedCrtEntries == baseFiles.kinds[disk].linkedCrtEntries + opened_per_wave + 1 &&
        files.kinds[disk].linkedCrtObjects == baseFiles.kinds[disk].linkedCrtObjects + opened_per_wave &&
        crt.crtBuffers == baseFiles.crt.crtBuffers + 1 && crt.crtBufferSpan == baseFiles.crt.crtBufferSpan + 4096 &&
        crt.callerBuffers == baseFiles.crt.callerBuffers + 1 &&
        crt.callerBufferSpan == baseFiles.crt.callerBufferSpan + callerBuffer.size(),
          "join the FILE slots, lowio descriptors, distinct disk objects, shared duplicate and both buffer origins");
    wave.bindings = cutum::DescribeWindowsPlatformResources(platform)["file_resources"];
    Require(_close(alias.fd) == 0, "retire the descriptor without retiring its independently held sibling");
    alias.fd = -1;
    for (auto &reader : readers)
      Require(!std::fclose(reader.release()), "close an independent CRT reader");
    return wave;
  };
  const auto first = observe();
  const auto second = observe();
  const auto after = InspectWindowsCrtFileTables();
  const auto retired = cutum::ObserveWindowsPlatformResources();
  const auto &crt = retired.handles.files.crt;
  if (baseFiles.crt.supported)
  {
    Require(crt.stable && crt.activeStreams == baseFiles.crt.activeStreams &&
      crt.openDescriptors == baseFiles.crt.openDescriptors && crt.linkedStreams == baseFiles.crt.linkedStreams &&
      crt.linkedDescriptors == baseFiles.crt.linkedDescriptors &&
      crt.crtBufferSpan == baseFiles.crt.crtBufferSpan && crt.callerBufferSpan == baseFiles.crt.callerBufferSpan,
        "retire the open stream, descriptor and buffer associations");
    Require(crt.lowioBlocks >= 2 && crt.lowioBlocks == first.lowio_blocks && crt.lowioBlocks == second.lowio_blocks &&
        crt.dynamicSlots >= opened_per_wave && crt.dynamicSlots == first.allocated_slots &&
        crt.dynamicSlots == second.allocated_slots, "keep the same allocated CRT slots after both waves");
    Require(crt.retainedTemporaryBytes == baseFiles.crt.retainedTemporaryBytes && !crt.temporaryAttachments,
            "keep the separately identified standard stream backing through file retirement");
  }
  Require(first.streams == second.streams && first.descriptors == second.descriptors,
          "CRT reuses closed FILE slots and descriptor indices across waves");
  Require(before.processHandlesObserved && after.processHandlesObserved &&
              first.descriptors.front() / before.descriptorsPerBlock !=
                  first.descriptors.back() / before.descriptorsPerBlock,
          "observe process handle counts and readers spanning lowio blocks");
  return {{"supported", true}, {"streams_per_wave", opened_per_wave},
          {"descriptor_first", first.descriptors.front()}, {"descriptor_last", first.descriptors.back()},
          {"file_slots_reused", true}, {"descriptors_reused", true}, {"handles_distinct", true},
          {"file_slot_bytes", before.fileSlotBytes}, {"descriptor_bytes", before.descriptorBytes},
          {"stream_slots", before.streamSlots}, {"current_requested_heap_upper", before.currentRequestedHeapUpper},
          {"requested_heap_upper", before.requestedHeapUpper}, {"table_statics_bytes", before.staticsBytes},
          {"process_handles_before", before.processHandles}, {"process_handles_after", after.processHandles},
          {"process_handles_open", {first.process_handles, second.process_handles}},
          {"io_bindings", {{"baseline", cutum::DescribeWindowsPlatformResources(baseline)["file_resources"]},
                            {"first", first.bindings}, {"second", second.bindings},
                            {"retired", cutum::DescribeWindowsPlatformResources(retired)["file_resources"]}}}};
#else
  (void)path;
  return {{"supported", false}};
#endif
}

nlohmann::json CheckWorkerLaunchAllocations()
{
  using cutum::UJobThreadPool;
  using cutum::io_codec_fixture::Require;
  constexpr size_t workers = 3;
  { UJobThreadPool warmup(workers); }
  const auto launch = UJobThreadPool::GetWorkerLaunchRequestBytes();
  if (!launch)
    return {{"supported", false}};
  size_t vector_bytes = 0;
  observation::Counts started;
  {
    observation::Scope observed;
    {
      UJobThreadPool pool(workers);
      const auto slots = pool.GetWorkerStorageSnapshot();
      vector_bytes = slots.thread_bytes;
      const auto queue = pool.GetQueueAllocationSnapshot();
      started = observation::Snapshot();
      Require(slots.workers == workers && started.live == vector_bytes + queue.live_bytes + workers * launch &&
                  started.blocks == 1 + queue.live_allocations + workers,
              "actual worker launch requests match vector, queue and active entry allocations");
    }
    const auto retired = observation::Snapshot();
    Require(!retired.live && !retired.blocks, "worker vector and cross-thread entries retire");
  }
  for (size_t index = 0; index < started.attempts; ++index)
  {
    observation::Scope refused(index);
    bool failed = false;
    try { UJobThreadPool pool(workers); }
    catch (const std::bad_alloc &) { failed = true; }
    const auto retired = observation::Snapshot();
    Require(failed && retired.refusals == 1 && !retired.live && !retired.blocks,
            "partially launched pool joins before releasing its construction state");
  }
  return {{"supported", true}, {"workers", workers}, {"vector_bytes", vector_bytes},
          {"launch_request_per_worker", launch}, {"ordinary_live", started.live},
          {"refused_prefixes", started.attempts}, {"retired", true}};
}
} // namespace

int main(int argc, char **argv)
{
  try
  {
    const std::filesystem::path report = argc > 1 ? argv[1] : "async-io-storage.json";
    const auto stamp = std::chrono::steady_clock::now().time_since_epoch().count();
    const auto folder = report.string() + ".data-" + std::to_string(stamp);
    // Codec observations run before any worker starts.
    const auto codec = cutum::io_codec_fixture::CheckCodecs();
    const auto json_writer = cutum::io_codec_fixture::CheckJsonWriter();
    const auto binary_writer = cutum::io_codec_fixture::CheckBinaryWriter();
    const auto save_snapshots = cutum::io_codec_fixture::CheckTerrainSaveSnapshots();
    const auto assignment = cutum::io_codec_fixture::CheckRuntimeIdAssignment();
    const auto synthetic = cutum::io_codec_fixture::CheckSyntheticRegistry();
    const auto disk = cutum::io_codec_fixture::CheckDiskIndex(folder + "-index");
    const auto malformed = cutum::io_codec_fixture::CheckMalformedJson();
    const auto receipt = cutum::io_codec_fixture::CheckReceiptTemporaries();
    const auto receipt_writer = cutum::io_codec_fixture::CheckReceiptWriter(folder + "-receipt-write");
    const auto streams = cutum::io_codec_fixture::CheckFileStreams(folder + "-streams");
    const auto launch = CheckWorkerLaunchAllocations();
    auto result = cutum::RunAsyncIoStorageProbe(folder);
    const auto stdio = std::filesystem::path(folder) / "stdio.bin";
    result["crt_file_tables"] = CheckCrtFileTables(stdio);
    result["windows_io_resources"] = cutum::windows_io_resource_fixture::Run(stdio);
    result["windows_address_space"] = cutum::windows_address_space_fixture::Run();
    result["windows_image_memory"] = cutum::windows_image_memory_fixture::Run();
    result["windows_module_tls"] = cutum::windows_module_tls_fixture::Run();
    result["windows_tls_vector_history"] = cutum::windows_tls_vector_history_fixture::Run(std::filesystem::path(folder) / "tls-vector-history");
    result["windows_slot_storage"] = cutum::windows_slot_fixture::Run();
    result["native_thread_stacks"] = cutum::native_thread_stack_fixture::Run();
    result["windows_crt_threads"] = cutum::windows_crt_thread_fixture::Run();
    result["windows_crt_locale"] = cutum::windows_crt_locale_fixture::Run();
    result["windows_executable_tls"] = cutum::windows_executable_tls_fixture::Run();
    result["ordinary_codec"] = codec;
    result["json_writer"] = json_writer;
    result["binary_writer"] = binary_writer;
    result["terrain_save_snapshots"] = save_snapshots;
    result["runtime_id_assignment"] = assignment;
    result["synthetic_registry"] = synthetic;
    result["ordinary_disk_index"] = disk;
    result["ordinary_failed_parse"] = malformed;
    result["ordinary_receipt_temporaries"] = receipt;
    result["receipt_writer"] = receipt_writer;
    result["ordinary_file_streams"] = streams;
    result["ordinary_worker_launch"] = launch;
    std::ofstream output(report);
    output << result.dump(2) << '\n';
    return output ? 0 : 1;
  }
  catch (const std::exception &error)
  {
    std::cerr << error.what() << '\n';
    return 1;
  }
}
