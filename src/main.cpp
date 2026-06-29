#ifdef _WIN32
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#include <shellapi.h>
#endif

#include "App/Platform/AppRunner.h"
#include "App/Platform/DesktopPlatformPaths.h"
#include "App/Platform/DesktopPlatformWindow.h"
#include "App/Platform/IPlatformPaths.h"
#include "App/Platform/Log.h"
#include "App/Utils.h"
#include "ResourcePacks/ResourcePackSmoke.h"

#include <cstring>
#include <iostream>
#include <string>
#include <vector>
int main(int argc, char *argv[])
{
#ifdef _WIN32
  cutum::CubatariumInstallWindowsDiagnostics();
  cutum::CubatariumLogInfo("App", "Cubatarium starting");
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
    if (std::strcmp(argv[i], "--creature-asset-smoke") == 0)
    {
#ifdef _WIN32
      cutum::CubatariumAttachParentConsole();
#endif
      auto paths = std::make_shared<cutum::UDesktopPlatformPaths>();
      cutum::IPlatformPaths::SetGlobal(paths);
      return cutum::RunCreatureAssetSmoke();
    }
    if (std::strcmp(argv[i], "--creature-spawn-smoke") == 0)
    {
#ifdef _WIN32
      cutum::CubatariumAttachParentConsole();
#endif
      auto paths = std::make_shared<cutum::UDesktopPlatformPaths>();
      cutum::IPlatformPaths::SetGlobal(paths);
      return cutum::RunCreatureSpawnSmoke();
    }
    if (std::strcmp(argv[i], "--creature-movement-smoke") == 0)
    {
#ifdef _WIN32
      cutum::CubatariumAttachParentConsole();
#endif
      auto paths = std::make_shared<cutum::UDesktopPlatformPaths>();
      cutum::IPlatformPaths::SetGlobal(paths);
      return cutum::RunCreatureMovementSmoke();
    }
    if (std::strcmp(argv[i], "--creature-wander-smoke") == 0)
    {
#ifdef _WIN32
      cutum::CubatariumAttachParentConsole();
#endif
      auto paths = std::make_shared<cutum::UDesktopPlatformPaths>();
      cutum::IPlatformPaths::SetGlobal(paths);
      return cutum::RunCreatureWanderSmoke();
    }
    if (std::strcmp(argv[i], "--creature-stack-smoke") == 0)
    {
#ifdef _WIN32
      cutum::CubatariumAttachParentConsole();
#endif
      auto paths = std::make_shared<cutum::UDesktopPlatformPaths>();
      cutum::IPlatformPaths::SetGlobal(paths);
      return cutum::RunCreatureStackSmoke();
    }
    if (std::strcmp(argv[i], "--creature-movement-diagnose") == 0)
    {
#ifdef _WIN32
      cutum::CubatariumAttachParentConsole();
#endif
      auto paths = std::make_shared<cutum::UDesktopPlatformPaths>();
      cutum::IPlatformPaths::SetGlobal(paths);
      if (i + 1 >= argc)
      {
        std::cerr << "--creature-movement-diagnose requires <species>\n";
        return 1;
      }
      return cutum::RunCreatureMovementDiagnose(argv[i + 1]);
    }
    if (std::strcmp(argv[i], "--creature-step-up-smoke") == 0)
    {
#ifdef _WIN32
      cutum::CubatariumAttachParentConsole();
#endif
      auto paths = std::make_shared<cutum::UDesktopPlatformPaths>();
      cutum::IPlatformPaths::SetGlobal(paths);
      return cutum::RunCreatureStepUpSmoke();
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

  int argc = 0;
  LPWSTR *argvWide = CommandLineToArgvW(GetCommandLineW(), &argc);
  std::vector<std::string> argStorage;
  std::vector<char *> argv;
  argStorage.reserve(static_cast<size_t>(argc));
  argv.reserve(static_cast<size_t>(argc));
  if (argvWide)
  {
    for (int i = 0; i < argc; ++i)
    {
      const int bytes = WideCharToMultiByte(CP_UTF8, 0, argvWide[i], -1, nullptr,
                                            0, nullptr, nullptr);
      argStorage.emplace_back(bytes > 0 ? static_cast<size_t>(bytes - 1) : 0, '\0');
      if (bytes > 1)
      {
        WideCharToMultiByte(CP_UTF8, 0, argvWide[i], -1, argStorage.back().data(),
                            bytes, nullptr, nullptr);
      }
      argv.push_back(argStorage.back().data());
    }
    LocalFree(argvWide);
  }
  return main(argc, argv.empty() ? nullptr : argv.data());
}
#endif
