#include "egl_context.h"

#include "App/Platform/Log.h"

#include <EGL/egl.h>
#include <android/native_window.h>
#include <game-activity/GameActivity.h>
#include <game-activity/native_app_glue/android_native_app_glue.h>
#include <sstream>
#include <string>

#include "Render/GlIncludes.h"

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
                                  EGL_ALPHA_SIZE,
                                  8,
                                  EGL_DEPTH_SIZE,
                                  16,
                                  EGL_STENCIL_SIZE,
                                  8,
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
  const EGLint renderableTypes[] = {EGL_OPENGL_ES3_BIT,
                                    EGL_OPENGL_ES3_BIT | EGL_OPENGL_ES2_BIT,
                                    EGL_OPENGL_ES2_BIT};
  for (const EGLint renderableType : renderableTypes)
  {
    if (ChooseConfig(static_cast<EGLDisplay>(display_), renderableType, &config))
    {
      break;
    }
    config = nullptr;
  }
  if (config == nullptr)
  {
    CubatariumLogError("EGL", "eglChooseConfig failed");
    eglTerminate(static_cast<EGLDisplay>(display_));
    display_ = EGL_NO_DISPLAY;
    return false;
  }

  const EGLint contextAttribs[] = {EGL_CONTEXT_CLIENT_VERSION, 3, EGL_NONE};
  context_ = eglCreateContext(static_cast<EGLDisplay>(display_), config,
                              EGL_NO_CONTEXT, contextAttribs);
  EGLint clientVersion = 0;
  if (context_ == EGL_NO_CONTEXT)
  {
    CubatariumLogError("EGL", "eglCreateContext GLES3 failed");
    eglTerminate(static_cast<EGLDisplay>(display_));
    display_ = EGL_NO_DISPLAY;
    return false;
  }
  eglQueryContext(static_cast<EGLDisplay>(display_),
                  static_cast<EGLContext>(context_), EGL_CONTEXT_CLIENT_VERSION,
                  &clientVersion);
  if (clientVersion < 3)
  {
    CubatariumLogError("EGL", "GLES 3 context required but got GLES" +
                                   std::to_string(clientVersion));
    eglDestroyContext(static_cast<EGLDisplay>(display_),
                      static_cast<EGLContext>(context_));
    context_ = EGL_NO_CONTEXT;
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
  GLint stencil_bits = 0;
  glGetIntegerv(GL_STENCIL_BITS, &stencil_bits);
  std::ostringstream msg;
  msg << "Context ready GLES" << clientVersion << " " << width_ << "x"
      << height_ << " stencil=" << stencil_bits;
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
