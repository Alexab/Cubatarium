#include "egl_context.h"

#include "App/Platform/Log.h"

#include <EGL/egl.h>
#include <android/native_window.h>
#include <game-activity/native_app_glue/android_native_app_glue.h>

namespace cutum
{

bool EglContext::Initialize(android_app *app)
{
  app_ = app;
  if (!app_ || !app_->window)
  {
    return false;
  }

  display_ = eglGetDisplay(EGL_DEFAULT_DISPLAY);
  if (display_ == EGL_NO_DISPLAY)
  {
    CubatariumLogError("EGL", "eglGetDisplay failed");
    return false;
  }
  if (!eglInitialize(static_cast<EGLDisplay>(display_), nullptr, nullptr))
  {
    CubatariumLogError("EGL", "eglInitialize failed");
    return false;
  }

  const EGLint configAttribs[] = {EGL_RENDERABLE_TYPE,
                                  EGL_OPENGL_ES3_BIT,
                                  EGL_SURFACE_TYPE,
                                  EGL_WINDOW_BIT,
                                  EGL_BLUE_SIZE,
                                  8,
                                  EGL_GREEN_SIZE,
                                  8,
                                  EGL_RED_SIZE,
                                  8,
                                  EGL_DEPTH_SIZE,
                                  24,
                                  EGL_NONE};

  EGLConfig config = nullptr;
  EGLint numConfigs = 0;
  if (!eglChooseConfig(static_cast<EGLDisplay>(display_), configAttribs, &config,
                       1, &numConfigs) ||
      numConfigs == 0)
  {
    CubatariumLogError("EGL", "eglChooseConfig failed");
    return false;
  }

  const EGLint contextAttribs[] = {EGL_CONTEXT_CLIENT_VERSION, 3, EGL_NONE};
  context_ = eglCreateContext(static_cast<EGLDisplay>(display_), config,
                              EGL_NO_CONTEXT, contextAttribs);
  if (context_ == EGL_NO_CONTEXT)
  {
    CubatariumLogError("EGL", "eglCreateContext failed");
    return false;
  }

  surface_ = eglCreateWindowSurface(
      static_cast<EGLDisplay>(display_), config, app_->window, nullptr);
  if (surface_ == EGL_NO_SURFACE)
  {
    CubatariumLogError("EGL", "eglCreateWindowSurface failed");
    return false;
  }

  if (!eglMakeCurrent(static_cast<EGLDisplay>(display_),
                      static_cast<EGLSurface>(surface_),
                      static_cast<EGLSurface>(surface_),
                      static_cast<EGLContext>(context_)))
  {
    CubatariumLogError("EGL", "eglMakeCurrent failed");
    return false;
  }

  width_ = ANativeWindow_getWidth(app_->window);
  height_ = ANativeWindow_getHeight(app_->window);
  CubatariumLogInfo("EGL", "Context ready " + std::to_string(width_) + "x" +
                                std::to_string(height_));
  return true;
}

void EglContext::Shutdown()
{
  if (display_ == EGL_NO_DISPLAY)
  {
    return;
  }
  eglMakeCurrent(static_cast<EGLDisplay>(display_), EGL_NO_SURFACE,
                 EGL_NO_SURFACE, EGL_NO_CONTEXT);
  if (surface_ != EGL_NO_SURFACE)
  {
    eglDestroySurface(static_cast<EGLDisplay>(display_),
                      static_cast<EGLSurface>(surface_));
    surface_ = EGL_NO_SURFACE;
  }
  if (context_ != EGL_NO_CONTEXT)
  {
    eglDestroyContext(static_cast<EGLDisplay>(display_),
                      static_cast<EGLContext>(context_));
    context_ = EGL_NO_CONTEXT;
  }
  eglTerminate(static_cast<EGLDisplay>(display_));
  display_ = EGL_NO_DISPLAY;
}

void EglContext::SwapBuffers()
{
  if (display_ != EGL_NO_DISPLAY && surface_ != EGL_NO_SURFACE)
  {
    eglSwapBuffers(static_cast<EGLDisplay>(display_),
                   static_cast<EGLSurface>(surface_));
  }
}

} // namespace cutum
