#include "App/Platform/AndroidPlatformWindow.h"

#include "App/Application.h"
#include "android_jni.h"
#include "android_soft_keyboard.h"
#include "App/Core.h"
#include "Gui/Core/GuiScale.h"
#include "App/Platform/InputManager.h"
#include "App/Platform/Log.h"
#include "App/Settings/AppState.h"
#include "App/Settings/UiSettings.h"
#include "Creatures/Player/User.h"
#include "Game/GameSession.h"
#include "Render/Camera/Camera.h"
#include "Render/Engine/GeometryEngine.h"
#include "Render/Engine/ViewEngine.h"
#include "Render/GlIncludes.h"
#include "World/Core/World.h"

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
  if (pointer >= TouchInputBridge::kMaxPointers)
  {
    return TouchInputBridge::kMaxPointers - 1;
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
                               GAMECOMMON_INSETS_TYPE_SYSTEM_BARS,
                               &systemBars);
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

AndroidPlatformWindow::AndroidPlatformWindow(android_app *app) : app_(app)
{
  lastFrame_ = std::chrono::high_resolution_clock::now();
  lastAutosave_ = std::chrono::steady_clock::now();
  blockInput_ = std::make_unique<UBlockInputController>();
}

bool AndroidPlatformWindow::Initialize(int width, int height, const char *title)
{
  (void)title;
  width_ = width;
  height_ = height;
  touch_.SetScreenSize(width_, height_);
  if (egl_.EnsureCurrent())
  {
    glEnable(GL_DEPTH_TEST);
    glEnable(GL_BLEND);
    glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
    glClearColor(0.5f, 0.7f, 1.0f, 1.0f);
  }
  initialized_ = true;
  return true;
}

bool AndroidPlatformWindow::InitEgl(android_app *app)
{
  return egl_.Initialize(app);
}

void AndroidPlatformWindow::OnAppCmd(int32_t cmd)
{
  switch (cmd)
  {
  case APP_CMD_INIT_WINDOW:
    if (app_ && app_->window)
    {
      if (!egl_.HasSurface())
      {
        if (!InitEgl(app_))
        {
          CubatariumLogError("Android", "EGL init failed on APP_CMD_INIT_WINDOW");
        }
      }
      else
      {
        egl_.EnsureCurrent();
      }
    }
    break;
  case APP_CMD_TERM_WINDOW:
    egl_.Shutdown();
    break;
  case APP_CMD_WINDOW_RESIZED:
  case APP_CMD_CONTENT_RECT_CHANGED:
    if (egl_.HasSurface())
    {
      egl_.UpdateSurfaceSize();
    }
    break;
  case APP_CMD_EDITOR_ACTION:
    if (application_)
    {
      AndroidSoftKeyboardHandleEditorAction(application_.get(),
                                            app_ ? app_->editorAction : 0);
    }
    break;
  default:
    break;
  }
}

void AndroidPlatformWindow::Run()
{
  if (!app_)
  {
    return;
  }
  AndroidSoftKeyboardAttachApp(app_);
  while (running_ && app_->destroyRequested == 0)
  {
    if (application_ && application_->IsQuitRequested())
    {
      running_ = false;
      break;
    }
    int events = 0;
    android_poll_source *source = nullptr;
    while (ALooper_pollOnce(egl_.HasSurface() ? 0 : -1, nullptr, &events,
                            reinterpret_cast<void **>(&source)) >= 0)
    {
      if (source)
      {
        source->process(app_, source);
      }
      if (app_->destroyRequested != 0)
      {
        break;
      }
    }
    if (app_->window && !egl_.HasSurface())
    {
      InitEgl(app_);
    }
    if (egl_.HasSurface())
    {
      egl_.EnsureCurrent();
      ProcessFrame();
    }
  }
  if (application_ && application_->IsQuitRequested())
  {
    CubatariumAndroidFinishActivity();
  }
}

void AndroidPlatformWindow::ProcessFrame()
{
  const auto now = std::chrono::high_resolution_clock::now();
  deltaTime_ =
      std::chrono::duration<double>(now - lastFrame_).count();
  lastFrame_ = now;

  if (app_)
  {
    if (android_input_buffer *input = android_app_swap_input_buffers(app_))
    {
      ProcessInputBuffer(input);
      android_app_clear_motion_events(input);
      android_app_clear_key_events(input);
    }
  }

  ProcessInput();
  if (application_)
  {
    AndroidSoftKeyboardProcess(application_.get());
    application_->Update(deltaTime_);
  }
  Update();
  Render();
  SwapBuffers();
  touch_.Update();
}

void AndroidPlatformWindow::PollEvents() {}

void AndroidPlatformWindow::SwapBuffers() { egl_.SwapBuffers(); }

glm::ivec2 AndroidPlatformWindow::GetFramebufferSize() const
{
  return {egl_.Width(), egl_.Height()};
}

bool AndroidPlatformWindow::ShouldClose() const { return !running_; }

void AndroidPlatformWindow::RequestClose() { running_ = false; }

void AndroidPlatformWindow::Shutdown() { egl_.Shutdown(); }

void AndroidPlatformWindow::SetInstances(std::shared_ptr<UCore> core,
                                         std::shared_ptr<UWorld> world,
                                         std::shared_ptr<UGeometryEngine> geometries,
                                         std::shared_ptr<UViewEngine> views)
{
  core_ = std::move(core);
  world_ = std::move(world);
  geometries_ = std::move(geometries);
  views_ = std::move(views);
}

void AndroidPlatformWindow::SetApplication(std::shared_ptr<UApplication> application)
{
  application_ = std::move(application);
  if (application_)
  {
    application_->SetTouchInputBridge(&touch_);
  }
}

void AndroidPlatformWindow::SetTextRenderer(
    std::shared_ptr<UTextRenderer> text_renderer)
{
  textRenderer_ = std::move(text_renderer);
}

void AndroidPlatformWindow::SetCharCallback(CharCallbackFn callback)
{
  charCallback_ = std::move(callback);
}

void AndroidPlatformWindow::ProcessInput()
{
  if (application_ && application_->WantsCaptureKeyboard())
  {
    return;
  }
  if (!world_ || !application_ ||
      application_->GetState() != AppState::InGame)
  {
    return;
  }

  BlockInputContext ctx;
  ctx.World = world_;
  ctx.Geometries = geometries_.get();
  ctx.Ui = core_ ? &core_->GetUiSettings() : nullptr;
  ctx.App = application_.get();
  glm::vec2 pos = touch_.GetMousePosition();

  if (touch_.ConsumeBlockInputCancel() && blockInput_)
  {
    blockInput_->CancelPointerInteraction(ctx);
  }

  glm::vec2 placeTapPos{};
  if (touch_.ConsumePendingPlaceTap(placeTapPos))
  {
    pos = placeTapPos;
    if (blockInput_ && world_)
    {
      if (auto camera = world_->GetCurrentUserCamera())
      {
        world_->UpdateIntersection(camera->GetPosition(), camera->GetFront());
      }
      blockInput_->OnQuickTap(ctx);
    }
  }

  const glm::vec2 lookDelta = touch_.ConsumeMouseDelta();

  if (auto camera = world_->GetCurrentUserCamera())
  {
    float baselineX = 0.f;
    float baselineY = 0.f;
    if (touch_.ConsumeCameraBaseline(baselineX, baselineY))
    {
      camera->ResetMouseMove(static_cast<double>(baselineX),
                             static_cast<double>(baselineY));
    }

    camera->UpdateKeyStatus(static_cast<int>(KeyCode::Key_W),
                            touch_.IsKeyPressed(KeyCode::Key_W));
    camera->UpdateKeyStatus(static_cast<int>(KeyCode::Key_S),
                            touch_.IsKeyPressed(KeyCode::Key_S));
    camera->UpdateKeyStatus(static_cast<int>(KeyCode::Key_A),
                            touch_.IsKeyPressed(KeyCode::Key_A));
    camera->UpdateKeyStatus(static_cast<int>(KeyCode::Key_D),
                            touch_.IsKeyPressed(KeyCode::Key_D));
    camera->UpdateKeyStatus(static_cast<int>(KeyCode::Key_Space),
                            touch_.IsKeyPressed(KeyCode::Key_Space));
    const bool shiftDown = touch_.IsKeyPressed(KeyCode::Key_Shift);
    camera->UpdateKeyStatus(GLFW_KEY_LEFT_SHIFT, shiftDown);
    camera->UpdateKeyStatus(GLFW_KEY_RIGHT_SHIFT, shiftDown);
    if (lookDelta.x != 0.f || lookDelta.y != 0.f)
    {
      camera->ApplyRelativeMouseMove(lookDelta.x, -lookDelta.y);
      world_->UpdateIntersection(camera->GetPosition(), camera->GetFront());
    }
  }

  if (!application_->WantsCaptureMouse() && blockInput_)
  {
    if (touch_.IsMouseButtonJustPressed(MouseButton::Left))
    {
      blockInput_->OnMouseButton(MouseButton::Left, true, pos, ctx);
    }
    if (touch_.IsMouseButtonJustReleased(MouseButton::Left))
    {
      blockInput_->OnMouseButton(MouseButton::Left, false, pos, ctx);
    }
    if (lookDelta.x != 0.f || lookDelta.y != 0.f)
    {
      blockInput_->OnMouseMove(pos, lookDelta, ctx);
    }
  }
}

void AndroidPlatformWindow::Update()
{
  if (views_)
  {
    views_->UpdateFrameTime();
  }
  if (world_ && application_ &&
      application_->GetState() == AppState::InGame)
  {
    if (!world_->IsStepUpEnabled())
    {
      world_->SetStepUpEnabled(true);
    }
    world_->DoMovement();
    if (blockInput_)
    {
      BlockInputContext ctx;
      ctx.World = world_;
      ctx.Geometries = geometries_.get();
      ctx.Ui = core_ ? &core_->GetUiSettings() : nullptr;
      ctx.App = application_.get();
      blockInput_->Tick(static_cast<float>(deltaTime_), ctx);
    }
    if (core_)
    {
      const auto now = std::chrono::steady_clock::now();
      const double elapsed =
          std::chrono::duration<double>(now - lastAutosave_).count();
      if (elapsed >= 60.0)
      {
        core_->SaveWorld(world_->GetWorldName());
        lastAutosave_ = now;
      }
    }
  }
}

void AndroidPlatformWindow::Render()
{
  if (!application_)
  {
    return;
  }
  egl_.UpdateSurfaceSize();
  const auto size = GetFramebufferSize();
  width_ = size.x;
  height_ = size.y;
  int insetLeft = 0;
  int insetTop = 0;
  int insetRight = 0;
  int insetBottom = 0;
  QueryViewportInsets(app_, insetLeft, insetTop, insetRight, insetBottom);
  touch_.SetScreenSize(width_, height_);
  touch_.SetContentInsets(insetLeft, insetTop, insetRight, insetBottom);
  static int lastDensityDpi = 0;
  static int lastUiWidth = 0;
  static int lastUiHeight = 0;
  const int densityDpi = QueryDensityDpi(app_);
  if (densityDpi != lastDensityDpi || width_ != lastUiWidth ||
      height_ != lastUiHeight)
  {
    application_->SetUiScale(
        ComputeUiScale(densityDpi, width_, height_));
    lastDensityDpi = densityDpi;
    lastUiWidth = width_;
    lastUiHeight = height_;
  }
  if (core_)
  {
    const UiSettings &ui = core_->GetUiSettings();
    touch_.SetPlaceClickMaxSeconds(ui.placeClickMaxSeconds);
    touch_.SetBreakHoldMinSeconds(ui.breakHoldMinSeconds);
    touch_.SetUiScale(application_->GetUiScale());
  }
  application_->SetViewportInsets(insetLeft, insetTop, insetRight,
                                  insetBottom);
  application_->SetKeyboardInsetBottom(QueryKeyboardInsetBottom(app_));
  application_->RenderFrame(size.x, size.y,
                            views_ ? views_->GetDurationUpdateMks() : 0.0);
}

bool AndroidPlatformWindow::HandleGameMotionEvent(
    const GameActivityMotionEvent &event)
{
  const int action = event.action;
  const int masked = action & AMOTION_EVENT_ACTION_MASK;
  const int pointer = (action & AMOTION_EVENT_ACTION_POINTER_INDEX_MASK) >>
                      AMOTION_EVENT_ACTION_POINTER_INDEX_SHIFT;
  if (pointer < 0 ||
      static_cast<uint32_t>(pointer) >= event.pointerCount)
  {
    return false;
  }
  const float x = GameActivityPointerAxes_getX(&event.pointers[pointer]);
  const float y = GameActivityPointerAxes_getY(&event.pointers[pointer]);
  if (masked == AMOTION_EVENT_ACTION_DOWN ||
      masked == AMOTION_EVENT_ACTION_POINTER_DOWN)
  {
    bool uiConsumed = false;
    if (application_)
    {
      uiConsumed = application_->RouteMouseButton(
          static_cast<int>(MouseButton::Left), true, static_cast<int>(x),
          static_cast<int>(y), pointer);
    }
    const int pointerIndex = NormalizePointerIndex(pointer);
    uiPointerCapture_[pointerIndex] = uiConsumed;
    touch_.OnTouchDown(pointer, x, y, !uiConsumed);
  }
  else if (masked == AMOTION_EVENT_ACTION_MOVE)
  {
    for (uint32_t i = 0; i < event.pointerCount; ++i)
    {
      const float px = GameActivityPointerAxes_getX(&event.pointers[i]);
      const float py = GameActivityPointerAxes_getY(&event.pointers[i]);
      const int pointerIndex = NormalizePointerIndex(static_cast<int>(i));
      touch_.OnTouchMove(static_cast<int>(i), px, py,
                         !uiPointerCapture_[pointerIndex]);
      if (application_ && uiPointerCapture_[pointerIndex])
      {
        application_->RouteMouseMove(static_cast<int>(px), static_cast<int>(py),
                                     static_cast<int>(i));
      }
    }
  }
  else if (masked == AMOTION_EVENT_ACTION_UP ||
           masked == AMOTION_EVENT_ACTION_POINTER_UP)
  {
    const int pointerIndex = NormalizePointerIndex(pointer);
    if (application_)
    {
      application_->RouteMouseButton(static_cast<int>(MouseButton::Left), false,
                                     static_cast<int>(x), static_cast<int>(y),
                                     pointer);
    }
    touch_.OnTouchUp(pointer, x, y);
    uiPointerCapture_[pointerIndex] = false;
  }
  else if (masked == AMOTION_EVENT_ACTION_CANCEL)
  {
    const int pointerIndex = NormalizePointerIndex(pointer);
    if (application_)
    {
      application_->RouteMouseButton(static_cast<int>(MouseButton::Left), false,
                                     static_cast<int>(x), static_cast<int>(y),
                                     pointer);
    }
    touch_.OnTouchUp(pointer, x, y, true);
    uiPointerCapture_[pointerIndex] = false;
  }
  return true;
}

void AndroidPlatformWindow::ProcessInputBuffer(android_input_buffer *buffer)
{
  if (!buffer)
  {
    return;
  }
  for (uint64_t i = 0; i < buffer->motionEventsCount; ++i)
  {
    HandleGameMotionEvent(buffer->motionEvents[i]);
  }
}

} // namespace cutum
