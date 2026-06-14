#include "App/Platform/DesktopPlatformWindow.h"

namespace cutum
{

bool DesktopPlatformWindow::Initialize(int width, int height, const char *title)
{
  return WindowManager_.Initialize(width, height, title);
}

void DesktopPlatformWindow::Run() { WindowManager_.Run(); }

void DesktopPlatformWindow::Shutdown() { WindowManager_.Shutdown(); }

void DesktopPlatformWindow::PollEvents() {}

void DesktopPlatformWindow::SwapBuffers() {}

glm::ivec2 DesktopPlatformWindow::GetFramebufferSize() const
{
  return {WindowManager_.GetWidth(), WindowManager_.GetHeight()};
}

bool DesktopPlatformWindow::ShouldClose() const
{
  return WindowManager_.ShouldClose();
}

void DesktopPlatformWindow::RequestClose() {}

double DesktopPlatformWindow::DeltaTime() const { return 0.0; }

void DesktopPlatformWindow::SetInstances(std::shared_ptr<UCore> core,
                                       std::shared_ptr<UWorld> world,
                                       std::shared_ptr<UGeometryEngine> geometries,
                                       std::shared_ptr<UViewEngine> views)
{
  WindowManager_.SetInstances(core, world, geometries, views);
}

void DesktopPlatformWindow::SetApplication(std::shared_ptr<UApplication> application)
{
  WindowManager_.SetApplication(application);
}

void DesktopPlatformWindow::SetTextRenderer(
    std::shared_ptr<UTextRenderer> text_renderer)
{
  WindowManager_.SetTextRenderer(text_renderer);
}

void DesktopPlatformWindow::SetCharCallback(CharCallbackFn callback)
{
  (void)callback;
}

} // namespace cutum
