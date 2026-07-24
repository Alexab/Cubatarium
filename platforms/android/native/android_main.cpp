#include "App/Platform/AndroidPlatformWindow.h"
#include "App/Platform/AndroidPlatformPaths.h"
#include "App/Platform/AppRunner.h"
#include "App/Platform/GameAssets.h"
#include "App/Platform/IUPlatformPaths.h"
#include "App/Platform/Log.h"
#include "android_jni.h"

#include <chrono>
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

bool GameAssetsReady(const IUPlatformPaths &paths)
{
  const auto root = paths.AssetRoot();
  const auto font = root / "fonts" / kBundledUiFontFileName;
  const auto uiShader = root / "shaders" / "gles" / "vshader_2d.glsl";
  const auto types = root / "content" / "types.json";
  return std::filesystem::exists(font) && std::filesystem::exists(uiShader) &&
         std::filesystem::exists(types);
}

bool WaitForGameAssets(android_app *app, const IUPlatformPaths &paths)
{
  if (GameAssetsReady(paths))
  {
    return true;
  }

  CubatariumLogInfo("Android", "Waiting for game assets extraction...");
  const auto waitStart = std::chrono::steady_clock::now();
  auto lastLog = waitStart;
  while (app->destroyRequested == 0)
  {
    if (GameAssetsReady(paths))
    {
      return true;
    }
    const auto now = std::chrono::steady_clock::now();
    if (now - lastLog >= std::chrono::seconds(2))
    {
      const auto root = paths.AssetRoot();
      const bool hasFont = std::filesystem::exists(
          root / "fonts" / kBundledUiFontFileName);
      const bool hasShaders = std::filesystem::exists(
          root / "shaders" / "gles" / "vshader_2d.glsl");
      CubatariumLogInfo(
          "Android",
          "Still waiting for assets (font=" + std::string(hasFont ? "yes" : "no") +
              ", shaders=" + std::string(hasShaders ? "yes" : "no") + ")");
      lastLog = now;
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
  auto lastLog = std::chrono::steady_clock::now();
  auto lastAttempt = lastLog - std::chrono::milliseconds(200);
  while (app->destroyRequested == 0)
  {
    const auto now = std::chrono::steady_clock::now();
    if (app->window != nullptr && !window.HasSurface() &&
        now - lastAttempt >= std::chrono::milliseconds(200))
    {
      window.InitEgl(app);
      lastAttempt = now;
      if (!window.HasSurface() && now - lastLog >= std::chrono::seconds(2))
      {
        CubatariumLogError("Android",
                           "EGL init still failing (GLES 3 context required)");
        lastLog = now;
      }
    }
    if (window.HasSurface())
    {
      return true;
    }
    int events = 0;
    android_poll_source *source = nullptr;
    // Block briefly so a failed EGL init cannot spin at 100% CPU.
    while (ALooper_pollOnce(50, nullptr, &events,
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
  IUPlatformPaths::SetGlobal(pathsPtr);

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
