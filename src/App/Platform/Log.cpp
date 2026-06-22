#include "App/Platform/Log.h"

#include "App/Core.h"

#include <chrono>
#include <fstream>
#include <iomanip>
#include <iostream>
#include <mutex>
#include <sstream>

#if defined(__ANDROID__)
#include <android/log.h>
#endif

#ifdef _WIN32
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#endif

namespace cutum
{

namespace
{

std::mutex &LogMutex()
{
  static std::mutex m;
  return m;
}

#ifdef _WIN32
std::wstring Utf8ToWide(const std::string &utf8)
{
  if (utf8.empty())
  {
    return {};
  }
  const int size =
      MultiByteToWideChar(CP_UTF8, 0, utf8.c_str(), -1, nullptr, 0);
  if (size <= 0)
  {
    return {};
  }
  std::wstring wide(static_cast<size_t>(size), L'\0');
  MultiByteToWideChar(CP_UTF8, 0, utf8.c_str(), -1, wide.data(), size);
  if (!wide.empty() && wide.back() == L'\0')
  {
    wide.pop_back();
  }
  return wide;
}

std::filesystem::path LogFilePath()
{
  return GetExecutableDirectory() / "cubatarium.log";
}

void AppendLogLine(const std::string &line)
{
  const auto path = LogFilePath();
  std::ofstream out(path, std::ios::app);
  if (!out.is_open())
  {
    return;
  }
  const auto now = std::chrono::system_clock::now();
  const auto t = std::chrono::system_clock::to_time_t(now);
  std::tm tm{};
  localtime_s(&tm, &t);
  out << std::put_time(&tm, "%Y-%m-%d %H:%M:%S") << ' ' << line << '\n';
}

void ShowWindowsErrorDialog(const std::string &message)
{
  const std::string logPath = LogFilePath().string();
  std::ostringstream body;
  body << message << "\n\nDetails were written to:\n" << logPath;
  const std::wstring wide = Utf8ToWide(body.str());
  MessageBoxW(nullptr, wide.c_str(), L"Cubatarium", MB_OK | MB_ICONERROR);
}

LONG WINAPI UnhandledExceptionFilter(_EXCEPTION_POINTERS *info)
{
  (void)info;
  std::ostringstream oss;
  oss << "[Crash] Unhandled native exception (code 0x"
      << std::hex << (info && info->ExceptionRecord
                          ? info->ExceptionRecord->ExceptionCode
                          : 0)
      << std::dec << ')';
  const std::string line = oss.str();
  AppendLogLine(line);
  std::cerr << line << std::endl;
  ShowWindowsErrorDialog(
      "Cubatarium stopped unexpectedly.\n"
      "Update your GPU driver or run Cubatarium.exe --smoke-packs from Command Prompt.");
  return EXCEPTION_EXECUTE_HANDLER;
}
#endif

void WriteLogLine(const char *tag, const std::string &msg, bool isError)
{
  std::ostringstream line;
  line << '[' << tag << "]" << (isError ? " ERROR: " : " ") << msg;
  const std::string text = line.str();

#if defined(__ANDROID__)
  if (isError)
  {
    __android_log_print(ANDROID_LOG_ERROR, tag, "%s", msg.c_str());
  }
  else
  {
    __android_log_print(ANDROID_LOG_INFO, tag, "%s", msg.c_str());
  }
#else
  std::cerr << text << std::endl;
#endif

#ifdef _WIN32
  if (isError)
  {
    AppendLogLine(text);
    ShowWindowsErrorDialog(msg);
  }
#endif
}

} // namespace

void CubatariumInstallWindowsDiagnostics()
{
#ifdef _WIN32
  SetUnhandledExceptionFilter(UnhandledExceptionFilter);
#endif
}

void CubatariumAttachParentConsole()
{
#ifdef _WIN32
  if (!AttachConsole(ATTACH_PARENT_PROCESS))
  {
    return;
  }
  FILE *err = nullptr;
  freopen_s(&err, "CONOUT$", "w", stderr);
  (void)err;
  FILE *out = nullptr;
  freopen_s(&out, "CONOUT$", "w", stdout);
  (void)out;
#endif
}

void CubatariumAttachWindowsConsole()
{
#ifdef _WIN32
  if (!AllocConsole())
  {
    return;
  }
  FILE *err = nullptr;
  freopen_s(&err, "CONOUT$", "w", stderr);
  (void)err;
  FILE *out = nullptr;
  freopen_s(&out, "CONOUT$", "w", stdout);
  (void)out;
  std::cerr << "Cubatarium debug console attached." << std::endl;
#endif
}

void CubatariumLogInfo(const char *tag, const std::string &msg)
{
  std::lock_guard<std::mutex> lock(LogMutex());
  WriteLogLine(tag, msg, false);
}

void CubatariumLogError(const char *tag, const std::string &msg)
{
  std::lock_guard<std::mutex> lock(LogMutex());
  WriteLogLine(tag, msg, true);
}

} // namespace cutum
