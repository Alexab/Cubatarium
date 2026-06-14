#ifndef ANDROID_PLATFORM_WINDOW_H
#define ANDROID_PLATFORM_WINDOW_H

#include "App/Platform/IPlatformWindow.h"
#include "App/Platform/TouchInputBridge.h"
#include "Blocks/Input/BlockInputController.h"
#include "egl_context.h"

#include <game-activity/GameActivityEvents.h>
#include <chrono>

struct android_app;
struct android_input_buffer;
struct GameActivityMotionEvent;

namespace cutum
{

class AndroidPlatformWindow : public IPlatformWindow
{
public:
  explicit AndroidPlatformWindow(android_app *app);

  bool Initialize(int width, int height, const char *title) override;
  bool InitEgl(android_app *app);
  void OnAppCmd(int32_t cmd);
  void Run() override;
  void Shutdown() override;
  void PollEvents() override;
  void SwapBuffers() override;
  glm::ivec2 GetFramebufferSize() const override;
  bool ShouldClose() const override;
  void RequestClose() override;
  double DeltaTime() const override { return deltaTime_; }

  void SetInstances(std::shared_ptr<UCore> core, std::shared_ptr<UWorld> world,
                    std::shared_ptr<UGeometryEngine> geometries,
                    std::shared_ptr<UViewEngine> views) override;
  void SetApplication(std::shared_ptr<UApplication> application) override;
  void SetTextRenderer(std::shared_ptr<UTextRenderer> text_renderer) override;
  void SetCharCallback(CharCallbackFn callback) override;

  void ProcessFrame();
  void ProcessInputBuffer(struct android_input_buffer *buffer);
  bool HandleGameMotionEvent(const GameActivityMotionEvent &event);
  bool HasSurface() const { return egl_.HasSurface(); }

private:
  void ProcessInput();
  void Update();
  void Render();

  android_app *app_;
  EglContext egl_;
  TouchInputBridge touch_;
  std::unique_ptr<UBlockInputController> blockInput_;
  std::shared_ptr<UCore> core_;
  std::shared_ptr<UWorld> world_;
  std::shared_ptr<UGeometryEngine> geometries_;
  std::shared_ptr<UViewEngine> views_;
  std::shared_ptr<UTextRenderer> textRenderer_;
  std::shared_ptr<UApplication> application_;
  CharCallbackFn charCallback_;
  bool running_{true};
  bool initialized_{false};
  int width_{1280};
  int height_{720};
  double deltaTime_{0.0};
  std::chrono::high_resolution_clock::time_point lastFrame_;
  std::chrono::steady_clock::time_point lastAutosave_;
};

} // namespace cutum

#endif
