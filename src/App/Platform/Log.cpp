#include "App/Platform/Log.h"

#include "App/Core.h"

#include <chrono>
#include <exception>
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
#include <dbghelp.h>
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

std::mutex &LogMutex()
{
  static std::mutex m;
  return m;
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
#ifdef _WIN32
  localtime_s(&tm, &t);
#else
  localtime_r(&t, &tm);
#endif
  out << std::put_time(&tm, "%Y-%m-%d %H:%M:%S") << ' ' << line << '\n';
}

void LogCrashLine(const std::string &line)
{
  AppendLogLine(line);
  std::cerr << line << std::endl;
#if defined(__ANDROID__)
  __android_log_print(ANDROID_LOG_ERROR, "Crash", "%s", line.c_str());
#endif
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
  const std::string logPath = LogFilePath().string();
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

  LogCrashLine(line.str());
}

void AppendStackTraceFromContext(const CONTEXT *context)
{
  HANDLE process = GetCurrentProcess();
  SymSetOptions(SYMOPT_UNDNAME | SYMOPT_DEFERRED_LOADS | SYMOPT_LOAD_LINES);
  if (!SymInitialize(process, nullptr, TRUE))
  {
    LogCrashLine("[Crash]   (failed to initialize symbol resolver)");
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

  LogCrashLine("[Crash] Stack trace:");

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
  LogCrashLine("[Crash]   (stack walk unsupported on this architecture)");
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
    LogCrashLine("[Crash]   (empty stack capture)");
    return;
  }

  HANDLE process = GetCurrentProcess();
  SymSetOptions(SYMOPT_UNDNAME | SYMOPT_DEFERRED_LOADS | SYMOPT_LOAD_LINES);
  if (!SymInitialize(process, nullptr, TRUE))
  {
    LogCrashLine("[Crash]   (failed to initialize symbol resolver)");
    return;
  }

  LogCrashLine("[Crash] Stack trace:");
  for (USHORT frameIndex = 0; frameIndex < count; ++frameIndex)
  {
    AppendSymbolizedFrame(process,
                          reinterpret_cast<DWORD64>(frames[frameIndex]),
                          static_cast<int>(frameIndex));
  }
  SymCleanup(process);
}

void LogNativeCrash(const char *kind, _EXCEPTION_POINTERS *info)
{
  static LONG handling = 0;
  if (InterlockedExchange(&handling, 1) != 0)
  {
    return;
  }

  std::ostringstream header;
  header << "[Crash] " << kind;
  if (info && info->ExceptionRecord)
  {
    header << " (code 0x" << std::hex << info->ExceptionRecord->ExceptionCode
           << std::dec << ", address 0x" << std::hex
           << reinterpret_cast<uintptr_t>(info->ExceptionRecord->ExceptionAddress)
           << std::dec << ')';
  }
  LogCrashLine(header.str());

  if (info && info->ContextRecord)
  {
    AppendStackTraceFromContext(info->ContextRecord);
  }
  else
  {
    AppendStackTraceFromCapture();
  }
}

LONG WINAPI UnhandledExceptionFilter(_EXCEPTION_POINTERS *info)
{
  LogNativeCrash("Unhandled native exception", info);
  ShowWindowsErrorDialog(
      "Cubatarium stopped unexpectedly.\n"
      "Update your GPU driver or run Cubatarium.exe --smoke-packs from Command Prompt.");
  return EXCEPTION_EXECUTE_HANDLER;
}

#elif !defined(__ANDROID__) && __has_include(<execinfo.h>)

void AppendStackTracePosix()
{
  void *frames[64];
  const int count = backtrace(frames, static_cast<int>(std::size(frames)));
  if (count <= 0)
  {
    LogCrashLine("[Crash]   (empty stack capture)");
    return;
  }

  char **symbols = backtrace_symbols(frames, count);
  LogCrashLine("[Crash] Stack trace:");
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
    LogCrashLine(line.str());
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

  std::ostringstream header;
  header << "[Crash] Signal " << sig;
  if (info != nullptr)
  {
    header << " (address 0x" << std::hex
           << reinterpret_cast<uintptr_t>(info->si_addr) << std::dec << ')';
  }
  LogCrashLine(header.str());
  AppendStackTracePosix();
  _exit(128 + sig);
}

void PosixTerminateHandler()
{
  try
  {
    if (const auto reason = std::current_exception())
    {
      std::rethrow_exception(reason);
    }
  }
  catch (const std::exception &e)
  {
    LogCrashLine(std::string("[Crash] std::terminate: ") + e.what());
  }
  catch (...)
  {
    LogCrashLine("[Crash] std::terminate: unknown exception");
  }
  AppendStackTracePosix();
  std::abort();
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
#if defined(_WIN32)
  try
  {
    if (const auto reason = std::current_exception())
    {
      std::rethrow_exception(reason);
    }
  }
  catch (const std::exception &e)
  {
    LogCrashLine(std::string("[Crash] std::terminate: ") + e.what());
  }
  catch (...)
  {
    LogCrashLine("[Crash] std::terminate: unknown exception");
  }
  AppendStackTraceFromCapture();
  std::abort();
#elif !defined(__ANDROID__) && __has_include(<execinfo.h>)
  PosixTerminateHandler();
#elif defined(__ANDROID__)
  try
  {
    if (const auto reason = std::current_exception())
    {
      std::rethrow_exception(reason);
    }
  }
  catch (const std::exception &e)
  {
    LogCrashLine(std::string("[Crash] std::terminate: ") + e.what());
  }
  catch (...)
  {
    LogCrashLine("[Crash] std::terminate: unknown exception");
  }
  std::abort();
#else
  std::abort();
#endif
}

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
  WriteLogLine(tag, msg, false);
}

void CubatariumLogError(const char *tag, const std::string &msg)
{
  std::lock_guard<std::mutex> lock(LogMutex());
  WriteLogLine(tag, msg, true);
}

} // namespace cutum
