#include "App/Platform/WindowManager.h"
#include "App/Application.h"
#include "App/Core.h"
#include "App/Platform/InputManager.h"
#include "App/Platform/Log.h"
#include "App/Settings/AppState.h"
#include "Blocks/Input/BlockInputController.h"
#include "Creatures/Core/Creature.h"
#include "Creatures/Core/CreatureInventory.h"
#include "Creatures/Player/User.h"
#include "Game/Inventory/InventoryTypes.h"
#include "Render/Engine/GeometryEngine.h"
#include "Render/Engine/ViewEngine.h"
#include "World/Core/World.h"
#include "WorldGen/Core/ProceduralSettings.h"
#include "ThirdParty/stb_image.h"
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

bool UWindowManager::Initialize(int width, int height, const char *title)
{
  glfwSetErrorCallback(ErrorCallback);

  if (!glfwInit())
  {
    CubatariumLogError("Window", "Failed to initialize GLFW");
    return false;
  }

  auto createWindow = [&](bool multisample) -> GLFWwindow * {
    glfwDefaultWindowHints();
    glfwWindowHint(GLFW_CONTEXT_VERSION_MAJOR, 3);
    glfwWindowHint(GLFW_CONTEXT_VERSION_MINOR, 3);
    glfwWindowHint(GLFW_OPENGL_PROFILE, GLFW_OPENGL_CORE_PROFILE);
    if (multisample)
    {
      glfwWindowHint(GLFW_SAMPLES, 4);
    }
#ifdef __APPLE__
    glfwWindowHint(GLFW_OPENGL_FORWARD_COMPAT, GL_TRUE);
#endif
    return glfwCreateWindow(width, height, title, nullptr, nullptr);
  };

  Window = createWindow(true);
  if (!Window)
  {
    CubatariumLogInfo("Window",
                      "OpenGL window with MSAA failed, retrying without MSAA");
    Window = createWindow(false);
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

  // Enable MSAA
  glEnable(GL_MULTISAMPLE);

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

void UWindowManager::Run()
{
  if (!IsInitialized)
  {
    std::cerr << "WindowManager not initialized" << std::endl;
    return;
  }

  IsRunning = true;

  while (!glfwWindowShouldClose(Window) && IsRunning)
  {
    // Time update
    auto current_time = std::chrono::high_resolution_clock::now();
    DeltaTime =
        std::chrono::duration<double>(current_time - LastFrameTime).count();
    LastFrameTime = current_time;

    glfwPollEvents();

    // Input processing
    ProcessInput();
    if (Application)
    {
      Application->Update(DeltaTime);
    }

    // Logic update
    Update();

    // Rendering
    Render();

    glfwSwapBuffers(Window);
  }
}

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
      const bool shift_down =
          InputManager->IsKeyPressed(KeyCode::Key_Shift) ||
          (Window && glfwGetKey(Window, GLFW_KEY_RIGHT_SHIFT) == GLFW_PRESS);
      const bool left_ctrl_down =
          InputManager->IsKeyPressed(KeyCode::Key_Ctrl);
      const bool right_ctrl_down =
          Window &&
          glfwGetKey(Window, GLFW_KEY_RIGHT_CONTROL) == GLFW_PRESS;
      camera->UpdateKeyStatus(static_cast<int>(KeyCode::Key_W),
                              InputManager->IsKeyPressed(KeyCode::Key_W));
      camera->UpdateKeyStatus(static_cast<int>(KeyCode::Key_S),
                              InputManager->IsKeyPressed(KeyCode::Key_S));
      camera->UpdateKeyStatus(static_cast<int>(KeyCode::Key_A),
                              InputManager->IsKeyPressed(KeyCode::Key_A));
      camera->UpdateKeyStatus(static_cast<int>(KeyCode::Key_D),
                              InputManager->IsKeyPressed(KeyCode::Key_D));
      camera->UpdateKeyStatus(static_cast<int>(KeyCode::Key_Space),
                              InputManager->IsKeyPressed(KeyCode::Key_Space));
      camera->UpdateKeyStatus(GLFW_KEY_LEFT_SHIFT, shift_down);
      camera->UpdateKeyStatus(GLFW_KEY_RIGHT_SHIFT, shift_down);
      camera->UpdateKeyStatus(GLFW_KEY_LEFT_CONTROL, left_ctrl_down);
      camera->UpdateKeyStatus(GLFW_KEY_RIGHT_CONTROL, right_ctrl_down);
    }
  }
}

void UWindowManager::Update()
{
  if (Views)
  {
    Views->UpdateFrameTime();
  }

  if (World && Application && Application->GetState() == AppState::InGame)
  {
    World->DoMovement();
    if (BlockInput)
    {
      BlockInputContext ctx;
      ctx.World = World;
      ctx.Geometries = Geometries.get();
      ctx.Ui = Core ? &Core->GetUiSettings() : nullptr;
      ctx.Window = Window;
      ctx.App = Application.get();
      BlockInput->Tick(static_cast<float>(DeltaTime), ctx);
    }
  }

  if (Core && World && Application &&
      Application->GetState() == AppState::InGame)
  {
    const auto now = std::chrono::steady_clock::now();
    const double elapsed =
        std::chrono::duration<double>(now - LastAutosaveTime).count();
    if (elapsed >= KAutosaveIntervalSec)
    {
      Core->SaveWorld(World->GetWorldName());
      LastAutosaveTime = now;
    }
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
      // reserved
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
    else if (key == KeyCode::Key_F1)
    {
      SetSkyColor(0.5f, 0.7f, 1.0f, 1.0f); // Blue sky
    }
    else if (key == KeyCode::Key_F2)
    {
      SetSkyColor(1.0f, 0.6f, 0.3f, 1.0f); // Orange sky
    }
    else if (key == KeyCode::Key_F3)
    {
      SetSkyColor(0.1f, 0.1f, 0.3f, 1.0f); // Dark blue sky
    }
    else if (key == KeyCode::Key_F4)
    {
      SetSkyColor(0.6f, 0.6f, 0.6f, 1.0f); // Gray sky
    }
    else if (key == KeyCode::Key_F6)
    {
      SetSkyColor(1.0f, 0.6f, 0.3f, 1.0f);
      SetGradientSky(true);
    }
    else if (key == KeyCode::Key_F7)
    {
      if (auto anchor = World->FindPrefabAnchorFromView(
              World->GetCurrentUserCamera()->GetPosition(),
              World->GetCurrentUserCamera()->GetFront()))
      {
        World->PlacePrefab("tree_small", anchor.value());
      }
    }
    else if (key == KeyCode::Key_F8)
    {
      SetSkyColor(0.6f, 0.6f, 0.6f, 1.0f);
      SetGradientSky(true);
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
      camera->SetAspectRatio(aspect);
    }
  }
  if (World)
  {
    if (auto camera = World->GetCurrentUserCamera())
    {
      camera->SetAspectRatio(aspect);
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
  if (InputManager)
  {
    InputManager->Shutdown();
  }

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

  // Save current OpenGL state
  GLboolean depthTestEnabled;
  glGetBooleanv(GL_DEPTH_TEST, &depthTestEnabled);
  GLboolean blendEnabled;
  glGetBooleanv(GL_BLEND, &blendEnabled);

  // Configure OpenGL for 2D rendering
  glDisable(GL_DEPTH_TEST);
  glEnable(GL_BLEND);
  glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);

  // Display hints
  RenderHelpText();

  // Restore OpenGL state
  if (depthTestEnabled)
  {
    glEnable(GL_DEPTH_TEST);
  }
  else
  {
    glDisable(GL_DEPTH_TEST);
  }

  if (blendEnabled)
  {
    glEnable(GL_BLEND);
  }
  else
  {
    glDisable(GL_BLEND);
  }
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
      "WASD - Move, Space - Jump, dbl Space - Fly, F5 - Toggle perspective, "
      "RMB hold - Look, ` - Console",
      "Classic (Minecraft): mouse look, LMB break, RMB place; Cubatarium: RMB "
      "look",
      "Shift+F10 - Procedural world (from config), Shift+F12 - Heightmap, "
      "Shift+F11 - Flat",
      "Delete - Remove block, F9 HUD, F10 perf, F11 crosshair"};

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
    self->Application->GetGameSession().SaveCommandHistory();
  }
  if (self && self->Core)
  {
    self->Core->SaveSystem("config.json");
  }
}

} // namespace cutum
