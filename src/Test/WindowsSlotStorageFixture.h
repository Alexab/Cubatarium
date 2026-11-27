#pragma once

#include "App/Platform/WindowsMemoryCheckpoint.h"
#include "Test/IoCodecAllocationFixture.h"

namespace cutum::windows_slot_fixture
{
#if CUTUM_WINDOWS_STATIC_THREAD_CRT_PROFILE
inline unsigned Retired{};

inline void CALLBACK RetirePayload(void *payload)
{
  if (HeapFree(GetProcessHeap(), 0, payload))
    ++Retired;
}

inline void *SetFlsPayload(DWORD index, SIZE_T bytes) noexcept
{
  auto *payload = HeapAlloc(GetProcessHeap(), 0, bytes);
  if (!payload)
    return nullptr;
  if (!FlsSetValue(index, payload))
  {
    HeapFree(GetProcessHeap(), 0, payload);
    return nullptr;
  }
  return FlsGetValue(index) == payload ? payload : nullptr;
}

struct ChildState
{
  void *parent;
  DWORD low, high;
  void *first{}, *second{};
  std::uintptr_t root{};
};

inline void CALLBACK RunChild(void *parameter)
{
  auto &child = *static_cast<ChildState *>(parameter);
  child.first = SetFlsPayload(child.low, 239);
  child.second = SetFlsPayload(child.high, 283);
  windows_module_tls_detail::ReadValue(GetCurrentProcess(), reinterpret_cast<std::uintptr_t>(NtCurrentTeb()),
      windows_slot_detail::FlsDataOffset, child.root);
  SwitchToFiber(child.parent);
}
#endif

inline nlohmann::json Run()
{
#if CUTUM_WINDOWS_STATIC_THREAD_CRT_PROFILE
  using io_codec_fixture::Require;
  using namespace windows_module_tls_detail;
  if (!windows_slot_detail::MatchesProfile(GetModuleHandleW(L"ntdll.dll")))
    return {{"status", "UNSUPPORTED"}};
  struct Indices
  {
    std::vector<DWORD> tls, fls;
    ~Indices()
    {
      for (auto index : tls) TlsFree(index);
      for (auto index : fls) FlsFree(index);
    }
  } indices;
  indices.tls.reserve(InlineDynamicTlsSlots + 1);
  do
  {
    const auto index = TlsAlloc();
    Require(index != TLS_OUT_OF_INDEXES, "allocate an ordinary dynamic TLS index");
    indices.tls.push_back(index);
  } while (indices.tls.back() < InlineDynamicTlsSlots && indices.tls.size() <= InlineDynamicTlsSlots);
  Require(indices.tls.size() >= 4 && indices.tls[2] < InlineDynamicTlsSlots &&
      indices.tls.back() >= InlineDynamicTlsSlots, "obtain three inline and one expanded TLS indices");
  const auto expanded = indices.tls.back();
  const auto inlineBase = indices.tls[0], inlineMetadata = indices.tls[1], inlineStack = indices.tls[2];

  indices.fls.reserve(128);
  do
  {
    const auto index = FlsAlloc(RetirePayload);
    Require(index != FLS_OUT_OF_INDEXES, "allocate an FLS index with a retirement callback");
    indices.fls.push_back(index);
  } while (indices.fls.back() < windows_slot_detail::FirstFlsSlots && indices.fls.size() < 128);
  const auto low = indices.fls.front(), high = indices.fls.back();
  Require(low < windows_slot_detail::FirstFlsSlots && high >= windows_slot_detail::FirstFlsSlots,
      "obtain indices for two distinct FLS chunks");
  const auto retiredAtStart = Retired;
  Require(!IsThreadAFiber(), "start from the normal thread context");
  const auto parent = ConvertThreadToFiber(nullptr);
  Require(parent != nullptr, "convert the fixture thread to the parent fiber");
  struct ConvertBack
  {
    ~ConvertBack() { ConvertFiberToThread(); }
  } convertBack;
  const auto baseline = ObserveWindowsSlots();
  Require(baseline.stable && baseline.flsListObserved && baseline.heapWalkFinished, "observe the stable baseline slot storage");
  auto *parentValue = SetFlsPayload(low, 195);
  Require(parentValue != nullptr, "publish a distinct parent FLS payload");
  ChildState child{parent, low, high};
  const auto deleter = [](void *fiber) { DeleteFiber(fiber); };
  std::unique_ptr<void, decltype(deleter)> fiber(CreateFiber(0, RunChild, &child), deleter);
  Require(fiber != nullptr, "create a child fiber for the independent slot values");
  SwitchToFiber(fiber.get());
  Require(child.first && child.second && child.root && FlsGetValue(low) == parentValue,
      "park the child with its own FLS payloads and restore the parent FLS value");
  const unsigned char stackValue{};
  auto *interior = static_cast<unsigned char *>(child.first) + 8;
  auto *metadata = reinterpret_cast<void *>(child.root + sizeof(void *));
  Require(TlsSetValue(inlineBase, parentValue) && TlsSetValue(inlineMetadata, metadata) &&
      TlsSetValue(inlineStack, const_cast<unsigned char *>(&stackValue)) && TlsSetValue(expanded, interior) &&
      TlsGetValue(expanded) == interior, "publish inline, expansion, interior, metadata and stack slot values");

  NativeThreadTlsBlock payloads[]{{reinterpret_cast<std::uintptr_t>(parentValue)},
      {reinterpret_cast<std::uintptr_t>(child.first)}, {reinterpret_cast<std::uintptr_t>(child.second)}};
  Require(ReadWindowsExecutableTlsBlocks(std::begin(payloads), std::end(payloads)),
      "independently measure the three distinct FLS payload allocations");
  std::size_t payloadBytes{}, payloadOverhead{};
  for (const auto &allocation : payloads)
  {
    payloadBytes += allocation.dataBytes;
    payloadOverhead += allocation.overheadBytes;
  }
  const auto live = ObserveWindowsSlots();
  Require(live.stable && live.processThreads == baseline.processThreads &&
      live.activeFlsRoots == baseline.activeFlsRoots && live.detachedFlsRoots == baseline.detachedFlsRoots + 1 &&
      live.flsHeaders.blocks == baseline.flsHeaders.blocks + 1 &&
      live.flsChunks.blocks >= baseline.flsChunks.blocks + 2 &&
      live.inlineTls.values == baseline.inlineTls.values + 3 &&
      live.inlineTls.inDefaultHeap == baseline.inlineTls.inDefaultHeap + 2 &&
      live.expandedTls.values == baseline.expandedTls.values + 1 &&
      live.expandedTls.inDefaultHeap == baseline.expandedTls.inDefaultHeap + 1 &&
      live.fls.values == baseline.fls.values + 3 && live.fls.inDefaultHeap == baseline.fls.inDefaultHeap + 3 &&
      live.distinctPayload.blocks == baseline.distinctPayload.blocks + 3 &&
      live.distinctPayload.dataBytes == baseline.distinctPayload.dataBytes + payloadBytes &&
      live.distinctPayload.overheadBytes == baseline.distinctPayload.overheadBytes + payloadOverhead &&
      live.payloadAliases == baseline.payloadAliases + 2 && live.metadataAliases == baseline.metadataAliases + 1,
      "verify the exact detached root, cross-family aliases, stack value and physical payload deltas");
  Require(TlsSetValue(inlineMetadata, nullptr) && TlsSetValue(expanded, nullptr),
      "clear TLS aliases to the child before releasing its owner");
  fiber.reset();
  NativeThreadTlsBlock retired[]{{child.root}, {reinterpret_cast<std::uintptr_t>(child.first)},
      {reinterpret_cast<std::uintptr_t>(child.second)}};
  const auto gone = ScanWindowsTlsHeapBlocks(std::begin(retired), std::end(retired));
  Require(Retired == retiredAtStart + 2 && gone.finished && gone.matched == 0 && FlsGetValue(low) == parentValue,
      "verify both FLS callbacks and retirement of the detached header and payloads");
  const auto deleted = ObserveWindowsSlots();
  Require(deleted.stable && deleted.detachedFlsRoots == baseline.detachedFlsRoots &&
      deleted.flsHeaders.blocks == baseline.flsHeaders.blocks && deleted.fls.values == baseline.fls.values + 1 &&
      deleted.distinctPayload.blocks == baseline.distinctPayload.blocks + 1 &&
      deleted.payloadAliases == baseline.payloadAliases + 1 && deleted.metadataAliases == baseline.metadataAliases,
      "the parent block and alias survive after removing the child");
  Require(TlsSetValue(inlineBase, nullptr) && TlsSetValue(inlineStack, nullptr) && FlsFree(low) &&
      Retired == retiredAtStart + 3, "retire the parent payload on FLS index release");
  indices.fls.erase(indices.fls.begin());
  return {{"status", "PASS"}, {"initial", DescribeWindowsSlotStorage(baseline)},
      {"parked_fiber", DescribeWindowsSlotStorage(live)}, {"deleted_fiber", DescribeWindowsSlotStorage(deleted)},
      {"controlled_payload_bytes", payloadBytes}, {"controlled_payload_overhead", payloadOverhead},
      {"retired_child_blocks", std::size(retired)}, {"callbacks", Retired - retiredAtStart}};
#else
  return {{"status", "UNSUPPORTED"}};
#endif
}
} // namespace cutum::windows_slot_fixture
