#pragma once

#include "Core/Memory/WindowsProcessMemory.h"
#include "Test/IoCodecAllocationFixture.h"
#include <array>
#include <memory>

namespace cutum::windows_io_resource_fixture
{
#ifdef _WIN32
using io_codec_fixture::Require;
using Json = nlohmann::json;

struct CloseFile
{
  void operator()(HANDLE handle) const noexcept { CloseHandle(handle); }
};
struct CloseSearch
{
  void operator()(HANDLE handle) const noexcept { FindClose(handle); }
};
using File = std::unique_ptr<void, CloseFile>;
using Search = std::unique_ptr<void, CloseSearch>;

struct Sample
{
  WindowsProcessMemory memory;
  DWORD handles{};
  std::size_t validated{};

  Json Report() const
  {
    return {{"process_handles", handles}, {"validated_handles", validated}, {"working_set_bytes", memory.workingSet},
        {"peak_working_set_bytes", memory.peakWorkingSet}, {"private_commit_bytes", memory.privateCommit},
        {"peak_private_commit_bytes", memory.peakPrivateCommit}, {"paged_pool_quota_bytes", memory.pagedPoolQuota},
        {"peak_paged_pool_quota_bytes", memory.peakPagedPoolQuota}, {"nonpaged_pool_quota_bytes", memory.nonpagedPoolQuota},
        {"peak_nonpaged_pool_quota_bytes", memory.peakNonpagedPoolQuota}};
  }
};

inline Sample Observe(const HANDLE *selection = nullptr, std::size_t selectionSize = 0)
{
  const HANDLE self = GetCurrentProcess();
  Sample result;
  result.memory = ReadWindowsProcessMemory();
  Require(result.memory.observed && GetProcessHandleCount(self, &result.handles) && result.handles,
          "sample current process memory, pool charges and handles");
  for (std::size_t i = 0; i < selectionSize; ++i)
  {
    DWORD flags{};
    Require(GetHandleInformation(selection[i], &flags) != 0, "validate each selected file handle");
    ++result.validated;
  }
  return result;
}

inline Json Run(const std::filesystem::path &file)
{
  constexpr std::size_t count = 96;
  const auto root = file.parent_path() / "windows-search";
  Require(std::filesystem::create_directory(root), "create isolated search fixture");
  std::ofstream(root / "first.bin").put('1');
  std::ofstream(root / "second.bin").put('2');
  const auto pattern = root / L"*.bin";
  const auto absent = root / L"absent*.bin";
  const auto open = [&] {
    const auto handle = CreateFileW(file.c_str(), GENERIC_READ, FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE,
                                  nullptr, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr);
    Require(handle != INVALID_HANDLE_VALUE, "open an independent Win32 file object");
    return File(handle);
  };
  const auto position = [](HANDLE handle) {
    LARGE_INTEGER current{};
    Require(SetFilePointerEx(handle, {}, &current, FILE_CURRENT) != 0, "observe the file object's current position");
    return current.QuadPart;
  };
  LARGE_INTEGER one{};
  one.QuadPart = 1;
  // Initialize the memory and handle readers before sampling the quiescent process.
  (void)Observe();
  const auto baseline = Observe();

  Sample independent, duplicate, lastDuplicate, search, iterators, lastIterator;
  {
    std::array<File, count> files;
    std::array<HANDLE, count> handles{};
    for (std::size_t i = 0; i < count; ++i)
    {
      files[i] = open();
      handles[i] = files[i].get();
    }
    BY_HANDLE_FILE_INFORMATION identity{}, other{};
    Require(GetFileInformationByHandle(handles.front(), &identity) &&
                GetFileInformationByHandle(handles.back(), &other) &&
                identity.dwVolumeSerialNumber == other.dwVolumeSerialNumber &&
                identity.nFileIndexHigh == other.nFileIndexHigh && identity.nFileIndexLow == other.nFileIndexLow &&
                SetFilePointerEx(handles.front(), one, nullptr, FILE_BEGIN) &&
                position(handles.front()) == 1 && position(handles.back()) == 0,
            "independent opens of the same disk file have distinct kernel objects");
    independent = Observe(handles.data(), handles.size());
    Require(independent.validated == count && independent.handles == baseline.handles + count,
            "the actual process table contains every independent file handle");
  }
  const auto afterIndependent = Observe();
  Require(afterIndependent.handles == baseline.handles, "close all independent kernel handles");
  {
    auto original = open();
    std::array<File, count> files;
    std::array<HANDLE, count + 1> handles{};
    handles[0] = original.get();
    for (std::size_t i = 0; i < count; ++i)
    {
      HANDLE copied{};
      Require(DuplicateHandle(GetCurrentProcess(), original.get(), GetCurrentProcess(), &copied,
                              0, FALSE, DUPLICATE_SAME_ACCESS) != 0, "duplicate a file handle");
      files[i].reset(copied);
      handles[i + 1] = copied;
    }
    Require(SetFilePointerEx(original.get(), one, nullptr, FILE_BEGIN) != 0,
            "move the position of the shared file object after duplication");
    for (const auto &file : files)
      Require(position(file.get()) == 1, "existing duplicates observe the new shared file position");
    duplicate = Observe(handles.data(), handles.size());
    Require(duplicate.validated == count + 1 && duplicate.handles == baseline.handles + count + 1,
            "each duplicate occupies a process handle entry");
    original.reset();
    for (std::size_t i = 1; i < count; ++i)
      files[i].reset();
    lastDuplicate = Observe(&handles[1], 1);
    char marker{};
    DWORD actual{};
    Require(lastDuplicate.handles == baseline.handles + 1 && lastDuplicate.validated == 1 &&
                position(files.front().get()) == 1 && SetFilePointerEx(files.front().get(), {}, nullptr, FILE_BEGIN) &&
                ReadFile(files.front().get(), &marker, 1, &actual, nullptr) && actual == 1 && marker == 'F',
            "the last duplicate keeps the file open after the original handle closes");
  }
  const auto afterDuplicate = Observe();
  Require(afterDuplicate.handles == baseline.handles, "close the last handle to the shared file object");
  {
    std::array<Search, count> searches;
    WIN32_FIND_DATAW entry{};
    for (std::size_t i = 0; i < count; ++i)
    {
      const auto handle = FindFirstFileExW(pattern.c_str(), FindExInfoBasic, &entry,
                                           FindExSearchNameMatch, nullptr, 0);
      Require(handle != INVALID_HANDLE_VALUE, "open an independent Win32 search");
      searches[i].reset(handle);
    }
    Require(FindNextFileW(searches.front().get(), &entry) &&
                !FindNextFileW(searches.front().get(), &entry) && GetLastError() == ERROR_NO_MORE_FILES,
            "advance a Win32 search to exhaustion before closing it");
    search = Observe();
  }
  WIN32_FIND_DATAW ignored{};
  const auto missed = FindFirstFileExW(absent.c_str(), FindExInfoBasic, &ignored, FindExSearchNameMatch, nullptr, 0);
  const auto missCode = GetLastError();
  if (missed != INVALID_HANDLE_VALUE)
    FindClose(missed);
  const auto afterSearch = Observe();
  Require(missed == INVALID_HANDLE_VALUE && missCode == ERROR_FILE_NOT_FOUND &&
              afterSearch.handles == baseline.handles,
          "exhausted, unvisited and unmatched searches leave no live process handle");
  {
    std::array<std::filesystem::directory_iterator, count> owned;
    for (auto &iterator : owned)
      iterator = std::filesystem::directory_iterator(root);
    iterators = Observe();
    Require(iterators.handles == baseline.handles + count, "observe every held STL directory search");
    auto alias = owned.front();
    for (auto &iterator : owned)
      iterator = {};
    lastIterator = Observe();
    Require(lastIterator.handles == baseline.handles + 1 && alias != std::filesystem::directory_iterator{},
            "iterator copies share the surviving search owner");
  }
  const auto afterIterators = Observe();
  Require(afterIterators.handles == baseline.handles, "closing the last iterator retires its search");
  return {{"status", "PASS"}, {"concurrent_owners", count}, {"baseline", baseline.Report()},
      {"independent_files", independent.Report()}, {"after_independent", afterIndependent.Report()},
      {"duplicate_handles", duplicate.Report()}, {"last_duplicate", lastDuplicate.Report()},
      {"after_duplicate", afterDuplicate.Report()}, {"win32_searches", search.Report()},
      {"after_search", afterSearch.Report()}, {"stl_iterators", iterators.Report()},
      {"last_iterator_copy", lastIterator.Report()}, {"after_iterators", afterIterators.Report()},
      {"same_file_independent_objects", true}, {"duplicate_shared_object", true},
      {"unmatched_search_retired", true}, {"per_object_charge_observed", false}};
}
#else
inline nlohmann::json Run(const std::filesystem::path &) { return {{"status", "UNSUPPORTED"}}; }
#endif
} // namespace cutum::windows_io_resource_fixture
