#include "App/Platform/AppRunner.h"
#include "App/Platform/DesktopPlatformPaths.h"
#include "App/Platform/DesktopPlatformWindow.h"
#include "App/Platform/IPlatformPaths.h"
#include "ResourcePacks/ResourcePackSmoke.h"

#ifdef _WIN32
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#endif

#include <cstring>

int main(int argc, char *argv[])
{
  for (int i = 1; i < argc; ++i)
  {
    if (std::strcmp(argv[i], "--smoke-packs") == 0)
    {
      auto paths = std::make_shared<cutum::UDesktopPlatformPaths>();
      cutum::IPlatformPaths::SetGlobal(paths);
      return cutum::RunResourcePackSmoke(*paths);
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
