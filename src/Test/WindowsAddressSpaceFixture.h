#pragma once

#include "App/Platform/WindowsMemoryCheckpoint.h"
#include <memory>
#include <stdexcept>

namespace cutum::windows_address_space_fixture
{
#ifdef _WIN32
inline void Require(bool condition, const char *message)
{
  if (!condition)
    throw std::runtime_error(message);
}

inline nlohmann::json Run()
{
  constexpr std::size_t extent = 8 * 1024 * 1024;
  SYSTEM_INFO system{};
  GetSystemInfo(&system);
  const auto page = system.dwPageSize;
  Require(page && extent % page == 0, "use complete Windows pages in the controlled address ranges");
  const auto read = [](const void *address, std::size_t size, std::size_t limit = 65536) {
    const auto base = reinterpret_cast<std::uintptr_t>(address);
    return windows_address_space_detail::ReadRange(base, base + size, limit);
  };
  const auto valid = [](const WindowsAddressSpace &row) {
    return row.complete && row.inspected == row.requested &&
        row.free + row.reserved + row.Committed() == row.inspected &&
        row.guard <= row.Committed() && row.noAccess <= row.Committed();
  };
  const auto touch = [page](void *address, std::size_t size) {
    auto bytes = static_cast<volatile unsigned char *>(address);
    for (std::size_t i = 0; i < size; i += page)
      bytes[i] = 42;
  };

  Require(ReadWindowsProcessMemory().observed, "observe legacy PSAPI process counters");
  const auto baseline = ReadWindowsProcessMemory(/*includeShared=*/true);
  Require(baseline.observed, "observe the ordinary process memory counters in the checkpoint profile");
  const auto release = [](void *address) { VirtualFree(address, 0, MEM_RELEASE); };
  std::unique_ptr<void, decltype(release)> memory(VirtualAlloc(nullptr, 2 * extent, MEM_RESERVE, PAGE_READWRITE), release);
  Require(memory != nullptr, "reserve a controlled private range");
  const auto reserved = read(memory.get(), 2 * extent);
  Require(valid(reserved) && reserved.reserved == 2 * extent, "read an entirely reserved range before commit");
  Require(VirtualAlloc(memory.get(), extent, MEM_COMMIT, PAGE_READWRITE) == memory.get(), "commit half the reserved range");
  touch(memory.get(), extent);
  const auto privateLive = ReadWindowsProcessMemory(/*includeShared=*/true);
  Require(privateLive.observed && privateLive.privateCommit >= baseline.privateCommit + extent,
          "PSAPI accounts for the committed private allocation");
  DWORD oldProtection{};
  Require(VirtualProtect(memory.get(), page, PAGE_READWRITE | PAGE_GUARD, &oldProtection) != 0,
          "install a guard page in an otherwise committed range");
  const auto committed = read(memory.get(), 2 * extent);
  Require(valid(committed) && committed.privateCommitted == extent && committed.reserved == extent && committed.guard == page,
          "the guard is a subset of private committed pages");
  const auto capped = read(memory.get(), 2 * extent, 1);
  Require(!capped.complete && capped.limitReached && capped.regions == 1 && capped.inspected == page && capped.guard == page,
          "reject a truncated address-space walk as complete");
  Require(VirtualFree(memory.get(), extent, MEM_DECOMMIT) != 0, "decommit the original private pages");
  const auto decommitted = read(memory.get(), 2 * extent);
  Require(valid(decommitted) && decommitted.reserved == 2 * extent && !decommitted.Committed(),
          "decommit returns the range to its reserved state");
  const auto privateBase = reinterpret_cast<std::uintptr_t>(memory.get());
  Require(VirtualFree(memory.get(), 0, MEM_RELEASE) != 0, "release the private reservation");
  memory.release();
  const auto released = windows_address_space_detail::ReadRange(privateBase, privateBase + 2 * extent);
  const auto privateRetired = ReadWindowsProcessMemory(/*includeShared=*/true);
  Require(valid(released) && released.free == 2 * extent && privateRetired.observed &&
              privateLive.privateCommit >= privateRetired.privateCommit + extent,
          "observe the private committed bytes and their address space retire");

  const auto close = [](void *handle) { CloseHandle(handle); };
  std::unique_ptr<void, decltype(close)> section(
      CreateFileMappingW(INVALID_HANDLE_VALUE, nullptr, PAGE_READWRITE, 0, static_cast<DWORD>(extent), nullptr), close);
  const auto unmap = [](void *address) { UnmapViewOfFile(address); };
  std::unique_ptr<void, decltype(unmap)> shared(MapViewOfFile(section.get(), FILE_MAP_WRITE, 0, 0, extent), unmap);
  Require(section && shared, "open a pagefile-backed mapping and its shared view");
  touch(shared.get(), extent);
  const auto mapped = read(shared.get(), extent);
  const auto sharedLive = ReadWindowsProcessMemory(/*includeShared=*/true);
  Require(valid(mapped) && mapped.mappedCommitted == extent && sharedLive.observed,
          "report committed mapping bytes separately from the private reservation");
  WindowsAddressSpace cow;
  {
    std::unique_ptr<void, decltype(unmap)> copy(MapViewOfFile(section.get(), FILE_MAP_COPY, 0, 0, extent), unmap);
    auto bytes = static_cast<volatile unsigned char *>(copy.get());
    const auto original = static_cast<volatile unsigned char *>(shared.get());
    Require(copy && bytes[0] == original[0] && bytes[0] == 42, "the second view initially reads the same shared page");
    bytes[0] = 17;
    cow = read(copy.get(), extent);
    Require(bytes[0] == 17 && original[0] == 42 && valid(cow) && cow.mappedCommitted == extent,
            "a private copy still reports the mapping type in VirtualQuery");
  }
  const auto mappedBase = reinterpret_cast<std::uintptr_t>(shared.get());
  Require(UnmapViewOfFile(shared.get()) != 0, "release the shared mapping view");
  shared.release();
  Require(CloseHandle(section.get()) != 0, "release the mapping's kernel handle");
  section.release();
  const auto unmapped = windows_address_space_detail::ReadRange(mappedBase, mappedBase + extent);
  const auto sharedRetired = ReadWindowsProcessMemory(/*includeShared=*/true);
  Require(valid(unmapped) && unmapped.free == extent && sharedRetired.observed, "observe mapping address retirement");

  if (baseline.extendedObserved)
    Require(privateLive.extendedObserved && privateRetired.extendedObserved && sharedLive.extendedObserved && sharedRetired.extendedObserved &&
                privateLive.privateWorkingSet > 0 && privateLive.privateWorkingSet <= privateLive.workingSet &&
                sharedLive.sharedCommit >= privateRetired.sharedCommit + extent && sharedLive.sharedCommit >= sharedRetired.sharedCommit + extent,
            "PSAPI shared commit grows by the actual mapping size and retires after unmap");

  const auto image = read(GetModuleHandleW(nullptr), page);
  const auto full = ReadWindowsAddressSpace();
  Require(valid(image) && image.imageCommitted == page && valid(full) && full.regions > 1 &&
              full.privateCommitted > 0 && full.imageCommitted > 0 && full.free > 0,
          "partition the Windows user address range and identify the executable image");
  return {{"status", "PASS"}, {"page_bytes", page}, {"authored_extent_bytes", extent},
      {"baseline", DescribeWindowsProcessCommit(baseline)}, {"private_live", DescribeWindowsProcessCommit(privateLive)},
      {"private_retired", DescribeWindowsProcessCommit(privateRetired)}, {"shared_live", DescribeWindowsProcessCommit(sharedLive)},
      {"shared_retired", DescribeWindowsProcessCommit(sharedRetired)}, {"full_address_space", DescribeWindowsAddressSpace(full)},
      {"reserved", DescribeWindowsAddressSpace(reserved)}, {"committed", DescribeWindowsAddressSpace(committed)},
      {"truncated", DescribeWindowsAddressSpace(capped)}, {"decommitted", DescribeWindowsAddressSpace(decommitted)},
      {"released", DescribeWindowsAddressSpace(released)}, {"mapped", DescribeWindowsAddressSpace(mapped)},
      {"copy_on_write", DescribeWindowsAddressSpace(cow)}, {"unmapped", DescribeWindowsAddressSpace(unmapped)},
      {"image", DescribeWindowsAddressSpace(image)}};
}
#else
inline nlohmann::json Run() { return {{"status", "UNSUPPORTED"}}; }
#endif
} // namespace cutum::windows_address_space_fixture
