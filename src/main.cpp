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
#include "App/Platform/IUPlatformPaths.h"
#include "App/Platform/Log.h"
#include "App/Utils.h"
#include "ResourcePacks/ResourcePackSmoke.h"

#include <cstring>
#include <cstdlib>
int main(int argc, char *argv[])
{
#ifdef _WIN32
  cutum::CubatariumInstallWindowsDiagnostics();
#endif
  const bool also_stderr =
      argc > 1; // CLI / console modes also get ERROR+ on stderr via wrappers
  cutum::CubatariumInitLogging(argc > 0 ? argv[0] : "Cubatarium", also_stderr);

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
      cutum::IUPlatformPaths::SetGlobal(paths);
      return cutum::RunResourcePackSmoke(*paths);
    }
    if (std::strcmp(argv[i], "--load-world") == 0)
    {
#ifdef _WIN32
      cutum::CubatariumAttachParentConsole();
#endif
      auto paths = std::make_shared<cutum::UDesktopPlatformPaths>();
      cutum::IUPlatformPaths::SetGlobal(paths);
      return cutum::RunLoadWorld(argc, argv, i + 1);
    }
    if (std::strcmp(argv[i], "--enter-game-smoke") == 0)
    {
#ifdef _WIN32
      cutum::CubatariumAttachParentConsole();
#endif
      auto paths = std::make_shared<cutum::UDesktopPlatformPaths>();
      cutum::IUPlatformPaths::SetGlobal(paths);
      int in_game_frames = 5;
      if (i + 1 < argc && argv[i + 1][0] != '-')
      {
        in_game_frames = std::atoi(argv[i + 1]);
      }
      return cutum::RunEnterGameSmoke(*paths, in_game_frames);
    }
    if (std::strcmp(argv[i], "--autoload-last-world") == 0)
    {
#ifdef _WIN32
      cutum::CubatariumAttachParentConsole();
#endif
      auto paths = std::make_shared<cutum::UDesktopPlatformPaths>();
      cutum::IUPlatformPaths::SetGlobal(paths);
      cutum::AutoloadLastWorldOptions opt;
      for (int j = i + 1; j < argc; ++j)
      {
        if (std::strcmp(argv[j], "--visible") == 0)
        {
          opt.VisibleWindow = true;
        }
        else if (std::strcmp(argv[j], "--world") == 0 && j + 1 < argc)
        {
          opt.WorldName = argv[++j];
        }
        else if (std::strcmp(argv[j], "--timeout-sec") == 0 && j + 1 < argc)
        {
          opt.TimeoutSec = std::atof(argv[++j]);
        }
        else if (std::strcmp(argv[j], "--ingame-frames") == 0 && j + 1 < argc)
        {
          opt.InGameFrames = std::atoi(argv[++j]);
        }
        else if (argv[j][0] == '-')
        {
          break;
        }
      }
      return cutum::RunAutoloadLastWorld(*paths, opt);
    }
    if (std::strcmp(argv[i], "--flight-sim") == 0)
    {
#ifdef _WIN32
      cutum::CubatariumAttachParentConsole();
#endif
      auto paths = std::make_shared<cutum::UDesktopPlatformPaths>();
      cutum::IUPlatformPaths::SetGlobal(paths);
      cutum::FlightSimOptions opt;
      opt.WorldName = "World_164";
      for (int j = i + 1; j < argc; ++j)
      {
        if (std::strcmp(argv[j], "--world") == 0 && j + 1 < argc)
        {
          opt.WorldName = argv[++j];
        }
        else if (std::strcmp(argv[j], "--seconds") == 0 && j + 1 < argc)
        {
          opt.InGameSeconds = std::atof(argv[++j]);
        }
        else if (std::strcmp(argv[j], "--fly") == 0)
        {
          opt.Fly = true;
        }
        else if (std::strcmp(argv[j], "--no-fly") == 0)
        {
          opt.Fly = false;
        }
        else if (std::strcmp(argv[j], "--hold-forward") == 0)
        {
          opt.HoldForward = true;
        }
        else if (std::strcmp(argv[j], "--no-hold-forward") == 0)
        {
          opt.HoldForward = false;
        }
        else if (std::strcmp(argv[j], "--idle") == 0 && j + 1 < argc)
        {
          opt.IdleBeforeFlySec = std::atof(argv[++j]);
        }
        else if (std::strcmp(argv[j], "--yaw") == 0 && j + 1 < argc)
        {
          opt.FaceYawDeg = static_cast<float>(std::atof(argv[++j]));
        }
        else if (std::strcmp(argv[j], "--pitch") == 0 && j + 1 < argc)
        {
          opt.FacePitchDeg = static_cast<float>(std::atof(argv[++j]));
        }
        else if (std::strcmp(argv[j], "--sprint") == 0)
        {
          opt.Sprint = true;
        }
        else if (std::strcmp(argv[j], "--hold-space") == 0)
        {
          opt.HoldSpace = true;
        }
        else if (std::strcmp(argv[j], "--teleport-cruise") == 0)
        {
          opt.TeleportToCruiseStart = true;
        }
        else if (std::strcmp(argv[j], "--no-teleport-cruise") == 0)
        {
          opt.TeleportToCruiseStart = false;
        }
        else if (std::strcmp(argv[j], "--cruise-cx") == 0 && j + 1 < argc)
        {
          opt.CruiseStartChunkX = static_cast<float>(std::atof(argv[++j]));
        }
        else if (std::strcmp(argv[j], "--cruise-cz") == 0 && j + 1 < argc)
        {
          opt.CruiseStartChunkZ = static_cast<float>(std::atof(argv[++j]));
        }
        else if (std::strcmp(argv[j], "--min-alt-above-sea") == 0 &&
                 j + 1 < argc)
        {
          opt.MinAltitudeAboveSea = static_cast<float>(std::atof(argv[++j]));
        }
        else if (std::strcmp(argv[j], "--cruise-eye-y") == 0 && j + 1 < argc)
        {
          opt.CruiseEyeY = static_cast<float>(std::atof(argv[++j]));
        }
        else if (std::strcmp(argv[j], "--perf-out") == 0 && j + 1 < argc)
        {
          opt.PerfOutPath = argv[++j];
        }
        else if (std::strcmp(argv[j], "--report") == 0 && j + 1 < argc)
        {
          opt.ReportPath = argv[++j];
        }
        else if (std::strcmp(argv[j], "--fly-stop") == 0)
        {
          opt.FlyStopMode = true;
        }
        else if (std::strcmp(argv[j], "--fly-phase") == 0 && j + 1 < argc)
        {
          opt.FlyPhaseSec = std::atof(argv[++j]);
        }
        else if (std::strcmp(argv[j], "--stop-phase") == 0 && j + 1 < argc)
        {
          opt.StopPhaseSec = std::atof(argv[++j]);
        }
        else if (std::strcmp(argv[j], "--visible") == 0)
        {
          opt.VisibleWindow = true;
        }
        else if (std::strcmp(argv[j], "--break-stand") == 0)
        {
          opt.BreakStandMode = true;
          opt.Fly = false;
          opt.HoldForward = false;
          opt.HoldSpace = false;
          opt.TeleportToCruiseStart = false;
          opt.FacePitchDeg = 55.0f;
          opt.MinAltitudeAboveSea = 0.0f;
          if (opt.IdleBeforeFlySec < 8.0)
          {
            opt.IdleBeforeFlySec = 8.0;
          }
          if (opt.BreakPhaseSec < 10.0)
          {
            opt.BreakPhaseSec = 20.0;
          }
        }
        else if (std::strcmp(argv[j], "--yaw-sweep") == 0)
        {
          opt.YawSweepMode = true;
          opt.Fly = false;
          opt.HoldForward = false;
          opt.HoldSpace = false;
          opt.BreakStandMode = false;
          opt.FlyStopMode = false;
          opt.MinAltitudeAboveSea = 0.0f;
          if (opt.IdleBeforeFlySec < 5.0)
          {
            opt.IdleBeforeFlySec = 5.0;
          }
        }
        else if (std::strcmp(argv[j], "--yaw-sweep-sec") == 0 && j + 1 < argc)
        {
          opt.YawSweepSec = std::atof(argv[++j]);
        }
        else if (std::strcmp(argv[j], "--break-phase") == 0 && j + 1 < argc)
        {
          opt.BreakPhaseSec = std::atof(argv[++j]);
        }
        else if (std::strcmp(argv[j], "--break-interval") == 0 && j + 1 < argc)
        {
          opt.BreakIntervalSec = std::atof(argv[++j]);
        }
      }
      return cutum::RunFlightSim(*paths, opt);
    }
    if (std::strcmp(argv[i], "--validate-load") == 0)
    {
#ifdef _WIN32
      cutum::CubatariumAttachParentConsole();
#endif
      auto paths = std::make_shared<cutum::UDesktopPlatformPaths>();
      cutum::IUPlatformPaths::SetGlobal(paths);
      return cutum::RunValidateLoad();
    }
    if (std::strcmp(argv[i], "--bench-io") == 0)
    {
#ifdef _WIN32
      cutum::CubatariumAttachParentConsole();
#endif
      auto paths = std::make_shared<cutum::UDesktopPlatformPaths>();
      cutum::IUPlatformPaths::SetGlobal(paths);
      return cutum::RunBenchChunkIo();
    }
    if (std::strcmp(argv[i], "--create-world") == 0)
    {
#ifdef _WIN32
      cutum::CubatariumAttachParentConsole();
#endif
      auto paths = std::make_shared<cutum::UDesktopPlatformPaths>();
      cutum::IUPlatformPaths::SetGlobal(paths);
      return cutum::RunCreateWorld(argc, argv, i + 1);
    }
  }

  auto paths = std::make_shared<cutum::UDesktopPlatformPaths>();
  cutum::IUPlatformPaths::SetGlobal(paths);
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
