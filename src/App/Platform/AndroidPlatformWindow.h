#ifndef ANDROID_PLATFORM_WINDOW_H
#define ANDROID_PLATFORM_WINDOW_H

#include "App/Platform/IPlatformWindow.h"
#include "App/Platform/TouchInputBridge.h"
#include "Blocks/Input/BlockInputController.h"
#include "egl_context.h"

#include <array>
#include <chrono>
#include <game-activity/GameActivityEvents.h>

struct android_app;
struct android_input_buffer;
struct GameActivityMotionEvent;

namespace cutum
{

class UAndroidPlatformWindow : public IPlatformWindow
{
public:
  explicit UAndroidPlatformWindow(android_app *app);

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
  double DeltaTime() const override { return FrameDeltaTime; }

  void SetInstances(std::shared_ptr<UCore> core, std::shared_ptr<UWorld> world,
                    std::shared_ptr<UGeometryEngine> geometries,
                    std::shared_ptr<UViewEngine> views) override;
  void SetApplication(std::shared_ptr<UApplication> application) override;
  void SetTextRenderer(std::shared_ptr<UTextRenderer> text_renderer) override;
  void SetCharCallback(CharCallbackFn callback) override;

  void ProcessFrame();
  void ProcessInputBuffer(struct android_input_buffer *buffer);
  bool HandleGameMotionEvent(const GameActivityMotionEvent &event);
  bool HasSurface() const { return Egl.HasSurface(); }

private:
  void ProcessInput();
  void Update();
  void Render();

  android_app *App;
  EglContext Egl;
  UTouchInputBridge Touch;
  std::unique_ptr<UBlockInputController> BlockInput;
  std::shared_ptr<UCore> Core;
  std::shared_ptr<UWorld> World;
  std::shared_ptr<UGeometryEngine> Geometries;
  std::shared_ptr<UViewEngine> Views;
  std::shared_ptr<UTextRenderer> TextRenderer;
  std::shared_ptr<UApplication> Application;
  CharCallbackFn CharCallback;
  bool Running{true};
  bool Initialized{false};
  int Width{1280};
  int Height{720};
  double FrameDeltaTime{0.0};
  std::chrono::high_resolution_clock::time_point LastFrame;
  std::chrono::steady_clock::time_point LastAutosave;
  std::array<bool, UTouchInputBridge::kMaxPointers> UiPointerCapture{};
};

} // namespace cutum

#endif
