#pragma once

#include "NativeThreadStack.h"
#include "WindowsCrtThreadStorage.h"
#include <algorithm>
#include <vector>

namespace cutum
{
#if CUTUM_WINDOWS_STATIC_THREAD_CRT_PROFILE
struct WindowsTlsHeapScan
{
  std::size_t matched{};
  bool finished{};
};

enum class WindowsTlsHeapAnchors { DistinctBlocks, AllowAliases };

// Callers keep the anchors live or verify that their owners survived the
// entire walk. The walk handles only integer addresses on the default heap.
// It never locks the registry, allocates or dereferences an anchor.
template<class Block>
inline WindowsTlsHeapScan ScanWindowsTlsHeapBlocks(Block *begin, Block *end,
    WindowsTlsHeapAnchors policy = WindowsTlsHeapAnchors::DistinctBlocks) noexcept
{
  if (begin == end)
    return {};
  const auto less = [](const auto &a, const auto &b) { return a.anchor < b.anchor; };
  std::sort(begin, end, less);
  const bool distinct = policy == WindowsTlsHeapAnchors::DistinctBlocks;
  if (!begin->anchor || (distinct && std::adjacent_find(begin, end, [](const auto &a, const auto &b) {
        return a.anchor == b.anchor;
      }) != end))
    return {};
  const auto heap = GetProcessHeap();
  if (!heap || !HeapLock(heap))
    return {};
  struct Unlock
  {
    HANDLE heap;
    ~Unlock() { HeapUnlock(heap); }
  };
  const Unlock unlock{heap};
  WindowsTlsHeapScan result;
  const auto count = static_cast<std::size_t>(end - begin);
  PROCESS_HEAP_ENTRY entry{};
  while (HeapWalk(heap, &entry))
  {
    if (!(entry.wFlags & PROCESS_HEAP_ENTRY_BUSY))
      continue;
    const auto base = reinterpret_cast<std::uintptr_t>(entry.lpData);
    const auto row = std::lower_bound(begin, end, base, [](const auto &block, std::uintptr_t address) {
      return block.anchor < address;
    });
    if (row == end || row->anchor - base >= entry.cbData)
      continue;
    if (distinct && row + 1 != end && (row + 1)->anchor - base < entry.cbData)
      return {};
    for (auto match = row; match != end && match->anchor - base < entry.cbData; ++match)
    {
      if (match->base)
        return {};
      match->base = base;
      match->dataBytes = entry.cbData;
      match->overheadBytes = entry.cbOverhead;
      ++result.matched;
    }
    if (result.matched == count)
    {
      result.finished = true;
      return result;
    }
  }
  result.finished = GetLastError() == ERROR_NO_MORE_ITEMS;
  return result;
}

inline bool ReadWindowsExecutableTlsBlocks(NativeThreadTlsBlock *begin, NativeThreadTlsBlock *end) noexcept
{
  const auto scan = ScanWindowsTlsHeapBlocks(begin, end);
  return scan.finished && scan.matched == static_cast<std::size_t>(end - begin);
}
#endif

enum class ExecutableTlsStatus { Unsupported, Unobserved, CohortChanged, Observed };

struct WindowsExecutableTlsStorage
{
  std::size_t threads{}, dataBytes{}, overheadBytes{}, templateBytes{}, largestBlockBytes{};
  ExecutableTlsStatus status{ExecutableTlsStatus::Unsupported};
};

inline WindowsExecutableTlsStorage ObserveWindowsExecutableTls(const NativeThreadStacks &stacks,
                                                               const WindowsCrtThreadStorage &crt)
{
  WindowsExecutableTlsStorage result;
#if CUTUM_WINDOWS_STATIC_THREAD_CRT_PROFILE
  using namespace native_thread_stack_detail;
  if (!crt.supported || crt.primaryScopes != stacks.registered)
    return result;
  result.status = ExecutableTlsStatus::Unobserved;
  if (!stacks.registered || !crt.tlsTemplatePayload)
    return result;
  std::vector<NativeThreadTlsBlock> blocks(stacks.registered);
  result.status = ExecutableTlsStatus::CohortChanged;
  if (!CopyTlsAnchors(stacks.registryRevision, blocks.data(), blocks.size()))
    return result;
  const bool observed = ReadWindowsExecutableTlsBlocks(blocks.data(), blocks.data() + blocks.size());
  if (!RevisionMatches(stacks.registryRevision))
    return result;
  result.status = ExecutableTlsStatus::Unobserved;
  const auto templatePerThread = crt.tlsTemplatePayload / stacks.registered;
  if (!observed || std::any_of(blocks.begin(), blocks.end(), [templatePerThread](const auto &block) {
        return block.dataBytes < templatePerThread;
      }))
    return result;
  for (const auto &block : blocks)
  {
    result.dataBytes += block.dataBytes;
    result.overheadBytes += block.overheadBytes;
    result.largestBlockBytes = std::max(result.largestBlockBytes, block.dataBytes);
  }
  result.threads = stacks.registered;
  result.templateBytes = crt.tlsTemplatePayload;
  result.status = ExecutableTlsStatus::Observed;
#else
  (void)stacks;
  (void)crt;
#endif
  return result;
}
} // namespace cutum
