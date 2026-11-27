#pragma once

#include "Core/Memory/WindowsModuleTlsStorage.h"

namespace cutum
{
struct WindowsSlotReferences
{
  std::size_t values{}, inDefaultHeap{};
};

struct WindowsSlotStorage
{
  std::size_t processThreads{}, activeFlsRoots{}, detachedFlsRoots{};
  std::size_t payloadAliases{}, metadataAliases{};
  WindowsSlotReferences inlineTls, expandedTls, fls;
  WindowsTlsBlockStorage expansionArrays, flsHeaders, flsChunks, distinctPayload;
  bool supported{}, flsListObserved{}, heapWalkFinished{}, stable{};
};

#if CUTUM_WINDOWS_STATIC_THREAD_CRT_PROFILE
namespace windows_slot_detail
{
using namespace windows_module_tls_detail;

// This private layout is measured on Windows 10 x64 build 19045. The
// diagnostic checks its complete ring and exact structural heap blocks.
constexpr std::size_t FlsDataOffset = 0x17c8;
constexpr std::size_t FlsChunkCount = 8, FirstFlsSlots = 16, FlsRootLimit = 512;
constexpr std::size_t LargestFlsChunkWords = (FirstFlsSlots << (FlsChunkCount - 1)) + 1;
constexpr std::size_t InlineTlsOffset = offsetof(TEB, TlsSlots);

inline bool MatchesProfile(HMODULE ntdll) noexcept
{
  using QueryVersion = LONG (NTAPI *)(OSVERSIONINFOW *);
  const auto query = reinterpret_cast<QueryVersion>(GetProcAddress(ntdll, "RtlGetVersion"));
  OSVERSIONINFOW version{};
  version.dwOSVersionInfoSize = sizeof(version);
  return query && query(&version) >= 0 && version.dwMajorVersion == 10 &&
      version.dwMinorVersion == 0 && version.dwBuildNumber == 19045;
}

using Words = std::vector<std::uintptr_t>;
using Links = std::array<std::uintptr_t, 2>;
struct FlsHeader
{
  Links links;
  std::array<std::uintptr_t, FlsChunkCount> chunks;
  bool operator==(const FlsHeader &other) const { return links == other.links && chunks == other.chunks; }
};
static_assert(sizeof(FlsHeader) == 80);

inline bool ReadWords(HANDLE self, std::uintptr_t address, std::uintptr_t *words, std::size_t count) noexcept
{
  const auto bytes = count * sizeof(*words);
  SIZE_T read{};
  return address && count <= LargestFlsChunkWords && bytes <= UINTPTR_MAX - address &&
      ReadProcessMemory(self, reinterpret_cast<const void *>(address), words, bytes, &read) &&
      read == bytes;
}

inline bool MatchWords(HANDLE self, std::uintptr_t address, const Words &previous) noexcept
{
  if (previous.empty())
    return true;
  std::array<std::uintptr_t, LargestFlsChunkWords> now;
  return ReadWords(self, address, now.data(), previous.size()) &&
      std::equal(previous.begin(), previous.end(), now.begin());
}

struct ThreadSlots
{
  NativeHandle handle;
  std::uintptr_t teb{}, expansion{}, flsRoot{};
  std::array<std::uintptr_t, InlineDynamicTlsSlots> inlineSlots;
  Words expandedSlots;

  bool Matches(HANDLE self) const noexcept
  {
    std::uintptr_t currentExpansion{}, currentRoot{};
    decltype(inlineSlots) currentInline;
    return WaitForSingleObject(handle.get(), 0) == WAIT_TIMEOUT &&
        ReadValue(self, teb, DynamicExpansionOffset, currentExpansion) && currentExpansion == expansion &&
        ReadValue(self, teb, FlsDataOffset, currentRoot) && currentRoot == flsRoot &&
        ReadValue(self, teb, InlineTlsOffset, currentInline) && currentInline == inlineSlots &&
        MatchWords(self, expansion, expandedSlots) &&
        WaitForSingleObject(handle.get(), 0) == WAIT_TIMEOUT;
  }
};

struct FlsRoot
{
  std::uintptr_t address{};
  FlsHeader header;
  std::array<Words, FlsChunkCount> chunks;
};

struct FlsList
{
  std::uintptr_t head{};
  Links links;
  std::vector<FlsRoot> roots;

  bool Read(HANDLE self, HMODULE ntdll, std::uintptr_t start)
  {
    std::uintptr_t current = start, previous{};
    do
    {
      Links node;
      MEMORY_BASIC_INFORMATION mapping{};
      if (!ReadValue(self, current, 0, node) || !node[0] || !node[1] ||
          (previous && previous != node[1]) ||
          !VirtualQuery(reinterpret_cast<const void *>(current), &mapping, sizeof(mapping)))
        return false;
      if (mapping.Type == MEM_IMAGE && mapping.AllocationBase == ntdll)
      {
        if (head)
          return false;
        head = current;
        links = node;
      }
      else
      {
        if (mapping.Type != MEM_PRIVATE || roots.size() == FlsRootLimit)
          return false;
        auto &root = roots.emplace_back();
        root.address = current;
        if (!ReadValue(self, current, 0, root.header) || root.header.links != node)
          return false;
        for (std::size_t i = 0; i < FlsChunkCount; ++i)
          if (const auto chunk = root.header.chunks[i])
          {
            auto &words = root.chunks[i];
            words.resize((FirstFlsSlots << i) + 1);
            if (!ReadWords(self, chunk, words.data(), words.size()))
              return false;
          }
      }
      previous = current;
      current = node[0];
    } while (current != start);
    return head && !roots.empty() && roots.front().header.links[1] == previous;
  }

  bool Matches(HANDLE self) const noexcept
  {
    Links currentHead;
    if (!ReadValue(self, head, 0, currentHead) || currentHead != links)
      return false;
    for (const auto &root : roots)
    {
      FlsHeader header;
      if (!ReadValue(self, root.address, 0, header) || !(header == root.header))
        return false;
      for (std::size_t i = 0; i < FlsChunkCount; ++i)
        if (!MatchWords(self, header.chunks[i], root.chunks[i]))
          return false;
    }
    return true;
  }
};

enum class SlotKind { InlineTls, ExpandedTls, Fls, ExpansionArray, FlsHeader, FlsChunk };
struct SlotTarget : NativeThreadTlsBlock
{
  SlotKind kind;
  std::size_t structureBytes{};
};

template<class Slots>
inline void AddReferences(std::vector<SlotTarget> &targets, SlotKind kind, const Slots &words, std::size_t first = 0)
{
  for (std::size_t i = first; i < words.size(); ++i)
    if (words[i])
      targets.push_back({{words[i]}, kind});
}

inline bool CheckStructures(const std::vector<SlotTarget> &targets) noexcept
{
  std::uintptr_t lastBase{};
  for (const auto &target : targets)
    if (target.structureBytes)
    {
      if (target.base != target.anchor || target.dataBytes != target.structureBytes || target.base == lastBase)
        return false;
      lastBase = target.base;
    }
  return true;
}

inline void Summarize(WindowsSlotStorage &result, const std::vector<SlotTarget> &targets)
{
  std::uintptr_t base{};
  const SlotTarget *payload{};
  std::size_t references{};
  bool metadata{};
  const auto finishBlock = [&] {
    if (metadata)
      result.metadataAliases += references;
    else if (payload)
    {
      result.distinctPayload.Add(*payload);
      result.payloadAliases += references - 1;
    }
  };
  for (const auto &target : targets)
  {
    if (target.base && target.base != base)
    {
      finishBlock();
      base = target.base;
      payload = nullptr;
      references = 0;
      metadata = false;
    }
    WindowsSlotReferences *slots{};
    switch (target.kind)
    {
      case SlotKind::InlineTls: slots = &result.inlineTls; break;
      case SlotKind::ExpandedTls: slots = &result.expandedTls; break;
      case SlotKind::Fls: slots = &result.fls; break;
      case SlotKind::ExpansionArray: result.expansionArrays.Add(target); break;
      case SlotKind::FlsHeader: result.flsHeaders.Add(target); break;
      case SlotKind::FlsChunk: result.flsChunks.Add(target); break;
    }
    metadata |= target.structureBytes != 0;
    if (!slots)
      continue;
    ++slots->values;
    if (target.base)
    {
      ++slots->inDefaultHeap;
      ++references;
      payload = &target;
    }
  }
  finishBlock();
}
} // namespace windows_slot_detail
#endif

inline WindowsSlotStorage ObserveWindowsSlots(const WindowsModuleTlsStorage *cohort = nullptr)
{
  WindowsSlotStorage result;
#if CUTUM_WINDOWS_STATIC_THREAD_CRT_PROFILE
  using namespace windows_slot_detail;
  const auto ntdll = GetModuleHandleW(L"ntdll.dll");
  if (!ntdll || !MatchesProfile(ntdll))
    return result;
  result.supported = true;
  const auto query = reinterpret_cast<QueryThread>(GetProcAddress(ntdll, "NtQueryInformationThread"));
  const auto self = GetCurrentProcess();
  const auto process = GetCurrentProcessId();
  const auto current = GetCurrentThreadId();
  const auto ids = ReadThreadIds(process);
  if (!query || !std::binary_search(ids.begin(), ids.end(), current))
    return result;

  result.processThreads = ids.size();
  if (cohort && (!cohort->complete || !std::equal(ids.begin(), ids.end(),
          cohort->threads.begin(), cohort->threads.end(), [](auto id, const auto &thread) { return id == thread.id; })))
    return result;
  std::vector<ThreadSlots> threads(ids.size());
  std::vector<std::uintptr_t> active;
  std::vector<SlotTarget> targets;
  for (std::size_t i = 0; i < ids.size(); ++i)
  {
    auto &sample = threads[i];
    sample.handle.reset(OpenThread(THREAD_QUERY_INFORMATION | SYNCHRONIZE, FALSE, ids[i]));
    if (!sample.handle)
      return result;
    sample.teb = ReadThreadTeb(query, sample.handle.get(), process, ids[i]);
    if ((ids[i] == current && sample.teb != reinterpret_cast<std::uintptr_t>(NtCurrentTeb())) ||
        !ReadValue(self, sample.teb, InlineTlsOffset, sample.inlineSlots) ||
        !ReadValue(self, sample.teb, DynamicExpansionOffset, sample.expansion) ||
        !ReadValue(self, sample.teb, FlsDataOffset, sample.flsRoot))
      return result;
    AddReferences(targets, SlotKind::InlineTls, sample.inlineSlots);
    if (sample.expansion)
    {
      sample.expandedSlots.resize(ExpansionDynamicTlsSlots);
      if (!ReadWords(self, sample.expansion, sample.expandedSlots.data(), sample.expandedSlots.size()))
        return result;
      targets.push_back({{sample.expansion}, SlotKind::ExpansionArray, sample.expandedSlots.size() * sizeof(void *)});
      AddReferences(targets, SlotKind::ExpandedTls, sample.expandedSlots);
    }
    if (sample.flsRoot)
      active.push_back(sample.flsRoot);
  }
  std::sort(active.begin(), active.end());
  FlsList fls;
  if (active.empty() || std::adjacent_find(active.begin(), active.end()) != active.end() ||
      !fls.Read(self, ntdll, active.front()))
    return result;
  std::size_t matchedActive{};
  for (const auto &root : fls.roots)
  {
    matchedActive += std::binary_search(active.begin(), active.end(), root.address);
    targets.push_back({{root.address}, SlotKind::FlsHeader, sizeof(FlsHeader)});
    for (std::size_t i = 0; i < FlsChunkCount; ++i)
      if (!root.chunks[i].empty())
      {
        targets.push_back({{root.header.chunks[i]}, SlotKind::FlsChunk, root.chunks[i].size() * sizeof(void *)});
        AddReferences(targets, SlotKind::Fls, root.chunks[i], 1);
      }
  }
  if (matchedActive != active.size())
    return result;
  result.flsListObserved = true;
  result.heapWalkFinished = ScanWindowsTlsHeapBlocks(targets.data(), targets.data() + targets.size(),
      WindowsTlsHeapAnchors::AllowAliases).finished;
  if (!result.heapWalkFinished || !CheckStructures(targets) || !fls.Matches(self) ||
      !std::all_of(threads.begin(), threads.end(), [&](const auto &sample) { return sample.Matches(self); }) ||
      ReadThreadIds(process) != ids)
    return result;
  result.activeFlsRoots = matchedActive;
  result.detachedFlsRoots = fls.roots.size() - matchedActive;
  Summarize(result, targets);
  result.stable = true;
#else
  (void)cohort;
#endif
  return result;
}
} // namespace cutum
