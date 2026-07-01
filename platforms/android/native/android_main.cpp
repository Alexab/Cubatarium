#include "App/Platform/AndroidPlatformWindow.h"
#include "App/Platform/AndroidPlatformPaths.h"
#include "App/Platform/AppRunner.h"
#include "App/Platform/GameAssets.h"
#include "App/Platform/IPlatformPaths.h"
#include "App/Platform/Log.h"
#include "android_jni.h"

#include <filesystem>
#include <game-activity/native_app_glue/android_native_app_glue.h>

namespace cutum
{

namespace
{

struct AndroidAppState
{
  UAndroidPlatformWindow *window{nullptr};
};

void HandleAppCmd(android_app *app, int32_t cmd)
{
  auto *state = static_cast<AndroidAppState *>(app->userData);
  if (!state || !state->window)
  {
    return;
  }
  state->window->OnAppCmd(cmd);
}

bool GameAssetsReady(const IPlatformPaths &paths)
{
  const auto font = paths.AssetRoot() / "fonts" / kBundledUiFontFileName;
  return std::filesystem::exists(font);
}

bool WaitForGameAssets(android_app *app, const IPlatformPaths &paths)
{
  if (GameAssetsReady(paths))
  {
    return true;
  }

  CubatariumLogInfo("Android", "Waiting for game assets extraction...");
  while (app->destroyRequested == 0)
  {
    if (GameAssetsReady(paths))
    {
      return true;
    }
    int events = 0;
    android_poll_source *source = nullptr;
    while (ALooper_pollOnce(0, nullptr, &events,
                            reinterpret_cast<void **>(&source)) >= 0)
    {
      if (source)
      {
        source->process(app, source);
      }
      if (app->destroyRequested != 0)
      {
        return false;
      }
    }
  }
  return false;
}

bool WaitForEglSurface(android_app *app, UAndroidPlatformWindow &window)
{
  if (window.HasSurface())
  {
    return true;
  }

  CubatariumLogInfo("Android", "Waiting for native window / EGL...");
  while (app->destroyRequested == 0)
  {
    if (app->window != nullptr && !window.HasSurface())
    {
      window.InitEgl(app);
    }
    if (window.HasSurface())
    {
      return true;
    }
    int events = 0;
    android_poll_source *source = nullptr;
    while (ALooper_pollOnce(0, nullptr, &events,
                            reinterpret_cast<void **>(&source)) >= 0)
    {
      if (source)
      {
        source->process(app, source);
      }
      if (app->destroyRequested != 0)
      {
        return false;
      }
    }
  }
  return false;
}

} // namespace

} // namespace cutum

extern "C" void android_main(struct android_app *app)
{
  using namespace cutum;

  CubatariumInstallWindowsDiagnostics();

  UAndroidPlatformWindow window(app);
  AndroidAppState state;
  state.window = &window;
  app->userData = &state;
  app->onAppCmd = HandleAppCmd;

  if (!CubatariumAndroidWaitForJavaInit(app))
  {
    return;
  }

  auto pathsPtr =
      std::make_shared<UAndroidPlatformPaths>(
          CubatariumAndroidGetAssetManager());
  pathsPtr->EnsureWritableConfig();
  IPlatformPaths::SetGlobal(pathsPtr);

  if (app->window != nullptr)
  {
    window.InitEgl(app);
  }

  if (!WaitForGameAssets(app, *pathsPtr))
  {
    CubatariumLogError("Android", "Game assets not ready before destroy");
    return;
  }

  if (!WaitForEglSurface(app, window))
  {
    CubatariumLogError("Android", "No EGL surface before game start");
    return;
  }

  std::filesystem::current_path(pathsPtr->AssetRoot());
  const int result = RunCubatarium(window, *pathsPtr);
  if (result != 0)
  {
    CubatariumLogError("Android",
                       "RunCubatarium failed with code " + std::to_string(result));
  }
}
