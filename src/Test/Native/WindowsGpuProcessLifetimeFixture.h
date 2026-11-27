#pragma once

#include "Test/Native/WindowsGpuMemoryFixture.h"
#include <array>
#include <memory>

namespace cutum::windows_gpu_process_fixture
{
#if CUTUM_WINDOWS_GPU_MEMORY_API
using namespace windows_gpu_memory_fixture;
inline constexpr std::array Phases{"window_current", "texture_live", "texture_deleted", "context_detached",
                                  "window_destroyed", "glfw_terminated"};
inline constexpr std::size_t Cycles = 2;
inline constexpr DWORD StageTimeoutMs = 15000;
inline constexpr auto QuietInterval = std::chrono::milliseconds(250);
inline constexpr std::uint32_t ProcessIsTerminating = 0xc000010a;

inline void Stage(HANDLE ready, HANDLE advance)
{
  Require(SetEvent(ready) != 0 && WaitForSingleObject(advance, StageTimeoutMs) == WAIT_OBJECT_0,
          "hold at the requested graphics lifetime boundary");
}

inline int RunChild(HANDLE ready, HANDLE advance)
{
  Stage(ready, advance);
  for (std::size_t cycle = 0; cycle < Cycles; ++cycle)
  {
    Require(glfwInit() != 0, "initialize GLFW in the owned fixture process");
    glfwWindowHint(GLFW_VISIBLE, GLFW_FALSE);
    glfwWindowHint(GLFW_CONTEXT_VERSION_MAJOR, 3);
    glfwWindowHint(GLFW_CONTEXT_VERSION_MINOR, 3);
    glfwWindowHint(GLFW_OPENGL_PROFILE, GLFW_OPENGL_CORE_PROFILE);
    auto *window = glfwCreateWindow(32, 32, "Graphics lifetime fixture", nullptr, nullptr);
    Require(window != nullptr, "create a fresh WGL context for the identical workload");
    glfwMakeContextCurrent(window);
    glewExperimental = GL_TRUE;
    Require(glewInit() == GLEW_OK, "load the required OpenGL functions");
    while (glGetError() != GL_NO_ERROR)
    {
    }
    const auto hwnd = WindowFromDC(wglGetCurrentDC());
    Require(hwnd && IsWindow(hwnd) && glfwGetCurrentContext() == window && wglGetCurrentContext(),
            "verify both the native window and its current WGL context");
    Stage(ready, advance);
    GLuint texture{}, framebuffer{};
    {
      TextureTarget target;
      UploadVerifiedTexture(target);
      texture = target.texture;
      framebuffer = target.framebuffer;
      Stage(ready, advance);
    }
    glFinish();
    Require(!glIsTexture(texture) && !glIsFramebuffer(framebuffer) && glGetError() == GL_NO_ERROR,
            "retire both authored OpenGL object names with the context current");
    Stage(ready, advance);
    glfwMakeContextCurrent(nullptr);
    Require(!glfwGetCurrentContext() && !wglGetCurrentContext() && IsWindow(hwnd),
            "detach the native rendering context while its window is still live");
    Stage(ready, advance);
    glfwDestroyWindow(window);
    Require(!glfwGetCurrentContext() && !wglGetCurrentContext() && !IsWindow(hwnd),
            "destroy the native window and its context before ending GLFW");
    Stage(ready, advance);
    glfwTerminate();
    Require(!wglGetCurrentContext(), "retire the GLFW instance");
    Stage(ready, advance);
  }
  std::this_thread::sleep_for(QuietInterval);
  Stage(ready, advance);
  return 0;
}

struct CloseHandleOwner
{
  void operator()(void *handle) const noexcept { CloseHandle(handle); }
};
using KernelHandle = std::unique_ptr<void, CloseHandleOwner>;

struct OwnedChild
{
  KernelHandle process;

  ~OwnedChild()
  {
    if (process && WaitForSingleObject(process.get(), 0) == WAIT_TIMEOUT)
    {
      TerminateProcess(process.get(), 1);
      WaitForSingleObject(process.get(), 5000);
    }
  }
};

inline int Run(const char *path)
{
  SECURITY_ATTRIBUTES inherited{sizeof(SECURITY_ATTRIBUTES), nullptr, TRUE};
  const KernelHandle ready(CreateEventW(&inherited, FALSE, FALSE, nullptr));
  const KernelHandle advance(CreateEventW(&inherited, FALSE, FALSE, nullptr));
  Require(ready && advance, "create the fixture synchronization events");
  std::array<wchar_t, 32768> executable;
  const auto length = GetModuleFileNameW(nullptr, executable.data(), static_cast<DWORD>(executable.size()));
  Require(length && length < executable.size(), "identify the current Release fixture binary");
  auto command = L"\"" + std::wstring(executable.data(), length) + L"\" --windows-gpu-memory-process-child " +
      std::to_wstring(reinterpret_cast<std::uintptr_t>(ready.get())) + L" " +
      std::to_wstring(reinterpret_cast<std::uintptr_t>(advance.get()));

  SIZE_T attributeBytes{};
  InitializeProcThreadAttributeList(nullptr, 1, 0, &attributeBytes);
  Require(attributeBytes != 0, "size the two-event inheritance configuration");
  std::vector<ULONG_PTR> storage((attributeBytes + sizeof(ULONG_PTR) - 1) / sizeof(ULONG_PTR));
  auto *attributes = reinterpret_cast<LPPROC_THREAD_ATTRIBUTE_LIST>(storage.data());
  Require(InitializeProcThreadAttributeList(attributes, 1, 0, &attributeBytes) != 0,
          "initialize the fixture process attributes");
  const auto deleteAttributes = [](void *list) { DeleteProcThreadAttributeList(static_cast<LPPROC_THREAD_ATTRIBUTE_LIST>(list)); };
  const std::unique_ptr<void, decltype(deleteAttributes)> attributeOwner(attributes, deleteAttributes);
  std::array<HANDLE, 2> events{ready.get(), advance.get()};
  Require(UpdateProcThreadAttribute(attributes, 0, PROC_THREAD_ATTRIBUTE_HANDLE_LIST,
              events.data(), sizeof(events), nullptr, nullptr) != 0, "inherit only the two synchronization events");
  STARTUPINFOEXW startup{};
  startup.StartupInfo.cb = sizeof(startup);
  startup.lpAttributeList = attributes;
  PROCESS_INFORMATION process{};
  Require(CreateProcessW(executable.data(), command.data(), nullptr, nullptr, TRUE, EXTENDED_STARTUPINFO_PRESENT |
              CREATE_NO_WINDOW, nullptr, nullptr, &startup.StartupInfo, &process) != 0, "launch the owned Release fixture child");
  const OwnedChild child{KernelHandle(process.hProcess)};
  CloseHandle(process.hThread);
  const auto begin = std::chrono::steady_clock::now();
  const std::array<HANDLE, 2> waits{ready.get(), child.process.get()};
  const auto sample = [&](const char *label) {
    if (WaitForMultipleObjects(static_cast<DWORD>(waits.size()), waits.data(), FALSE, StageTimeoutMs) != WAIT_OBJECT_0)
      throw std::runtime_error(std::string("fixture child did not reach ") + label);
    auto memory = windows_gpu_memory_detail::ReadForProcess(child.process.get());
    Require(memory.complete && WaitForSingleObject(child.process.get(), 0) == WAIT_TIMEOUT,
            "observe the still-live child on every physical adapter and segment");
    Require(SetEvent(advance.get()) != 0, "let the child advance to its next graphics lifetime boundary");
    return memory;
  };
  const auto cold = sample("cold");
  std::array<std::array<WindowsGpuMemory, Phases.size()>, Cycles> cycles;
  for (auto &cycle : cycles)
    for (std::size_t phase = 0; phase < Phases.size(); ++phase)
      cycle[phase] = sample(Phases[phase]);
  const auto quiet = sample("after_quiet_interval");
  Require(WaitForSingleObject(child.process.get(), StageTimeoutMs) == WAIT_OBJECT_0,
          "observe the owned child process signal after its ordinary exit");
  DWORD exit{};
  Require(GetExitCodeProcess(child.process.get(), &exit) && !exit, "verify the child's complete ordinary Release run");
  const auto exited = windows_gpu_memory_detail::ReadForProcess(child.process.get());

  auto data = nlohmann::json::array();
  for (const auto &cycle : cycles)
  {
    auto phaseData = nlohmann::json::object();
    for (std::size_t phase = 0; phase < Phases.size(); ++phase)
      phaseData[Phases[phase]] = DescribeWindowsGpuMemory(cycle[phase]);
    data.push_back(std::move(phaseData));
  }
  bool respondedToUpload = false;
  for (const auto &adapter : cycles.front()[1].adapters)
    if (adapter.renderSupported && !adapter.softwareDevice)
      for (const auto &node : adapter.nodes)
        for (const auto nonLocal : {false, true})
        {
          const NodeAddress address{adapter.luidHigh, adapter.luidLow, node.index, nonLocal};
          const auto baseline = CurrentUsage(cycles.front()[0], address);
          const auto live = CurrentUsage(cycles.front()[1], address);
          if (baseline && live && *live >= *baseline && *live - *baseline >= ObservableTextureDelta)
            respondedToUpload = true;
        }
  Require(respondedToUpload, "the first verified texture changes the hardware segment observation");
  Require(!exited.adapters.empty() && std::all_of(exited.adapters.begin(), exited.adapters.end(), [](const auto &adapter) {
    const auto retired = [](const auto &segment) {
      return segment.status == 0 ? segment.usage == 0 : static_cast<std::uint32_t>(segment.status) == ProcessIsTerminating;
    };
    return adapter.closeStatus == 0 && !adapter.nodes.empty() &&
        std::all_of(adapter.nodes.begin(), adapter.nodes.end(), [&](const auto &node) {
          return retired(node.local) && retired(node.nonLocal);
        });
  }), "record the terminal child query without substituting zero for an unavailable measurement");

  const nlohmann::json report{{"schema", "windows-gpu-process-lifetime-fixture.v1"}, {"status", "PASS"},
      {"texture_bytes", TextureBytes}, {"verified_pixels_per_cycle", 2}, {"quiet_interval_ms", QuietInterval.count()},
      {"elapsed_ms", std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now() - begin).count()},
      {"child_exit_code", exit}, {"process_signaled", true}, {"cold", DescribeWindowsGpuMemory(cold)},
      {"cycles", std::move(data)}, {"after_quiet_interval", DescribeWindowsGpuMemory(quiet)},
      {"process_signaled_gpu", DescribeWindowsGpuMemory(exited)}};
  std::ofstream output(path);
  output << report.dump(2) << '\n';
  Require(static_cast<bool>(output), "write the controlled graphics lifetime report");
  return 0;
}
#endif
} // namespace cutum::windows_gpu_process_fixture
