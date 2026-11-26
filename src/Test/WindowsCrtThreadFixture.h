#pragma once

#include "Core/Memory/WindowsCrtThreadStorage.h"
#include "Test/NativeThreadStackFixture.h"
#include <algorithm>
#include <array>
#include <cstring>
#include <ctime>

#if CUTUM_WINDOWS_STATIC_THREAD_CRT_PROFILE
#include <malloc.h>
struct __acrt_ptd;
struct __vcrt_ptd;
extern "C" __acrt_ptd *__cdecl __acrt_getptd_head();
extern "C" __vcrt_ptd *__cdecl __vcrt_getptd_noinit();
#endif

namespace cutum::windows_crt_thread_fixture
{
#if CUTUM_WINDOWS_STATIC_THREAD_CRT_PROFILE
using io_codec_fixture::Require;
using native_thread_stack_fixture::ParkedThread;

inline thread_local int TlsMarker = 29;
enum class Block { Ucrt, Vcrt, Parameter, Time, NarrowTime, WideTime, Count };
inline constexpr auto BlockCount = static_cast<std::size_t>(Block::Count);

struct Sample
{
  std::array<const void *, BlockCount> addresses{};
  std::array<std::size_t, BlockCount> requestBytes{};
  const int *tlsMarker{};
  int initialTls{};
  bool initializedVcrtOnFirstException{}, errnoAligned{}, scratchReused{};
};

__declspec(noinline) inline void ThrowTestMarker() { throw 29; }

#pragma warning(push)
#pragma warning(disable : 4996)
inline Sample OnWorker(int value)
{
  using namespace windows_crt_thread_profile;
  Sample out;
  auto *ucrt = __acrt_getptd_head();
  const auto heap = GetProcessHeap();
  Require(reinterpret_cast<HANDLE>(_get_heap_handle()) == heap && HeapValidate(heap, 0, ucrt),
          "the static UCRT PTD belongs to the process heap");
  Require(HeapSize(heap, 0, ucrt) == UcrtPtdBytes, "the static UCRT allocates one PTD in this profile");
  out.errnoAligned = reinterpret_cast<const char *>(_errno()) ==
      reinterpret_cast<const char *>(ucrt) + ErrnoOffset;
  const void *parameter{};
  std::memcpy(&parameter, reinterpret_cast<const char *>(ucrt) + LaunchContextOffset, sizeof(parameter));
  const auto vcrtBefore = __vcrt_getptd_noinit();
  try { ThrowTestMarker(); }
  catch (int mark) { Require(mark == 29, "handle the controlled C++ exception"); }
  const auto vcrtAfter = __vcrt_getptd_noinit();
  out.initializedVcrtOnFirstException = !vcrtBefore && vcrtAfter;
  Require(vcrtAfter && (!vcrtBefore || vcrtBefore == vcrtAfter), "retain the worker VCRuntime PTD");
  const __time64_t instant = 1700000000;
  const auto *tm = _gmtime64(&instant);
  Require(tm != nullptr, "obtain the retained time structure");
  const auto *narrow = asctime(tm);
  const auto *wide = _wasctime(tm);
  out.addresses = {ucrt, vcrtAfter, parameter, tm, narrow, wide};
  const std::array<std::size_t, BlockCount> expected{
      UcrtPtdBytes, VcrtPtdBytes, LaunchParameterBytes, TimeStructBytes, NarrowTimeTextBytes, WideTimeTextBytes};
  for (std::size_t i = 0; i < out.addresses.size(); ++i)
  {
    const auto *address = out.addresses[i];
    Require(address && HeapValidate(heap, 0, address), "validate each retained CRT block on its owning heap");
    out.requestBytes[i] = HeapSize(heap, 0, address);
  }
  Require(out.requestBytes == expected, "actual CRT heap requests match the compiled runtime layouts");
  out.scratchReused = _gmtime64(&instant) == tm && asctime(tm) == narrow && _wasctime(tm) == wide;
  out.tlsMarker = &TlsMarker;
  out.initialTls = TlsMarker;
  TlsMarker = value;
  return out;
}
#pragma warning(pop)

template<std::size_t Size>
inline std::size_t CountTrackedBusyBlocks(const std::array<std::uintptr_t, Size> &tracked)
{
  const auto heap = GetProcessHeap();
  Require(HeapLock(heap) != 0, "lock the process heap for the isolated CRT lifetime check");
  struct Unlock
  {
    HANDLE heap;
    ~Unlock() { HeapUnlock(heap); }
  };
  std::size_t count = 0;
  DWORD status = 0;
  {
    const Unlock unlock{heap};
    PROCESS_HEAP_ENTRY entry{};
    while (HeapWalk(heap, &entry))
      if (entry.wFlags & PROCESS_HEAP_ENTRY_BUSY)
        count += std::find(tracked.begin(), tracked.end(), reinterpret_cast<std::uintptr_t>(entry.lpData)) != tracked.end();
    status = GetLastError();
  }
  Require(status == ERROR_NO_MORE_ITEMS, "walk all busy blocks in the process heap");
  return count;
}
#endif

inline nlohmann::json Run()
{
#if CUTUM_WINDOWS_STATIC_THREAD_CRT_PROFILE
  using namespace windows_crt_thread_profile;
  const auto layout = BoundWindowsCrtThreadStorage(3, 1);
  Require(layout.supported && layout.ucrtPtdUpper == 3 * UcrtPtdBytes &&
              layout.workerVcrtPtdUpper == 2 * VcrtPtdBytes && layout.launchParameterUpper == 2 * LaunchParameterBytes &&
              layout.timeScratchUpper == 3 * TimeScratchBytes && layout.mainVcrtStaticBytes == VcrtPtdBytes &&
              layout.tlsTemplatePayload == 3 * (layout.tlsRawPerThread + layout.tlsZeroFillPerThread) &&
              layout.tlsTemplatePayload >= 3 * sizeof(TlsMarker) &&
              !BoundWindowsCrtThreadStorage(SIZE_MAX, 1).supported && !BoundWindowsCrtThreadStorage(1, 2).supported,
          "join the linked CRT layout to valid scope counts and reject overflow");
  MEMORY_BASIC_INFORMATION mainVcrt{};
  Require(VirtualQuery(__vcrt_getptd_noinit(), &mainVcrt, sizeof(mainVcrt)) && mainVcrt.Type == MEM_IMAGE &&
              mainVcrt.AllocationBase == GetModuleHandleW(nullptr),
          "the startup thread VCRuntime PTD is in the linked image");
  TlsMarker = 83;
  std::array<Sample, 2> workers;
  std::array<std::uintptr_t, 2 * BlockCount> blocks{};
  std::size_t held{};
  {
    std::promise<Sample> first, second;
    auto a = first.get_future(), b = second.get_future();
    const ParkedThread firstWorker([&](std::future<void> &released) {
      try { first.set_value(OnWorker(41)); }
      catch (...) { first.set_exception(std::current_exception()); }
      released.wait();
    });
    const ParkedThread secondWorker([&](std::future<void> &released) {
      try { second.set_value(OnWorker(47)); }
      catch (...) { second.set_exception(std::current_exception()); }
      released.wait();
    });
    Require(a.wait_for(std::chrono::seconds(8)) == std::future_status::ready &&
                b.wait_for(std::chrono::seconds(8)) == std::future_status::ready,
            "park both CRT thread owners with live storage");
    workers = {a.get(), b.get()};
    Require(workers[0].tlsMarker != workers[1].tlsMarker &&
                workers[0].tlsMarker != &TlsMarker && workers[1].tlsMarker != &TlsMarker &&
                workers[0].initialTls == 29 && workers[1].initialTls == 29 && TlsMarker == 83,
            "the loaded executable initializes a private TLS cell on each thread");
    for (std::size_t thread = 0; thread < workers.size(); ++thread)
    {
      const auto &worker = workers[thread];
      Require(worker.errnoAligned && worker.scratchReused, "observe independent errno and reusable time scratch");
      for (std::size_t i = 0; i < worker.addresses.size(); ++i)
        blocks[thread * BlockCount + i] = reinterpret_cast<std::uintptr_t>(worker.addresses[i]);
    }
    std::sort(blocks.begin(), blocks.end());
    Require(std::adjacent_find(blocks.begin(), blocks.end()) == blocks.end(), "the workers own distinct live CRT blocks");
    held = CountTrackedBusyBlocks(blocks);
    Require(held == blocks.size(), "the process heap sees the PTD, launch and scratch blocks of both active workers");
  }
  const auto remaining = CountTrackedBusyBlocks(blocks);
  Require(remaining == 0, "the process heap retires the worker CRT blocks after join");
  return {{"status", "PASS"}, {"workers", workers.size()}, {"ucrt_ptd_request", UcrtPtdBytes},
      {"vcrt_worker_ptd_request", VcrtPtdBytes}, {"launch_parameter_request", LaunchParameterBytes},
      {"gmtime_request", TimeStructBytes}, {"asctime_request", NarrowTimeTextBytes}, {"wasctime_request", WideTimeTextBytes},
      {"first_exception_initialized_vcrt", {workers[0].initializedVcrtOnFirstException, workers[1].initializedVcrtOnFirstException}},
      {"verified_live_heap_blocks", held}, {"remaining_heap_blocks_after_join", remaining},
      {"exe_tls_raw_per_thread", layout.tlsRawPerThread}, {"exe_tls_zero_fill_per_thread", layout.tlsZeroFillPerThread},
      {"tls_initialization_isolated", true}, {"main_vcrt_is_static", true}};
#else
  return {{"status", "UNSUPPORTED"}};
#endif
}
} // namespace cutum::windows_crt_thread_fixture
