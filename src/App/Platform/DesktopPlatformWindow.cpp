#include "App/Platform/DesktopPlatformWindow.h"

namespace cutum
{

bool UDesktopPlatformWindow::Initialize(int width, int height,
                                        const char *title)
{
  return WindowManager.Initialize(width, height, title, true);
}

bool UDesktopPlatformWindow::InitializeHidden(int width, int height,
                                              const char *title)
{
  return WindowManager.Initialize(width, height, title, false);
}

void UDesktopPlatformWindow::SetStopPredicate(std::function<bool()> predicate)
{
  WindowManager.SetStopPredicate(std::move(predicate));
}

void UDesktopPlatformWindow::Run() { WindowManager.Run(); }

void UDesktopPlatformWindow::Shutdown() { WindowManager.Shutdown(); }

void UDesktopPlatformWindow::PollEvents() {}

void UDesktopPlatformWindow::SwapBuffers() {}

glm::ivec2 UDesktopPlatformWindow::GetFramebufferSize() const
{
  return {WindowManager.GetWidth(), WindowManager.GetHeight()};
}

bool UDesktopPlatformWindow::ShouldClose() const
{
  return WindowManager.ShouldClose();
}

void UDesktopPlatformWindow::RequestClose() {}

double UDesktopPlatformWindow::DeltaTime() const { return 0.0; }

void UDesktopPlatformWindow::SetInstances(
    std::shared_ptr<UCore> core, std::shared_ptr<UWorld> world,
    std::shared_ptr<UGeometryEngine> geometries,
    std::shared_ptr<UViewEngine> views)
{
  WindowManager.SetInstances(core, world, geometries, views);
}

void UDesktopPlatformWindow::SetApplication(
    std::shared_ptr<UApplication> application)
{
  WindowManager.SetApplication(application);
}

void UDesktopPlatformWindow::SetTextRenderer(
    std::shared_ptr<UTextRenderer> text_renderer)
{
  WindowManager.SetTextRenderer(text_renderer);
}

void UDesktopPlatformWindow::SetCharCallback(CharCallbackFn callback)
{
  (void)callback;
}

} // namespace cutum
