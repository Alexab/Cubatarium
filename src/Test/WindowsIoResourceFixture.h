#pragma once

#include "App/Platform/WindowsMemoryCheckpoint.h"
#include "Test/IoCodecAllocationFixture.h"
#include <array>
#include <memory>
#ifdef _WIN32
#include <winioctl.h>
#endif

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
  WindowsPlatformResources platform;
  DWORD handles{};
  std::size_t validated{};

  Json Report() const
  {
    return {{"process_handles", handles}, {"validated_handles", validated}, {"working_set_bytes", memory.workingSet},
        {"peak_working_set_bytes", memory.peakWorkingSet}, {"private_commit_bytes", memory.privateCommit},
        {"peak_private_commit_bytes", memory.peakPrivateCommit}, {"paged_pool_quota_bytes", memory.pagedPoolQuota},
        {"peak_paged_pool_quota_bytes", memory.peakPagedPoolQuota}, {"nonpaged_pool_quota_bytes", memory.nonpagedPoolQuota},
        {"peak_nonpaged_pool_quota_bytes", memory.peakNonpagedPoolQuota},
        {"platform_resources", DescribeWindowsPlatformResources(platform)}};
  }
};

inline Sample Observe(const HANDLE *selection = nullptr, std::size_t selectionSize = 0)
{
  const HANDLE self = GetCurrentProcess();
  Sample result;
  result.platform = ObserveWindowsPlatformResources();
  result.memory = ReadWindowsProcessMemory();
  Require(result.memory.observed && GetProcessHandleCount(self, &result.handles) && result.handles,
          "sample current process memory, pool charges and handles");
  if (result.platform.handles.supported)
    Require(result.platform.handles.Complete() && result.platform.handles.entries == result.handles,
            "join the current process handle cohort to its independent total");
  for (std::size_t i = 0; i < selectionSize; ++i)
  {
    DWORD flags{};
    Require(GetHandleInformation(selection[i], &flags) != 0, "validate each selected file handle");
    ++result.validated;
  }
  return result;
}

inline void CheckFileObjects(const Sample &baseline, const Sample &sample, std::size_t entries, std::size_t objects)
{
  if (!baseline.platform.handles.supported)
    return;
  const auto fileTotals = [](const Sample &point) -> std::pair<std::size_t, std::size_t> {
    for (const auto &type : point.platform.handles.types)
      if (type.type == "File")
        return {type.entries, type.objects.value()};
    return {};
  };
  const auto previous = fileTotals(baseline);
  const auto current = fileTotals(sample);
  Require(current.first == previous.first + entries && current.second == previous.second + objects,
          "distinct File objects and handle entries must follow the selected owner lifetime");
}

inline void CheckFileKind(const Sample &baseline, const Sample &sample, WindowsFileKind kind,
                          std::size_t entries, std::size_t objects)
{
  if (!baseline.platform.handles.supported)
    return;
  const auto &before = baseline.platform.handles.files;
  const auto &now = sample.platform.handles.files;
  const auto index = static_cast<std::size_t>(kind);
  Require(before.stable && now.stable && now.kinds[index].entries == before.kinds[index].entries + entries &&
      now.kinds[index].objects == before.kinds[index].objects + objects,
      "validate the File subtype of the selected Windows resource and its aliases");
}

inline void CheckDevice(const Sample &before, const Sample &after, std::uint32_t type,
                        std::size_t entries, std::size_t objects)
{
  const auto find = [type](const Sample &sample) {
    const auto &types = sample.platform.handles.files.deviceTypes;
    const auto entry = types.find(type);
    return entry == types.end() ? WindowsFileResourceRow{} : entry->second;
  };
  const auto a = find(before), b = find(after);
  Require(b.entries == a.entries + entries && b.objects == a.objects + objects,
          "verify the native device type and distinct object count");
}

inline Json CheckWorkingDirectory(const std::filesystem::path &root, const Sample &baseline)
{
#if defined(_WIN64) && defined(_M_X64)
  using namespace windows_file_object_detail;
  if (!baseline.platform.handles.supported || !baseline.platform.handles.files.deviceQueryAvailable)
    return {{"status", "UNSUPPORTED"}};
  const auto original = ReadWorkingDirectory();
  Require(original.has_value(), "match the initial directory path and its exact process handle");
  const auto absolute = std::filesystem::absolute(root);
  struct RestoreDirectory
  {
    std::wstring path;
    ~RestoreDirectory() { if (!path.empty()) SetCurrentDirectoryW(path.c_str()); }
  } restore{original->path + L"\\"};
  Require(SetCurrentDirectoryW(absolute.c_str()) != 0, "change the isolated fixture current directory");
  const auto selected = ReadWorkingDirectory();
  Require(selected && !SamePath(selected->path, original->path) && selected->handle != original->handle,
          "observe the replacement directory and native handle");
  const auto changed = Observe();
  const auto &changedRoot = changed.platform.handles.files.workingDirectory;
  Require(changedRoot && changedRoot->entries == 1 && changedRoot->objects == 1 && !changedRoot->linkedCrtEntries,
          "join the current directory to exactly one live native File object");

  Sample independent, aliased;
  {
    const auto opened = CreateFileW(absolute.c_str(), FILE_READ_ATTRIBUTES,
        FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE, nullptr, OPEN_EXISTING, FILE_FLAG_BACKUP_SEMANTICS, nullptr);
    Require(opened != INVALID_HANDLE_VALUE, "open the same directory independently");
    const File separate(opened);
    FILE_ID_INFO expected{}, actual{};
    Require(GetFileInformationByHandleEx(separate.get(), FileIdInfo, &expected, sizeof(expected)) &&
        GetFileInformationByHandleEx(selected->handle, FileIdInfo, &actual, sizeof(actual)) &&
        expected.VolumeSerialNumber == actual.VolumeSerialNumber &&
        std::memcmp(&expected.FileId, &actual.FileId, sizeof(actual.FileId)) == 0,
        "verify that the independent handle refers to the same filesystem directory");
    WIN32_FIND_DATAW entry{};
    const auto search = FindFirstFileExW((absolute / L"*.bin").c_str(), FindExInfoBasic, &entry,
                                         FindExSearchNameMatch, nullptr, 0);
    Require(search != INVALID_HANDLE_VALUE, "start an independent search on the same directory");
    const Search find(search);
    independent = Observe();
    CheckFileKind(changed, independent, WindowsFileKind::Directory, 2, 2);
    const auto &sameRoot = independent.platform.handles.files.workingDirectory;
    Require(sameRoot && sameRoot->entries == 1 && sameRoot->objects == 1,
            "keep both same-path objects outside the current directory's alias count");

    HANDLE duplicate{};
    Require(DuplicateHandle(GetCurrentProcess(), selected->handle, GetCurrentProcess(), &duplicate, 0, FALSE,
              DUPLICATE_SAME_ACCESS) != 0, "duplicate the actual current directory handle");
    const File alias(duplicate);
    aliased = Observe();
    CheckFileKind(changed, aliased, WindowsFileKind::Directory, 3, 2);
    const auto &aliasRoot = aliased.platform.handles.files.workingDirectory;
    Require(aliasRoot && aliasRoot->entries == 2 && aliasRoot->objects == 1,
            "count the duplicate only on the current directory object");
    CheckDevice(changed, aliased, FILE_DEVICE_DISK, 3, 2);
  }
  const auto retired = Observe();
  CheckFileKind(changed, retired, WindowsFileKind::Directory, 0, 0);
  CheckDevice(changed, retired, FILE_DEVICE_DISK, 0, 0);
  const auto &remaining = retired.platform.handles.files.workingDirectory;
  Require(remaining && remaining->entries == 1 && remaining->objects == 1,
          "retire the duplicate and same-path handles while the selected directory remains");
  Require(SetCurrentDirectoryW(restore.path.c_str()) != 0, "restore the original fixture directory");
  restore.path.clear();
  const auto restored = ReadWorkingDirectory();
  Require(restored && SamePath(restored->path, original->path), "verify restoration of the original directory");
  return {{"status", "PASS"}, {"changed", changed.Report()}, {"independent", independent.Report()},
          {"aliased", aliased.Report()}, {"retired", retired.Report()}, {"restored", true}};
#else
  (void)root;
  (void)baseline;
  return {{"status", "UNSUPPORTED"}};
#endif
}

inline Json CheckDeviceKinds()
{
  const auto cold = Observe();
  if (!cold.platform.handles.supported || !cold.platform.handles.files.deviceQueryAvailable)
    return {{"status", "UNSUPPORTED"}};
  const auto makePipe = [] {
    HANDLE input{}, output{};
    Require(CreatePipe(&input, &output, nullptr, 0) != 0, "create an isolated pair of pipe endpoints");
    return std::array<File, 2>{File(input), File(output)};
  };
  // The first CreatePipe can retain a process-wide named pipe root.
  (void)makePipe();
  const auto before = Observe();
  Sample active;
  {
    const auto handle = CreateFileW(L"NUL", GENERIC_READ | GENERIC_WRITE, FILE_SHARE_READ | FILE_SHARE_WRITE,
                                    nullptr, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr);
    Require(handle != INVALID_HANDLE_VALUE, "open the ordinary null device");
    const File null(handle);
    const auto endpoints = makePipe();
    active = Observe();
    CheckFileKind(before, active, WindowsFileKind::Character, 1, 1);
    CheckFileKind(before, active, WindowsFileKind::Pipe, 2, 2);
    CheckDevice(before, active, FILE_DEVICE_NULL, 1, 1);
    CheckDevice(before, active, FILE_DEVICE_NAMED_PIPE, 2, 2);
  }
  const auto retired = Observe();
  CheckDevice(before, retired, FILE_DEVICE_NULL, 0, 0);
  CheckDevice(before, retired, FILE_DEVICE_NAMED_PIPE, 0, 0);
  return {{"status", "PASS"}, {"cold", cold.Report()}, {"baseline", before.Report()},
          {"active", active.Report()}, {"retired", retired.Report()}};
}

inline Json CheckGuiObjects()
{
  (void)ObserveWindowsPlatformResources();
  const auto before = ObserveWindowsPlatformResources();
  Require(before.gdi && before.user && before.peakGdi && before.peakUser, "read current and peak GDI and USER counts");
  const auto destroyMenu = [](void *menu) { DestroyMenu(static_cast<HMENU>(menu)); };
  const auto deleteBitmap = [](void *bitmap) { DeleteObject(static_cast<HBITMAP>(bitmap)); };
  std::unique_ptr<void, decltype(destroyMenu)> menu(CreatePopupMenu(), destroyMenu);
  std::unique_ptr<void, decltype(deleteBitmap)> bitmap(CreateBitmap(2, 2, 1, 1, nullptr), deleteBitmap);
  Require(menu && bitmap, "create one menu and one memory bitmap");
  const auto live = ObserveWindowsPlatformResources();
  Require(live.gdi == *before.gdi + 1 && live.user == *before.user + 1 &&
      live.peakGdi && *live.peakGdi >= *live.gdi && live.peakUser && *live.peakUser >= *live.user,
      "observe the exact additional GUI resources and both high-water counts");
  Require(DestroyMenu(static_cast<HMENU>(menu.get())) != 0, "destroy the controlled menu");
  menu.release();
  Require(DeleteObject(static_cast<HBITMAP>(bitmap.get())) != 0, "delete the controlled bitmap");
  bitmap.release();
  const auto retired = ObserveWindowsPlatformResources();
  Require(retired.gdi == before.gdi && retired.user == before.user &&
      retired.peakGdi && *retired.peakGdi >= *live.peakGdi && retired.peakUser && *retired.peakUser >= *live.peakUser,
      "observe GUI resource retirement and retained high-water counts");
  return {{"before", DescribeWindowsPlatformResources(before)["gui_objects"]},
      {"live", DescribeWindowsPlatformResources(live)["gui_objects"]},
      {"retired", DescribeWindowsPlatformResources(retired)["gui_objects"]}};
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
  CheckFileObjects(baseline, independent, count, count);
  CheckFileObjects(baseline, duplicate, count + 1, 1);
  CheckFileObjects(baseline, lastDuplicate, 1, 1);
  CheckFileObjects(baseline, search, count, count);
  CheckFileObjects(baseline, iterators, count, count);
  CheckFileObjects(baseline, lastIterator, 1, 1);
  CheckFileKind(baseline, independent, WindowsFileKind::DiskFile, count, count);
  CheckFileKind(baseline, duplicate, WindowsFileKind::DiskFile, count + 1, 1);
  CheckFileKind(baseline, lastDuplicate, WindowsFileKind::DiskFile, 1, 1);
  CheckFileKind(baseline, search, WindowsFileKind::Directory, count, count);
  CheckFileKind(baseline, iterators, WindowsFileKind::Directory, count, count);
  CheckFileKind(baseline, lastIterator, WindowsFileKind::Directory, 1, 1);
  for (const auto *retired : {&afterIndependent, &afterDuplicate, &afterSearch, &afterIterators})
  {
    CheckFileObjects(baseline, *retired, 0, 0);
    CheckFileKind(baseline, *retired, WindowsFileKind::DiskFile, 0, 0);
    CheckFileKind(baseline, *retired, WindowsFileKind::Directory, 0, 0);
  }
  return {{"status", "PASS"}, {"concurrent_owners", count}, {"baseline", baseline.Report()},
      {"independent_files", independent.Report()}, {"after_independent", afterIndependent.Report()},
      {"duplicate_handles", duplicate.Report()}, {"last_duplicate", lastDuplicate.Report()},
      {"after_duplicate", afterDuplicate.Report()}, {"win32_searches", search.Report()},
      {"after_search", afterSearch.Report()}, {"stl_iterators", iterators.Report()},
      {"last_iterator_copy", lastIterator.Report()}, {"after_iterators", afterIterators.Report()},
      {"same_file_independent_objects", true}, {"duplicate_shared_object", true},
      {"gui_lifetime", CheckGuiObjects()},
      {"working_directory_lifetime", CheckWorkingDirectory(root, baseline)}, {"device_lifetime", CheckDeviceKinds()},
      {"unmatched_search_retired", true}, {"per_object_charge_observed", false}};
}
#else
inline nlohmann::json Run(const std::filesystem::path &) { return {{"status", "UNSUPPORTED"}}; }
#endif
} // namespace cutum::windows_io_resource_fixture
