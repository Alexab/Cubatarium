#include "App/Platform/WindowManager.h"
#include "App/Application.h"
#include "App/Core.h"
#include "App/Platform/CursorCapture.h"
#include "App/Platform/InputManager.h"
#include "App/Platform/Log.h"
#include "App/Settings/AppState.h"
#include "Blocks/Input/BlockInputController.h"
#include "Core/FrameStageWatchdog.h"
#include "Core/Progress/IUProgressSink.h"
#include "Creatures/Core/Creature.h"
#include "Creatures/Core/CreatureInventory.h"
#include "Creatures/Definition/CreatureDefinition.h"
#include "Creatures/Influence/InfluenceApplier.h"
#include "Creatures/Influence/InfluenceResolver.h"
#include "Creatures/Player/User.h"
#include "Game/CreatureVisualQaSpawner.h"
#include "Game/Inventory/InventoryTypes.h"
#include "Game/ModePolicy.h"
#include "Game/WorldGameMode.h"
#include "Gui/Core/GuiMetrics.h"
#include "Gui/Interfaces/IUInventoryViewModel.h"
#include "Render/Backend/RenderBackendCaps.h"
#include "Render/Engine/GeometryEngine.h"
#include "Render/Engine/ViewEngine.h"
#include "Render/Pipeline/GlStateMask.h"
#include "Render/Pipeline/GlStateScope.h"
#include "ThirdParty/stb_image.h"
#include "ThirdParty/stb_image_write.h"
#include "World/Core/World.h"
#include "World/Diagnostics/FramePerfMonitor.h"
#include "World/Diagnostics/Profile.h"
#include "World/Math/BlockTypes.h"
#include "World/Mesh/WorldMeshService.h"
#include "WorldGen/Core/ProceduralSettings.h"
#include <algorithm>
#include <array>
#include <cmath>
#include <condition_variable>
#include <cstdlib>
#include <cstring>
#include <deque>
#include <filesystem>
#include <iomanip>
#include <iostream>
#include <limits>
#include <mutex>
#include <sstream>
#include <stdexcept>
#include <thread>
#include <utility>
#include <vector>

namespace cutum
{

namespace
{

glm::ivec2 CursorToFramebufferPixels(GLFWwindow *window, float x, float y)
{
  if (!window)
  {
    return {static_cast<int>(x), static_cast<int>(y)};
  }
  int fb_w = 0;
  int fb_h = 0;
  int win_w = 0;
  int win_h = 0;
  glfwGetFramebufferSize(window, &fb_w, &fb_h);
  glfwGetWindowSize(window, &win_w, &win_h);
  if (win_w <= 0 || win_h <= 0 || fb_w <= 0 || fb_h <= 0)
  {
    return {static_cast<int>(x), static_cast<int>(y)};
  }
  const float sx = static_cast<float>(fb_w) / static_cast<float>(win_w);
  const float sy = static_cast<float>(fb_h) / static_cast<float>(win_h);
  return {static_cast<int>(x * sx), static_cast<int>(y * sy)};
}

void TrySetWindowIcon(GLFWwindow *window)
{
  if (!window)
  {
    return;
  }
  const char *paths[] = {"icon.png", "resources/branding/icon-64.png"};
  for (const char *path : paths)
  {
    int width = 0;
    int height = 0;
    int channels = 0;
    unsigned char *pixels = stbi_load(path, &width, &height, &channels, 4);
    if (!pixels || width <= 0 || height <= 0)
    {
      continue;
    }
    GLFWimage image{};
    image.width = width;
    image.height = height;
    image.pixels = pixels;
    glfwSetWindowIcon(window, 1, &image);
    stbi_image_free(pixels);
    return;
  }
}

float ReadFlightCaptureEnvFloat(const char *name, float fallback)
{
  const char *value = std::getenv(name);
  if (!value || value[0] == '\0')
  {
    return fallback;
  }
  char *end = nullptr;
  const float parsed = std::strtof(value, &end);
  if (end == value || *end != '\0' || !std::isfinite(parsed))
  {
    return fallback;
  }
  return parsed;
}

} // namespace

/// Opt-in evidence capture. GPU readback is pipelined through pixel-pack
/// buffers and PNG encoding/writes run off the render thread.
class UFlightCaptureService
{
public:
  UFlightCaptureService() : Worker([this] { WorkerLoop(); }) {}

  ~UFlightCaptureService() { Shutdown(); }

  bool HasPendingGpuReadback() const
  {
    return std::any_of(PboSlots.begin(), PboSlots.end(),
                       [](const PboSlot &slot) { return slot.Busy; });
  }

  void PollCompleted()
  {
    for (PboSlot &slot : PboSlots)
    {
      if (!slot.Busy || !slot.Fence)
      {
        continue;
      }

      const GLenum wait = glClientWaitSync(slot.Fence, 0, 0);
      if (wait == GL_TIMEOUT_EXPIRED)
      {
        continue;
      }
      if (wait == GL_WAIT_FAILED)
      {
        CubatariumLogInfo("FlightCapture",
                          "GPU readback fence failed index=" +
                              std::to_string(slot.Payload.Index));
        ReleaseSlot(slot);
        continue;
      }

      GLint previous_pack_buffer = 0;
      glGetIntegerv(GL_PIXEL_PACK_BUFFER_BINDING, &previous_pack_buffer);
      glBindBuffer(GL_PIXEL_PACK_BUFFER, slot.Buffer);
      const size_t byte_count = static_cast<size_t>(slot.Payload.Width) *
                                static_cast<size_t>(slot.Payload.Height) * 4u;
      const auto *mapped = static_cast<const unsigned char *>(glMapBufferRange(
          GL_PIXEL_PACK_BUFFER, 0, static_cast<GLsizeiptr>(byte_count),
          GL_MAP_READ_BIT));
      bool copied = false;
      if (mapped)
      {
        slot.Payload.Pixels.assign(mapped, mapped + byte_count);
        copied = glUnmapBuffer(GL_PIXEL_PACK_BUFFER) == GL_TRUE;
      }
      glBindBuffer(GL_PIXEL_PACK_BUFFER,
                   static_cast<GLuint>(previous_pack_buffer));

      if (!copied)
      {
        CubatariumLogInfo("FlightCapture",
                          "GPU readback map failed index=" +
                              std::to_string(slot.Payload.Index));
        ReleaseSlot(slot);
        continue;
      }

      Frame frame = std::move(slot.Payload);
      const uint32_t frame_index = frame.Index;
      ReleaseSlot(slot);
      if (!QueueImage(std::move(frame), false))
      {
        CubatariumLogInfo(
            "FlightCapture",
            "PNG worker queue full; dropped completed frame index=" +
                std::to_string(frame_index));
      }
    }
  }

  bool QueueCapture(GLFWwindow *window, const std::filesystem::path &path,
                    uint32_t index, float camera_x, float camera_y)
  {
    if (!window || !EnsurePboBuffers())
    {
      return false;
    }

    PboSlot *slot = nullptr;
    for (PboSlot &candidate : PboSlots)
    {
      if (!candidate.Busy)
      {
        slot = &candidate;
        break;
      }
    }
    if (!slot)
    {
      return false;
    }

    int width = 0;
    int height = 0;
    glfwGetFramebufferSize(window, &width, &height);
    if (width <= 0 || height <= 0)
    {
      return false;
    }
    const size_t byte_count =
        static_cast<size_t>(width) * static_cast<size_t>(height) * 4u;
    if (byte_count >
        static_cast<size_t>(std::numeric_limits<GLsizeiptr>::max()))
    {
      return false;
    }

    GLint previous_read_framebuffer = 0;
    GLint previous_read_buffer = GL_BACK;
    GLint previous_pack_alignment = 4;
    GLint previous_pack_buffer = 0;
    glGetIntegerv(GL_READ_FRAMEBUFFER_BINDING, &previous_read_framebuffer);
    glGetIntegerv(GL_READ_BUFFER, &previous_read_buffer);
    glGetIntegerv(GL_PACK_ALIGNMENT, &previous_pack_alignment);
    glGetIntegerv(GL_PIXEL_PACK_BUFFER_BINDING, &previous_pack_buffer);

    glBindFramebuffer(GL_READ_FRAMEBUFFER, 0);
    glReadBuffer(GL_BACK);
    glPixelStorei(GL_PACK_ALIGNMENT, 1);
    glBindBuffer(GL_PIXEL_PACK_BUFFER, slot->Buffer);
    glBufferData(GL_PIXEL_PACK_BUFFER, static_cast<GLsizeiptr>(byte_count),
                 nullptr, GL_STREAM_READ);
    glReadPixels(0, 0, width, height, GL_RGBA, GL_UNSIGNED_BYTE, nullptr);
    GLsync fence = glFenceSync(GL_SYNC_GPU_COMMANDS_COMPLETE, 0);

    glBindBuffer(GL_PIXEL_PACK_BUFFER,
                 static_cast<GLuint>(previous_pack_buffer));
    glPixelStorei(GL_PACK_ALIGNMENT, previous_pack_alignment);
    glBindFramebuffer(GL_READ_FRAMEBUFFER,
                      static_cast<GLuint>(previous_read_framebuffer));
    glReadBuffer(static_cast<GLenum>(previous_read_buffer));

    if (!fence)
    {
      // The PBO contents cannot be reused safely without a completion fence.
      glFinish();
      CubatariumLogInfo("FlightCapture",
                        "Could not create GPU readback fence index=" +
                            std::to_string(index));
      return false;
    }

    slot->Payload = Frame{width, height, path, index, camera_x, camera_y, {}};
    slot->Fence = fence;
    slot->Busy = true;
    return true;
  }

  void Shutdown()
  {
    if (Stopped)
    {
      return;
    }

    if (HasPendingGpuReadback())
    {
      // Shutdown is the only place where we wait: retain queued evidence while
      // the GL context is still current, then drain image jobs before teardown.
      glFinish();
      PollCompletedForShutdown();
    }

    {
      std::lock_guard<std::mutex> lock(Mutex);
      StopWorker = true;
    }
    WorkAvailable.notify_all();
    if (Worker.joinable())
    {
      Worker.join();
    }

    for (PboSlot &slot : PboSlots)
    {
      if (slot.Fence)
      {
        glDeleteSync(slot.Fence);
        slot.Fence = nullptr;
      }
    }
    glDeleteBuffers(static_cast<GLsizei>(PboSlots.size()), PboBuffers.data());
    PboBuffers.fill(0);
    Stopped = true;
  }

private:
  struct Frame
  {
    int Width{};
    int Height{};
    std::filesystem::path Path;
    uint32_t Index{};
    float CameraX{};
    float CameraY{};
    std::vector<unsigned char> Pixels;
  };

  struct PboSlot
  {
    GLuint Buffer{};
    GLsync Fence{};
    Frame Payload;
    bool Busy{};
  };

  static constexpr size_t KPboSlotCount = 3;
  static constexpr size_t KMaxQueuedImages = 4;

  bool EnsurePboBuffers()
  {
    if (std::all_of(PboBuffers.begin(), PboBuffers.end(),
                    [](GLuint buffer) { return buffer != 0; }))
    {
      return true;
    }
    if (std::any_of(PboBuffers.begin(), PboBuffers.end(),
                    [](GLuint buffer) { return buffer != 0; }))
    {
      glDeleteBuffers(static_cast<GLsizei>(PboBuffers.size()),
                      PboBuffers.data());
      PboBuffers.fill(0);
      for (PboSlot &slot : PboSlots)
      {
        slot.Buffer = 0;
      }
    }
    glGenBuffers(static_cast<GLsizei>(PboBuffers.size()), PboBuffers.data());
    for (size_t i = 0; i < PboSlots.size(); ++i)
    {
      PboSlots[i].Buffer = PboBuffers[i];
    }
    return PboBuffers[0] != 0 && PboBuffers[1] != 0 && PboBuffers[2] != 0;
  }

  void ReleaseSlot(PboSlot &slot)
  {
    if (slot.Fence)
    {
      glDeleteSync(slot.Fence);
      slot.Fence = nullptr;
    }
    slot.Payload = Frame{};
    slot.Busy = false;
  }

  bool QueueImage(Frame &&frame, bool wait_for_space)
  {
    std::unique_lock<std::mutex> lock(Mutex);
    if (wait_for_space)
    {
      QueueSpaceAvailable.wait(
          lock,
          [this] { return ImageJobs.size() < KMaxQueuedImages || StopWorker; });
    }
    if (ImageJobs.size() >= KMaxQueuedImages || StopWorker)
    {
      return false;
    }
    ImageJobs.push_back(std::move(frame));
    lock.unlock();
    WorkAvailable.notify_one();
    return true;
  }

  void PollCompletedForShutdown()
  {
    for (PboSlot &slot : PboSlots)
    {
      if (!slot.Busy)
      {
        continue;
      }
      // glFinish above guarantees this zero-time query must already complete.
      const GLenum wait =
          slot.Fence ? glClientWaitSync(slot.Fence, 0, 0) : GL_WAIT_FAILED;
      if (wait == GL_WAIT_FAILED)
      {
        ReleaseSlot(slot);
        continue;
      }
      GLint previous_pack_buffer = 0;
      glGetIntegerv(GL_PIXEL_PACK_BUFFER_BINDING, &previous_pack_buffer);
      glBindBuffer(GL_PIXEL_PACK_BUFFER, slot.Buffer);
      const size_t byte_count = static_cast<size_t>(slot.Payload.Width) *
                                static_cast<size_t>(slot.Payload.Height) * 4u;
      const auto *mapped = static_cast<const unsigned char *>(glMapBufferRange(
          GL_PIXEL_PACK_BUFFER, 0, static_cast<GLsizeiptr>(byte_count),
          GL_MAP_READ_BIT));
      bool copied = false;
      if (mapped)
      {
        slot.Payload.Pixels.assign(mapped, mapped + byte_count);
        copied = glUnmapBuffer(GL_PIXEL_PACK_BUFFER) == GL_TRUE;
      }
      glBindBuffer(GL_PIXEL_PACK_BUFFER,
                   static_cast<GLuint>(previous_pack_buffer));
      if (copied)
      {
        Frame frame = std::move(slot.Payload);
        ReleaseSlot(slot);
        if (!QueueImage(std::move(frame), true))
        {
          CubatariumLogInfo("FlightCapture",
                            "could not queue completed frame during shutdown");
        }
      }
      else
      {
        CubatariumLogInfo("FlightCapture",
                          "GPU readback map failed during shutdown index=" +
                              std::to_string(slot.Payload.Index));
        ReleaseSlot(slot);
      }
    }
  }

  void WorkerLoop()
  {
    for (;;)
    {
      Frame frame;
      {
        std::unique_lock<std::mutex> lock(Mutex);
        WorkAvailable.wait(lock,
                           [this] { return StopWorker || !ImageJobs.empty(); });
        if (ImageJobs.empty() && StopWorker)
        {
          return;
        }
        frame = std::move(ImageJobs.front());
        ImageJobs.pop_front();
      }
      QueueSpaceAvailable.notify_one();

      bool saved = false;
      try
      {
        const size_t row_bytes = static_cast<size_t>(frame.Width) * 4u;
        std::vector<unsigned char> row(row_bytes);
        for (int y = 0; y < frame.Height / 2; ++y)
        {
          const size_t top = static_cast<size_t>(y) * row_bytes;
          const size_t bottom =
              static_cast<size_t>(frame.Height - 1 - y) * row_bytes;
          std::memcpy(row.data(), frame.Pixels.data() + top, row_bytes);
          std::memcpy(frame.Pixels.data() + top, frame.Pixels.data() + bottom,
                      row_bytes);
          std::memcpy(frame.Pixels.data() + bottom, row.data(), row_bytes);
        }

        std::error_code ec;
        std::filesystem::create_directories(frame.Path.parent_path(), ec);
        saved =
            !ec &&
            row_bytes <= static_cast<size_t>(std::numeric_limits<int>::max()) &&
            stbi_write_png(frame.Path.string().c_str(), frame.Width,
                           frame.Height, 4, frame.Pixels.data(),
                           static_cast<int>(row_bytes)) != 0;
      }
      catch (...)
      {
        saved = false;
      }
      CubatariumLogInfo("FlightCapture",
                        std::string(saved ? "saved" : "write failed") +
                            " index=" + std::to_string(frame.Index) +
                            " camera_x=" + std::to_string(frame.CameraX) +
                            " camera_y=" + std::to_string(frame.CameraY) +
                            " path=" + frame.Path.string());
    }
  }

  std::array<GLuint, KPboSlotCount> PboBuffers{};
  std::array<PboSlot, KPboSlotCount> PboSlots{};
  std::mutex Mutex;
  std::condition_variable WorkAvailable;
  std::condition_variable QueueSpaceAvailable;
  std::deque<Frame> ImageJobs;
  bool StopWorker{};
  bool Stopped{};
  std::thread Worker;
};

UWindowManager::UWindowManager()
    : Window(nullptr), WindowWidth(1280), WindowHeight(720), IsRunning(false),
      IsInitialized(false), DeltaTime(0.0), SkyColor(0.5f, 0.7f, 1.0f, 1.0f),
      UseGradientSky(true),
      BlockInput(std::make_unique<UBlockInputController>())
{
  LastFrameTime = std::chrono::high_resolution_clock::now();
  LastAutosaveTime = std::chrono::steady_clock::now();
}

UWindowManager::~UWindowManager() { Shutdown(); }

bool UWindowManager::Initialize(int width, int height, const char *title,
                                bool visible)
{
  glfwSetErrorCallback(ErrorCallback);

  if (!glfwInit())
  {
    CubatariumLogError("Window", "Failed to initialize GLFW");
    return false;
  }

  auto createWindow = [&](bool multisample) -> GLFWwindow *
  {
    glfwDefaultWindowHints();
    glfwWindowHint(GLFW_VISIBLE, visible ? GLFW_TRUE : GLFW_FALSE);
    if (visible)
    {
      glfwWindowHint(GLFW_FOCUS_ON_SHOW, GLFW_TRUE);
    }
    glfwWindowHint(GLFW_CONTEXT_VERSION_MAJOR, 3);
    glfwWindowHint(GLFW_CONTEXT_VERSION_MINOR, 3);
    glfwWindowHint(GLFW_OPENGL_PROFILE, GLFW_OPENGL_CORE_PROFILE);
    if (multisample)
    {
      glfwWindowHint(GLFW_SAMPLES, 4);
    }
    else
    {
      glfwWindowHint(GLFW_SAMPLES, 0);
    }
#ifdef __APPLE__
    glfwWindowHint(GLFW_OPENGL_FORWARD_COMPAT, GL_TRUE);
#endif
    return glfwCreateWindow(width, height, title, nullptr, nullptr);
  };

  // Prefer no MSAA: Wall≪Sim cases are often GPU/present bound; 4x MSAA
  // inflates fragment cost. Config msaa_samples>0 can request MSAA later only
  // at recreate (not supported yet) — create without MSAA by default.
  Window = createWindow(false);
  if (!Window)
  {
    CubatariumLogInfo("Window",
                      "OpenGL window without MSAA failed, retrying with MSAA");
    Window = createWindow(true);
  }
  if (!Window)
  {
    CubatariumLogError(
        "Window",
        "Failed to create OpenGL 3.3 window. Install or update your GPU "
        "driver (OpenGL 3.3 Core required).");
    glfwTerminate();
    return false;
  }

  TrySetWindowIcon(Window);

  WindowWidth = width;
  WindowHeight = height;

  // OpenGL context creation
  glfwMakeContextCurrent(Window);

  // Default uncapped present; ApplyPresentSettings after config load may
  // enable VSync.
  glfwSwapInterval(0);
  CubatariumLogInfo("Window", "SwapInterval set to 0 (vsync off)");

  // GLEW initialization (must be after context creation)
#ifndef __ANDROID__
  if (glewInit() != GLEW_OK)
  {
    CubatariumLogError("Window", "Failed to initialize GLEW");
    glfwDestroyWindow(Window);
    Window = nullptr;
    glfwTerminate();
    return false;
  }
  RefreshRenderBackendCapsFromGl();
#endif

  // Настройка OpenGL
  InitializeOpenGL();

  InputManager = std::make_shared<UInputManager>();

  // TextRenderer will be set later via SetTextRenderer

  // Настройка callbacks
  SetupCallbacks();

  // Input manager creation
  InputManager->Initialize(Window);

  // Keep explicit flight-sim visibility reliable on Windows. The GLFW hint
  // requests an initially visible window, but some launch paths create the
  // context while the parent console is in the foreground. Re-show after GL
  // initialization so the operator gets a real window to inspect.
  if (visible)
  {
    glfwShowWindow(Window);
    glfwFocusWindow(Window);
    CubatariumLogInfo("Window",
                      glfwGetWindowAttrib(Window, GLFW_VISIBLE) == GLFW_TRUE
                          ? "Visible window shown"
                          : "Visible window request did not take effect");
  }

  IsInitialized = true;
  return true;
}

void UWindowManager::InitializeOpenGL()
{
  // Enable depth testing
  glEnable(GL_DEPTH_TEST);
  glDepthFunc(GL_LESS);

  // MSAA off by default (window created without samples).
  glDisable(GL_MULTISAMPLE);

  // Enable blending for transparency
  glEnable(GL_BLEND);
  glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);

  // Set clear color (sky)
  glClearColor(SkyColor.r, SkyColor.g, SkyColor.b, SkyColor.a);

  // Viewport configuration
  glViewport(0, 0, WindowWidth, WindowHeight);
}

void UWindowManager::SetupCallbacks()
{
  // Set callback functions
  glfwSetFramebufferSizeCallback(Window,
                                 UInputManager::GLFWFramebufferSizeCallback);
  glfwSetKeyCallback(Window, UInputManager::GLFWKeyCallback);
  glfwSetMouseButtonCallback(Window, UInputManager::GLFWMouseButtonCallback);
  glfwSetCursorPosCallback(Window, UInputManager::GLFWCursorPosCallback);
  glfwSetScrollCallback(Window, UInputManager::GLFWScrollCallback);
  glfwSetErrorCallback(ErrorCallback);
  glfwSetWindowCloseCallback(Window, WindowCloseCallback);
  glfwSetWindowUserPointer(Window, this);

  glfwSetWindowFocusCallback(
      Window,
      [](GLFWwindow *win, int focused)
      {
        auto *self =
            static_cast<UWindowManager *>(glfwGetWindowUserPointer(win));
        if (self && self->Application)
        {
          self->Application->HandleWindowFocus(focused == GLFW_TRUE);
        }
      });

  // Configure callbacks for InputManager
  InputManager->SetKeyCallback([this](KeyCode key, KeyState state, int Mods)
                               { HandleKeyEvent(key, state, Mods); });

  InputManager->SetMouseButtonCallback(
      [this](MouseButton Button, bool Pressed, glm::vec2 pos)
      { HandleMouseButtonEvent(Button, Pressed, pos); });

  InputManager->SetMouseMoveCallback([this](glm::vec2 pos, glm::vec2 delta)
                                     { HandleMouseMoveEvent(pos, delta); });

  InputManager->SetWindowResizeCallback(
      [this](int width, int height)
      { HandleWindowResizeEvent(width, height); });

  InputManager->SetMouseScrollCallback(
      [this](double Xoffset, double Yoffset)
      {
        if (!Application)
        {
          return;
        }
        const glm::vec2 pos = InputManager->GetMousePosition();
        const glm::ivec2 fbPos =
            CursorToFramebufferPixels(Window, pos.x, pos.y);
        if (Application->RouteScroll(Xoffset, Yoffset, fbPos.x, fbPos.y))
        {
          return;
        }
        if (World)
        {
          if (auto camera = World->GetCurrentUserCamera())
          {
            camera->UpdateMouseScroll(Xoffset, Yoffset);
          }
        }
      });

  glfwSetCharCallback(Window,
                      [](GLFWwindow *win, unsigned int Codepoint)
                      {
                        auto *self = static_cast<UWindowManager *>(
                            glfwGetWindowUserPointer(win));
                        if (self && self->Application)
                        {
                          self->Application->RouteChar(Codepoint);
                        }
                      });
}

void UWindowManager::ApplyPresentSettings()
{
  if (!Window || !Core)
  {
    return;
  }
  const bool vsync = Core->GetRenderSettings().VSync;
  const int interval = vsync ? 1 : 0;
  glfwSwapInterval(interval);
  CubatariumLogInfo("Window", std::string("ApplyPresentSettings vsync=") +
                                  (vsync ? "true" : "false") +
                                  " SwapInterval=" + std::to_string(interval));
}

void UWindowManager::Run()
{
  if (!IsInitialized)
  {
    std::cerr << "WindowManager not initialized" << std::endl;
    return;
  }

  IsRunning = true;
  ApplyPresentSettings();
  UFramePerfMonitor::EnsureSession();

  while ((!glfwWindowShouldClose(Window) &&
          !(Application && Application->IsQuitRequested())) &&
         IsRunning)
  {
    const auto frame_begin = std::chrono::high_resolution_clock::now();
    DeltaTime =
        std::chrono::duration<double>(frame_begin - LastFrameTime).count();
    LastFrameTime = frame_begin;

    {
      UFrameStageWatchdog::Scope stage("window.poll_events");
      glfwPollEvents();
    }

    if (World)
    {
      World->SetWallFrameDelta(DeltaTime);
    }

    // Input processing
    const auto input_begin = std::chrono::high_resolution_clock::now();
    {
      UFrameStageWatchdog::Scope stage("window.process_input");
      ProcessInput();
    }
    const double input_ms =
        std::chrono::duration<double, std::milli>(
            std::chrono::high_resolution_clock::now() - input_begin)
            .count();
    const auto app_begin = std::chrono::high_resolution_clock::now();
    if (Application)
    {
      UFrameStageWatchdog::Scope stage("window.application_update");
      Application->Update(DeltaTime);
    }
    const double app_ms =
        std::chrono::duration<double, std::milli>(
            std::chrono::high_resolution_clock::now() - app_begin)
            .count();

    // Logic update (includes DoMovement → phys_ms)
    const auto world_begin = std::chrono::high_resolution_clock::now();
    {
      UFrameStageWatchdog::Scope stage("window.logic_update");
      Update();
    }
    const double world_ms =
        std::chrono::duration<double, std::milli>(
            std::chrono::high_resolution_clock::now() - world_begin)
            .count();
    if (World)
    {
      World->SetLastInputMs(input_ms);
      World->SetLastAppUpdateMs(app_ms);
      World->SetLastWorldTickMs(world_ms);
    }

    // Outside world_ms so autosave Init/Ticks do not inflate world_extra.
    {
      const auto t_autosave = std::chrono::high_resolution_clock::now();
      {
        UFrameStageWatchdog::Scope stage("window.autosave");
        TickBudgetedAutosave();
      }
      if (World)
      {
        World->SetLastAutosaveMs(
            std::chrono::duration<double, std::milli>(
                std::chrono::high_resolution_clock::now() - t_autosave)
                .count());
      }
    }

    // Flight-sim stop: skip a final heavy render once the harness predicate
    // fires.
    bool stop_before_render = false;
    {
      UFrameStageWatchdog::Scope stage("window.stop_predicate");
      stop_before_render = StopPredicate && StopPredicate();
    }
    if (stop_before_render)
    {
      IsRunning = false;
      break;
    }

    // Rendering
    CUBA_FRAME_MARK;
    const auto render_begin = std::chrono::high_resolution_clock::now();
    {
      UFrameStageWatchdog::Scope stage("window.render");
      Render();
    }
    if (World)
    {
      World->SetLastRenderTotalMs(
          std::chrono::duration<double, std::milli>(
              std::chrono::high_resolution_clock::now() - render_begin)
              .count());
    }

    // Opt-in visual evidence for visible flight-sim runs. Queue a default
    // framebuffer readback before swap; PBO completion and PNG output are
    // handled asynchronously so evidence capture does not stall the flight.
    static const char *flight_capture_dir =
        std::getenv("CUBA_FLIGHT_CAPTURE_DIR");
    if (flight_capture_dir && flight_capture_dir[0] != '\0' && World &&
        Application && Application->GetState() == AppState::InGame)
    {
      if (!FlightCapture)
      {
        FlightCapture = std::make_unique<UFlightCaptureService>();
      }
      FlightCapture->PollCompleted();

      static const float capture_interval_seconds = std::clamp(
          ReadFlightCaptureEnvFloat("CUBA_FLIGHT_CAPTURE_INTERVAL_SEC", 15.0f),
          0.1f, 3600.0f);
      static const float capture_min_x = ReadFlightCaptureEnvFloat(
          "CUBA_FLIGHT_CAPTURE_MIN_X", -std::numeric_limits<float>::infinity());
      static const float capture_max_x = ReadFlightCaptureEnvFloat(
          "CUBA_FLIGHT_CAPTURE_MAX_X", std::numeric_limits<float>::infinity());
      static auto next_capture = std::chrono::steady_clock::time_point{};
      static uint32_t capture_index = 0;
      const auto capture_now = std::chrono::steady_clock::now();
      const auto camera = World->GetCurrentUserCamera();
      const float camera_x = camera ? camera->GetPosition().x
                                    : std::numeric_limits<float>::quiet_NaN();
      const bool inside_capture_x_range =
          camera_x >= capture_min_x && camera_x <= capture_max_x;
      if (!inside_capture_x_range)
      {
        // Capture immediately on each entry into the selected X interval.
        next_capture = std::chrono::steady_clock::time_point{};
      }
      else if (next_capture == std::chrono::steady_clock::time_point{} ||
               capture_now >= next_capture)
      {
        next_capture =
            capture_now +
            std::chrono::duration_cast<std::chrono::steady_clock::duration>(
                std::chrono::duration<double>(capture_interval_seconds));
        std::ostringstream filename;
        filename << "frame_" << std::setw(3) << std::setfill('0')
                 << capture_index++ << ".png";
        const std::filesystem::path capture_path =
            std::filesystem::path(flight_capture_dir) / filename.str();
        {
          UFrameStageWatchdog::Scope stage("window.frame_capture");
          const uint32_t frame_index = capture_index - 1;
          const float camera_y = camera
                                     ? camera->GetPosition().y
                                     : std::numeric_limits<float>::quiet_NaN();
          if (!FlightCapture->QueueCapture(Window, capture_path, frame_index,
                                           camera_x, camera_y))
          {
            CubatariumLogInfo("FlightCapture",
                              "Unable to queue framebuffer capture index=" +
                                  std::to_string(frame_index) +
                                  " camera_x=" + std::to_string(camera_x));
          }
          else
          {
            CubatariumLogInfo("FlightCapture",
                              "queued index=" + std::to_string(frame_index) +
                                  " camera_x=" + std::to_string(camera_x) +
                                  " camera_y=" + std::to_string(camera_y) +
                                  " path=" + capture_path.string());
          }
        }
      }
    }

    const auto swap_begin = std::chrono::high_resolution_clock::now();
    {
      UFrameStageWatchdog::Scope stage("window.swap_buffers");
      glfwSwapBuffers(Window);
    }
    const auto frame_end = std::chrono::high_resolution_clock::now();
    const double swap_wait_ms =
        std::chrono::duration<double, std::milli>(frame_end - swap_begin)
            .count();
    const double frame_wall_ms =
        std::chrono::duration<double, std::milli>(frame_end - frame_begin)
            .count();
    if (World)
    {
      World->SetLastSwapWaitMs(swap_wait_ms);
    }
    if (World && Application && Application->GetState() == AppState::InGame)
    {
      double interval = 2.0;
      if (Core)
      {
        interval = Core->GetUiSettings().PerfLogIntervalSec;
      }
      UFrameStageWatchdog::Scope stage("window.perf_emit");
      UFramePerfMonitor::OnInGameFrame(*World, swap_wait_ms, interval,
                                       frame_wall_ms);
    }

    bool stop_after_frame = false;
    {
      UFrameStageWatchdog::Scope stage("window.stop_predicate");
      stop_after_frame = StopPredicate && StopPredicate();
    }
    if (stop_after_frame)
    {
      IsRunning = false;
    }
  }

  UFramePerfMonitor::Shutdown();
}

void UWindowManager::SetStopPredicate(std::function<bool()> predicate)
{
  StopPredicate = std::move(predicate);
}

void UWindowManager::SetAutopilotKey(KeyCode key, bool held)
{
  AutopilotKeys[static_cast<int>(key)] = held;
}

void UWindowManager::ClearAutopilotKeys() { AutopilotKeys.clear(); }

void UWindowManager::ProcessInput()
{
  InputManager->Update();

  if (Application && Application->WantsCaptureKeyboard())
  {
    if (World)
    {
      if (auto camera = World->GetCurrentUserCamera())
      {
        camera->ResetAllKeyStatus();
      }
    }
    return;
  }

  // UCamera control key processing
  if (World && Application && Application->GetState() == AppState::InGame)
  {
    auto camera = World->GetCurrentUserCamera();
    if (camera)
    {
      auto keyDown = [this](KeyCode key) -> bool
      {
        if (InputManager->IsKeyPressed(key))
        {
          return true;
        }
        if (Window && glfwGetKey(Window, static_cast<int>(key)) == GLFW_PRESS)
        {
          return true;
        }
        const auto it = AutopilotKeys.find(static_cast<int>(key));
        return it != AutopilotKeys.end() && it->second;
      };
      const bool shift_down =
          keyDown(KeyCode::Key_Shift) ||
          (Window && glfwGetKey(Window, GLFW_KEY_RIGHT_SHIFT) == GLFW_PRESS);
      const bool left_ctrl_down = keyDown(KeyCode::Key_Ctrl);
      const bool right_ctrl_down =
          Window && glfwGetKey(Window, GLFW_KEY_RIGHT_CONTROL) == GLFW_PRESS;
      camera->UpdateKeyStatus(static_cast<int>(KeyCode::Key_W),
                              keyDown(KeyCode::Key_W));
      camera->UpdateKeyStatus(static_cast<int>(KeyCode::Key_S),
                              keyDown(KeyCode::Key_S));
      camera->UpdateKeyStatus(static_cast<int>(KeyCode::Key_A),
                              keyDown(KeyCode::Key_A));
      camera->UpdateKeyStatus(static_cast<int>(KeyCode::Key_D),
                              keyDown(KeyCode::Key_D));
      camera->UpdateKeyStatus(static_cast<int>(KeyCode::Key_Space),
                              keyDown(KeyCode::Key_Space));
      camera->UpdateKeyStatus(GLFW_KEY_LEFT_SHIFT, shift_down);
      camera->UpdateKeyStatus(GLFW_KEY_RIGHT_SHIFT, shift_down);
      camera->UpdateKeyStatus(GLFW_KEY_LEFT_CONTROL, left_ctrl_down);
      camera->UpdateKeyStatus(GLFW_KEY_RIGHT_CONTROL, right_ctrl_down);
    }
  }
}

void UWindowManager::Update()
{
  using clock = std::chrono::steady_clock;
  if (World)
  {
    PhysicsTelemetry &tele = World->GetPhysicsTelemetryMutable();
    tele.ViewsMs = 0.0;
    tele.DoMovementMs = 0.0;
    tele.EnsureCollisionMs = 0.0;
    tele.CreatureTickMs = 0.0;
    tele.CameraDoMovementMs = 0.0;
    tele.CameraGroundSupportMs = 0.0;
    tele.CameraLocomotionMs = 0.0;
    tele.CameraHorizMoveMs = 0.0;
    tele.CameraSyncMs = 0.0;
    tele.EnvironmentTickMs = 0.0;
    tele.NpcIntentExecuteMs = 0.0;
    tele.ControlledInfluenceMs = 0.0;
    tele.VitalsTickMs = 0.0;
    tele.StatusEffectsTickMs = 0.0;
    tele.CreaturesTotal = 0;
    tele.CreaturesAiTicked = 0;
    tele.WorldCreaturesSkipped = 0;
    tele.PlayerLocomotionBlockMs = 0.0;
    tele.WorldAiAfterPlayerMs = 0.0;
    tele.CreaturesAiBudget = 0;
    tele.CreaturesAiDeferred = 0;
    tele.WorldStreamingPhaseMs = 0.0;
    tele.BlockInputMs = 0.0;
    tele.TickEnvMs = 0.0;
    tele.BreakCompleteN = 0;
    tele.BreakInflightRaceN = 0;
    tele.BreakDarkFaceN = 0;
    tele.PlaceCompleteN = 0;
    tele.PlaceEmissionN = 0;
    tele.AutosaveDeferredN = 0;
    tele.AutosaveSkippedTickN = 0;
    tele.DigSeamPendingN = 0;
    tele.DigSeamRemeshN = 0;
    tele.StaleRepairWaveN = 0;
    tele.StandRimDirtyN = 0;
    tele.StandRimImmN = 0;
    tele.EditLightEmission = 0;
    tele.FastRelightMs = 0.0;
    tele.EditToFirstMeshMs = 0.0;
  }

  if (Views)
  {
    const auto t0 = clock::now();
    {
      UFrameStageWatchdog::Scope stage("world.views_update");
      Views->UpdateFrameTime();
    }
    if (World)
    {
      World->GetPhysicsTelemetryMutable().ViewsMs =
          std::chrono::duration<double, std::milli>(clock::now() - t0).count();
    }
  }

  if (World && Application && Application->GetState() == AppState::InGame)
  {
    {
      const auto t0 = clock::now();
      UFrameStageWatchdog::Scope stage("world.do_movement");
      World->DoMovement();
      World->GetPhysicsTelemetryMutable().DoMovementMs =
          std::chrono::duration<double, std::milli>(clock::now() - t0).count();
    }
    // Era14: stream/mesh outside DoMovement so phys_ms stays locomotion-only.
    {
      const auto t0 = clock::now();
      UFrameStageWatchdog::Scope stage("world.streaming_phase");
      World->TickWorldStreamingPhase();
      World->GetPhysicsTelemetryMutable().WorldStreamingPhaseMs =
          std::chrono::duration<double, std::milli>(clock::now() - t0).count();
    }
    UFrameStageWatchdog::Scope break_stage("world.post_streaming_logic");
    if (World->ConsumeFlightSimBreakRequest())
    {
      if (auto camera = World->GetCurrentUserCamera())
      {
        World->UpdateIntersection(camera->GetPosition(), camera->GetFront());
      }
      glm::ivec3 target = World->GetBreakBlockPos();
      bool have_target = World->GetIsBlockIntersectionExists() &&
                         World->GetBlockWorld().GetBlock(target) != BLOCK_AIR;
      if (!have_target)
      {
        // Standing ocean save may look at empty air/water; break underfeet
        // solid.
        if (auto camera = World->GetCurrentUserCamera())
        {
          const glm::vec3 eye = camera->GetPosition();
          const int bx = static_cast<int>(std::floor(eye.x));
          const int by = static_cast<int>(std::floor(eye.y));
          const int bz = static_cast<int>(std::floor(eye.z));
          for (int dy = 1; dy <= 12 && !have_target; ++dy)
          {
            const glm::ivec3 cand(bx, by - dy, bz);
            const BlockId id = World->GetBlockWorld().GetBlock(cand);
            if (id == BLOCK_AIR)
            {
              continue;
            }
            if (!World->GetBlockRegistry().IsSolid(id))
            {
              continue;
            }
            target = cand;
            have_target = true;
          }
        }
      }
      if (have_target)
      {
        if (UCreature *controlled = World->GetControlledCreature())
        {
          CreatureIntent intent = controlled->GetIntent();
          intent.attackTargetId = 0;
          intent.Influence = InfluenceIntent{};
          intent.Influence.Channel = InfluenceChannel::Dig;
          intent.Influence.TargetBlockPos = target;
          intent.Influence.HasTargetBlock = true;
          controlled->SetIntent(intent);
          InfluencePrediction pred = InfluenceResolver::Resolve(
              *World, *controlled, World->GetGameMode(), nullptr);
          InfluenceApplier::Apply(*World, pred, World->GetGameMode(),
                                  /*dt=*/1.0e6f);
          CreatureIntent cleared = controlled->GetIntent();
          cleared.Influence = InfluenceIntent{};
          controlled->SetIntent(cleared);
        }
        else
        {
          World->StartBreakSession(target);
          World->CompleteBreakSession();
        }
      }
    }
    if (BlockInput)
    {
      const auto t0 = clock::now();
      UFrameStageWatchdog::Scope stage("world.block_input");
      BlockInputContext ctx;
      ctx.World = World;
      ctx.Geometries = Geometries.get();
      ctx.Ui = Core ? &Core->GetUiSettings() : nullptr;
      ctx.Window = Window;
      ctx.App = Application.get();
      BlockInput->Tick(static_cast<float>(DeltaTime), ctx);
      World->GetPhysicsTelemetryMutable().BlockInputMs =
          std::chrono::duration<double, std::milli>(clock::now() - t0).count();
    }
    RefreshEditHotSticky();
    // DigSeam after BlockInput so dig Immediate is visible and we do not stack
    // a second Immediate on the dig frame (manual 215711).
    {
      PhysicsTelemetry &tele = World->GetPhysicsTelemetryMutable();
      UWorldMeshService &mesh = World->GetMeshService();
      UFrameStageWatchdog::Scope stage("world.dig_seam_drain");
      mesh.TickDigSeamDrain(World->GetBlockWorld(), World->GetBlockRegistry(),
                            &tele);
      tele.DigSeamPendingN = mesh.GetLastDigSeamPendingN();
      tele.DigSeamRemeshN = mesh.GetLastDigSeamRemeshN();
      if (tele.DigSeamRemeshN > 0)
      {
        tele.MeshImmediateCount = mesh.GetLastMeshImmediateCount();
        tele.MeshImmediateMs = mesh.GetLastMeshImmediateMs();
      }
    }
  }

  if (Core && World && Application &&
      Application->GetState() == AppState::InGame)
  {
    const auto now = std::chrono::steady_clock::now();
    // Loading can exceed the interval; do not fire autosave on the first
    // InGame frame or the hitch starves streaming/flight for minutes.
    if (!SeenInGameForAutosave)
    {
      SeenInGameForAutosave = true;
      LastAutosaveTime = now;
    }
    else if (AutosaveEnabled && !AutosaveInProgress && !AutosaveRequested &&
             std::chrono::duration<double>(now - LastAutosaveTime).count() >=
                 KAutosaveIntervalSec)
    {
      // Do not advance LastAutosaveTime here — deferred Begin would burn the
      // interval (dig hitch manual 215711).
      AutosaveRequested = true;
    }
  }
  else
  {
    SeenInGameForAutosave = false;
  }
}

bool UWindowManager::IsEditHotForAutosave() const
{
  if (!World)
  {
    return false;
  }
  const PhysicsTelemetry &tele = World->GetPhysicsTelemetry();
  if (tele.BreakCompleteN > 0 || tele.PlaceCompleteN > 0)
  {
    return true;
  }
  // Use per-frame Immediate count only — LastEditImmediateN is last-edit policy
  // size and stays non-zero across idle frames.
  if (World->GetMeshService().GetLastMeshImmediateCount() > 0)
  {
    return true;
  }
  return std::chrono::steady_clock::now() < EditHotUntil;
}

void UWindowManager::RefreshEditHotSticky()
{
  if (!World)
  {
    return;
  }
  const PhysicsTelemetry &tele = World->GetPhysicsTelemetry();
  if (tele.BreakCompleteN > 0 || tele.PlaceCompleteN > 0 ||
      World->GetMeshService().GetLastMeshImmediateCount() > 0)
  {
    EditHotUntil =
        std::chrono::steady_clock::now() +
        std::chrono::duration_cast<std::chrono::steady_clock::duration>(
            std::chrono::duration<double>(KEditHotStickySec));
  }
}

void UWindowManager::TickBudgetedAutosave()
{
  if (!AutosaveEnabled)
  {
    AutosaveRequested = false;
    AutosaveInProgress = false;
    return;
  }
  if (!World || !Core || !Application ||
      Application->GetState() != AppState::InGame)
  {
    if (AutosaveInProgress && Application &&
        Application->GetState() != AppState::InGame)
    {
      AutosaveInProgress = false;
      AutosaveRequested = false;
    }
    return;
  }
  PhysicsTelemetry &tele = World->GetPhysicsTelemetryMutable();
  if (AutosaveRequested && !AutosaveInProgress)
  {
    if (IsEditHotForAutosave())
    {
      ++tele.AutosaveDeferredN;
      return;
    }
    const std::string folder = Core->GetActiveWorldFolder().string();
    if (folder.empty() || World->HasActiveCooperativeOperation())
    {
      // Keep AutosaveRequested so we retry next frame.
      return;
    }
    World->BeginCooperativeSave(folder);
    AutosaveInProgress = true;
    AutosaveRequested = false;
    LastAutosaveTime = std::chrono::steady_clock::now();
  }
  if (!AutosaveInProgress)
  {
    return;
  }
  if (!World->HasActiveCooperativeOperation())
  {
    AutosaveInProgress = false;
    return;
  }
  if (IsEditHotForAutosave())
  {
    ++tele.AutosaveSkippedTickN;
    return;
  }
  UNullProgressSink sink;
  if (World->TickCooperativeSave(sink, /*chunkBudget=*/1))
  {
    World->ResumeAfterSessionSave();
    AutosaveInProgress = false;
    LastAutosaveTime = std::chrono::steady_clock::now();
  }
}

void UWindowManager::Render()
{
  if (Application)
  {
    Application->SetWindow(Window);
    int fb_w = WindowWidth;
    int fb_h = WindowHeight;
    if (Window)
    {
      glfwGetFramebufferSize(Window, &fb_w, &fb_h);
      if (fb_w > 0 && fb_h > 0)
      {
        WindowWidth = fb_w;
        WindowHeight = fb_h;
      }
      float content_scale_x = 1.f;
      float content_scale_y = 1.f;
      glfwGetWindowContentScale(Window, &content_scale_x, &content_scale_y);
      PlatformUiMetrics platform;
      platform.ContentScaleX = content_scale_x;
      platform.ContentScaleY = content_scale_y;
      Application->UpdateUiScale(fb_w, fb_h, platform);
    }
    Application->RenderFrame(fb_w, fb_h,
                             Views ? Views->GetDurationUpdateMks() : 0.0);
    return;
  }

  if (Geometries)
  {
    Geometries->PrepareFrameRendering();
    const glm::vec4 clear_color = Geometries->GetSkyColor();
    glClearColor(clear_color.r, clear_color.g, clear_color.b, clear_color.a);
  }
  glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT | GL_STENCIL_BUFFER_BIT);

  if (Geometries && Views)
  {
    Geometries->Paint(WindowWidth, WindowHeight,
                      static_cast<double>(Views->GetDurationUpdateMks()));
  }
}

void UWindowManager::HandleKeyEvent(KeyCode key, KeyState state, int Mods)
{
  const int glfw_key = static_cast<int>(key);
  int glfw_action = GLFW_RELEASE;
  if (state == KeyState::Pressed)
  {
    glfw_action = GLFW_PRESS;
  }
  else if (state == KeyState::Repeated)
  {
    glfw_action = GLFW_REPEAT;
  }
  if (Application && Application->RouteKey(glfw_key, glfw_action, Mods))
  {
    return;
  }

  if (Application && Application->WantsCaptureKeyboard())
  {
    return;
  }

  if (!World)
  {
    return;
  }

  if (Application && Application->GetState() != AppState::InGame)
  {
    return;
  }

  const bool key_down =
      (state == KeyState::Pressed || state == KeyState::Repeated);

  // Update key states in camera
  if (auto camera = World->GetCurrentUserCamera())
  {
    camera->UpdateKeyStatus(static_cast<int>(key), key_down);
    if (static_cast<int>(key) == GLFW_KEY_RIGHT_SHIFT)
    {
      camera->UpdateKeyStatus(GLFW_KEY_LEFT_SHIFT, key_down);
    }
  }

  // Special key processing
  if (state == KeyState::Pressed)
  {
    if (key == KeyCode::Key_Space)
    {
      if (auto camera = World->GetCurrentUserCamera())
      {
        CreatureHabitat habitat = CreatureHabitat::Terrestrial;
        if (UCreature *controlled = World->GetControlledCreature())
        {
          if (const CreatureDefinition *def =
                  World->GetCreatureDefinition(controlled->GetTypeId()))
          {
            habitat = def->habitat;
          }
        }
        if (ModePolicy::AllowsFlight(World->GetGameMode(), habitat) &&
            camera->TryToggleFlightOnDoubleSpace() && Geometries)
        {
          const std::string msg =
              camera->GetFreeMove()
                  ? "Flight ON (Space up, Shift down, 2xSpace off)"
                  : "Flight mode OFF";
          Geometries->ShowTransientMessage(msg, 2.5);
        }
      }
    }
    else if (key == KeyCode::Key_F12)
    {
#ifndef __ANDROID__
      if (Application && World &&
          ModePolicy::AllowsQaSpawner(World->GetGameMode()))
      {
        UCreatureVisualQaSpawner spawner(*World);
        const bool batch = (Mods & GLFW_MOD_SHIFT) != 0;
        const CreatureVisualQaSpawnResult spawnResult =
            batch ? spawner.SpawnAllInGrid() : spawner.SpawnNextSpecies();
        if (Geometries)
        {
          Geometries->ShowTransientMessage(spawnResult.Message, 2.5);
        }
      }
#endif
    }
    else if (key == KeyCode::Key_Delete)
    {
      if (BlockInput)
      {
        BlockInputContext ctx;
        ctx.World = World;
        ctx.Geometries = Geometries.get();
        ctx.Ui = Core ? &Core->GetUiSettings() : nullptr;
        ctx.Window = Window;
        ctx.App = Application.get();
        BlockInput->OnKeyDelete(ctx);
      }
    }
    else if (key == KeyCode::Key_F8)
    {
      const UWorld::WeatherType current = World->GetEnvironmentState().Weather;
      UWorld::WeatherType next = UWorld::WeatherType::Clear;
      switch (current)
      {
      case UWorld::WeatherType::Clear:
        next = UWorld::WeatherType::Rain;
        break;
      case UWorld::WeatherType::Rain:
        next = UWorld::WeatherType::Storm;
        break;
      case UWorld::WeatherType::Storm:
        next = UWorld::WeatherType::Snow;
        break;
      case UWorld::WeatherType::Snow:
        next = UWorld::WeatherType::Cloudy;
        break;
      case UWorld::WeatherType::Cloudy:
      default:
        next = UWorld::WeatherType::Clear;
        break;
      }
      World->SetWeather(next, 1.2f);
      World->SetWeatherOverlayEnabled(false);
      World->SetWeatherParticlesEnabled(true);
      if (Geometries)
      {
        Geometries->ShowTransientMessage(
            "Weather: " + UWorld::WeatherTypeToString(next), 2.2);
      }
    }
    else if (key == KeyCode::Key_F9)
    {
      if (Geometries)
      {
        Geometries->SetShowHud(!Geometries->GetShowHud());
      }
    }
    else if (key == KeyCode::Key_F10)
    {
      if (Geometries)
      {
        Geometries->SetShowPerformance(!Geometries->GetShowPerformance());
      }
    }
    else if (key == KeyCode::Key_F11)
    {
      if (Geometries)
      {
        Geometries->SetShowCrosshair(!Geometries->GetShowCrosshair());
      }
    }
  }
}

void UWindowManager::ResetGameplayMouseCapture()
{
  if (World && Window)
  {
    double x = 0.0;
    double y = 0.0;
    glfwGetCursorPos(Window, &x, &y);
    double fb_x = x;
    double fb_y = y;
    CursorWindowToFramebuffer(Window, x, y, fb_x, fb_y);
    if (auto camera = World->GetCurrentUserCamera())
    {
      camera->ResetMouseMove(fb_x, fb_y);
    }
  }
}

void UWindowManager::CancelGameplayPointerInteraction()
{
  if (!BlockInput || !World)
  {
    return;
  }
  BlockInputContext ctx;
  ctx.World = World;
  ctx.Geometries = Geometries.get();
  ctx.Ui = Core ? &Core->GetUiSettings() : nullptr;
  ctx.Window = Window;
  ctx.App = Application.get();
  BlockInput->CancelPointerInteraction(ctx);
}

void UWindowManager::HandleMouseButtonEvent(MouseButton Button, bool Pressed,
                                            glm::vec2 pos)
{
  const glm::ivec2 fbPos = CursorToFramebufferPixels(Window, pos.x, pos.y);
  const int glfwButton = Button == MouseButton::Left ? GLFW_MOUSE_BUTTON_LEFT
                         : Button == MouseButton::Right
                             ? GLFW_MOUSE_BUTTON_RIGHT
                             : GLFW_MOUSE_BUTTON_MIDDLE;

  if (Application &&
      Application->RouteMouseButton(glfwButton, Pressed, fbPos.x, fbPos.y))
  {
    return;
  }

  if (!World || !BlockInput)
  {
    return;
  }
  if (Application && Application->GetState() != AppState::InGame)
  {
    return;
  }

  if (Application && Application->WantsCaptureMouse())
  {
    const bool allowPlace = Button == MouseButton::Left && !Pressed &&
                            Application->AllowsWorldMousePlacement();
    if (!allowPlace)
    {
      return;
    }
  }

  BlockInputContext ctx;
  ctx.World = World;
  ctx.Geometries = Geometries.get();
  ctx.Ui = Core ? &Core->GetUiSettings() : nullptr;
  ctx.Window = Window;
  ctx.App = Application.get();
  BlockInput->OnMouseButton(
      Button, Pressed,
      glm::vec2(static_cast<float>(fbPos.x), static_cast<float>(fbPos.y)), ctx);
}

void UWindowManager::HandleMouseMoveEvent(glm::vec2 pos, glm::vec2 delta)
{
  (void)delta;
  const glm::ivec2 fbPos = CursorToFramebufferPixels(Window, pos.x, pos.y);
  if (Application && Application->RouteMouseMove(fbPos.x, fbPos.y))
  {
    return;
  }

  if (!World)
  {
    return;
  }
  if (Application && Application->GetState() != AppState::InGame)
  {
    return;
  }
  if (Application && Application->WantsCaptureMouse())
  {
    return;
  }

  BlockInputContext ctx;
  ctx.World = World;
  ctx.Geometries = Geometries.get();
  ctx.Ui = Core ? &Core->GetUiSettings() : nullptr;
  ctx.Window = Window;
  ctx.App = Application.get();
  BlockInput->OnMouseMove(
      glm::vec2(static_cast<float>(fbPos.x), static_cast<float>(fbPos.y)),
      delta, ctx);
}

void UWindowManager::HandleWindowResizeEvent(int width, int height)
{
  WindowWidth = width;
  WindowHeight = height;
  glViewport(0, 0, width, height);

  const float aspect =
      static_cast<float>(width) / static_cast<float>(height ? height : 1);
  if (Views)
  {
    if (auto camera = Views->GetActiveCamera())
    {
      camera->SetViewportSize(width, height);
    }
  }
  if (World)
  {
    if (auto camera = World->GetCurrentUserCamera())
    {
      camera->SetViewportSize(width, height);
    }
  }

  if (TextRenderer)
  {
    TextRenderer->SetWindowSize(width, height);
  }
}

void UWindowManager::SetInstances(std::shared_ptr<UCore> core,
                                  std::shared_ptr<UWorld> world,
                                  std::shared_ptr<UGeometryEngine> geometries,
                                  std::shared_ptr<UViewEngine> views)
{
  Core = core;
  World = world;
  Geometries = geometries;
  Views = views;
}

void UWindowManager::SetApplication(std::shared_ptr<UApplication> application)
{
  Application = std::move(application);
}

void UWindowManager::SetTextRenderer(
    std::shared_ptr<UTextRenderer> text_renderer)
{
  TextRenderer = text_renderer;
  if (TextRenderer)
  {
    TextRenderer->SetWindowSize(WindowWidth, WindowHeight);
  }
}

void UWindowManager::Shutdown()
{
  if (!IsInitialized)
  {
    return;
  }

  if (InputManager)
  {
    InputManager->Shutdown();
  }

  if (Window)
  {
    glfwMakeContextCurrent(Window);
  }

  if (FlightCapture)
  {
    FlightCapture->Shutdown();
    FlightCapture.reset();
  }

  if (World)
  {
    World->PrepareForShutdown();
  }

  if (Application)
  {
    Application->PrepareForShutdown();
  }

  Application.reset();

  TextRenderer.reset();
  Geometries.reset();
  Views.reset();
  World.reset();
  Core.reset();

  if (Window)
  {
    glfwDestroyWindow(Window);
    Window = nullptr;
  }

  glfwTerminate();
  IsInitialized = false;
  IsRunning = false;
}

void UWindowManager::RenderUI()
{
  if (!TextRenderer)
  {
    return;
  }

  UGlStateScope glGuard(kGlMaskOverlay2D);
  glDisable(GL_DEPTH_TEST);
  glEnable(GL_BLEND);
  glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);

  // Display hints
  RenderHelpText();
}

void UWindowManager::RenderHelpText()
{
  if (!TextRenderer)
    return;

  float y = WindowHeight - 30.0f; // Margin from top of screen
  float scale = 1.0f;
  glm::vec3 text_color(1.0f, 1.0f, 1.0f); // White color

  // Main control hints in English
  std::vector<std::string> help_lines = {
      "WASD - Move, Space - Jump, dbl Space - Fly, F5 - Cycle view, "
      "Q/E - Iso camera snap, RMB - Iso orbit, LMB - Place/break at cursor, "
      "RMB hold - Look, ` - Console",
      "Classic: mouse look, LMB break, RMB place; Cubatarium: RMB "
      "look",
      "Shift+F10 - Procedural world (from config), Shift+F12 - Heightmap, "
      "Shift+F11 - Flat",
      "Delete - Remove block, F8 weather, F9 HUD, F10 perf, F11 crosshair"};

  for (const auto &line : help_lines)
  {
    TextRenderer->RenderText(line, 10.0f, y, scale, text_color);
    y -= 25.0f; // Margin between lines
  }
}

void UWindowManager::SetWindowSize(int width, int height)
{
  if (Window)
  {
    glfwSetWindowSize(Window, width, height);
  }
}

void UWindowManager::SetFullscreen(bool fullscreen)
{
  if (Window)
  {
    GLFWmonitor *monitor = glfwGetPrimaryMonitor();
    const GLFWvidmode *mode = glfwGetVideoMode(monitor);

    if (fullscreen)
    {
      glfwSetWindowMonitor(Window, monitor, 0, 0, mode->width, mode->height,
                           mode->refreshRate);
    }
    else
    {
      glfwSetWindowMonitor(Window, nullptr, 100, 100, 1280, 720, 0);
    }
  }
}

bool UWindowManager::ShouldClose() const
{
  return glfwWindowShouldClose(Window);
}

// Methods for sky color management
void UWindowManager::SetSkyColor(float r, float g, float b, float a)
{
  SkyColor = glm::vec4(r, g, b, a);
  glClearColor(r, g, b, a);
  if (Geometries)
  {
    Geometries->SetSkyColor(r, g, b, a);
  }
}

void UWindowManager::SetSkyColor(const glm::vec4 &color)
{
  SkyColor = color;
  glClearColor(color.r, color.g, color.b, color.a);
  if (Geometries)
  {
    Geometries->SetSkyColor(color.r, color.g, color.b, color.a);
  }
}

glm::vec4 UWindowManager::GetSkyColor() const { return SkyColor; }

void UWindowManager::SetGradientSky(bool useGradient)
{
  UseGradientSky = useGradient;
  if (Geometries)
  {
    Geometries->SetGradientSky(useGradient);
  }
}

bool UWindowManager::IsGradientSky() const { return UseGradientSky; }

// GLFW callback functions
void UWindowManager::FramebufferSizeCallback(GLFWwindow *window, int width,
                                             int height)
{
  UInputManager::GLFWFramebufferSizeCallback(window, width, height);
}

void UWindowManager::KeyCallback(GLFWwindow *window, int key, int scancode,
                                 int Action, int Mods)
{
  UInputManager::GLFWKeyCallback(window, key, scancode, Action, Mods);
}

void UWindowManager::MouseButtonCallback(GLFWwindow *window, int Button,
                                         int Action, int Mods)
{
  UInputManager::GLFWMouseButtonCallback(window, Button, Action, Mods);
}

void UWindowManager::CursorPosCallback(GLFWwindow *window, double xpos,
                                       double ypos)
{
  UInputManager::GLFWCursorPosCallback(window, xpos, ypos);
}

void UWindowManager::ScrollCallback(GLFWwindow *window, double Xoffset,
                                    double Yoffset)
{
  UInputManager::GLFWScrollCallback(window, Xoffset, Yoffset);
}

void UWindowManager::ErrorCallback(int error, const char *description)
{
  std::ostringstream oss;
  oss << "GLFW error " << error << ": "
      << (description ? description : "(no description)");
  CubatariumLogInfo("GLFW", oss.str());
}

void UWindowManager::WindowCloseCallback(GLFWwindow *w)
{
  auto *self = static_cast<UWindowManager *>(glfwGetWindowUserPointer(w));
  if (self && self->Application)
  {
    if (self->Application->TryBeginShutdownFromWindowClose())
    {
      glfwSetWindowShouldClose(w, GLFW_FALSE);
      return;
    }
  }
  if (self && self->Core)
  {
    self->Core->SaveSystem("config.json");
  }
}

} // namespace cutum
