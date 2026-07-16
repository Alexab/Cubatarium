#include "App/Platform/Log.h"

#ifndef GLOG_NO_ABBREVIATED_SEVERITIES
#define GLOG_NO_ABBREVIATED_SEVERITIES
#endif
#include <glog/logging.h>

#include <atomic>
#include <algorithm>
#include <chrono>
#include <exception>
#include <filesystem>
#include <fstream>
#include <iomanip>
#include <iostream>
#include <mutex>
#include <sstream>
#include <string>
#include <vector>

#if defined(__ANDROID__)
#include <android/log.h>
#endif

#ifdef _WIN32
#ifndef NOMINMAX
#define NOMINMAX
#endif
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>
#include <dbghelp.h>
#include <cstdlib>
#elif !defined(__ANDROID__) && __has_include(<execinfo.h>)
#include <csignal>
#include <cstdlib>
#include <execinfo.h>
#include <unistd.h>
#endif

namespace cutum
{

namespace
{

std::filesystem::path ExecutableDirectory()
{
#ifdef _WIN32
  wchar_t buffer[MAX_PATH];
  const DWORD length = GetModuleFileNameW(nullptr, buffer, MAX_PATH);
  if (length == 0 || length >= MAX_PATH)
  {
    return std::filesystem::current_path();
  }
  return std::filesystem::path(buffer).parent_path();
#else
  return std::filesystem::current_path();
#endif
}

std::mutex &LogMutex()
{
  static std::mutex m;
  return m;
}

std::atomic<bool> &SuppressErrorDialogsFlag()
{
  static std::atomic<bool> flag{false};
  return flag;
}

std::atomic<bool> &LoggingInitializedFlag()
{
  static std::atomic<bool> flag{false};
  return flag;
}

std::filesystem::path LogsDirectory()
{
  return ExecutableDirectory() / "logs";
}

void EnsureLogsDirectory()
{
  std::error_code ec;
  std::filesystem::create_directories(LogsDirectory(), ec);
}

void PruneOldLogs()
{
  EnsureLogsDirectory();
  const auto logs_dir = LogsDirectory();
  std::vector<std::filesystem::directory_entry> entries;
  std::error_code ec;
  for (const auto &entry : std::filesystem::directory_iterator(logs_dir, ec))
  {
    if (!entry.is_regular_file(ec))
    {
      continue;
    }
    const auto ext = entry.path().extension().string();
    if (ext == ".dmp" || ext == ".log" ||
        entry.path().filename().string().find(".log.") != std::string::npos ||
        entry.path().filename().string().find("Cubatarium.") == 0)
    {
      entries.push_back(entry);
    }
  }

  const auto now = std::filesystem::file_time_type::clock::now();
  constexpr auto kMaxAge = std::chrono::hours(24 * 14);
  for (auto it = entries.begin(); it != entries.end();)
  {
    const auto age = now - it->last_write_time(ec);
    if (!ec && age > kMaxAge)
    {
      std::filesystem::remove(it->path(), ec);
      it = entries.erase(it);
    }
    else
    {
      ++it;
    }
  }

  constexpr size_t kMaxFiles = 50;
  if (entries.size() <= kMaxFiles)
  {
    return;
  }
  std::sort(entries.begin(), entries.end(),
            [](const std::filesystem::directory_entry &a,
               const std::filesystem::directory_entry &b)
            {
              std::error_code local_ec;
              return a.last_write_time(local_ec) < b.last_write_time(local_ec);
            });
  while (entries.size() > kMaxFiles)
  {
    std::filesystem::remove(entries.front().path(), ec);
    entries.erase(entries.begin());
  }
}

void FlushLogsHard()
{
  if (LoggingInitializedFlag().load(std::memory_order_relaxed))
  {
    google::FlushLogFiles(google::GLOG_INFO);
  }
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

void ShowWindowsErrorDialog(const std::string &message)
{
  if (SuppressErrorDialogsFlag().load(std::memory_order_relaxed))
  {
    return;
  }
  const std::string logPath = LogsDirectory().string();
  std::ostringstream body;
  body << message << "\n\nDetails were written to:\n" << logPath;
  const std::wstring wide = Utf8ToWide(body.str());
  MessageBoxW(nullptr, wide.c_str(), L"Cubatarium", MB_OK | MB_ICONERROR);
}

void AppendSymbolizedFrame(HANDLE process, DWORD64 address, int frameIndex)
{
  std::ostringstream line;
  line << "[Crash]   #" << frameIndex << " 0x" << std::hex << address << std::dec;

  alignas(SYMBOL_INFO) char symbolStorage[sizeof(SYMBOL_INFO) + MAX_SYM_NAME];
  auto *symbol = reinterpret_cast<SYMBOL_INFO *>(symbolStorage);
  symbol->SizeOfStruct = sizeof(SYMBOL_INFO);
  symbol->MaxNameLen = MAX_SYM_NAME;
  DWORD64 displacement = 0;
  if (SymFromAddr(process, address, &displacement, symbol))
  {
    line << ' ' << symbol->Name << "+0x" << std::hex << displacement << std::dec;
  }

  IMAGEHLP_LINE64 lineInfo{};
  lineInfo.SizeOfStruct = sizeof(lineInfo);
  DWORD lineDisplacement = 0;
  if (SymGetLineFromAddr64(process, address, &lineDisplacement, &lineInfo))
  {
    line << " at " << lineInfo.FileName << ':' << lineInfo.LineNumber;
  }

  LOG(ERROR) << line.str();
}

void AppendStackTraceFromContext(const CONTEXT *context)
{
  HANDLE process = GetCurrentProcess();
  SymSetOptions(SYMOPT_UNDNAME | SYMOPT_DEFERRED_LOADS | SYMOPT_LOAD_LINES);
  if (!SymInitialize(process, nullptr, TRUE))
  {
    LOG(ERROR) << "[Crash]   (failed to initialize symbol resolver)";
    return;
  }

  CONTEXT ctx = context ? *context : CONTEXT{};
  if (!context)
  {
    RtlCaptureContext(&ctx);
  }

  STACKFRAME64 frame{};
  frame.AddrPC.Mode = AddrModeFlat;
  frame.AddrFrame.Mode = AddrModeFlat;
  frame.AddrStack.Mode = AddrModeFlat;

#if defined(_M_X64)
  const DWORD machine = IMAGE_FILE_MACHINE_AMD64;
  frame.AddrPC.Offset = ctx.Rip;
  frame.AddrFrame.Offset = ctx.Rbp;
  frame.AddrStack.Offset = ctx.Rsp;
#elif defined(_M_IX86)
  const DWORD machine = IMAGE_FILE_MACHINE_I386;
  frame.AddrPC.Offset = ctx.Eip;
  frame.AddrFrame.Offset = ctx.Ebp;
  frame.AddrStack.Offset = ctx.Esp;
#else
  const DWORD machine = 0;
#endif

  LOG(ERROR) << "[Crash] Stack trace:";

#if defined(_M_X64) || defined(_M_IX86)
  for (int frameIndex = 0; frameIndex < 64; ++frameIndex)
  {
    if (!StackWalk64(machine, process, GetCurrentThread(), &frame, &ctx, nullptr,
                     SymFunctionTableAccess64, SymGetModuleBase64, nullptr))
    {
      break;
    }
    const DWORD64 address = frame.AddrPC.Offset;
    if (address == 0)
    {
      break;
    }
    AppendSymbolizedFrame(process, address, frameIndex);
  }
#else
  LOG(ERROR) << "[Crash]   (stack walk unsupported on this architecture)";
#endif

  SymCleanup(process);
}

void AppendStackTraceFromCapture()
{
  void *frames[64];
  const USHORT count =
      CaptureStackBackTrace(0, static_cast<DWORD>(std::size(frames)), frames, nullptr);
  if (count == 0)
  {
    LOG(ERROR) << "[Crash]   (empty stack capture)";
    return;
  }

  HANDLE process = GetCurrentProcess();
  SymSetOptions(SYMOPT_UNDNAME | SYMOPT_DEFERRED_LOADS | SYMOPT_LOAD_LINES);
  if (!SymInitialize(process, nullptr, TRUE))
  {
    LOG(ERROR) << "[Crash]   (failed to initialize symbol resolver)";
    return;
  }

  LOG(ERROR) << "[Crash] Stack trace:";
  for (USHORT frameIndex = 0; frameIndex < count; ++frameIndex)
  {
    AppendSymbolizedFrame(process,
                          reinterpret_cast<DWORD64>(frames[frameIndex]),
                          static_cast<int>(frameIndex));
  }
  SymCleanup(process);
}

void WriteMiniDump(_EXCEPTION_POINTERS *info)
{
  EnsureLogsDirectory();
  const auto now = std::chrono::system_clock::now();
  const auto t = std::chrono::system_clock::to_time_t(now);
  std::tm tm{};
  localtime_s(&tm, &t);
  std::ostringstream name;
  name << "cubatarium_crash_" << std::put_time(&tm, "%Y%m%d_%H%M%S") << '_'
       << GetCurrentProcessId() << ".dmp";
  const auto dump_path = LogsDirectory() / name.str();
  HANDLE file = CreateFileW(Utf8ToWide(dump_path.string()).c_str(), GENERIC_WRITE,
                            0, nullptr, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL,
                            nullptr);
  if (file == INVALID_HANDLE_VALUE)
  {
    LOG(ERROR) << "[Crash] Failed to create minidump file "
               << dump_path.string();
    return;
  }

  MINIDUMP_EXCEPTION_INFORMATION mei{};
  mei.ThreadId = GetCurrentThreadId();
  mei.ExceptionPointers = info;
  mei.ClientPointers = FALSE;

  const BOOL ok = MiniDumpWriteDump(
      GetCurrentProcess(), GetCurrentProcessId(), file, MiniDumpWithDataSegs,
      info ? &mei : nullptr, nullptr, nullptr);
  CloseHandle(file);
  if (ok)
  {
    LOG(ERROR) << "[Crash] Minidump written: " << dump_path.string();
  }
  else
  {
    LOG(ERROR) << "[Crash] MiniDumpWriteDump failed err=" << GetLastError();
  }
}

void LogFatalWithStack(const char *kind, _EXCEPTION_POINTERS *info)
{
  static LONG handling = 0;
  if (InterlockedExchange(&handling, 1) != 0)
  {
    return;
  }

  if (!LoggingInitializedFlag().load(std::memory_order_relaxed))
  {
    CubatariumInitLogging("Cubatarium", true);
  }

  std::ostringstream header;
  header << "[Crash] " << kind << " thread=" << GetCurrentThreadId()
         << " job_kind=" << CubatariumGetWorkerJobKind();
  if (info && info->ExceptionRecord)
  {
    header << " (code 0x" << std::hex << info->ExceptionRecord->ExceptionCode
           << std::dec << ", address 0x" << std::hex
           << reinterpret_cast<uintptr_t>(info->ExceptionRecord->ExceptionAddress)
           << std::dec << ')';
  }
  LOG(ERROR) << header.str();

  if (info && info->ContextRecord)
  {
    AppendStackTraceFromContext(info->ContextRecord);
  }
  else
  {
    AppendStackTraceFromCapture();
  }
  WriteMiniDump(info);
  FlushLogsHard();
}

LONG WINAPI UnhandledExceptionFilter(_EXCEPTION_POINTERS *info)
{
  LogFatalWithStack("Unhandled native exception", info);
  if (!SuppressErrorDialogsFlag().load(std::memory_order_relaxed))
  {
    ShowWindowsErrorDialog(
        "Cubatarium stopped unexpectedly.\n"
        "See bin/logs/ for the crash log and minidump.");
  }
  return EXCEPTION_EXECUTE_HANDLER;
}

#elif !defined(__ANDROID__) && __has_include(<execinfo.h>)

void AppendStackTracePosix()
{
  void *frames[64];
  const int count = backtrace(frames, static_cast<int>(std::size(frames)));
  if (count <= 0)
  {
    LOG(ERROR) << "[Crash]   (empty stack capture)";
    return;
  }

  char **symbols = backtrace_symbols(frames, count);
  LOG(ERROR) << "[Crash] Stack trace:";
  for (int frameIndex = 0; frameIndex < count; ++frameIndex)
  {
    std::ostringstream line;
    line << "[Crash]   #" << frameIndex;
    if (symbols && symbols[frameIndex])
    {
      line << ' ' << symbols[frameIndex];
    }
    else
    {
      line << " 0x" << std::hex << reinterpret_cast<uintptr_t>(frames[frameIndex])
           << std::dec;
    }
    LOG(ERROR) << line.str();
  }
  std::free(symbols);
}

void PosixSignalHandler(int sig, siginfo_t *info, void * /*ucontext*/)
{
  static volatile sig_atomic_t handling = 0;
  if (handling != 0)
  {
    _exit(128 + sig);
  }
  handling = 1;

  if (!LoggingInitializedFlag().load(std::memory_order_relaxed))
  {
    CubatariumInitLogging("Cubatarium", true);
  }

  std::ostringstream header;
  header << "[Crash] Signal " << sig << " job_kind="
         << CubatariumGetWorkerJobKind();
  if (info != nullptr)
  {
    header << " (address 0x" << std::hex
           << reinterpret_cast<uintptr_t>(info->si_addr) << std::dec << ')';
  }
  LOG(ERROR) << header.str();
  AppendStackTracePosix();
  FlushLogsHard();
  _exit(128 + sig);
}

void InstallPosixSignalHandlers()
{
  struct sigaction action {};
  action.sa_sigaction = PosixSignalHandler;
  action.sa_flags = SA_SIGINFO;
  sigemptyset(&action.sa_mask);

  const int signals[] = {SIGSEGV, SIGABRT, SIGFPE, SIGILL, SIGBUS};
  for (const int sig : signals)
  {
    sigaction(sig, &action, nullptr);
  }
}

#endif

void TerminateHandler()
{
  if (!LoggingInitializedFlag().load(std::memory_order_relaxed))
  {
    CubatariumInitLogging("Cubatarium", true);
  }

  try
  {
    if (const auto reason = std::current_exception())
    {
      std::rethrow_exception(reason);
    }
  }
  catch (const std::exception &e)
  {
    LOG(ERROR) << "[Crash] std::terminate: " << e.what()
               << " job_kind=" << CubatariumGetWorkerJobKind();
  }
  catch (...)
  {
    LOG(ERROR) << "[Crash] std::terminate: unknown exception job_kind="
               << CubatariumGetWorkerJobKind();
  }

#if defined(_WIN32)
  AppendStackTraceFromCapture();
  WriteMiniDump(nullptr);
  FlushLogsHard();
  std::abort();
#elif !defined(__ANDROID__) && __has_include(<execinfo.h>)
  AppendStackTracePosix();
  FlushLogsHard();
  std::abort();
#else
  FlushLogsHard();
  std::abort();
#endif
}

} // namespace

void CubatariumInitLogging(const char *argv0, bool also_log_to_stderr)
{
  bool expected = false;
  if (!LoggingInitializedFlag().compare_exchange_strong(expected, true))
  {
    return;
  }

  EnsureLogsDirectory();
  PruneOldLogs();

  FLAGS_log_dir = LogsDirectory().string();
  FLAGS_max_log_size = 32;
  FLAGS_alsologtostderr = also_log_to_stderr;
  FLAGS_logtostderr = false;
  FLAGS_colorlogtostderr = also_log_to_stderr;
  google::InitGoogleLogging(argv0 ? argv0 : "Cubatarium");
  google::InstallFailureSignalHandler();
  LOG(INFO) << "[Log] initialized dir=" << FLAGS_log_dir;
}

void CubatariumShutdownLogging()
{
  if (!LoggingInitializedFlag().exchange(false))
  {
    return;
  }
  google::ShutdownGoogleLogging();
}

void CubatariumInstallWindowsDiagnostics()
{
#ifdef _WIN32
  CubatariumSetSuppressErrorDialogs(true);
  _set_error_mode(_OUT_TO_STDERR);
  _set_abort_behavior(0, _WRITE_ABORT_MSG | _CALL_REPORTFAULT);
  SetUnhandledExceptionFilter(UnhandledExceptionFilter);
#elif !defined(__ANDROID__) && __has_include(<execinfo.h>)
  InstallPosixSignalHandlers();
#endif
  std::set_terminate(TerminateHandler);
}

#ifdef _WIN32
void CubatariumAttachParentConsole()
{
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
}

void CubatariumAttachWindowsConsole()
{
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
}
#endif

void CubatariumLogInfo(const char *tag, const std::string &msg)
{
  std::lock_guard<std::mutex> lock(LogMutex());
  if (!LoggingInitializedFlag().load(std::memory_order_relaxed))
  {
    CubatariumInitLogging("Cubatarium", false);
  }
  LOG(INFO) << '[' << (tag ? tag : "?") << "] " << msg;
#if defined(__ANDROID__)
  __android_log_print(ANDROID_LOG_INFO, tag ? tag : "Cubatarium", "%s",
                      msg.c_str());
#endif
}

void CubatariumLogError(const char *tag, const std::string &msg)
{
  std::lock_guard<std::mutex> lock(LogMutex());
  if (!LoggingInitializedFlag().load(std::memory_order_relaxed))
  {
    CubatariumInitLogging("Cubatarium", true);
  }
  LOG(ERROR) << '[' << (tag ? tag : "?") << "] ERROR: " << msg;
#if defined(__ANDROID__)
  __android_log_print(ANDROID_LOG_ERROR, tag ? tag : "Cubatarium", "%s",
                      msg.c_str());
#else
  std::cerr << '[' << (tag ? tag : "?") << "] ERROR: " << msg << std::endl;
#endif
#ifdef _WIN32
  if (!SuppressErrorDialogsFlag().load(std::memory_order_relaxed))
  {
    ShowWindowsErrorDialog(msg);
  }
#endif
}

void CubatariumSetSuppressErrorDialogs(bool suppress)
{
  SuppressErrorDialogsFlag().store(suppress, std::memory_order_relaxed);
}

} // namespace cutum
