#ifdef _WIN32
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#endif

#include "App/Platform/AppRunner.h"
#include "App/Platform/DesktopPlatformPaths.h"
#include "App/Platform/DesktopPlatformWindow.h"
#include "App/Platform/IPlatformPaths.h"
#include "App/Platform/Log.h"
#include "App/Utils.h"
#include "ResourcePacks/ResourcePackSmoke.h"

#include <cstring>
int main(int argc, char *argv[])
{
#ifdef _WIN32
  cutum::CubatariumInstallWindowsDiagnostics();
#endif

  for (int i = 1; i < argc; ++i)
  {
    if (std::strcmp(argv[i], "--console") == 0)
    {
#ifdef _WIN32
      cutum::CubatariumAttachWindowsConsole();
#endif
      continue;
    }
    if (std::strcmp(argv[i], "--smoke-packs") == 0)
    {
#ifdef _WIN32
      cutum::CubatariumAttachParentConsole();
#endif
      auto paths = std::make_shared<cutum::UDesktopPlatformPaths>();
      cutum::IPlatformPaths::SetGlobal(paths);
      return cutum::RunResourcePackSmoke(*paths);
    }
    if (std::strcmp(argv[i], "--validate-load") == 0)
    {
#ifdef _WIN32
      cutum::CubatariumAttachParentConsole();
#endif
      auto paths = std::make_shared<cutum::UDesktopPlatformPaths>();
      cutum::IPlatformPaths::SetGlobal(paths);
      return cutum::RunValidateLoad();
    }
    if (std::strcmp(argv[i], "--bench-io") == 0)
    {
#ifdef _WIN32
      cutum::CubatariumAttachParentConsole();
#endif
      auto paths = std::make_shared<cutum::UDesktopPlatformPaths>();
      cutum::IPlatformPaths::SetGlobal(paths);
      return cutum::RunBenchChunkIo();
    }
    if (std::strcmp(argv[i], "--create-world") == 0)
    {
#ifdef _WIN32
      cutum::CubatariumAttachParentConsole();
#endif
      auto paths = std::make_shared<cutum::UDesktopPlatformPaths>();
      cutum::IPlatformPaths::SetGlobal(paths);
      return cutum::RunCreateWorld(argc, argv, i + 1);
    }
  }

  auto paths = std::make_shared<cutum::UDesktopPlatformPaths>();
  cutum::IPlatformPaths::SetGlobal(paths);
  cutum::UDesktopPlatformWindow window;
  return cutum::RunCubatarium(window, *paths);
}

#ifdef _WIN32
int WINAPI WinMain(HINSTANCE hInstance, HINSTANCE hPrevInstance,
                   LPSTR lpCmdLine, int nCmdShow)
{
  (void)hInstance;
  (void)hPrevInstance;
  (void)lpCmdLine;
  (void)nCmdShow;
  return main(__argc, __argv);
}
#endif
