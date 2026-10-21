#include "App/Platform/SampleProfiler.h"

#ifdef _WIN32

#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <tlhelp32.h>
// clang-format off
#include <dbghelp.h>
// clang-format on

#include "App/Core.h"

#include <algorithm>
#include <atomic>
#include <chrono>
#include <cstdint>
#include <cstdlib>
#include <fstream>
#include <string>
#include <thread>
#include <unordered_map>
#include <vector>

#pragma comment(lib, "dbghelp.lib")

namespace cutum
{

namespace
{

constexpr int kMaxFrames = 28;
constexpr size_t kMaxSamples = 400000;

struct Sample
{
  uint8_t depth;
  uint64_t frames[kMaxFrames];
};

std::vector<Sample> gSamples; // preallocated, never reallocated while running
std::atomic<size_t> gSampleCount{0};
std::atomic<bool> gStop{false};
std::thread gThread;
HANDLE gMainThread = nullptr;

bool CaptureThreadStack(HANDLE thread, Sample &sample)
{
  if (SuspendThread(thread) == static_cast<DWORD>(-1))
  {
    return false;
  }
  CONTEXT context{};
  context.ContextFlags = CONTEXT_FULL;
  sample.depth = 0;
  if (GetThreadContext(thread, &context))
  {
    for (int i = 0; i < kMaxFrames; ++i)
    {
      const uint64_t pc = context.Rip;
      if (pc == 0)
      {
        break;
      }
      sample.frames[sample.depth++] = pc;
      DWORD64 image_base = 0;
      const PRUNTIME_FUNCTION entry =
          RtlLookupFunctionEntry(pc, &image_base, nullptr);
      if (!entry)
      {
        context.Rip = *reinterpret_cast<uint64_t *>(context.Rsp);
        context.Rsp += 8;
        continue;
      }
      void *handler_data = nullptr;
      DWORD64 establisher = 0;
      RtlVirtualUnwind(UNW_FLAG_NHANDLER, image_base, pc, entry, &context,
                       &handler_data, &establisher, nullptr);
    }
  }
  ResumeThread(thread);
  return sample.depth > 0;
}

/// CUBA_SAMPLE_PROFILE_ALL_THREADS=1: sample every thread of the process
/// (worker pools included) in round-robin; used to profile world generation
/// and meshing workers.
void SamplerLoopAllThreads(int interval_ms, int delay_ms)
{
  const auto t0 = std::chrono::steady_clock::now();
  const DWORD self_tid = GetCurrentThreadId();
  std::vector<std::pair<DWORD, HANDLE>> threads;
  auto refresh = [&]()
  {
    for (auto &entry : threads)
    {
      CloseHandle(entry.second);
    }
    threads.clear();
    const HANDLE snap = CreateToolhelp32Snapshot(TH32CS_SNAPTHREAD, 0);
    if (snap == INVALID_HANDLE_VALUE)
    {
      return;
    }
    THREADENTRY32 te{};
    te.dwSize = sizeof(te);
    const DWORD pid = GetCurrentProcessId();
    if (Thread32First(snap, &te))
    {
      do
      {
        if (te.th32OwnerProcessID == pid && te.th32ThreadID != self_tid)
        {
          HANDLE h = OpenThread(THREAD_SUSPEND_RESUME | THREAD_GET_CONTEXT |
                                    THREAD_QUERY_INFORMATION,
                                FALSE, te.th32ThreadID);
          if (h)
          {
            threads.emplace_back(te.th32ThreadID, h);
          }
        }
      } while (Thread32Next(snap, &te));
    }
    CloseHandle(snap);
  };
  int ticks = 0;
  while (!gStop.load(std::memory_order_relaxed))
  {
    Sleep(static_cast<DWORD>(interval_ms));
    if (std::chrono::duration_cast<std::chrono::milliseconds>(
            std::chrono::steady_clock::now() - t0)
            .count() < delay_ms)
    {
      continue;
    }
    if (ticks++ % 50 == 0)
    {
      refresh();
    }
    for (const auto &entry : threads)
    {
      const size_t index = gSampleCount.load(std::memory_order_relaxed);
      if (index >= kMaxSamples)
      {
        break;
      }
      Sample &sample = gSamples[index];
      if (CaptureThreadStack(entry.second, sample))
      {
        // Skip threads parked in a wait: they are idle, not working.
        gSampleCount.store(index + 1, std::memory_order_relaxed);
      }
    }
  }
  for (auto &entry : threads)
  {
    CloseHandle(entry.second);
  }
}

void SamplerLoop(int interval_ms, int delay_ms)
{
  const auto t0 = std::chrono::steady_clock::now();
  while (!gStop.load(std::memory_order_relaxed))
  {
    Sleep(static_cast<DWORD>(interval_ms));
    if (std::chrono::duration_cast<std::chrono::milliseconds>(
            std::chrono::steady_clock::now() - t0)
            .count() < delay_ms)
    {
      continue;
    }
    const size_t index = gSampleCount.load(std::memory_order_relaxed);
    if (index >= kMaxSamples)
    {
      continue;
    }
    if (SuspendThread(gMainThread) == static_cast<DWORD>(-1))
    {
      continue;
    }
    CONTEXT context{};
    context.ContextFlags = CONTEXT_FULL;
    Sample &sample = gSamples[index];
    sample.depth = 0;
    if (GetThreadContext(gMainThread, &context))
    {
      for (int i = 0; i < kMaxFrames; ++i)
      {
        const uint64_t pc = context.Rip;
        if (pc == 0)
        {
          break;
        }
        sample.frames[sample.depth++] = pc;
        DWORD64 image_base = 0;
        const PRUNTIME_FUNCTION entry =
            RtlLookupFunctionEntry(pc, &image_base, nullptr);
        if (!entry)
        {
          // Leaf function without unwind info: return address is at [rsp].
          context.Rip = *reinterpret_cast<uint64_t *>(context.Rsp);
          context.Rsp += 8;
          continue;
        }
        void *handler_data = nullptr;
        DWORD64 establisher = 0;
        RtlVirtualUnwind(UNW_FLAG_NHANDLER, image_base, pc, entry, &context,
                         &handler_data, &establisher, nullptr);
      }
    }
    ResumeThread(gMainThread);
    if (sample.depth > 0)
    {
      gSampleCount.store(index + 1, std::memory_order_relaxed);
    }
  }
}

void DumpProfileImpl()
{
  gStop.store(true);
  if (gThread.joinable())
  {
    gThread.join();
  }
  const size_t count = gSampleCount.load();
  if (count == 0)
  {
    return;
  }
  const HANDLE process = GetCurrentProcess();
  SymSetOptions(SYMOPT_UNDNAME | SYMOPT_DEFERRED_LOADS | SYMOPT_LOAD_LINES);
  SymInitialize(process, nullptr, TRUE);
  std::unordered_map<uint64_t, std::string> names;
  auto resolve = [&](uint64_t address) -> const std::string &
  {
    auto it = names.find(address);
    if (it != names.end())
    {
      return it->second;
    }
    alignas(SYMBOL_INFO) char buffer[sizeof(SYMBOL_INFO) + 512];
    auto *symbol = reinterpret_cast<SYMBOL_INFO *>(buffer);
    symbol->SizeOfStruct = sizeof(SYMBOL_INFO);
    symbol->MaxNameLen = 511;
    DWORD64 displacement = 0;
    std::string name = "?";
    if (SymFromAddr(process, address, &displacement, symbol))
    {
      name = symbol->Name;
    }
    return names.emplace(address, std::move(name)).first->second;
  };
  std::unordered_map<std::string, size_t> self_count;
  std::unordered_map<std::string, size_t> incl_count;
  for (size_t i = 0; i < count; ++i)
  {
    const Sample &sample = gSamples[i];
    if (sample.depth == 0)
    {
      continue;
    }
    ++self_count[resolve(sample.frames[0])];
    std::vector<const std::string *> seen;
    for (int f = 0; f < sample.depth; ++f)
    {
      const std::string &name = resolve(sample.frames[f]);
      if (std::find(seen.begin(), seen.end(), &name) == seen.end())
      {
        seen.push_back(&name);
        ++incl_count[name];
      }
    }
  }
  auto top = [&](const std::unordered_map<std::string, size_t> &m, size_t n)
  {
    std::vector<std::pair<std::string, size_t>> v(m.begin(), m.end());
    std::sort(v.begin(), v.end(),
              [](const auto &a, const auto &b) { return a.second > b.second; });
    if (v.size() > n)
    {
      v.resize(n);
    }
    return v;
  };
  std::ofstream out(GetExecutableDirectory() / "sample_profile.txt",
                    std::ios::trunc);
  out << "samples=" << count << "\n\n== inclusive (top 70) ==\n";
  for (const auto &entry : top(incl_count, 70))
  {
    out << (100.0 * entry.second / static_cast<double>(count)) << "%  "
        << entry.second << "  " << entry.first << "\n";
  }
  out << "\n== self (top 50) ==\n";
  for (const auto &entry : top(self_count, 50))
  {
    out << (100.0 * entry.second / static_cast<double>(count)) << "%  "
        << entry.second << "  " << entry.first << "\n";
  }
  // Callers (4 frames up) of the hottest self functions.
  out << "\n== callers of top self functions ==\n";
  for (const auto &hot : top(self_count, 6))
  {
    out << "-- " << hot.first << " (" << hot.second << ")\n";
    std::unordered_map<std::string, size_t> chains;
    for (size_t i = 0; i < count; ++i)
    {
      const Sample &sample = gSamples[i];
      if (sample.depth < 2 || resolve(sample.frames[0]) != hot.first)
      {
        continue;
      }
      std::string chain;
      for (int f = 1; f < std::min<int>(sample.depth, 5); ++f)
      {
        chain += resolve(sample.frames[f]) + " <- ";
      }
      ++chains[chain];
    }
    for (const auto &entry : top(chains, 6))
    {
      out << "   " << entry.second << "  " << entry.first << "\n";
    }
  }
  if (const char *needle = std::getenv("CUBA_SAMPLE_CALLERS_OF"))
  {
    out << "\n== callers of functions matching '" << needle << "' ==\n";
    std::unordered_map<std::string, size_t> chains;
    for (size_t i = 0; i < count; ++i)
    {
      const Sample &sample = gSamples[i];
      for (int f = 0; f < sample.depth; ++f)
      {
        if (resolve(sample.frames[f]).find(needle) == std::string::npos)
        {
          continue;
        }
        std::string chain;
        for (int g = f + 1; g < std::min<int>(sample.depth, f + 6); ++g)
        {
          chain += resolve(sample.frames[g]) + " <- ";
        }
        ++chains[chain];
        break;
      }
    }
    for (const auto &entry : top(chains, 12))
    {
      out << "   " << entry.second << "  " << entry.first << "\n";
    }
  }
  SymCleanup(process);
}

} // namespace

void FinishSampleProfiler()
{
  static std::atomic<bool> done{false};
  if (done.exchange(true))
  {
    return;
  }
  if (gSamples.empty())
  {
    return;
  }
  DumpProfileImpl();
}

void StartSampleProfilerIfRequested()
{
  const char *enabled = std::getenv("CUBA_SAMPLE_PROFILE");
  if (!enabled || enabled[0] == '\0' || enabled[0] == '0')
  {
    return;
  }
  int interval_ms = 2;
  if (const char *value = std::getenv("CUBA_SAMPLE_PROFILE_INTERVAL_MS"))
  {
    interval_ms = std::max(1, std::atoi(value));
  }
  gSamples.resize(kMaxSamples);
  if (!DuplicateHandle(GetCurrentProcess(), GetCurrentThread(),
                       GetCurrentProcess(), &gMainThread, THREAD_ALL_ACCESS,
                       FALSE, 0))
  {
    return;
  }
  int delay_ms = 0;
  if (const char *value = std::getenv("CUBA_SAMPLE_PROFILE_DELAY_SEC"))
  {
    delay_ms = std::max(0, std::atoi(value)) * 1000;
  }
  if (const char *all = std::getenv("CUBA_SAMPLE_PROFILE_ALL_THREADS");
      all && all[0] != 'x' && all[0] != '0')
  {
    gThread = std::thread(SamplerLoopAllThreads, interval_ms, delay_ms);
  }
  else
  {
    gThread = std::thread(SamplerLoop, interval_ms, delay_ms);
  }
  std::atexit(FinishSampleProfiler);
}

} // namespace cutum

#else

namespace cutum
{
void StartSampleProfilerIfRequested() {}
void FinishSampleProfiler() {}
} // namespace cutum

#endif
