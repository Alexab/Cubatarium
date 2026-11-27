#pragma once

#include "App/Platform/WindowsMemoryCheckpoint.h"
#include "Test/WindowsCrtThreadFixture.h"

namespace cutum::windows_module_tls_fixture
{
inline nlohmann::json Run()
{
#if CUTUM_WINDOWS_STATIC_THREAD_CRT_PROFILE
  using io_codec_fixture::Require;
  using native_thread_stack_fixture::ParkedThread;
  using windows_crt_thread_fixture::CountTrackedBusyBlocks;
  constexpr auto duration = std::chrono::seconds(10);
  constexpr auto dllName = L"windows_module_tls_fixture.dll";
  constexpr auto reportName = "windows_module_tls_fixture.dll";
  using Accessor = unsigned char *(*)();
  const auto observe = [] {
    WindowsAddressSpace space;
    const auto images = ReadWindowsImageMemory(space);
    auto result = ObserveWindowsModuleTls(images);
    if (!result.complete)
      throw std::runtime_error("incomplete controlled module TLS snapshot: " + DescribeWindowsModuleTls(result).dump());
    return result;
  };
  const auto findModule = [reportName](const auto &memory) {
    return std::find_if(memory.modules.begin(), memory.modules.end(), [reportName](const auto &row) {
      return row.filename == reportName;
    });
  };
  const auto findThread = [](const auto &memory, DWORD id) {
    return std::find_if(memory.threads.begin(), memory.threads.end(), [id](const auto &row) { return row.id == id; });
  };

  NativeThreadStackOwner main(NativeThreadKind::Main, true);
  const auto baseline = observe();
  Require(findModule(baseline) == baseline.modules.end(), "the controlled TLS DLL starts unloaded");
  std::array<wchar_t, 1024> exePath{};
  const auto length = GetModuleFileNameW(nullptr, exePath.data(), static_cast<DWORD>(exePath.size()));
  Require(length && length < exePath.size(), "identify the directory of the Release TLS fixture");
  const auto path = std::filesystem::path(exePath.data()).parent_path() / dllName;
  const auto unload = [](void *handle) { FreeLibrary(static_cast<HMODULE>(handle)); };
  WindowsModuleTlsStorage held, joined;
  DWORD existingId{}, lateId{};
  std::array<std::uintptr_t, 2> workerBlocks{};
  std::size_t knownBlockBytes{};
  {
    std::unique_ptr<void, decltype(unload)> library(nullptr, unload);
    {
      std::promise<DWORD> existingReady;
      auto existing = existingReady.get_future();
      std::promise<Accessor> publishAccessor;
      auto accessor = publishAccessor.get_future();
      using WorkerVisit = std::pair<DWORD, std::uintptr_t>;
      std::promise<WorkerVisit> existingVisit, lateVisit;
      auto visitedExisting = existingVisit.get_future();
      auto visitedLate = lateVisit.get_future();
      const auto visit = [](Accessor getCell, unsigned char value) -> WorkerVisit {
        auto *cell = getCell();
        if (!cell || *cell != 17)
          return {};
        *cell = value;
        return {GetCurrentThreadId(), reinterpret_cast<std::uintptr_t>(cell)};
      };
      const ParkedThread existingWorker([&](auto &release) {
        const NativeThreadStackOwner owner(NativeThreadKind::JobPool, true);
        existingReady.set_value(GetCurrentThreadId());
        if (accessor.wait_for(duration) != std::future_status::ready)
          return;
        existingVisit.set_value(visit(accessor.get(), 23));
        release.wait();
      });
      Require(existing.wait_for(duration) == std::future_status::ready, "park an engine thread before loading the DLL");
      existingId = existing.get();
      library.reset(LoadLibraryExW(path.c_str(), nullptr, LOAD_LIBRARY_SEARCH_DLL_LOAD_DIR | LOAD_LIBRARY_SEARCH_SYSTEM32));
      Require(library != nullptr, "load the adjacent Release TLS test DLL");
      const auto getCell = reinterpret_cast<Accessor>(GetProcAddress(static_cast<HMODULE>(library.get()), "CubatariumFixtureTlsCell"));
      Require(getCell != nullptr, "identify the DLL's independently compiled TLS accessor");
      publishAccessor.set_value(getCell);
      const ParkedThread lateWorker([&](auto &release) {
        lateVisit.set_value(visit(getCell, 37));
        release.wait();
      });
      Require(visitedExisting.wait_for(duration) == std::future_status::ready &&
                  visitedLate.wait_for(duration) == std::future_status::ready,
          "both workers reach their actual thread-local cells");
      const auto [firstId, firstCell] = visitedExisting.get();
      const auto [secondId, secondCell] = visitedLate.get();
      lateId = secondId;
      auto *mainCell = getCell();
      Require(firstId == existingId && secondId && firstCell && secondCell && firstCell != secondCell && mainCell && *mainCell == 17,
              "DLL TLS stays isolated on the main, pre-existing and new threads");
      std::array<NativeThreadTlsBlock, 3> anchors{{{firstCell}, {secondCell}, {reinterpret_cast<std::uintptr_t>(mainCell)}}};
      Require(ReadWindowsExecutableTlsBlocks(anchors.data(), anchors.data() + anchors.size()),
              "independently locate the actual DLL cells in three distinct live heap allocations");
      std::size_t cellBytes{};
      for (const auto &block : anchors)
      {
        knownBlockBytes += block.dataBytes;
        if (block.anchor == firstCell || block.anchor == secondCell)
          workerBlocks[cellBytes++] = block.base;
      }
      held = observe();
      const auto module = findModule(held);
      const auto first = findThread(held, firstId);
      const auto second = findThread(held, secondId);
      Require(module != held.modules.end() && module->templateBytes >= 320 &&
                  module->storage.blocks == held.threadsWithVector && module->storage.dataBytes >= knownBlockBytes &&
                  first != held.threads.end() && first->kind == static_cast<std::size_t>(NativeThreadKind::JobPool) &&
                  second != held.threads.end() && second->kind == NativeThreadNames.size(),
          "join the DLL allocations to their registered and unregistered live thread owners");
    }
    Require(CountTrackedBusyBlocks(workerBlocks) == 0, "the exact DLL heap allocations of the two joined workers retire");
    joined = observe();
    const auto surviving = findModule(joined);
    Require(held.processThreads == joined.processThreads + 2 && surviving != joined.modules.end() &&
                surviving->storage.blocks + 2 == findModule(held)->storage.blocks &&
                findThread(joined, existingId) == joined.threads.end() && findThread(joined, lateId) == joined.threads.end(),
        "retire precisely the two worker records while the DLL remains loaded");
  }
  const auto unloaded = observe();
  Require(!GetModuleHandleW(dllName) && findModule(unloaded) == unloaded.modules.end() &&
              unloaded.processThreads == baseline.processThreads,
          "retire the module association after unloading the controlled DLL");
  return {{"status", "PASS"}, {"independent_live_dll_bytes", knownBlockBytes},
          {"worker_blocks_retired", workerBlocks.size()}, {"existing_thread", existingId}, {"late_thread", lateId},
          {"baseline", DescribeWindowsModuleTls(baseline)}, {"held", DescribeWindowsModuleTls(held)},
          {"after_join", DescribeWindowsModuleTls(joined)}, {"after_unload", DescribeWindowsModuleTls(unloaded)}};
#else
  return {{"status", "UNSUPPORTED"}};
#endif
}
} // namespace cutum::windows_module_tls_fixture
