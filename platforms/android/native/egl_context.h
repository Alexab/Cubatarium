#ifndef EGL_CONTEXT_H
#define EGL_CONTEXT_H

struct android_app;

namespace cutum
{

class EglContext
{
public:
  bool Initialize(android_app *app);
  void Shutdown();
  bool HasSurface() const { return surface_ != nullptr; }
  bool EnsureCurrent();
  void SwapBuffers();
  int Width() const { return width_; }
  int Height() const { return height_; }

private:
  android_app *app_{nullptr};
  void *display_{nullptr};
  void *surface_{nullptr};
  void *context_{nullptr};
  int width_{0};
  int height_{0};
};

} // namespace cutum

#endif
