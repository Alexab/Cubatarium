#pragma once

#include "Core/Memory/WindowsExecutableTlsStorage.h"
#include "Core/Memory/WindowsImageMemory.h"
#include <memory>
#if CUTUM_WINDOWS_STATIC_THREAD_CRT_PROFILE
#include <winternl.h>
#endif

namespace cutum
{
struct WindowsTlsBlockStorage
{
  std::size_t blocks{}, dataBytes{}, overheadBytes{};

  void Add(const NativeThreadTlsBlock &block) noexcept
  {
    ++blocks;
    dataBytes += block.dataBytes;
    overheadBytes += block.overheadBytes;
  }
};

struct WindowsModuleTlsRow
{
  std::string filename;
  std::size_t templateBytes{};
  WindowsTlsBlockStorage storage;
};

enum class WindowsThreadTlsState { Unobserved, Changed, VectorNotInHeap, VectorTooSmall, NoVector, Observed };
enum class WindowsDynamicTlsState { Unobserved, Absent, Observed };

struct WindowsTlsVectorWitness
{
  NativeThreadTlsBlock allocation;
  std::uint64_t threadCreated{};
  std::array<std::size_t, 2> slotOffsets{};
  std::array<std::uintptr_t, 2> values{};
};

struct WindowsThreadTlsRow
{
  std::uint32_t id{};
  std::size_t kind{NativeThreadNames.size()};
  WindowsTlsBlockStorage vector, blocks;
  WindowsTlsBlockStorage dynamicExpansion;
  WindowsTlsVectorWitness vectorWitness;
  WindowsThreadTlsState state{WindowsThreadTlsState::Unobserved};
  WindowsDynamicTlsState dynamicState{WindowsDynamicTlsState::Unobserved};

  bool Stable() const { return state == WindowsThreadTlsState::Observed || state == WindowsThreadTlsState::NoVector; }
};

struct WindowsModuleTlsStorage
{
  std::vector<WindowsModuleTlsRow> modules;
  std::vector<WindowsThreadTlsRow> threads;
  std::size_t processThreads{}, stableThreads{}, threadsWithVector{}, threadsWithoutVector{};
  bool supported{}, complete{}, tebVerified{}, moduleListStable{}, threadListStable{}, heapWalkFinished{};
};

#if CUTUM_WINDOWS_STATIC_THREAD_CRT_PROFILE
namespace windows_module_tls_detail
{
constexpr std::size_t ThreadLimit = 512, TlsIndexLimit = 4096;
// Windows x64 TEB ThreadLocalStoragePointer. Verify the profile against the
// calling thread, the executable TLS declaration and a linked TLS anchor.
constexpr std::size_t StaticTlsVectorOffset = 0x58;
constexpr std::size_t DynamicExpansionOffset = offsetof(TEB, TlsExpansionSlots);
constexpr std::size_t InlineDynamicTlsSlots = 64, ExpansionDynamicTlsSlots = 1024;

struct CloseHandleOwner
{
  void operator()(void *handle) const noexcept { CloseHandle(handle); }
};
using NativeHandle = std::unique_ptr<void, CloseHandleOwner>;

inline std::vector<DWORD> ReadThreadIds(DWORD process)
{
  const auto raw = CreateToolhelp32Snapshot(TH32CS_SNAPTHREAD, 0);
  if (raw == INVALID_HANDLE_VALUE)
    return {};
  const NativeHandle snapshot(raw);
  std::vector<DWORD> ids;
  THREADENTRY32 entry{};
  entry.dwSize = sizeof(entry);
  for (auto success = Thread32First(snapshot.get(), &entry); success; success = Thread32Next(snapshot.get(), &entry))
  {
    if (entry.dwSize < offsetof(THREADENTRY32, th32OwnerProcessID) + sizeof(entry.th32OwnerProcessID))
      return {};
    if (entry.th32OwnerProcessID == process)
    {
      if (ids.size() == ThreadLimit)
        return {};
      ids.push_back(entry.th32ThreadID);
    }
    entry.dwSize = sizeof(entry);
  }
  if (GetLastError() != ERROR_NO_MORE_FILES)
    return {};
  std::sort(ids.begin(), ids.end());
  return ids;
}

struct ThreadBasicInformation
{
  LONG exitStatus;
  void *teb;
  HANDLE processId, threadId;
  ULONG_PTR affinityMask;
  LONG priority, basePriority;
};
static_assert(sizeof(ThreadBasicInformation) == 48);
using QueryThread = LONG (NTAPI *)(HANDLE, ULONG, void *, ULONG, ULONG *);

inline std::uintptr_t ReadThreadTeb(QueryThread query, HANDLE thread, DWORD process, DWORD id) noexcept
{
  ThreadBasicInformation info{};
  ULONG bytes{};
  if (query(thread, 0, &info, sizeof(info), &bytes) < 0 || bytes != sizeof(info) ||
      reinterpret_cast<ULONG_PTR>(info.processId) != process || reinterpret_cast<ULONG_PTR>(info.threadId) != id)
    return 0;
  return reinterpret_cast<std::uintptr_t>(info.teb);
}

template<class T> bool ReadValue(HANDLE self, std::uintptr_t address, std::size_t offset, T &value) noexcept
{
  SIZE_T bytes{};
  return address && offset <= UINTPTR_MAX - address && sizeof(T) <= UINTPTR_MAX - address - offset &&
      ReadProcessMemory(self, reinterpret_cast<const void *>(address + offset), &value, sizeof(T), &bytes) &&
      bytes == sizeof(T);
}

struct ThreadSample
{
  NativeHandle handle;
  std::uintptr_t teb{}, vector{};
  std::vector<std::uintptr_t> slots;
  NativeThreadTlsBlock vectorBlock, dynamicBlock;
  std::uintptr_t dynamicExpansion{};
  bool vectorRead{}, slotsRead{}, dynamicRead{};
};

struct HeapTarget : NativeThreadTlsBlock
{
  std::size_t thread{}, module{};
};

inline bool MatchesLoaderList(const WindowsImageMemory &images, HANDLE self) noexcept
{
  const auto now = windows_image_memory_detail::ReadLoaderModules(self);
  return now.observed && now.count == images.loaderModules &&
      std::all_of(images.images.begin(), images.images.end(), [&](const auto &image) {
        return !image.loaderModule || std::binary_search(now.entries.begin(), now.entries.begin() + now.count,
            reinterpret_cast<HMODULE>(image.allocation), windows_image_memory_detail::ModuleLess);
      });
}

inline bool MatchesModuleIndices(HANDLE self, const std::vector<const WindowsImageMemoryRow *> &modules) noexcept
{
  return std::all_of(modules.begin(), modules.end(), [self](const auto *module) {
    DWORD index{};
    return ReadValue(self, module->tlsIndexAddress, 0, index) && index == module->tlsIndex;
  });
}

inline std::uint64_t ThreadCreationTime(HANDLE thread) noexcept
{
  FILETIME created{}, exited{}, kernel{}, user{};
  if (!GetThreadTimes(thread, &created, &exited, &kernel, &user))
    return 0;
  return (std::uint64_t{created.dwHighDateTime} << 32) | created.dwLowDateTime;
}
} // namespace windows_module_tls_detail
#endif

inline WindowsModuleTlsStorage ObserveWindowsModuleTls(const WindowsImageMemory &images)
{
  WindowsModuleTlsStorage result;
#if CUTUM_WINDOWS_STATIC_THREAD_CRT_PROFILE
  using namespace windows_module_tls_detail;
  result.supported = true;
  const auto self = GetCurrentProcess();
  if (!images.tlsHeadersComplete || !MatchesLoaderList(images, self))
    return result;
  std::vector<const WindowsImageMemoryRow *> modules;
  const WindowsImageMemoryRow *executable{};
  for (const auto &image : images.images)
    if (image.loaderModule && image.tls == WindowsImageTlsState::Declared)
    {
      if (!image.tlsIndexAddress || image.tlsIndex >= TlsIndexLimit || image.tlsRaw > SIZE_MAX - image.tlsZeroFill)
        return result;
      modules.push_back(&image);
      if (image.executable)
        executable = &image;
    }
  if (!executable || !MatchesModuleIndices(self, modules))
    return result;
  std::sort(modules.begin(), modules.end(), [](const auto *a, const auto *b) { return a->tlsIndex < b->tlsIndex; });
  if (std::adjacent_find(modules.begin(), modules.end(), [](const auto *a, const auto *b) {
        return a->tlsIndex == b->tlsIndex;
      }) != modules.end())
    return result;

  const auto query = reinterpret_cast<QueryThread>(GetProcAddress(GetModuleHandleW(L"ntdll.dll"), "NtQueryInformationThread"));
  if (!query)
    return result;
  const auto process = GetCurrentProcessId();
  const auto current = GetCurrentThreadId();
  const auto teb = ReadThreadTeb(query, GetCurrentThread(), process, current);
  const auto marker = native_thread_stack_detail::CallingExecutableTlsAnchor();
  std::uintptr_t ownVector{}, ownBlock{};
  if (!teb || teb != reinterpret_cast<std::uintptr_t>(NtCurrentTeb()) ||
      !ReadValue(self, teb, StaticTlsVectorOffset, ownVector) ||
      !ReadValue(self, ownVector, executable->tlsIndex * sizeof(void *), ownBlock) ||
      !ownBlock || marker < ownBlock || marker - ownBlock >= executable->tlsRaw + executable->tlsZeroFill)
    return result;
  result.tebVerified = true;

  const auto ids = ReadThreadIds(process);
  if (!std::binary_search(ids.begin(), ids.end(), current))
    return result;
  result.processThreads = ids.size();
  result.threads.resize(ids.size());
  for (const auto *module : modules)
    result.modules.push_back({module->filename, static_cast<std::size_t>(module->tlsRaw + module->tlsZeroFill), {}});
  std::vector<ThreadSample> samples(ids.size());
  std::vector<HeapTarget> targets;
  const auto staticVectorTarget = modules.size();
  const auto dynamicExpansionTarget = modules.size() + 1;
  targets.reserve(ids.size() * (modules.size() + 2));
  for (std::size_t i = 0; i < ids.size(); ++i)
  {
    auto &row = result.threads[i];
    row.id = ids[i];
    row.kind = native_thread_stack_detail::RegisteredThreadKind(row.id);
    auto &sample = samples[i];
    sample.handle.reset(OpenThread(THREAD_QUERY_INFORMATION | SYNCHRONIZE, FALSE, row.id));
    if (!sample.handle)
      continue;
    sample.teb = ReadThreadTeb(query, sample.handle.get(), process, row.id);
    sample.dynamicRead = ReadValue(self, sample.teb, DynamicExpansionOffset, sample.dynamicExpansion);
    if (sample.dynamicRead && sample.dynamicExpansion)
      targets.push_back({{sample.dynamicExpansion}, i, dynamicExpansionTarget});
    sample.vectorRead = ReadValue(self, sample.teb, StaticTlsVectorOffset, sample.vector);
    if (!sample.vectorRead || !sample.vector)
      continue;
    sample.slots.resize(modules.size());
    sample.slotsRead = true;
    targets.push_back({{sample.vector}, i, staticVectorTarget});
    for (std::size_t m = 0; m < modules.size(); ++m)
    {
      sample.slotsRead &= ReadValue(self, sample.vector, modules[m]->tlsIndex * sizeof(void *), sample.slots[m]);
      if (sample.slots[m])
        targets.push_back({{sample.slots[m]}, i, m});
    }
  }
  if (targets.empty())
    return result;
  const auto heap = ScanWindowsTlsHeapBlocks(targets.data(), targets.data() + targets.size());
  result.heapWalkFinished = heap.finished;
  result.moduleListStable = MatchesLoaderList(images, self) && MatchesModuleIndices(self, modules);
  result.threadListStable = ReadThreadIds(process) == ids;
  if (!heap.finished || !result.moduleListStable)
    return result;

  for (const auto &target : targets)
    if (target.module == staticVectorTarget)
      samples[target.thread].vectorBlock = target;
    else if (target.module == dynamicExpansionTarget)
      samples[target.thread].dynamicBlock = target;
  const auto vectorBytes = (modules.back()->tlsIndex + 1) * sizeof(void *);
  for (std::size_t i = 0; i < samples.size(); ++i)
  {
    const auto &sample = samples[i];
    const auto &heapVector = sample.vectorBlock;
    auto &row = result.threads[i];
    std::uintptr_t expansion{};
    if (sample.dynamicRead && WaitForSingleObject(sample.handle.get(), 0) == WAIT_TIMEOUT &&
        ReadValue(self, sample.teb, DynamicExpansionOffset, expansion) && expansion == sample.dynamicExpansion &&
        WaitForSingleObject(sample.handle.get(), 0) == WAIT_TIMEOUT)
    {
      if (!expansion)
        row.dynamicState = WindowsDynamicTlsState::Absent;
      else if (sample.dynamicBlock.base == expansion &&
               sample.dynamicBlock.dataBytes >= ExpansionDynamicTlsSlots * sizeof(void *))
      {
        row.dynamicExpansion.Add(sample.dynamicBlock);
        row.dynamicState = WindowsDynamicTlsState::Observed;
      }
    }
    std::uintptr_t currentVector{};
    if (!sample.vectorRead)
      continue;
    if (WaitForSingleObject(sample.handle.get(), 0) != WAIT_TIMEOUT ||
        !ReadValue(self, sample.teb, StaticTlsVectorOffset, currentVector) || currentVector != sample.vector)
    {
      row.state = WindowsThreadTlsState::Changed;
      continue;
    }
    if (!currentVector)
    {
      if (WaitForSingleObject(sample.handle.get(), 0) == WAIT_TIMEOUT)
      {
        row.state = WindowsThreadTlsState::NoVector;
        ++result.threadsWithoutVector;
      }
      else
        row.state = WindowsThreadTlsState::Changed;
      continue;
    }
    if (!heapVector.base)
    {
      row.state = WindowsThreadTlsState::VectorNotInHeap;
      continue;
    }
    const auto vectorOffset = sample.vector - heapVector.base;
    if (vectorOffset > heapVector.dataBytes || vectorBytes > heapVector.dataBytes - vectorOffset)
    {
      row.state = WindowsThreadTlsState::VectorTooSmall;
      continue;
    }
    if (!sample.slotsRead)
      continue;
    bool stable = true;
    for (std::size_t m = 0; m < modules.size(); ++m)
    {
      std::uintptr_t block{};
      stable &= ReadValue(self, currentVector, modules[m]->tlsIndex * sizeof(void *), block) && block == sample.slots[m];
    }
    if (!stable || WaitForSingleObject(sample.handle.get(), 0) != WAIT_TIMEOUT)
    {
      row.state = WindowsThreadTlsState::Changed;
      continue;
    }
    row.state = WindowsThreadTlsState::Observed;
    row.vector.Add(heapVector);
    row.vectorWitness.allocation = heapVector;
    row.vectorWitness.threadCreated = ThreadCreationTime(sample.handle.get());
    std::size_t witness = 0;
    for (std::size_t m = 0; m < modules.size() && witness < row.vectorWitness.values.size(); ++m)
      if (sample.slots[m])
      {
        row.vectorWitness.values[witness] = sample.slots[m];
        row.vectorWitness.slotOffsets[witness++] = modules[m]->tlsIndex * sizeof(void *);
      }
    ++result.threadsWithVector;
  }
  result.stableThreads = result.threadsWithVector + result.threadsWithoutVector;
  result.moduleListStable = MatchesLoaderList(images, self) && MatchesModuleIndices(self, modules);
  result.threadListStable &= ReadThreadIds(process) == ids;
  if (!result.moduleListStable)
    return result;
  for (const auto &target : targets)
    if (target.module < modules.size() && target.base && result.threads[target.thread].state == WindowsThreadTlsState::Observed)
    {
      auto &module = result.modules[target.module];
      const auto offset = target.anchor - target.base;
      if (offset <= target.dataBytes && module.templateBytes <= target.dataBytes - offset)
      {
        module.storage.Add(target);
        result.threads[target.thread].blocks.Add(target);
      }
    }
  result.complete = result.threadListStable && result.stableThreads == result.processThreads &&
      std::all_of(result.modules.begin(), result.modules.end(), [&](const auto &module) {
        return module.storage.blocks == result.threadsWithVector;
      });
#else
  (void)images;
#endif
  return result;
}
} // namespace cutum
