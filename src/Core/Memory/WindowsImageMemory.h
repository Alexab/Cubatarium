#pragma once

#include "Core/Memory/WindowsAddressSpace.h"
#include <array>
#include <string>
#include <string_view>
#include <vector>

namespace cutum
{
enum class WindowsImageTlsState { Unobserved, Absent, Declared };

struct WindowsImageMemoryRow
{
  std::uintptr_t allocation{};
  std::uint64_t committed{}, queried{}, residentPrivate{}, residentShareable{};
  std::uint64_t tlsRaw{}, tlsZeroFill{};
  std::string filename;
  WindowsImageTlsState tls{WindowsImageTlsState::Unobserved};
  bool loaderModule{}, executable{};
};

struct WindowsImageMemory
{
  std::vector<WindowsImageMemoryRow> images;
  std::uint64_t unattributedCommitted{};
  std::size_t pageBytes{}, loaderModules{}, loaderWithoutMapping{};
  bool supported{}, complete{}, stableLoaderList{}, tlsHeadersComplete{}, pageLimitReached{};
};

#ifdef _WIN32
namespace windows_image_memory_detail
{
constexpr std::size_t ImageLimit = 512, WorkingSetBatch = 256, WorkingSetPageLimit = 262144;

struct LoaderModules
{
  std::array<HMODULE, ImageLimit> entries{};
  std::size_t count{};
  bool observed{};
};

inline bool ModuleLess(HMODULE a, HMODULE b) noexcept
{
  return reinterpret_cast<std::uintptr_t>(a) < reinterpret_cast<std::uintptr_t>(b);
}

inline LoaderModules ReadLoaderModules(HANDLE self) noexcept
{
  LoaderModules list;
  DWORD needed{};
  if (EnumProcessModulesEx(self, list.entries.data(), sizeof(list.entries), &needed, LIST_MODULES_ALL) &&
      needed && needed <= sizeof(list.entries) && needed % sizeof(HMODULE) == 0)
  {
    list.count = needed / sizeof(HMODULE);
    std::sort(list.entries.begin(), list.entries.begin() + list.count, ModuleLess);
    list.observed = true;
  }
  return list;
}

inline std::string ReadImageFilename(HANDLE self, HMODULE image, bool loaderModule)
{
  std::array<wchar_t, 1024> path{};
  const DWORD length = loaderModule ? GetModuleFileNameW(image, path.data(), static_cast<DWORD>(path.size())) :
      GetMappedFileNameW(self, image, path.data(), static_cast<DWORD>(path.size()));
  if (!length || length >= path.size() - 1)
    return {};
  std::wstring_view leaf(path.data(), length);
  const auto slash = leaf.find_last_of(L"\\/");
  if (slash != std::wstring_view::npos)
    leaf.remove_prefix(slash + 1);
  const int lengthUtf8 = WideCharToMultiByte(CP_UTF8, WC_ERR_INVALID_CHARS, leaf.data(),
      static_cast<int>(leaf.size()), nullptr, 0, nullptr, nullptr);
  if (!lengthUtf8)
    return {};
  std::string result(static_cast<std::size_t>(lengthUtf8), '\0');
  if (WideCharToMultiByte(CP_UTF8, WC_ERR_INVALID_CHARS, leaf.data(), static_cast<int>(leaf.size()),
                        result.data(), lengthUtf8, nullptr, nullptr) != lengthUtf8)
    return {};
  return result;
}

template<class T> bool ReadImageValue(HANDLE self, std::uintptr_t base, std::size_t extent,
                                      std::size_t offset, T &out) noexcept
{
  SIZE_T bytes{};
  return extent <= UINTPTR_MAX - base && offset <= extent && sizeof(T) <= extent - offset &&
      ReadProcessMemory(self, reinterpret_cast<const void *>(base + offset), &out, sizeof(T), &bytes) && bytes == sizeof(T);
}

inline void ReadImageTls(HANDLE self, HMODULE image, WindowsImageMemoryRow &row) noexcept
{
  MODULEINFO info{};
  if (!GetModuleInformation(self, image, &info, sizeof(info)) || info.lpBaseOfDll != image)
    return;
  const auto base = row.allocation;
  const auto extent = info.SizeOfImage;
  IMAGE_DOS_HEADER dos{};
  IMAGE_NT_HEADERS nt{};
  if (!ReadImageValue(self, base, extent, 0, dos) || dos.e_magic != IMAGE_DOS_SIGNATURE || dos.e_lfanew < 0 ||
      !ReadImageValue(self, base, extent, static_cast<std::size_t>(dos.e_lfanew), nt) ||
      nt.Signature != IMAGE_NT_SIGNATURE || nt.OptionalHeader.Magic != IMAGE_NT_OPTIONAL_HDR_MAGIC ||
      nt.FileHeader.SizeOfOptionalHeader < offsetof(IMAGE_OPTIONAL_HEADER, DataDirectory))
    return;
  if (nt.OptionalHeader.NumberOfRvaAndSizes <= IMAGE_DIRECTORY_ENTRY_TLS)
  {
    row.tls = WindowsImageTlsState::Absent;
    return;
  }
  constexpr auto directoryEnd = offsetof(IMAGE_OPTIONAL_HEADER, DataDirectory) +
      (IMAGE_DIRECTORY_ENTRY_TLS + 1) * sizeof(IMAGE_DATA_DIRECTORY);
  if (nt.FileHeader.SizeOfOptionalHeader < directoryEnd)
    return;
  const auto &directory = nt.OptionalHeader.DataDirectory[IMAGE_DIRECTORY_ENTRY_TLS];
  if (!directory.VirtualAddress && !directory.Size)
  {
    row.tls = WindowsImageTlsState::Absent;
    return;
  }
  IMAGE_TLS_DIRECTORY tls{};
  if (!directory.VirtualAddress || directory.Size < sizeof(tls) ||
      !ReadImageValue(self, base, extent, directory.VirtualAddress, tls) ||
      tls.EndAddressOfRawData < tls.StartAddressOfRawData)
    return;
  const auto raw = tls.EndAddressOfRawData - tls.StartAddressOfRawData;
  if (raw && (tls.StartAddressOfRawData < base || tls.EndAddressOfRawData > base + extent))
    return;
  row.tls = WindowsImageTlsState::Declared;
  row.tlsRaw = raw;
  row.tlsZeroFill = tls.SizeOfZeroFill;
}

struct ImageObserver
{
  WindowsImageMemory &result;
  HANDLE self;
  std::size_t pagesRemaining;

  static void Observe(void *state, std::uintptr_t allocation, std::uintptr_t begin, std::uintptr_t end) noexcept
  {
    auto &observer = *static_cast<ImageObserver *>(state);
    auto &images = observer.result.images;
    const auto bytes = end - begin;
    if (images.empty() || images.back().allocation != allocation)
    {
      if (!allocation || allocation > begin || images.size() == ImageLimit ||
          (!images.empty() && images.back().allocation > allocation))
      {
        observer.result.unattributedCommitted += bytes;
        return;
      }
      images.emplace_back().allocation = allocation;
    }
    auto &image = images.back();
    image.committed += bytes;
    observer.ReadResidentPages(image, begin, bytes);
  }

  void ReadResidentPages(WindowsImageMemoryRow &image, std::uintptr_t begin, std::size_t bytes) noexcept
  {
    const auto page = result.pageBytes;
    if (!page || begin % page || bytes % page)
      return;
    const auto pages = bytes / page;
    const auto admitted = std::min(pages, pagesRemaining);
    pagesRemaining -= admitted;
    result.pageLimitReached |= admitted < pages;
    std::array<PSAPI_WORKING_SET_EX_INFORMATION, WorkingSetBatch> query{};
    for (std::size_t offset = 0; offset < admitted;)
    {
      const auto count = std::min(admitted - offset, query.size());
      for (std::size_t i = 0; i < count; ++i)
        query[i] = {reinterpret_cast<void *>(begin + (offset + i) * page), {}};
      if (QueryWorkingSetEx(self, query.data(), static_cast<DWORD>(count * sizeof(query[0]))))
      {
        image.queried += count * page;
        for (std::size_t i = 0; i < count; ++i)
        {
          const auto &attributes = query[i].VirtualAttributes;
          if (attributes.Valid)
            (attributes.Shared ? image.residentShareable : image.residentPrivate) += page;
        }
      }
      offset += count;
    }
  }
};

inline WindowsImageMemory Read(WindowsAddressSpace &space, std::size_t pageLimit = WorkingSetPageLimit)
{
  WindowsImageMemory result;
  result.supported = true;
  SYSTEM_INFO system{};
  GetSystemInfo(&system);
  result.pageBytes = system.dwPageSize;
  result.images.reserve(ImageLimit);
  const auto self = GetCurrentProcess();
  const auto before = ReadLoaderModules(self);
  const auto executable = GetModuleHandleW(nullptr);
  ImageObserver observer{result, self, pageLimit};
  space = ReadWindowsAddressSpace({&observer, ImageObserver::Observe});

  result.loaderModules = before.count;
  std::size_t matched{};
  std::uint64_t committed = result.unattributedCommitted;
  bool imagesComplete = !result.unattributedCommitted;
  bool tlsComplete = before.observed;
  for (auto &row : result.images)
  {
    const auto image = reinterpret_cast<HMODULE>(row.allocation);
    row.loaderModule = std::binary_search(before.entries.begin(), before.entries.begin() + before.count, image, ModuleLess);
    row.executable = image == executable;
    row.filename = ReadImageFilename(self, image, row.loaderModule);
    if (row.loaderModule)
    {
      ++matched;
      ReadImageTls(self, image, row);
      tlsComplete &= row.tls != WindowsImageTlsState::Unobserved;
    }
    committed += row.committed;
    imagesComplete &= row.committed == row.queried && !row.filename.empty();
  }
  const auto after = ReadLoaderModules(self);
  result.stableLoaderList = before.observed && after.observed && before.count == after.count &&
      std::equal(before.entries.begin(), before.entries.begin() + before.count, after.entries.begin());
  result.loaderWithoutMapping = before.count - matched;
  result.complete = space.complete && result.stableLoaderList && !result.loaderWithoutMapping && imagesComplete &&
      committed == space.imageCommitted;
  result.tlsHeadersComplete = result.stableLoaderList && !result.loaderWithoutMapping && tlsComplete;
  return result;
}
} // namespace windows_image_memory_detail
#endif

inline WindowsImageMemory ReadWindowsImageMemory(WindowsAddressSpace &space)
{
#ifdef _WIN32
  return windows_image_memory_detail::Read(space);
#else
  space = ReadWindowsAddressSpace();
  return {};
#endif
}
} // namespace cutum
