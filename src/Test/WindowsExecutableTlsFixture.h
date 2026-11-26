#pragma once

#include "Core/Memory/WindowsExecutableTlsStorage.h"
#include "Test/WindowsCrtThreadFixture.h"

namespace cutum::windows_executable_tls_fixture
{
#if CUTUM_WINDOWS_STATIC_THREAD_CRT_PROFILE
using io_codec_fixture::Require;
using native_thread_stack_detail::CallingExecutableTlsAnchor;
using native_thread_stack_fixture::ParkedThread;
using windows_crt_thread_fixture::CountTrackedBusyBlocks;

alignas(64) inline thread_local unsigned char AlignedCell{17};

struct Anchors
{
  std::uintptr_t marker{}, aligned{};
};

inline Anchors CallingAnchors() noexcept
{
  return {CallingExecutableTlsAnchor(), reinterpret_cast<std::uintptr_t>(&AlignedCell)};
}

inline nlohmann::json Run()
{
  const NativeThreadStackOwner main(NativeThreadKind::Main, true);
  const auto mainOnly = InspectNativeThreadStacks();
  Require(mainOnly.registered == 1, "start the TLS heap fixture with one registered thread");
  const auto mainLayout = BoundWindowsCrtThreadStorage(mainOnly.registered, 1);
  const auto mainAnchors = CallingAnchors();
  WindowsExecutableTlsStorage selected;
  WindowsCrtThreadStorage layout;
  std::array<std::uintptr_t, 3> workerBlocks{};
  std::array<std::size_t, 4> requests{}, overhead{};
  {
    const auto registeredWorker = [](std::promise<Anchors> &ready, std::future<void> &release) {
      const NativeThreadStackOwner worker(NativeThreadKind::JobPool, true);
      const NativeThreadStackOwner nested(NativeThreadKind::SkyField, true);
      ready.set_value(CallingAnchors());
      release.wait();
    };
    std::promise<Anchors> first, second, foreign;
    auto a = first.get_future(), b = second.get_future(), c = foreign.get_future();
    const ParkedThread firstWorker([&](std::future<void> &release) { registeredWorker(first, release); });
    const ParkedThread secondWorker([&](std::future<void> &release) { registeredWorker(second, release); });
    const ParkedThread unregistered([&](std::future<void> &release) {
      foreign.set_value(CallingAnchors());
      release.wait();
    });
    constexpr auto wait = std::chrono::seconds(8);
    Require(a.wait_for(wait) == std::future_status::ready && b.wait_for(wait) == std::future_status::ready &&
                c.wait_for(wait) == std::future_status::ready,
            "park two registered TLS owners and their unregistered peer");
    const std::array<Anchors, 4> anchors{mainAnchors, a.get(), b.get(), c.get()};
    const auto held = InspectNativeThreadStacks();
    Require(held.registered == 3 && held.matched == 3 && held.processThreads == mainOnly.processThreads + 3,
            "the TLS cohort contains the main and two workers, once for each registered thread");
    layout = BoundWindowsCrtThreadStorage(held.registered, 1);
    selected = ObserveWindowsExecutableTls(held, layout);
    Require(selected.status == ExecutableTlsStatus::Observed && selected.threads == held.registered,
            "observe one executable TLS heap block per registered live thread");

    std::array<NativeThreadTlsBlock, anchors.size()> raw{};
    for (std::size_t i = 0; i < anchors.size(); ++i)
      raw[i].anchor = anchors[i].marker;
    Require(ReadWindowsExecutableTlsBlocks(raw.data(), raw.data() + raw.size()),
            "the unregistered peer still owns a separate executable TLS heap block");
    std::size_t selectedRequests = 0, selectedOverhead = 0;
    const auto heap = GetProcessHeap();
    for (std::size_t i = 0; i < anchors.size(); ++i)
    {
      const auto &anchor = anchors[i];
      const auto entry = std::find_if(raw.begin(), raw.end(), [&](const auto &block) {
        return block.anchor == anchor.marker;
      });
      Require(entry != raw.end() && anchor.aligned % 64 == 0 && anchor.aligned >= entry->base &&
                  anchor.aligned - entry->base < entry->dataBytes,
              "both executable TLS cells are within the same heap block, and the test cell is aligned");
      requests[i] = HeapSize(heap, 0, reinterpret_cast<const void *>(entry->base));
      overhead[i] = entry->overheadBytes;
      Require(requests[i] == entry->dataBytes && requests[i] >= layout.tlsRawPerThread + layout.tlsZeroFillPerThread,
              "the containing busy block data length matches HeapSize and covers the TLS template");
      if (i > 0)
        workerBlocks[i - 1] = entry->base;
      if (i < held.registered)
      {
        selectedRequests += requests[i];
        selectedOverhead += overhead[i];
      }
    }
    Require(selected.dataBytes == selectedRequests && selected.overheadBytes == selectedOverhead &&
                selected.templateBytes == layout.tlsTemplatePayload,
            "compose only registered distinct blocks, with template bytes already included");
    std::array<NativeThreadTlsBlock, 2> duplicateBlock{{{mainAnchors.marker}, {mainAnchors.aligned}}};
    Require(!ReadWindowsExecutableTlsBlocks(duplicateBlock.data(), duplicateBlock.data() + duplicateBlock.size()),
            "two anchors in one TLS allocation cannot be charged as separate thread blocks");
  }
  const auto remaining = CountTrackedBusyBlocks(workerBlocks);
  Require(remaining == 0, "the executable TLS heap blocks retire after all workers join");
  const auto final = InspectNativeThreadStacks();
  Require(final.registered == mainOnly.registered &&
              ObserveWindowsExecutableTls(mainOnly, mainLayout).status == ExecutableTlsStatus::CohortChanged,
          "reject a stale cohort even when its thread count is unchanged");
  const auto after = ObserveWindowsExecutableTls(final, mainLayout);
  Require(after.status == ExecutableTlsStatus::Observed && after.threads == 1 && after.dataBytes == requests[0],
          "the main TLS block remains after joined worker blocks retire");

  return {{"status", "PASS"}, {"registered_threads", selected.threads}, {"observed_threads", requests.size()},
      {"template_raw_per_thread", layout.tlsRawPerThread}, {"template_zero_fill_per_thread", layout.tlsZeroFillPerThread},
      {"block_data_bytes_main_workers_foreign", requests}, {"heap_entry_overhead_bytes_main_workers_foreign", overhead},
      {"registered_block_data_bytes", selected.dataBytes}, {"registered_heap_entry_overhead_bytes", selected.overheadBytes},
      {"registered_template_payload_bytes", selected.templateBytes}, {"largest_registered_block_data_bytes", selected.largestBlockBytes},
      {"worker_blocks_after_join", remaining}, {"main_block_data_bytes_after_join", after.dataBytes},
      {"duplicate_block_rejected", true}, {"stale_same_count_cohort_rejected", true}};
}
#else
inline nlohmann::json Run() { return {{"status", "UNSUPPORTED"}}; }
#endif
} // namespace cutum::windows_executable_tls_fixture
