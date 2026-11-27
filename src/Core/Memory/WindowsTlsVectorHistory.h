#pragma once

#include "Core/Memory/WindowsModuleTlsStorage.h"

namespace cutum
{
struct WindowsReplacedTlsVectors
{
  std::size_t trackedVectors{}, observedReplacements{}, released{}, unconfirmed{};
  WindowsTlsBlockStorage retained;
  bool sampled{}, limitReached{};
};

// Track vector addresses witnessed in earlier checkpoints. A block counts as
// retained only while its original thread survives, both sampled slot values
// still match, and no current thread uses the old block as its active vector.
class WindowsTlsVectorHistory
{
public:
  WindowsReplacedTlsVectors Observe(const WindowsModuleTlsStorage &current)
  {
    WindowsReplacedTlsVectors result;
#if CUTUM_WINDOWS_STATIC_THREAD_CRT_PROFILE
    using namespace windows_module_tls_detail;
    if (!current.complete)
      return result;
    constexpr std::size_t limit = 4096;
    struct Candidate : NativeThreadTlsBlock
    {
      std::size_t entry{};
      bool ownerPresent{};
    };
    std::vector<Candidate> candidates;
    candidates.reserve(Vectors.size());
    for (std::size_t i = 0; i < Vectors.size(); ++i)
    {
      auto &entry = Vectors[i];
      const auto &old = entry.witness;
      const auto thread = std::lower_bound(current.threads.begin(), current.threads.end(), entry.thread,
          [](const auto &row, std::uint32_t id) { return row.id < id; });
      const bool ownerPresent = thread != current.threads.end() && thread->id == entry.thread &&
          thread->vectorWitness.threadCreated == old.threadCreated;
      if (ownerPresent && thread->vectorWitness.allocation.anchor != old.allocation.anchor)
        entry.replaced = true;
      if (!entry.replaced)
        continue;
      ++result.observedReplacements;
      if (entry.released)
      {
        ++result.released;
        continue;
      }
      if (std::any_of(current.threads.begin(), current.threads.end(), [&](const auto &row) {
            return row.vectorWitness.allocation.base == old.allocation.base;
          }))
      {
        ++result.unconfirmed;
        continue;
      }
      candidates.push_back({{old.allocation.anchor}, i, ownerPresent});
    }
    result.sampled = candidates.empty() ||
        ScanWindowsTlsHeapBlocks(candidates.data(), candidates.data() + candidates.size()).finished;
    if (result.sampled)
    {
      const auto self = GetCurrentProcess();
      for (const auto &candidate : candidates)
      {
        auto &entry = Vectors[candidate.entry];
        const auto &old = entry.witness;
        if (!candidate.base)
        {
          entry.released = true;
          ++result.released;
          continue;
        }
        bool matches = candidate.ownerPresent && candidate.base == old.allocation.base &&
            candidate.dataBytes == old.allocation.dataBytes && candidate.overheadBytes == old.allocation.overheadBytes;
        if (matches)
        {
          const NativeHandle owner(OpenThread(THREAD_QUERY_LIMITED_INFORMATION | SYNCHRONIZE, FALSE, entry.thread));
          matches = owner && GetProcessIdOfThread(owner.get()) == GetCurrentProcessId() &&
              ThreadCreationTime(owner.get()) == old.threadCreated && WaitForSingleObject(owner.get(), 0) == WAIT_TIMEOUT;
          if (matches)
            for (std::size_t k = 0; k < old.values.size(); ++k)
            {
              std::uintptr_t actual{};
              matches &= ReadValue(self, candidate.anchor, old.slotOffsets[k], actual) && actual == old.values[k];
            }
          matches = matches && WaitForSingleObject(owner.get(), 0) == WAIT_TIMEOUT;
        }
        if (matches)
          result.retained.Add(candidate);
        else
          ++result.unconfirmed;
      }
    }
    else
      result.unconfirmed += candidates.size();

    for (const auto &thread : current.threads)
    {
      const auto &witness = thread.vectorWitness;
      if (!witness.threadCreated || !witness.values.back())
        continue;
      if (std::any_of(Vectors.begin(), Vectors.end(), [&](const auto &entry) {
            return !entry.released && entry.thread == thread.id && entry.witness.threadCreated == witness.threadCreated &&
                entry.witness.allocation.anchor == witness.allocation.anchor;
          }))
        continue;
      if (Vectors.size() == limit)
      {
        result.limitReached = true;
        break;
      }
      Vectors.push_back({witness, thread.id});
    }
    result.trackedVectors = Vectors.size();
#else
    (void)current;
#endif
    return result;
  }

private:
  struct Vector
  {
    WindowsTlsVectorWitness witness;
    std::uint32_t thread{};
    bool replaced{}, released{};
  };
  std::vector<Vector> Vectors;
};
} // namespace cutum
