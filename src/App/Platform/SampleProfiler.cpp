#include "App/Platform/SampleProfiler.h"

#ifdef _WIN32

#define WIN32_LEAN_AND_MEAN
#include <windows.h>
// clang-format off
#include <dbghelp.h>
// clang-format on

#include "App/Core.h"

#include <algorithm>
#include <atomic>
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

void SamplerLoop(int interval_ms)
{
  while (!gStop.load(std::memory_order_relaxed))
  {
    Sleep(static_cast<DWORD>(interval_ms));
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
  gThread = std::thread(SamplerLoop, interval_ms);
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
