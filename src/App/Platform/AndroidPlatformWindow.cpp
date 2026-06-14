#include "App/Platform/AndroidPlatformWindow.h"

#include "App/Application.h"
#include "App/Core.h"
#include "App/Platform/InputManager.h"
#include "App/Platform/Log.h"
#include "App/Settings/AppState.h"
#include "Creatures/Player/User.h"
#include "Game/GameSession.h"
#include "Render/Engine/GeometryEngine.h"
#include "Render/Engine/ViewEngine.h"
#include "Render/GlIncludes.h"
#include "World/Core/World.h"

#include <android/input.h>
#include <game-activity/GameActivityEvents.h>
#include <game-activity/native_app_glue/android_native_app_glue.h>

namespace cutum
{

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
  glEnable(GL_DEPTH_TEST);
  glEnable(GL_BLEND);
  glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
  glClearColor(0.5f, 0.7f, 1.0f, 1.0f);
  initialized_ = true;
  return true;
}

bool AndroidPlatformWindow::InitEgl(android_app *app)
{
  return egl_.Initialize(app);
}

void AndroidPlatformWindow::OnAppCmd(int32_t cmd)
{
  if (cmd == APP_CMD_TERM_WINDOW)
  {
    egl_.Shutdown();
  }
}

void AndroidPlatformWindow::Run()
{
  if (!app_)
  {
    return;
  }
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
    if (egl_.HasSurface())
    {
      ProcessFrame();
    }
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
  const glm::vec2 pos = touch_.GetMousePosition();
  const glm::vec2 lookDelta = touch_.ConsumeMouseDelta();

  if (auto camera = world_->GetCurrentUserCamera())
  {
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
    if (lookDelta.x != 0.f || lookDelta.y != 0.f)
    {
      camera->UpdateMouseMove(world_, static_cast<double>(pos.x),
                              static_cast<double>(pos.y));
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
  const auto size = GetFramebufferSize();
  width_ = size.x;
  height_ = size.y;
  touch_.SetScreenSize(width_, height_);
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
    touch_.OnTouchDown(pointer, x, y);
    if (application_)
    {
      application_->RouteMouseButton(static_cast<int>(MouseButton::Left), true,
                                     static_cast<int>(x), static_cast<int>(y));
    }
  }
  else if (masked == AMOTION_EVENT_ACTION_MOVE)
  {
    for (uint32_t i = 0; i < event.pointerCount; ++i)
    {
      const float px = GameActivityPointerAxes_getX(&event.pointers[i]);
      const float py = GameActivityPointerAxes_getY(&event.pointers[i]);
      touch_.OnTouchMove(static_cast<int>(i), px, py);
    }
    const glm::vec2 delta = touch_.GetMouseDelta();
    if (application_ && (delta.x != 0.f || delta.y != 0.f))
    {
      application_->RouteMouseMove(static_cast<int>(x), static_cast<int>(y));
    }
  }
  else if (masked == AMOTION_EVENT_ACTION_UP ||
           masked == AMOTION_EVENT_ACTION_POINTER_UP)
  {
    touch_.OnTouchUp(pointer, x, y);
    if (application_)
    {
      application_->RouteMouseButton(static_cast<int>(MouseButton::Left), false,
                                     static_cast<int>(x), static_cast<int>(y));
    }
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
