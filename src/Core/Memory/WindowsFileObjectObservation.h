#pragma once

#include "Core/Memory/WindowsProcessMemory.h"
#include <algorithm>
#include <array>
#include <optional>
#include <string>
#include <string_view>

#if defined(_WIN64) && defined(_M_X64)
#include <winternl.h>
#endif

namespace cutum
{
enum class WindowsDiskPathArea { LogDirectory, RuntimeDirectory, LocalAppData, Other, Unobserved, NotDisk };
inline constexpr std::array WindowsDiskPathAreaNames{
    "log_directory", "runtime_directory", "local_app_data", "other", "unobserved"};

#if defined(_WIN64) && defined(_M_X64)
namespace windows_file_object_detail
{
constexpr DWORD PathLimit = 32768;

template<class Read>
inline std::wstring ReadPath(Read read)
{
  std::wstring text(512, L'\0');
  auto length = read(static_cast<DWORD>(text.size()), text.data());
  if (length >= text.size() && length <= PathLimit)
  {
    text.resize(PathLimit);
    length = read(static_cast<DWORD>(text.size()), text.data());
  }
  if (!length || length >= text.size())
    return {};
  text.resize(length);
  return text;
}

inline std::wstring NormalizeOpenedPath(std::wstring path)
{
  if (path.compare(0, 8, L"\\\\?\\UNC\\") == 0)
    path.replace(0, 8, L"\\\\");
  else if (path.compare(0, 4, L"\\\\?\\") == 0)
    path.erase(0, 4);
  std::replace(path.begin(), path.end(), L'/', L'\\');
  while (!path.empty() && path.back() == L'\\')
    path.pop_back();
  return path;
}

inline bool SamePath(std::wstring_view first, std::wstring_view second) noexcept
{
  return !first.empty() && first.size() == second.size() &&
      CompareStringOrdinal(first.data(), static_cast<int>(first.size()), second.data(),
                           static_cast<int>(second.size()), TRUE) == CSTR_EQUAL;
}

inline bool IsUnder(std::wstring_view path, std::wstring_view directory) noexcept
{
  return path.size() >= directory.size() &&
      (path.size() == directory.size() || path[directory.size()] == L'\\') &&
      SamePath(path.substr(0, directory.size()), directory);
}

inline std::wstring ReadOpenedPath(HANDLE file)
{
  return NormalizeOpenedPath(ReadPath([file](DWORD size, wchar_t *data) {
    return GetFinalPathNameByHandleW(file, data, size, FILE_NAME_OPENED | VOLUME_NAME_DOS);
  }));
}

struct PathAreas
{
  std::wstring logs, runtime, localAppData;

  WindowsDiskPathArea Classify(std::wstring_view path) const noexcept
  {
    if (path.empty())
      return WindowsDiskPathArea::Unobserved;
    if (IsUnder(path, logs))
      return WindowsDiskPathArea::LogDirectory;
    if (IsUnder(path, runtime))
      return WindowsDiskPathArea::RuntimeDirectory;
    if (IsUnder(path, localAppData))
      return WindowsDiskPathArea::LocalAppData;
    return WindowsDiskPathArea::Other;
  }
};

inline PathAreas ReadPathAreas()
{
  const auto readEnv = [](const wchar_t *name) {
    return ReadPath([name](DWORD size, wchar_t *data) {
      return GetEnvironmentVariableW(name, data, size);
    });
  };
  const auto absolute = [](const std::wstring &path) {
    if (path.empty())
      return std::wstring{};
    return NormalizeOpenedPath(ReadPath([&](DWORD size, wchar_t *data) {
      return GetFullPathNameW(path.c_str(), size, data, nullptr);
    }));
  };
  PathAreas areas;
  areas.runtime = NormalizeOpenedPath(ReadPath([](DWORD size, wchar_t *data) {
    return GetModuleFileNameW(nullptr, data, size);
  }));
  const auto slash = areas.runtime.find_last_of(L'\\');
  areas.runtime.resize(slash == std::wstring::npos ? 0 : slash);
  const auto logs = readEnv(L"CUBATARIUM_LOG_DIR");
  areas.logs = logs.empty() && !areas.runtime.empty() ? areas.runtime + L"\\logs" : absolute(logs);
  areas.localAppData = absolute(readEnv(L"LOCALAPPDATA"));
  return areas;
}

inline bool CopyProcessMemory(const void *source, void *destination, SIZE_T size) noexcept
{
  SIZE_T actual{};
  return source && ReadProcessMemory(GetCurrentProcess(), source, destination, size, &actual) != 0 && actual == size;
}

struct WorkingDirectory
{
  PRTL_USER_PROCESS_PARAMETERS parameters{};
  HANDLE handle{};
  std::wstring path;

  bool operator==(const WorkingDirectory &other) const
  {
    return parameters == other.parameters && handle == other.handle && SamePath(path, other.path);
  }
};

inline std::optional<WorkingDirectory> ReadWorkingDirectory()
{
  // The current directory pair is inside the private x64 parameter prefix.
  // Require agreement with the public directory string and the opened handle.
  constexpr std::size_t currentDirectoryOffset = 0x38;
  struct DirectoryPair
  {
    UNICODE_STRING path;
    HANDLE handle;
  } directory{};
  static_assert(sizeof(DirectoryPair) == 24);
  WorkingDirectory result;
  const auto *peb = NtCurrentTeb()->ProcessEnvironmentBlock;
  if (!peb || !CopyProcessMemory(&peb->ProcessParameters, &result.parameters, sizeof(result.parameters)) ||
      !result.parameters || !CopyProcessMemory(reinterpret_cast<const unsigned char *>(result.parameters) +
          currentDirectoryOffset, &directory, sizeof(directory)) ||
      !directory.handle || !directory.path.Length || directory.path.Length % sizeof(wchar_t) ||
      directory.path.Length > directory.path.MaximumLength)
    return std::nullopt;
  std::wstring declared(directory.path.Length / sizeof(wchar_t), L'\0');
  if (!CopyProcessMemory(directory.path.Buffer, declared.data(), directory.path.Length))
    return std::nullopt;
  result.path = NormalizeOpenedPath(ReadPath([](DWORD size, wchar_t *data) {
    return GetCurrentDirectoryW(size, data);
  }));
  result.handle = directory.handle;
  if (!SamePath(result.path, NormalizeOpenedPath(std::move(declared))) ||
      !SamePath(result.path, ReadOpenedPath(result.handle)))
    return std::nullopt;
  return result;
}

using QueryVolume = LONG (NTAPI *)(HANDLE, PIO_STATUS_BLOCK, PVOID, ULONG, ULONG);

inline std::optional<std::uint32_t> ReadDeviceType(QueryVolume query, HANDLE handle) noexcept
{
  constexpr ULONG fileFsDeviceInformation = 4;
  struct DeviceInfo
  {
    ULONG type, characteristics;
  } info{};
  IO_STATUS_BLOCK io{};
  if (!query || query(handle, &io, &info, sizeof(info), fileFsDeviceInformation) != 0 ||
      io.Information != sizeof(info))
    return std::nullopt;
  return info.type;
}
} // namespace windows_file_object_detail
#endif
} // namespace cutum
