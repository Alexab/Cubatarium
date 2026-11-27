#ifndef DESKTOP_PLATFORM_WINDOW_H
#define DESKTOP_PLATFORM_WINDOW_H

#include "App/Platform/IUPlatformWindow.h"
#include "App/Platform/WindowManager.h"
#include <functional>

namespace cutum
{

class UDesktopPlatformWindow : public IUPlatformWindow
{
public:
  bool Initialize(int width, int height, const char *title) override;
  bool InitializeHidden(int width, int height, const char *title);
  void SetStopPredicate(std::function<bool()> predicate);
  void SetAutopilotKey(KeyCode key, bool held);
  void ClearAutopilotKeys();
  bool IsDiagnosticCaptureReady()
  {
    return WindowManager.IsDiagnosticCaptureReady();
  }
  bool RequestDiagnosticCapture(unsigned id)
  {
    return WindowManager.RequestDiagnosticCapture(id);
  }
  unsigned GetDiagnosticCapturesQueued() const
  {
    return WindowManager.GetDiagnosticCapturesQueued();
  }
  void SetAutosaveEnabled(bool enabled);
  void Run() override;
  void Run(FlightCaptureGpuCheckpoint *checkpoint) { WindowManager.Run(checkpoint); }
  void InspectFlightCaptureGpuBuffers(class TerrainGpuBufferCensus &out) const override
  {
    WindowManager.InspectFlightCaptureGpuBuffers(out);
  }
  void Shutdown() override;
  void Shutdown(DesktopShutdownCheckpoint &checkpoint) { WindowManager.Shutdown(&checkpoint); }
  void PollEvents() override;
  void SwapBuffers() override;
  glm::ivec2 GetFramebufferSize() const override;
  bool ShouldClose() const override;
  void RequestClose() override;
  double DeltaTime() const override;

  void SetInstances(std::shared_ptr<UCore> core, std::shared_ptr<UWorld> world,
                    std::shared_ptr<UGeometryEngine> geometries,
                    std::shared_ptr<UViewEngine> views) override;
  void SetApplication(std::shared_ptr<UApplication> application) override;
  void SetTextRenderer(std::shared_ptr<UTextRenderer> text_renderer) override;
  void SetCharCallback(CharCallbackFn callback) override;

private:
  UWindowManager WindowManager;
};

} // namespace cutum

#endif
