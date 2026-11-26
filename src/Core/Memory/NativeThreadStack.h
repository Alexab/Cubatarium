#pragma once

#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdint>
#include <cstdlib>
#ifdef _WIN32
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#include <tlhelp32.h>
#endif

namespace cutum
{
struct NativeStackStorage
{
  std::size_t reserved{}, committed{}, guard{};
  bool observed{};
};

inline NativeStackStorage ReadNativeStack(const void *anchor) noexcept
{
  NativeStackStorage result;
#ifdef _WIN32
  MEMORY_BASIC_INFORMATION region{};
  if (!VirtualQuery(anchor, &region, sizeof(region)) || region.State != MEM_COMMIT)
    return result;
  const auto allocation = region.AllocationBase;
  auto cursor = reinterpret_cast<std::uintptr_t>(allocation);
  while (VirtualQuery(reinterpret_cast<const void *>(cursor), &region, sizeof(region)))
  {
    if (region.AllocationBase != allocation)
    {
      result.observed = result.reserved != 0 && result.committed != 0;
      break;
    }
    const auto base = reinterpret_cast<std::uintptr_t>(region.BaseAddress);
    if (base > cursor || region.RegionSize > UINTPTR_MAX - base)
      break;
    const auto next = base + region.RegionSize;
    if (next <= cursor || (region.State != MEM_RESERVE && region.State != MEM_COMMIT))
      break;
    const auto bytes = next - cursor;
    result.reserved += bytes;
    if (region.State == MEM_COMMIT)
    {
      result.committed += bytes;
      if (region.Protect & PAGE_GUARD)
        result.guard += bytes;
    }
    cursor = next;
  }
#else
  (void)anchor;
#endif
  return result;
}

inline NativeStackStorage ReadCurrentNativeStack() noexcept
{
  const unsigned char anchor{};
  return ReadNativeStack(&anchor);
}

enum class NativeThreadKind
{
  Main, JobPool, Audio, FrameCapture, FrameWatchdog, FluidSummary, SkyField, Profiler, ProfilerTimer, Count
};
inline constexpr std::array NativeThreadNames{
    "main", "job_pool", "audio", "frame_capture", "frame_watchdog", "fluid_summary", "sky_field",
    "profiler", "profiler_timer"};

struct NativeThreadStackRow
{
  std::size_t entered{}, retired{}, borrowed{}, active{}, peakActive{}, sampled{};
  std::size_t reserved{}, peakReserved{}, committed{}, guard{};
};
using NativeThreadStackRows = std::array<NativeThreadStackRow, NativeThreadNames.size()>;

struct NativeThreadStacks
{
  NativeThreadStackRows rows;
  std::size_t registered{}, matched{}, processThreads{}, registryRevision{};
  bool processThreadsObserved{};
};

struct NativeThreadTlsBlock
{
  std::uintptr_t anchor{}, base{};
  std::size_t dataBytes{}, overheadBytes{};
};

class NativeThreadStackOwner;
namespace native_thread_stack_detail
{
#ifdef _WIN32
inline SRWLOCK Lock = SRWLOCK_INIT;
inline NativeThreadStackOwner *Head{};
inline NativeThreadStackRows Rows{};
inline std::size_t Revision{};

struct RegistryLock
{
  RegistryLock() noexcept { AcquireSRWLockExclusive(&Lock); }
  ~RegistryLock() { ReleaseSRWLockExclusive(&Lock); }
};

inline std::uintptr_t CallingExecutableTlsAnchor() noexcept
{
  static thread_local unsigned char marker{};
  return reinterpret_cast<std::uintptr_t>(&marker);
}

inline bool CopyTlsAnchors(std::size_t revision, NativeThreadTlsBlock *blocks,
                           std::size_t count) noexcept;

inline bool RevisionMatches(std::size_t revision) noexcept
{
  const RegistryLock lock;
  return revision == Revision;
}
#endif

inline bool Enabled() noexcept
{
#ifdef _WIN32
  static const bool enabled = [] {
    const char *directory = std::getenv("CUBA_CATALOG_RUNTIME_DOMAIN");
    return directory && *directory;
  }();
  return enabled;
#else
  return false;
#endif
}
} // namespace native_thread_stack_detail

// Intrusive entry on the owning stack. A nested task can borrow the thread's
// existing registration. Holding the registry lock keeps each sampled entry
// alive through VirtualQuery without reading or suspending the target thread.
class NativeThreadStackOwner
{
public:
  explicit NativeThreadStackOwner(NativeThreadKind kind,
      bool enabled = native_thread_stack_detail::Enabled()) noexcept
  {
#ifdef _WIN32
    if (!enabled)
      return;
    using namespace native_thread_stack_detail;
    const auto id = GetCurrentThreadId();
    Kind = static_cast<std::size_t>(kind);
    const RegistryLock lock;
    auto &row = Rows[Kind];
    for (const auto *owner = Head; owner; owner = owner->Next)
      if (owner->Thread == id)
      {
        ++row.borrowed;
        return;
      }
    const auto stack = ReadNativeStack(this);
    Reservation = stack.observed ? stack.reserved : 0;
    TlsAnchor = CallingExecutableTlsAnchor();
    Thread = id;
    Next = Head;
    Head = this;
    ++Revision;
    ++row.entered;
    row.peakActive = std::max(row.peakActive, ++row.active);
    row.reserved += Reservation;
    row.peakReserved = std::max(row.peakReserved, row.reserved);
#else
    (void)kind;
    (void)enabled;
#endif
  }

  NativeThreadStackOwner(const NativeThreadStackOwner &) = delete;
  NativeThreadStackOwner &operator=(const NativeThreadStackOwner &) = delete;

  ~NativeThreadStackOwner()
  {
#ifdef _WIN32
    if (!Thread)
      return;
    using namespace native_thread_stack_detail;
    const RegistryLock lock;
    auto **position = &Head;
    while (*position != this)
      position = &(*position)->Next;
    *position = Next;
    ++Revision;
    auto &row = Rows[Kind];
    --row.active;
    ++row.retired;
    row.reserved -= Reservation;
#endif
  }

private:
#ifdef _WIN32
  friend NativeThreadStacks InspectNativeThreadStacks() noexcept;
  friend bool native_thread_stack_detail::CopyTlsAnchors(std::size_t, NativeThreadTlsBlock *,
                                                         std::size_t) noexcept;
  DWORD Thread{};
  std::size_t Kind{}, Reservation{};
  std::uintptr_t TlsAnchor{};
  NativeThreadStackOwner *Next{};
#endif
};

#ifdef _WIN32
inline bool native_thread_stack_detail::CopyTlsAnchors(std::size_t revision, NativeThreadTlsBlock *blocks,
                                                      std::size_t count) noexcept
{
  const RegistryLock lock;
  if (revision != Revision)
    return false;
  std::size_t copied = 0;
  for (const auto *owner = Head; owner; owner = owner->Next)
  {
    if (copied == count)
      return false;
    blocks[copied++].anchor = owner->TlsAnchor;
  }
  return copied == count;
}
#endif

inline NativeThreadStacks InspectNativeThreadStacks() noexcept
{
  NativeThreadStacks result{};
#ifdef _WIN32
  using namespace native_thread_stack_detail;
  const RegistryLock lock;
  result.rows = Rows;
  result.registryRevision = Revision;
  for (const auto *owner = Head; owner; owner = owner->Next)
  {
    ++result.registered;
    const auto stack = ReadNativeStack(owner);
    if (stack.observed && stack.reserved == owner->Reservation)
    {
      auto &row = result.rows[owner->Kind];
      ++row.sampled;
      row.committed += stack.committed;
      row.guard += stack.guard;
    }
  }
  const auto snapshot = CreateToolhelp32Snapshot(TH32CS_SNAPTHREAD, 0);
  if (snapshot == INVALID_HANDLE_VALUE)
    return result;
  const auto process = GetCurrentProcessId();
  THREADENTRY32 entry{};
  entry.dwSize = sizeof(entry);
  bool entriesValid = true;
  for (BOOL hasEntry = Thread32First(snapshot, &entry); hasEntry; hasEntry = Thread32Next(snapshot, &entry))
  {
    if (entry.dwSize < offsetof(THREADENTRY32, th32OwnerProcessID) + sizeof(entry.th32OwnerProcessID))
    {
      entriesValid = false;
      break;
    }
    if (entry.th32OwnerProcessID != process)
      continue;
    ++result.processThreads;
    for (const auto *owner = Head; owner; owner = owner->Next)
      if (owner->Thread == entry.th32ThreadID)
      {
        ++result.matched;
        break;
      }
  }
  result.processThreadsObserved = entriesValid && GetLastError() == ERROR_NO_MORE_FILES &&
      result.processThreads != 0;
  CloseHandle(snapshot);
#endif
  return result;
}
} // namespace cutum
