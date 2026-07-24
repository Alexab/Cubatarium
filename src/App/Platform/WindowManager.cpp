#include "App/Platform/WindowManager.h"
#include "App/Application.h"
#include "App/Core.h"
#include "App/Settings/AppState.h"
#include "App/Platform/InputManager.h"
#include "App/Platform/Log.h"
#include "Blocks/Input/BlockInputController.h"
#include "Creatures/Core/Creature.h"
#include "Creatures/Core/CreatureInventory.h"
#include "Creatures/Player/User.h"
#include "Game/CreatureVisualQaSpawner.h"
#include "Game/Inventory/InventoryTypes.h"
#include "Gui/Core/GuiMetrics.h"
#include "Gui/Interfaces/IUInventoryViewModel.h"
#include "Render/Engine/GeometryEngine.h"
#include "Render/Engine/ViewEngine.h"
#include "Render/Pipeline/GlStateMask.h"
#include "Render/Pipeline/GlStateScope.h"
#include "ThirdParty/stb_image.h"
#include "World/Core/World.h"
#include "World/Diagnostics/FramePerfMonitor.h"
#include "World/Math/BlockTypes.h"
#include "WorldGen/Core/ProceduralSettings.h"
#include "Core/Progress/IUProgressSink.h"
#include <cmath>
#include <iostream>
#include <sstream>
#include <stdexcept>
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

} // namespace

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
#endif

  // Настройка OpenGL
  InitializeOpenGL();

  InputManager = std::make_shared<UInputManager>();

  // TextRenderer will be set later via SetTextRenderer

  // Настройка callbacks
  SetupCallbacks();

  // Input manager creation
  InputManager->Initialize(Window);

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
  CubatariumLogInfo("Window",
                    std::string("ApplyPresentSettings vsync=") +
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

  while (!glfwWindowShouldClose(Window) && IsRunning)
  {
    const auto frame_begin = std::chrono::high_resolution_clock::now();
    DeltaTime =
        std::chrono::duration<double>(frame_begin - LastFrameTime).count();
    LastFrameTime = frame_begin;

    glfwPollEvents();

    if (World)
    {
      World->SetWallFrameDelta(DeltaTime);
    }

    // Input processing
    const auto input_begin = std::chrono::high_resolution_clock::now();
    ProcessInput();
    const double input_ms = std::chrono::duration<double, std::milli>(
                                std::chrono::high_resolution_clock::now() -
                                input_begin)
                                .count();
    const auto app_begin = std::chrono::high_resolution_clock::now();
    if (Application)
    {
      Application->Update(DeltaTime);
    }
    const double app_ms = std::chrono::duration<double, std::milli>(
                              std::chrono::high_resolution_clock::now() -
                              app_begin)
                              .count();

    // Logic update (includes DoMovement → phys_ms)
    const auto world_begin = std::chrono::high_resolution_clock::now();
    Update();
    const double world_ms = std::chrono::duration<double, std::milli>(
                                std::chrono::high_resolution_clock::now() -
                                world_begin)
                                .count();
    if (World)
    {
      World->SetLastInputMs(input_ms);
      World->SetLastAppUpdateMs(app_ms);
      World->SetLastWorldTickMs(world_ms);
    }

    // Outside world_ms so autosave Init/Ticks do not inflate world_extra.
    TickBudgetedAutosave();

    // Rendering
    Render();

    const auto swap_begin = std::chrono::high_resolution_clock::now();
    glfwSwapBuffers(Window);
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
      // Same-frame wall for perf log (HUD still uses inter-frame DeltaTime).
      World->SetWallFrameDelta(frame_wall_ms / 1000.0);
    }
    if (World && Application && Application->GetState() == AppState::InGame)
    {
      double interval = 2.0;
      if (Core)
      {
        interval = Core->GetUiSettings().PerfLogIntervalSec;
      }
      UFramePerfMonitor::OnInGameFrame(*World, swap_wait_ms, interval);
      // Restore inter-frame delta for gameplay timing consistency next frame.
      World->SetWallFrameDelta(DeltaTime);
    }

    if (StopPredicate && StopPredicate())
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
    tele.BlockInputMs = 0.0;
    tele.TickEnvMs = 0.0;
    tele.BreakCompleteN = 0;
    tele.BreakInflightRaceN = 0;
    tele.BreakDarkFaceN = 0;
    tele.PlaceCompleteN = 0;
    tele.PlaceEmissionN = 0;
    tele.EditLightEmission = 0;
    tele.FastRelightMs = 0.0;
    tele.EditToFirstMeshMs = 0.0;
  }

  if (Views)
  {
    const auto t0 = clock::now();
    Views->UpdateFrameTime();
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
      World->DoMovement();
      World->GetPhysicsTelemetryMutable().DoMovementMs =
          std::chrono::duration<double, std::milli>(clock::now() - t0).count();
    }
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
        // Standing ocean save may look at empty air/water; break underfeet solid.
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
        World->StartBreakSession(target);
        World->CompleteBreakSession();
      }
    }
    if (BlockInput)
    {
      const auto t0 = clock::now();
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
      AutosaveRequested = true;
      LastAutosaveTime = now;
    }
  }
  else
  {
    SeenInGameForAutosave = false;
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
  if (AutosaveRequested && !AutosaveInProgress)
  {
    AutosaveRequested = false;
    const std::string folder = Core->GetActiveWorldFolder().string();
    if (folder.empty() || World->HasActiveCooperativeOperation())
    {
      return;
    }
    World->BeginCooperativeSave(folder);
    AutosaveInProgress = true;
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
  UNullProgressSink sink;
  if (World->TickCooperativeSave(sink, /*chunkBudget=*/8))
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
        if (camera->TryToggleFlightOnDoubleSpace() && Geometries)
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
      if (Application && Application->GetGameSession().GetInventoryMode() ==
                             InventoryMode::Creative)
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
    if (auto camera = World->GetCurrentUserCamera())
    {
      camera->ResetMouseMove(x, y);
    }
  }
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
  BlockInput->OnMouseButton(Button, Pressed, pos, ctx);
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
  BlockInput->OnMouseMove(pos, delta, ctx);
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
      "Q/E - Iso camera snap (isometric), "
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
