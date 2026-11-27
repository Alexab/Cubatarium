#pragma once

#include "App/Platform/WindowsMemoryCheckpoint.h"
#include "Test/NativeThreadStackFixture.h"

namespace cutum::windows_tls_vector_history_fixture
{
inline nlohmann::json Run(const std::filesystem::path &root)
{
#if CUTUM_WINDOWS_STATIC_THREAD_CRT_PROFILE
  using io_codec_fixture::Require;
  using native_thread_stack_fixture::ParkedThread;
  using namespace windows_module_tls_detail;
  constexpr auto wait = std::chrono::seconds(10);
  constexpr unsigned loadCount = 24;
  std::array<wchar_t, 1024> executable{};
  const auto length = GetModuleFileNameW(nullptr, executable.data(), static_cast<DWORD>(executable.size()));
  Require(length && length < executable.size() && std::filesystem::create_directory(root),
          "identify the Release executable and isolate the TLS vector growth DLLs");
  const auto source = std::filesystem::path(executable.data()).parent_path() / L"windows_module_tls_fixture.dll";
  const auto unload = [](void *handle) { FreeLibrary(static_cast<HMODULE>(handle)); };
  std::vector<std::unique_ptr<void, decltype(unload)>> libraries;
  libraries.reserve(loadCount);
  struct Indices
  {
    std::vector<DWORD> values;
    ~Indices() { for (auto index : values) TlsFree(index); }
  } indices;
  indices.values.reserve(InlineDynamicTlsSlots + 1);
  WindowsTlsVectorHistory history;
  const auto observe = [&] {
    WindowsAddressSpace space;
    const auto images = ReadWindowsImageMemory(space);
    auto tls = ObserveWindowsModuleTls(images);
    Require(tls.complete, "observe the static TLS blocks of the controlled live thread cohort");
    const auto replaced = history.Observe(tls);
    Require(replaced.sampled && !replaced.unconfirmed && !replaced.limitReached,
            "verify the same addresses and slot values for the tracked replaced vectors");
    return std::pair(std::move(tls), replaced);
  };
  const auto findThread = [](const auto &tls, DWORD id) -> const WindowsThreadTlsRow & {
    const auto row = std::find_if(tls.threads.begin(), tls.threads.end(), [id](const auto &thread) { return thread.id == id; });
    Require(row != tls.threads.end(), "find the controlled worker in the process thread cohort");
    return *row;
  };

  std::vector<std::uintptr_t> oldWorkerVectors;
  oldWorkerVectors.reserve(loadCount);
  std::vector<NativeThreadTlsBlock> retired;
  retired.reserve(loadCount + 1);
  std::uintptr_t expansion{};
  std::size_t expansionBytes{};
  WindowsReplacedTlsVectors live, joined;
  WindowsDynamicTlsState expansionAfterFree{WindowsDynamicTlsState::Unobserved};
  {
    std::promise<std::pair<DWORD, std::uintptr_t>> ready;
    auto started = ready.get_future();
    std::promise<DWORD> useIndex;
    auto indexReady = useIndex.get_future();
    std::promise<std::uintptr_t> set;
    auto slotSet = set.get_future();
    const ParkedThread worker([&](auto &release) {
      ready.set_value({GetCurrentThreadId(), reinterpret_cast<std::uintptr_t>(NtCurrentTeb())});
      if (indexReady.wait_for(wait) != std::future_status::ready)
        return;
      const auto index = indexReady.get();
      unsigned char marker{23};
      set.set_value(TlsSetValue(index, &marker) && TlsGetValue(index) == &marker ?
                    reinterpret_cast<std::uintptr_t>(&marker) : 0);
      release.wait();
    });
    Require(started.wait_for(wait) == std::future_status::ready, "park the first thread before any controlled library load");
    const auto [id, teb] = started.get();
    auto current = observe();
    auto startingThread = findThread(current.first, id);
    Require(startingThread.dynamicState == WindowsDynamicTlsState::Absent && startingThread.vectorWitness.allocation.anchor,
            "the fresh thread has static TLS and an empty dynamic expansion pointer");
    auto previous = startingThread.vectorWitness.allocation.anchor;
    for (unsigned i = 0; i < loadCount; ++i)
    {
      const auto path = std::filesystem::absolute(root / ("tls-growth-" + std::to_string(i) + ".dll"));
      std::filesystem::copy_file(source, path);
      const auto module = LoadLibraryExW(path.c_str(), nullptr, LOAD_LIBRARY_SEARCH_DLL_LOAD_DIR | LOAD_LIBRARY_SEARCH_SYSTEM32);
      Require(module != nullptr, "load a separate copy of the controlled PE TLS image");
      libraries.emplace_back(module, unload);
      current = observe();
      const auto pointer = findThread(current.first, id).vectorWitness.allocation.anchor;
      Require(pointer != 0, "observe the active static TLS vector after a loader transition");
      if (pointer != previous)
      {
        oldWorkerVectors.push_back(previous);
        previous = pointer;
      }
    }
    Require(oldWorkerVectors.size() >= 2 && current.second.retained.blocks >= oldWorkerVectors.size(),
            "force multiple replacements and observe the earlier allocations while the original thread runs");
    std::vector<NativeThreadTlsBlock> liveBlocks;
    for (auto pointer : oldWorkerVectors)
      liveBlocks.push_back({pointer});
    Require(ReadWindowsExecutableTlsBlocks(liveBlocks.data(), liveBlocks.data() + liveBlocks.size()),
            "independently match all of the worker's replaced vectors to distinct live heap allocations");

    while (indices.values.size() <= InlineDynamicTlsSlots)
    {
      const auto index = TlsAlloc();
      Require(index != TLS_OUT_OF_INDEXES, "allocate an ordinary dynamic TLS slot");
      indices.values.push_back(index);
      if (index >= InlineDynamicTlsSlots)
        break;
    }
    const auto index = indices.values.back();
    Require(index >= InlineDynamicTlsSlots, "force a real dynamic TLS expansion index");
    useIndex.set_value(index);
    Require(slotSet.wait_for(wait) == std::future_status::ready, "set the TLS slot on its owning worker");
    const auto marker = slotSet.get();
    std::uintptr_t slot{};
    Require(marker && ReadValue(GetCurrentProcess(), teb, DynamicExpansionOffset, expansion) && expansion &&
                ReadValue(GetCurrentProcess(), expansion, (index - InlineDynamicTlsSlots) * sizeof(void *), slot) && slot == marker,
            "independently match the observed expansion array to an API-set slot");
    current = observe();
    live = current.second;
    const auto expansionRow = findThread(current.first, id);
    expansionBytes = expansionRow.dynamicExpansion.dataBytes;
    Require(expansionRow.dynamicState == WindowsDynamicTlsState::Observed && expansionRow.dynamicExpansion.blocks == 1 &&
                expansionBytes == ExpansionDynamicTlsSlots * sizeof(void *),
            "observe one real dynamic TLS array and its exact Win64 allocation size");
    Require(TlsFree(index) != 0, "release the global dynamic slot index while the worker is alive");
    indices.values.pop_back();
    current = observe();
    expansionAfterFree = findThread(current.first, id).dynamicState;
    Require(expansionAfterFree != WindowsDynamicTlsState::Unobserved,
          "separately observe the array after releasing the index");
    for (auto pointer : oldWorkerVectors)
      retired.push_back({pointer});
    retired.push_back({expansion});
  }
  const auto heap = ScanWindowsTlsHeapBlocks(retired.data(), retired.data() + retired.size());
  Require(heap.finished && !heap.matched, "the exact replaced worker vectors and dynamic array retire after thread exit");
  joined = observe().second;
  Require(joined.released >= oldWorkerVectors.size(), "verify the checkpoint history now recognizes the worker's released vectors");
  return {{"status", "PASS"}, {"loaded_dlls", libraries.size()}, {"verified_worker_replacements", oldWorkerVectors.size()},
          {"worker_expansion_bytes", expansionBytes}, {"expansion_retained_after_index_free", expansionAfterFree == WindowsDynamicTlsState::Observed},
          {"remaining_worker_blocks_after_join", heap.matched}, {"live", DescribeWindowsReplacedTlsVectors(live)},
          {"after_join", DescribeWindowsReplacedTlsVectors(joined)}};
#else
  (void)root;
  return {{"status", "UNSUPPORTED"}};
#endif
}
} // namespace cutum::windows_tls_vector_history_fixture
