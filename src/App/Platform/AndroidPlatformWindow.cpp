#include "App/Platform/AndroidPlatformWindow.h"

#include "App/Application.h"
#include "App/Core.h"
#include "App/Platform/InputManager.h"
#include "App/Platform/Log.h"
#include "App/Settings/AppState.h"
#include "App/Settings/UiSettings.h"
#include "Creatures/Player/User.h"
#include "Game/GameSession.h"
#include "Gui/Core/GuiScale.h"
#include "Render/Engine/GeometryEngine.h"
#include "Render/Engine/ViewEngine.h"
#include "Render/GlIncludes.h"
#include "Render/Camera/Camera.h"
#include "World/Core/World.h"
#include "android_jni.h"
#include "android_soft_keyboard.h"

#include <android/configuration.h>
#include <android/input.h>
#include <game-activity/GameActivity.h>
#include <game-activity/GameActivityEvents.h>
#include <game-activity/native_app_glue/android_native_app_glue.h>

#include <algorithm>

namespace cutum
{

namespace
{

int NormalizePointerIndex(int pointer)
{
  if (pointer < 0)
  {
    return 0;
  }
  if (pointer >= UTouchInputBridge::kMaxPointers)
  {
    return UTouchInputBridge::kMaxPointers - 1;
  }
  return pointer;
}

void QueryViewportInsets(android_app *app, int &left, int &top, int &right,
                         int &bottom)
{
  left = top = right = bottom = 0;
  if (!app || !app->activity)
  {
    return;
  }
  ARect systemBars{};
  GameActivity_getWindowInsets(reinterpret_cast<GameActivity *>(app->activity),
                               GAMECOMMON_INSETS_TYPE_SYSTEM_BARS, &systemBars);
  left = systemBars.left;
  top = systemBars.top;
  right = systemBars.right;
  bottom = systemBars.bottom;
}

int QueryKeyboardInsetBottom(android_app *app)
{
  if (!app || !app->activity)
  {
    return 0;
  }
  auto *activity = reinterpret_cast<GameActivity *>(app->activity);
  ARect ime{};
  ARect systemBars{};
  GameActivity_getWindowInsets(activity, GAMECOMMON_INSETS_TYPE_IME, &ime);
  GameActivity_getWindowInsets(activity, GAMECOMMON_INSETS_TYPE_SYSTEM_BARS,
                               &systemBars);
  return std::max(0, ime.bottom - systemBars.bottom);
}

int QueryDensityDpi(android_app *app)
{
  if (!app || !app->config)
  {
    return 0;
  }
  switch (AConfiguration_getDensity(app->config))
  {
  case ACONFIGURATION_DENSITY_LOW:
    return 120;
  case ACONFIGURATION_DENSITY_MEDIUM:
    return 160;
  case ACONFIGURATION_DENSITY_HIGH:
    return 240;
  case ACONFIGURATION_DENSITY_XHIGH:
    return 320;
  case ACONFIGURATION_DENSITY_XXHIGH:
    return 480;
  case ACONFIGURATION_DENSITY_XXXHIGH:
    return 640;
  default:
    return 160;
  }
}

} // namespace

UAndroidPlatformWindow::UAndroidPlatformWindow(android_app *app) : App(app)
{
  LastFrame = std::chrono::high_resolution_clock::now();
  LastAutosave = std::chrono::steady_clock::now();
  BlockInput = std::make_unique<UBlockInputController>();
}

bool UAndroidPlatformWindow::Initialize(int width, int height,
                                        const char *title)
{
  (void)title;
  Width = width;
  Height = height;
  Touch.SetScreenSize(Width, Height);
  if (Egl.EnsureCurrent())
  {
    glEnable(GL_DEPTH_TEST);
    glEnable(GL_BLEND);
    glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
    glClearColor(0.5f, 0.7f, 1.0f, 1.0f);
  }
  Initialized = true;
  return true;
}

bool UAndroidPlatformWindow::InitEgl(android_app *app)
{
  return Egl.Initialize(app);
}

void UAndroidPlatformWindow::OnAppCmd(int32_t cmd)
{
  switch (cmd)
  {
  case APP_CMD_INIT_WINDOW:
    if (App && App->window)
    {
      if (!Egl.HasSurface())
      {
        if (!InitEgl(App))
        {
          CubatariumLogError("Android",
                             "EGL init failed on APP_CMD_INIT_WINDOW");
        }
      }
      else
      {
        Egl.EnsureCurrent();
      }
    }
    break;
  case APP_CMD_TERM_WINDOW:
    Touch.ResetJoystick();
    Egl.Shutdown();
    break;
  case APP_CMD_WINDOW_RESIZED:
  case APP_CMD_CONTENT_RECT_CHANGED:
    if (Egl.HasSurface())
    {
      Egl.UpdateSurfaceSize();
    }
    break;
  case APP_CMD_EDITOR_ACTION:
    if (Application)
    {
      AndroidSoftKeyboardHandleEditorAction(Application.get(),
                                            App ? App->editorAction : 0);
    }
    break;
  default:
    break;
  }
}

void UAndroidPlatformWindow::Run()
{
  if (!App)
  {
    return;
  }
  AndroidSoftKeyboardAttachApp(App);
  while (Running && App->destroyRequested == 0)
  {
    if (Application && Application->IsQuitRequested())
    {
      Running = false;
      break;
    }
    int events = 0;
    android_poll_source *source = nullptr;
    while (ALooper_pollOnce(Egl.HasSurface() ? 0 : -1, nullptr, &events,
                            reinterpret_cast<void **>(&source)) >= 0)
    {
      if (source)
      {
        source->process(App, source);
      }
      if (App->destroyRequested != 0)
      {
        break;
      }
    }
    if (App->window && !Egl.HasSurface())
    {
      InitEgl(App);
    }
    if (Egl.HasSurface())
    {
      Egl.EnsureCurrent();
      ProcessFrame();
    }
  }
  if (Application && Application->IsQuitRequested())
  {
    CubatariumAndroidFinishActivity();
  }
}

void UAndroidPlatformWindow::ProcessFrame()
{
  try
  {
    const auto now = std::chrono::high_resolution_clock::now();
    FrameDeltaTime = std::chrono::duration<double>(now - LastFrame).count();
    LastFrame = now;

    if (App)
    {
      if (android_input_buffer *input = android_app_swap_input_buffers(App))
      {
        ProcessInputBuffer(input);
        android_app_clear_motion_events(input);
        android_app_clear_key_events(input);
      }
    }

    ProcessInput();
    if (World)
    {
      World->SetWallFrameDelta(FrameDeltaTime);
    }
    if (Application)
    {
      AndroidSoftKeyboardProcess(Application.get());
      Application->Update(FrameDeltaTime);
    }
    Update();
    Render();
    SwapBuffers();
    Touch.Update();
  }
  catch (const std::exception &e)
  {
    CubatariumLogError("Android",
                       std::string("ProcessFrame exception: ") + e.what());
  }
  catch (...)
  {
    CubatariumLogError("Android", "ProcessFrame unknown exception");
  }
}

void UAndroidPlatformWindow::PollEvents() {}

void UAndroidPlatformWindow::SwapBuffers() { Egl.SwapBuffers(); }

glm::ivec2 UAndroidPlatformWindow::GetFramebufferSize() const
{
  return {Egl.Width(), Egl.Height()};
}

bool UAndroidPlatformWindow::ShouldClose() const { return !Running; }

void UAndroidPlatformWindow::RequestClose() { Running = false; }

void UAndroidPlatformWindow::Shutdown() { Egl.Shutdown(); }

void UAndroidPlatformWindow::SetInstances(
    std::shared_ptr<UCore> core, std::shared_ptr<UWorld> world,
    std::shared_ptr<UGeometryEngine> geometries,
    std::shared_ptr<UViewEngine> views)
{
  Core = std::move(core);
  World = std::move(world);
  Geometries = std::move(geometries);
  Views = std::move(views);
}

void UAndroidPlatformWindow::SetApplication(
    std::shared_ptr<UApplication> application)
{
  Application = std::move(application);
  if (Application)
  {
    Application->SetTouchInputBridge(&Touch);
  }
}

void UAndroidPlatformWindow::SetTextRenderer(
    std::shared_ptr<UTextRenderer> text_renderer)
{
  TextRenderer = std::move(text_renderer);
}

void UAndroidPlatformWindow::SetCharCallback(CharCallbackFn callback)
{
  CharCallback = std::move(callback);
}

void UAndroidPlatformWindow::ProcessInput()
{
  if (Application && Application->WantsCaptureKeyboard())
  {
    return;
  }
  if (!World || !Application || Application->GetState() != AppState::InGame)
  {
    return;
  }

  BlockInputContext ctx;
  ctx.World = World;
  ctx.Geometries = Geometries.get();
  ctx.Ui = Core ? &Core->GetUiSettings() : nullptr;
  ctx.App = Application.get();
  glm::vec2 pos = Touch.GetMousePosition();

  if (Touch.ConsumeBlockInputCancel() && BlockInput)
  {
    BlockInput->CancelPointerInteraction(ctx);
  }

  glm::vec2 placeTapPos{};
  if (Touch.ConsumePendingPlaceTap(placeTapPos))
  {
    pos = placeTapPos;
    if (BlockInput && World)
    {
      if (auto camera = World->GetCurrentUserCamera())
      {
        World->UpdateIntersection(camera->GetPosition(), camera->GetFront());
      }
      BlockInput->OnQuickTap(ctx);
    }
  }

  const glm::vec2 lookDelta = Touch.ConsumeMouseDelta();

  if (auto camera = World->GetCurrentUserCamera())
  {
    float baselineX = 0.f;
    float baselineY = 0.f;
    if (Touch.ConsumeCameraBaseline(baselineX, baselineY))
    {
      camera->ResetMouseMove(static_cast<double>(baselineX),
                             static_cast<double>(baselineY));
    }

    camera->UpdateKeyStatus(static_cast<int>(KeyCode::Key_W),
                            Touch.IsKeyPressed(KeyCode::Key_W));
    camera->UpdateKeyStatus(static_cast<int>(KeyCode::Key_S),
                            Touch.IsKeyPressed(KeyCode::Key_S));
    camera->UpdateKeyStatus(static_cast<int>(KeyCode::Key_A),
                            Touch.IsKeyPressed(KeyCode::Key_A));
    camera->UpdateKeyStatus(static_cast<int>(KeyCode::Key_D),
                            Touch.IsKeyPressed(KeyCode::Key_D));
    camera->UpdateKeyStatus(static_cast<int>(KeyCode::Key_Space),
                            Touch.IsKeyPressed(KeyCode::Key_Space));
    const bool shiftDown = Touch.IsKeyPressed(KeyCode::Key_Shift);
    camera->UpdateKeyStatus(GLFW_KEY_LEFT_SHIFT, shiftDown);
    camera->UpdateKeyStatus(GLFW_KEY_RIGHT_SHIFT, shiftDown);
    camera->SetSprintActive(Touch.IsSprintActive());
    if (lookDelta.x != 0.f || lookDelta.y != 0.f)
    {
      if (!Application->WantsCaptureMouse())
      {
        camera->ApplyRelativeMouseMove(lookDelta.x, -lookDelta.y);
        World->UpdateIntersection(camera->GetPosition(), camera->GetFront());
      }
    }
  }

  if (!Application->WantsCaptureMouse() && BlockInput)
  {
    if (Touch.IsMouseButtonJustPressed(MouseButton::Left))
    {
      BlockInput->OnMouseButton(MouseButton::Left, true, pos, ctx);
    }
    if (Touch.IsMouseButtonJustReleased(MouseButton::Left))
    {
      BlockInput->OnMouseButton(MouseButton::Left, false, pos, ctx);
    }
    if (lookDelta.x != 0.f || lookDelta.y != 0.f)
    {
      BlockInput->OnMouseMove(pos, lookDelta, ctx);
    }
  }
}

void UAndroidPlatformWindow::Update()
{
  if (Views)
  {
    Views->UpdateFrameTime();
  }
  if (World && Application && Application->GetState() == AppState::InGame)
  {
    if (!World->IsStepUpEnabled())
    {
      World->SetStepUpEnabled(true);
    }
    World->DoMovement();
    if (BlockInput)
    {
      BlockInputContext ctx;
      ctx.World = World;
      ctx.Geometries = Geometries.get();
      ctx.Ui = Core ? &Core->GetUiSettings() : nullptr;
      ctx.App = Application.get();
      BlockInput->Tick(static_cast<float>(FrameDeltaTime), ctx);
    }
    if (Core)
    {
      const auto now = std::chrono::steady_clock::now();
      const double elapsed =
          std::chrono::duration<double>(now - LastAutosave).count();
      if (elapsed >= 60.0)
      {
        Core->SaveWorld(World->GetWorldName());
        LastAutosave = now;
      }
    }
  }
}

void UAndroidPlatformWindow::Render()
{
  if (!Application)
  {
    return;
  }
  Egl.UpdateSurfaceSize();
  const auto size = GetFramebufferSize();
  Width = size.x;
  Height = size.y;
  int insetLeft = 0;
  int insetTop = 0;
  int insetRight = 0;
  int insetBottom = 0;
  QueryViewportInsets(App, insetLeft, insetTop, insetRight, insetBottom);
  Touch.SetScreenSize(Width, Height);
  Touch.SetContentInsets(insetLeft, insetTop, insetRight, insetBottom);
  static int lastDensityDpi = 0;
  static int last_ui_width = 0;
  static int last_ui_height = 0;
  const int densityDpi = QueryDensityDpi(App);
  PlatformUiMetrics platform;
  platform.DensityDpi = densityDpi;
  platform.ScreenWidthPx = Width;
  platform.ScreenHeightPx = Height;
  if (densityDpi != lastDensityDpi || Width != last_ui_width ||
      Height != last_ui_height)
  {
    Application->UpdateUiScale(Width, Height, platform);
    lastDensityDpi = densityDpi;
    last_ui_width = Width;
    last_ui_height = Height;
  }
  if (Core)
  {
    const UiSettings &ui = Core->GetUiSettings();
    Touch.SetPlaceClickMaxSeconds(ui.PlaceClickMaxSeconds);
    Touch.SetBreakHoldMinSeconds(ui.BreakHoldMinSeconds);
    Touch.SetUiScale(Application->GetUiScale());
  }
  Application->SetViewportInsets(insetLeft, insetTop, insetRight, insetBottom);
  Application->SetKeyboardInsetBottom(QueryKeyboardInsetBottom(App));
  Application->RenderFrame(size.x, size.y,
                           Views ? Views->GetDurationUpdateMks() : 0.0);
}

bool UAndroidPlatformWindow::HandleGameMotionEvent(
    const GameActivityMotionEvent &event)
{
  const int Action = event.action;
  const int masked = Action & AMOTION_EVENT_ACTION_MASK;
  const int pointer = (Action & AMOTION_EVENT_ACTION_POINTER_INDEX_MASK) >>
                      AMOTION_EVENT_ACTION_POINTER_INDEX_SHIFT;
  if (pointer < 0 || static_cast<uint32_t>(pointer) >= event.pointerCount)
  {
    return false;
  }
  const float x = GameActivityPointerAxes_getX(&event.pointers[pointer]);
  const float y = GameActivityPointerAxes_getY(&event.pointers[pointer]);
  const int pointerId = static_cast<int>(event.pointers[pointer].id);
  if (masked == AMOTION_EVENT_ACTION_DOWN ||
      masked == AMOTION_EVENT_ACTION_POINTER_DOWN)
  {
    bool uiConsumed = false;
    if (Application)
    {
      uiConsumed = Application->RouteMouseButton(
          static_cast<int>(MouseButton::Left), true, static_cast<int>(x),
          static_cast<int>(y), pointerId);
    }
    const int pointerIndex = NormalizePointerIndex(pointerId);
    UiPointerCapture[pointerIndex] = uiConsumed;
    Touch.OnTouchDown(pointerId, x, y, !uiConsumed);
  }
  else if (masked == AMOTION_EVENT_ACTION_MOVE)
  {
    for (uint32_t i = 0; i < event.pointerCount; ++i)
    {
      const float px = GameActivityPointerAxes_getX(&event.pointers[i]);
      const float py = GameActivityPointerAxes_getY(&event.pointers[i]);
      const int movePointerId = static_cast<int>(event.pointers[i].id);
      const int pointerIndex = NormalizePointerIndex(movePointerId);
      Touch.OnTouchMove(movePointerId, px, py,
                        !UiPointerCapture[pointerIndex]);
      if (Application && UiPointerCapture[pointerIndex])
      {
        Application->RouteMouseMove(static_cast<int>(px), static_cast<int>(py),
                                    movePointerId);
      }
    }
  }
  else if (masked == AMOTION_EVENT_ACTION_UP ||
           masked == AMOTION_EVENT_ACTION_POINTER_UP)
  {
    const int pointerIndex = NormalizePointerIndex(pointerId);
    if (Application)
    {
      Application->RouteMouseButton(static_cast<int>(MouseButton::Left), false,
                                    static_cast<int>(x), static_cast<int>(y),
                                    pointerId);
    }
    Touch.OnTouchUp(pointerId, x, y);
    UiPointerCapture[pointerIndex] = false;
  }
  else if (masked == AMOTION_EVENT_ACTION_CANCEL)
  {
    for (uint32_t i = 0; i < event.pointerCount; ++i)
    {
      const int cancelPointerId = static_cast<int>(event.pointers[i].id);
      const float cx = GameActivityPointerAxes_getX(&event.pointers[i]);
      const float cy = GameActivityPointerAxes_getY(&event.pointers[i]);
      const int pointerIndex = NormalizePointerIndex(cancelPointerId);
      if (Application)
      {
        Application->RouteMouseButton(
            static_cast<int>(MouseButton::Left), false, static_cast<int>(cx),
            static_cast<int>(cy), cancelPointerId);
      }
      Touch.OnTouchUp(cancelPointerId, cx, cy, true);
      UiPointerCapture[pointerIndex] = false;
    }
    Touch.ResetJoystick();
  }
  return true;
}

void UAndroidPlatformWindow::ProcessInputBuffer(android_input_buffer *buffer)
{
  if (!buffer)
  {
    return;
  }
  for (uint64_t i = 0; i < buffer->motionEventsCount; ++i)
  {
    HandleGameMotionEvent(buffer->motionEvents[i]);
  }
  for (uint64_t i = 0; i < buffer->keyEventsCount; ++i)
  {
    const GameActivityKeyEvent &event = buffer->keyEvents[i];
    if (event.keyCode != AKEYCODE_BACK || !Application)
    {
      continue;
    }
    int action = 0;
    if (event.action == AKEY_EVENT_ACTION_DOWN)
    {
      action = GLFW_PRESS;
    }
    else if (event.action == AKEY_EVENT_ACTION_UP)
    {
      action = GLFW_RELEASE;
    }
    else
    {
      continue;
    }
    Application->RouteKey(GLFW_KEY_ESCAPE, action, 0);
  }
}

} // namespace cutum
