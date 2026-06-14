#include "App/Platform/AndroidPlatformWindow.h"
#include "App/Platform/AndroidPlatformPaths.h"
#include "App/Platform/AppRunner.h"
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
  AndroidPlatformWindow *window{nullptr};
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

} // namespace

} // namespace cutum

extern "C" void android_main(struct android_app *app)
{
  using namespace cutum;

  if (!CubatariumAndroidWaitForJavaInit(app))
  {
    return;
  }

  auto pathsPtr =
      std::make_shared<AndroidPlatformPaths>(CubatariumAndroidGetAssetManager());
  pathsPtr->EnsureWritableConfig();
  IPlatformPaths::SetGlobal(pathsPtr);

  AndroidPlatformWindow window(app);
  AndroidAppState state;
  state.window = &window;
  app->userData = &state;
  app->onAppCmd = HandleAppCmd;

  while (app->window == nullptr && app->destroyRequested == 0)
  {
    int events = 0;
    android_poll_source *source = nullptr;
    while (ALooper_pollOnce(-1, nullptr, &events,
                            reinterpret_cast<void **>(&source)) >= 0)
    {
      if (source)
      {
        source->process(app, source);
      }
      if (app->destroyRequested != 0)
      {
        return;
      }
    }
  }

  if (!window.InitEgl(app))
  {
    CubatariumLogError("Android", "EGL init failed");
    return;
  }

  std::filesystem::current_path(pathsPtr->AssetRoot());
  RunCubatarium(window, *pathsPtr);
}
