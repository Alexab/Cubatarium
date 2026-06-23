#include "egl_context.h"

#include "App/Platform/Log.h"

#include <EGL/egl.h>
#include <android/native_window.h>
#include <game-activity/GameActivity.h>
#include <game-activity/native_app_glue/android_native_app_glue.h>
#include <sstream>

namespace cutum
{

namespace
{

bool ChooseConfig(EGLDisplay display, EGLint renderableType, EGLConfig *config)
{
  const EGLint configAttribs[] = {EGL_RENDERABLE_TYPE,
                                  renderableType,
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
  EGLint numConfigs = 0;
  return eglChooseConfig(display, configAttribs, config, 1, &numConfigs) &&
         numConfigs > 0;
}

} // namespace

bool EglContext::Initialize(android_app *app)
{
  app_ = app;
  if (!app_ || !app_->window)
  {
    return false;
  }
  if (HasSurface())
  {
    return true;
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
    display_ = EGL_NO_DISPLAY;
    return false;
  }

  EGLConfig config = nullptr;
  EGLint clientVersion = 3;
  if (!ChooseConfig(static_cast<EGLDisplay>(display_), EGL_OPENGL_ES3_BIT, &config))
  {
    CubatariumLogInfo("EGL", "GLES 3 config unavailable, trying GLES 2 config");
    if (!ChooseConfig(static_cast<EGLDisplay>(display_), EGL_OPENGL_ES2_BIT, &config))
    {
      CubatariumLogError("EGL", "eglChooseConfig failed");
      eglTerminate(static_cast<EGLDisplay>(display_));
      display_ = EGL_NO_DISPLAY;
      return false;
    }
  }

  for (EGLint tryVersion : {3, 2})
  {
    const EGLint contextAttribs[] = {EGL_CONTEXT_CLIENT_VERSION, tryVersion,
                                     EGL_NONE};
    context_ = eglCreateContext(static_cast<EGLDisplay>(display_), config,
                                EGL_NO_CONTEXT, contextAttribs);
    if (context_ != EGL_NO_CONTEXT)
    {
      clientVersion = tryVersion;
      break;
    }
  }
  if (context_ == EGL_NO_CONTEXT)
  {
    CubatariumLogError("EGL", "eglCreateContext failed");
    eglTerminate(static_cast<EGLDisplay>(display_));
    display_ = EGL_NO_DISPLAY;
    return false;
  }

  surface_ = eglCreateWindowSurface(
      static_cast<EGLDisplay>(display_), config, app_->window, nullptr);
  if (surface_ == EGL_NO_SURFACE)
  {
    CubatariumLogError("EGL", "eglCreateWindowSurface failed");
    eglDestroyContext(static_cast<EGLDisplay>(display_),
                      static_cast<EGLContext>(context_));
    context_ = EGL_NO_CONTEXT;
    eglTerminate(static_cast<EGLDisplay>(display_));
    display_ = EGL_NO_DISPLAY;
    return false;
  }

  if (!eglMakeCurrent(static_cast<EGLDisplay>(display_),
                      static_cast<EGLSurface>(surface_),
                      static_cast<EGLSurface>(surface_),
                      static_cast<EGLContext>(context_)))
  {
    CubatariumLogError("EGL", "eglMakeCurrent failed");
    Shutdown();
    return false;
  }

  width_ = ANativeWindow_getWidth(app_->window);
  height_ = ANativeWindow_getHeight(app_->window);
  std::ostringstream msg;
  msg << "Context ready GLES" << clientVersion << " " << width_ << "x"
      << height_;
  CubatariumLogInfo("EGL", msg.str());
  return true;
}

bool EglContext::EnsureCurrent()
{
  if (!HasSurface())
  {
    return false;
  }
  return eglMakeCurrent(static_cast<EGLDisplay>(display_),
                        static_cast<EGLSurface>(surface_),
                        static_cast<EGLSurface>(surface_),
                        static_cast<EGLContext>(context_)) == EGL_TRUE;
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
  width_ = 0;
  height_ = 0;
}

void EglContext::SwapBuffers()
{
  if (display_ != EGL_NO_DISPLAY && surface_ != EGL_NO_SURFACE)
  {
    eglSwapBuffers(static_cast<EGLDisplay>(display_),
                   static_cast<EGLSurface>(surface_));
  }
}

void EglContext::UpdateSurfaceSize()
{
  if (!app_ || !app_->window)
  {
    return;
  }
  width_ = ANativeWindow_getWidth(app_->window);
  height_ = ANativeWindow_getHeight(app_->window);
}

} // namespace cutum
