#pragma once

#include "App/Platform/WindowsMemoryCheckpoint.h"
#include "Core/Memory/WindowsCrtThreadStorage.h"
#include <memory>
#include <stdexcept>

namespace cutum::windows_image_memory_fixture
{
#ifdef _WIN32
inline void Require(bool value, const char *message)
{
  if (!value)
    throw std::runtime_error(message);
}

inline nlohmann::json Run()
{
  using namespace windows_image_memory_detail;
  const auto rowAt = [](const WindowsImageMemory &memory, const void *address) -> const WindowsImageMemoryRow * {
    const auto base = reinterpret_cast<std::uintptr_t>(address);
    const auto row = std::find_if(memory.images.begin(), memory.images.end(), [base](const auto &candidate) {
      return candidate.allocation == base;
    });
    return row == memory.images.end() ? nullptr : &*row;
  };
  const auto verify = [](const WindowsImageMemory &memory, const WindowsAddressSpace &space) {
    Require(memory.complete && memory.stableLoaderList && memory.tlsHeadersComplete && !memory.unattributedCommitted,
            "join every committed image span with its working set query and stable module list");
    std::uint64_t committed{};
    for (const auto &row : memory.images)
    {
      Require(!row.filename.empty() && row.committed == row.queried && row.committed % memory.pageBytes == 0 &&
                  row.residentPrivate + row.residentShareable <= row.queried,
              "the resident page counts fit inside each observed image extent");
      committed += row.committed;
    }
    Require(space.complete && committed == space.imageCommitted, "image rows partition the same virtual address scan");
  };

  auto executable = GetModuleHandleW(nullptr);
  std::array<wchar_t, 1024> path{};
  const auto pathLength = GetModuleFileNameW(nullptr, path.data(), static_cast<DWORD>(path.size()));
  Require(executable && pathLength && pathLength < path.size(), "identify the fixture executable");

  WindowsAddressSpace baselineSpace, liveSpace, retiredSpace, limitedSpace;
  const auto baseline = ReadWindowsImageMemory(baselineSpace);
  verify(baseline, baselineSpace);
  const auto *main = rowAt(baseline, executable);
  Require(main && main->executable && main->loaderModule && main->tls == WindowsImageTlsState::Declared,
          "the live Release executable declares a TLS template");
  const auto linked = BoundWindowsCrtThreadStorage(1, 1);
  if (linked.supported)
    Require(main->tlsRaw == linked.tlsRawPerThread && main->tlsZeroFill == linked.tlsZeroFillPerThread,
            "module TLS metadata matches the independently linked executable template");

  constexpr std::array candidates{L"version.dll", L"winmm.dll", L"dwrite.dll", L"pdh.dll"};
  const auto unloaded = std::find_if(candidates.begin(), candidates.end(), [](const auto *name) {
    return !GetModuleHandleW(name);
  });
  Require(unloaded != candidates.end(), "choose a system library absent from the fixture loader list");
  const auto unload = [](void *handle) { FreeLibrary(static_cast<HMODULE>(handle)); };
  const auto close = [](void *handle) { CloseHandle(handle); };
  const auto unmap = [](void *address) { UnmapViewOfFile(address); };
  const auto rawFile = CreateFileW(path.data(), GENERIC_READ, FILE_SHARE_READ | FILE_SHARE_DELETE, nullptr,
                                   OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr);
  Require(rawFile != INVALID_HANDLE_VALUE, "open the fixture executable for read-only image mapping");
  const std::unique_ptr<void, decltype(close)> file(rawFile, close);

  WindowsImageMemory live;
  std::uintptr_t libraryBase{}, viewBase{};
  std::string libraryName;
  std::uint64_t passiveBytes{};
  {
    const std::unique_ptr<void, decltype(unload)> library(LoadLibraryExW(*unloaded, nullptr, LOAD_LIBRARY_SEARCH_SYSTEM32), unload);
    const std::unique_ptr<void, decltype(close)> section(
        CreateFileMappingW(file.get(), nullptr, PAGE_READONLY | SEC_IMAGE_NO_EXECUTE, 0, 0, nullptr), close);
    Require(library && section, "load a Windows library and open a separate nonexecuting image section");
    const std::unique_ptr<void, decltype(unmap)> view(MapViewOfFile(section.get(), FILE_MAP_READ, 0, 0, 0), unmap);
    Require(view && *static_cast<const volatile unsigned short *>(view.get()) == IMAGE_DOS_SIGNATURE,
            "observe the actual read-only PE mapping without loading it as a module");
    live = ReadWindowsImageMemory(liveSpace);
    verify(live, liveSpace);
    const auto *loaded = rowAt(live, library.get());
    const auto *passive = rowAt(live, view.get());
    Require(loaded && loaded->loaderModule && !loaded->executable && !rowAt(baseline, library.get()) &&
                passive && !passive->loaderModule && !passive->executable && passive->filename == main->filename &&
                passive->tls == WindowsImageTlsState::Unobserved && live.loaderModules > baseline.loaderModules,
            "separate a real DLL load from a second mapping of the executable with the same filename");
    libraryBase = reinterpret_cast<std::uintptr_t>(library.get());
    viewBase = reinterpret_cast<std::uintptr_t>(view.get());
    libraryName = loaded->filename;
    passiveBytes = passive->committed;
  }

  const auto retired = ReadWindowsImageMemory(retiredSpace);
  verify(retired, retiredSpace);
  Require(!GetModuleHandleW(*unloaded) && !rowAt(retired, reinterpret_cast<void *>(libraryBase)) &&
              !rowAt(retired, reinterpret_cast<void *>(viewBase)),
          "both the loaded DLL and the separate mapped view disappear on retirement");
  const auto limited = Read(limitedSpace, 0);
  Require(limitedSpace.complete && !limited.complete && limited.pageLimitReached &&
              std::none_of(limited.images.begin(), limited.images.end(), [](const auto &row) { return row.queried != 0; }),
          "a bounded observation distinguishes absent residency data from zero resident pages");
  return {{"status", "PASS"}, {"executable_tls_matches_linked", linked.supported},
      {"controlled_library", libraryName}, {"controlled_passive_bytes", passiveBytes},
      {"baseline", DescribeWindowsImageMemory(baseline)}, {"live", DescribeWindowsImageMemory(live)},
      {"retired", DescribeWindowsImageMemory(retired)}, {"limited", DescribeWindowsImageMemory(limited)},
      {"baseline_image_committed_bytes", baselineSpace.imageCommitted},
      {"live_image_committed_bytes", liveSpace.imageCommitted}, {"retired_image_committed_bytes", retiredSpace.imageCommitted}};
}
#else
inline nlohmann::json Run() { return {{"status", "UNSUPPORTED"}}; }
#endif
} // namespace cutum::windows_image_memory_fixture
