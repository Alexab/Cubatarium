#ifndef I_PLATFORM_WINDOW_H
#define I_PLATFORM_WINDOW_H

#include <functional>
#include <glm/glm.hpp>
#include <memory>

namespace cutum
{

class UCore;
class UWorld;
class UGeometryEngine;
class UViewEngine;
class UTextRenderer;
class UApplication;

using CharCallbackFn = std::function<void(unsigned int)>;

class IPlatformWindow
{
public:
  virtual ~IPlatformWindow() = default;

  virtual bool Initialize(int width, int height, const char *title) = 0;
  virtual void Run() = 0;
  virtual void Shutdown() = 0;
  virtual void PollEvents() = 0;
  virtual void SwapBuffers() = 0;
  virtual glm::ivec2 GetFramebufferSize() const = 0;
  virtual bool ShouldClose() const = 0;
  virtual void RequestClose() = 0;
  virtual double DeltaTime() const = 0;

  virtual void SetInstances(std::shared_ptr<UCore> core,
                            std::shared_ptr<UWorld> world,
                            std::shared_ptr<UGeometryEngine> geometries,
                            std::shared_ptr<UViewEngine> views) = 0;
  virtual void SetApplication(std::shared_ptr<UApplication> application) = 0;
  virtual void
  SetTextRenderer(std::shared_ptr<UTextRenderer> text_renderer) = 0;
  virtual void SetCharCallback(CharCallbackFn callback) = 0;
};

} // namespace cutum

#endif
