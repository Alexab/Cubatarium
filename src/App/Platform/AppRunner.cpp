#include "App/Platform/AppRunner.h"

#include "App/Application.h"
#include "App/Core.h"
#if !defined(__ANDROID__)
#include "App/Platform/DesktopPlatformWindow.h"
#include "App/Platform/GlfwKeyCompat.h"
#include "App/Platform/InputManager.h"
#endif
#include "App/Platform/IUPlatformPaths.h"
#include "App/Platform/IUPlatformWindow.h"
#include "App/Platform/Log.h"
#include "App/Settings/AppSettingsSnapshot.h"
#include "App/Settings/AppState.h"
#include "Blocks/BlockDefinitionStorage.h"
#include "Gui/Core/GuiMetrics.h"
#include "Render/Camera/Camera.h"
#include "Render/Engine/GeometryEngine.h"
#include "Render/Engine/TextRenderer.h"
#include "Render/Engine/ViewEngine.h"
#include "Render/Textures/TextureBase.h"
#include "Render/Textures/TextureCube.h"
#include "Creatures/Player/User.h"
#include "World/Chunks/ChunkManager.h"
#include "World/Core/World.h"
#include "World/Math/BlockTypes.h"
#include "World/Core/WorldLoadDiagnostics.h"
#include "World/Diagnostics/FramePerfMonitor.h"
#include "World/Mesh/WorldMeshService.h"
#include "World/Objects/ObjectLibrary.h"
#include "WorldGen/Core/ProceduralSettings.h"

#include <chrono>
#include <cmath>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <sstream>

namespace cutum
{

int RunCubatarium(IUPlatformWindow &window, IUPlatformPaths &paths)
{
  try
  {
    IUPlatformPaths::SetGlobal(
        std::shared_ptr<IUPlatformPaths>(&paths, [](IUPlatformPaths *) {}));

    const glm::ivec2 fbSize = window.GetFramebufferSize();
    const int initW = fbSize.x > 0 ? fbSize.x : 1280;
    const int initH = fbSize.y > 0 ? fbSize.y : 720;
    CubatariumLogInfo("App", "Initializing platform window...");
    if (!window.Initialize(initW, initH, "Cubatarium"))
    {
      CubatariumLogError("App",
                         "Failed to initialize platform window (OpenGL/GLFW)");
      return -1;
    }

    auto texture_base_instance = std::make_shared<UTextureBaseStorage>();
    auto texture_cube_instance =
        std::make_shared<UTextureCubeStorage>(texture_base_instance);
    auto block_definitions = std::make_shared<UBlockDefinitionStorage>();

    auto object_library = std::make_shared<UObjectLibrary>();
    auto view_engine = std::make_shared<UViewEngine>();
    auto world = std::make_shared<UWorld>(texture_cube_instance, view_engine);
    auto text_renderer = std::make_shared<UTextRenderer>();

    CubatariumLogInfo("App", "Initializing text renderer...");
    if (!text_renderer->Initialize(16))
    {
      CubatariumLogError("App",
                         "Failed to initialize text renderer (FreeType/fonts)");
      return -1;
    }

    text_renderer->SetWindowSize(initW, initH);

    auto geometry_engine = std::make_shared<UGeometryEngine>(
        world, texture_base_instance, texture_cube_instance, text_renderer);
    CubatariumLogInfo("App", "Initializing geometry engine...");
    if (!geometry_engine->InitEngine())
    {
      CubatariumLogError("App",
                         "Failed to initialize geometry engine (shaders/GPU)");
      return -1;
    }

    auto core = std::make_shared<UCore>(
        texture_base_instance, texture_cube_instance, object_library, world,
        geometry_engine, view_engine);
    geometry_engine->SetGameContent(core.get());

    texture_cube_instance->SetBlockDefinitions(block_definitions);
    world->SetBlockDefinitionStorage(block_definitions);

    window.SetInstances(core, world, geometry_engine, view_engine);
    window.SetTextRenderer(text_renderer);

    auto application = std::make_shared<UApplication>(
        core, world, geometry_engine, view_engine, text_renderer,
        geometry_engine->GetShaderManager(), block_definitions);
    window.SetApplication(application);

    CubatariumLogInfo("App", "Starting application...");
    application->Startup(paths.ResolveWritable("config.json").string());
    if (!application->StartupSucceeded())
    {
      CubatariumLogError("App", "Startup failed — staying on screen for diagnostics");
    }
    else
    {
      const glm::ivec2 size = window.GetFramebufferSize();
      if (size.x > 0 && size.y > 0)
      {
        PlatformUiMetrics platform;
        platform.ScreenWidthPx = size.x;
        platform.ScreenHeightPx = size.y;
        application->UpdateUiScale(size.x, size.y, platform);
      }
    }

    window.Run();

    try
    {
      core->SaveSystem(paths.ResolveWritable("config.json").string());
    }
    catch (const std::exception &e)
    {
      CubatariumLogError("App",
                         std::string("SaveSystem failed: ") + e.what());
    }
    application.reset();
    text_renderer.reset();
    geometry_engine.reset();
    view_engine.reset();
    core.reset();
    world.reset();
    return 0;
  }
  catch (const std::exception &e)
  {
    CubatariumLogError("App", std::string("Exception: ") + e.what());
    return -1;
  }
  catch (...)
  {
    CubatariumLogError("App", "Unknown exception");
    return -1;
  }
}

#if defined(__ANDROID__)
int RunEnterGameSmoke(IUPlatformPaths &, int)
{
  CubatariumLogError("App", "enter-game-smoke is desktop-only");
  return 1;
}

int RunFlightSim(IUPlatformPaths &, const FlightSimOptions &)
{
  CubatariumLogError("App", "flight-sim is desktop-only");
  return 1;
}
#else
int RunEnterGameSmoke(IUPlatformPaths &paths, int in_game_frames)
{
  if (in_game_frames < 1)
  {
    in_game_frames = 5;
  }

  UDesktopPlatformWindow window;
  if (!window.InitializeHidden(1280, 720, "enter-game-smoke"))
  {
    std::cerr << "enter-game-smoke: failed to initialize window" << std::endl;
    return 1;
  }

  try
  {
    IUPlatformPaths::SetGlobal(
        std::shared_ptr<IUPlatformPaths>(&paths, [](IUPlatformPaths *) {}));

    auto texture_base_instance = std::make_shared<UTextureBaseStorage>();
    auto texture_cube_instance =
        std::make_shared<UTextureCubeStorage>(texture_base_instance);
    auto block_definitions = std::make_shared<UBlockDefinitionStorage>();
    auto object_library = std::make_shared<UObjectLibrary>();
    auto view_engine = std::make_shared<UViewEngine>();
    auto world = std::make_shared<UWorld>(texture_cube_instance, view_engine);
    auto text_renderer = std::make_shared<UTextRenderer>();
    if (!text_renderer->Initialize(16))
    {
      std::cerr << "enter-game-smoke: text renderer init failed" << std::endl;
      return 1;
    }
    text_renderer->SetWindowSize(1280, 720);

    auto geometry_engine = std::make_shared<UGeometryEngine>(
        world, texture_base_instance, texture_cube_instance, text_renderer);
    if (!geometry_engine->InitEngine())
    {
      std::cerr << "enter-game-smoke: geometry engine init failed" << std::endl;
      return 1;
    }

    auto core = std::make_shared<UCore>(
        texture_base_instance, texture_cube_instance, object_library, world,
        geometry_engine, view_engine);
    geometry_engine->SetGameContent(core.get());
    texture_cube_instance->SetBlockDefinitions(block_definitions);
    world->SetBlockDefinitionStorage(block_definitions);

    window.SetInstances(core, world, geometry_engine, view_engine);
    window.SetTextRenderer(text_renderer);

    auto application = std::make_shared<UApplication>(
        core, world, geometry_engine, view_engine, text_renderer,
        geometry_engine->GetShaderManager(), block_definitions);
    window.SetApplication(application);

    application->Startup(paths.ResolveWritable("config.json").string());
    if (!application->StartupSucceeded())
    {
      std::cerr << "enter-game-smoke: startup failed" << std::endl;
      return 1;
    }

    application->ScheduleEnterGame();

    const auto started = std::chrono::steady_clock::now();
    int ingame_frames_seen = 0;
    bool loading_seen = false;
    window.SetStopPredicate(
        [&]()
        {
          const auto elapsed_ms =
              std::chrono::duration_cast<std::chrono::milliseconds>(
                  std::chrono::steady_clock::now() - started)
                  .count();
          if (elapsed_ms > 180000)
          {
            return true;
          }
          if (application->GetState() == AppState::Loading)
          {
            loading_seen = true;
          }
          if (application->GetState() == AppState::InGame)
          {
            ++ingame_frames_seen;
          }
          return ingame_frames_seen >= in_game_frames;
        });

    window.Run();

    world->PrepareForShutdown();

    LogWorldLoadDiag("enter_game_smoke_end", *world);
    WarnIfTerrainMeshesMissing(*world, "enter-game-smoke");

    const size_t blocks = world->GetBlockWorld().CountNonAir();
    const size_t cache = world->GetMeshService().GetGreedyCacheSize();
    const size_t batches =
        world->GetMeshService().GetCache().GetGreedyOpaqueCutoutRefs().size() +
        world->GetMeshService().GetCache().GetGreedyTransparentRefs().size();
    const size_t vertices = world->GetMeshService().GetGreedyVertexCount();

    int exit_code = 0;
    if (!loading_seen)
    {
      std::cerr << "enter-game-smoke: FAIL loading screen never shown"
                << std::endl;
      exit_code = 1;
    }
    else if (ingame_frames_seen < in_game_frames)
    {
      std::cerr << "enter-game-smoke: FAIL timed out before in-game (frames="
                << ingame_frames_seen << ")" << std::endl;
      exit_code = 1;
    }
    else if (blocks > 0 && cache == 0 && batches == 0)
    {
      std::cerr << "enter-game-smoke: FAIL terrain meshes missing" << std::endl;
      exit_code = 1;
    }
    else
    {
      std::cout << "enter-game-smoke: PASS blocks=" << blocks
                << " greedy_batches=" << batches << std::endl;
    }

    const std::filesystem::path report_path =
        GetExecutableDirectory() / "enter_game_smoke_report.txt";
    std::ofstream report(report_path);
    if (report)
    {
      report << "exit_code=" << exit_code << '\n'
             << "loading_seen=" << (loading_seen ? 1 : 0) << '\n'
             << "ingame_frames=" << ingame_frames_seen << '\n'
             << "default_world=" << core->GetAppSettings().DefaultWorld << '\n'
             << "blocks=" << blocks << '\n'
             << "greedy_cache=" << cache << '\n'
             << "greedy_batches=" << batches << '\n'
             << "greedy_vertices=" << vertices << '\n';
    }

    return exit_code;
  }
  catch (const std::exception &e)
  {
    std::cerr << "enter-game-smoke: exception: " << e.what() << std::endl;
    return 1;
  }
}

int RunFlightSim(IUPlatformPaths &paths, const FlightSimOptions &options)
{
  double in_game_seconds = options.InGameSeconds;
  if (options.FlyStopMode)
  {
    in_game_seconds = options.IdleBeforeFlySec + options.FlyPhaseSec +
                      options.StopPhaseSec;
  }
  if (options.BreakStandMode)
  {
    in_game_seconds = options.IdleBeforeFlySec + options.BreakPhaseSec;
  }
  if (in_game_seconds < 5.0)
  {
    in_game_seconds = 5.0;
  }
  double safety_timeout = options.SafetyTimeoutSec;
  if (safety_timeout < in_game_seconds + 30.0)
  {
    safety_timeout = in_game_seconds + 30.0;
  }

  UDesktopPlatformWindow window;
  const bool ok =
      options.VisibleWindow
          ? window.Initialize(1280, 720, "flight-sim")
          : window.InitializeHidden(1280, 720, "flight-sim");
  if (!ok)
  {
    std::cerr << "flight-sim: failed to initialize window" << std::endl;
    return 1;
  }
  // Long loads exceed the 60s autosave interval; firing save on first InGame
  // frame starves streaming (wall≈3s) and invalidates travel/stop gates.
  window.SetAutosaveEnabled(false);

  try
  {
    IUPlatformPaths::SetGlobal(
        std::shared_ptr<IUPlatformPaths>(&paths, [](IUPlatformPaths *) {}));

    auto texture_base_instance = std::make_shared<UTextureBaseStorage>();
    auto texture_cube_instance =
        std::make_shared<UTextureCubeStorage>(texture_base_instance);
    auto block_definitions = std::make_shared<UBlockDefinitionStorage>();
    auto object_library = std::make_shared<UObjectLibrary>();
    auto view_engine = std::make_shared<UViewEngine>();
    auto world = std::make_shared<UWorld>(texture_cube_instance, view_engine);
    auto text_renderer = std::make_shared<UTextRenderer>();
    if (!text_renderer->Initialize(16))
    {
      std::cerr << "flight-sim: text renderer init failed" << std::endl;
      return 1;
    }
    text_renderer->SetWindowSize(1280, 720);

    auto geometry_engine = std::make_shared<UGeometryEngine>(
        world, texture_base_instance, texture_cube_instance, text_renderer);
    if (!geometry_engine->InitEngine())
    {
      std::cerr << "flight-sim: geometry engine init failed" << std::endl;
      return 1;
    }

    auto core = std::make_shared<UCore>(
        texture_base_instance, texture_cube_instance, object_library, world,
        geometry_engine, view_engine);
    geometry_engine->SetGameContent(core.get());
    texture_cube_instance->SetBlockDefinitions(block_definitions);
    world->SetBlockDefinitionStorage(block_definitions);

    window.SetInstances(core, world, geometry_engine, view_engine);
    window.SetTextRenderer(text_renderer);

    auto application = std::make_shared<UApplication>(
        core, world, geometry_engine, view_engine, text_renderer,
        geometry_engine->GetShaderManager(), block_definitions);
    window.SetApplication(application);

    application->Startup(paths.ResolveWritable("config.json").string());
    if (!application->StartupSucceeded())
    {
      std::cerr << "flight-sim: startup failed" << std::endl;
      return 1;
    }

    if (!options.WorldName.empty())
    {
      AppSettingsSnapshot settings = core->GetAppSettings();
      settings.DefaultWorld = options.WorldName;
      core->ApplyAppSettings(settings);
    }

    UFramePerfMonitor::EnsureSession();
    application->ScheduleEnterGame();

    const auto started = std::chrono::steady_clock::now();
    std::chrono::steady_clock::time_point ingame_started{};
    bool ingame_clock_started = false;
    bool loading_seen = false;
    bool autopilot_armed = false;
    bool autopilot_flying = false;
    bool fly_stop_released = false;
    double last_break_request_sec = -1.0e9;
    int break_requests = 0;
    int ingame_frames_seen = 0;
    int start_focus_cx = 0;
    int start_focus_cz = 0;
    bool start_focus_captured = false;

    window.SetStopPredicate(
        [&]()
        {
          const auto now = std::chrono::steady_clock::now();
          const double elapsed_sec =
              std::chrono::duration<double>(now - started).count();
          if (elapsed_sec > safety_timeout)
          {
            return true;
          }
          if (application->GetState() == AppState::Loading)
          {
            loading_seen = true;
          }
          if (application->GetState() == AppState::InGame)
          {
            ++ingame_frames_seen;
            if (!ingame_clock_started)
            {
              ingame_started = now;
              ingame_clock_started = true;
            }
            const double ingame_sec =
                std::chrono::duration<double>(now - ingame_started).count();
            if (auto camera = world->GetCurrentUserCamera())
            {
              if (!autopilot_armed &&
                  (options.Fly || options.HoldForward || options.BreakStandMode))
              {
                if (options.Fly && !options.BreakStandMode)
                {
                  camera->SetFreeMove(true);
                }
                camera->SetOrientation(options.FaceYawDeg, options.FacePitchDeg);
                const float sea =
                    static_cast<float>(world->GetProceduralSettings().SeaLevel);
                glm::vec3 pos = camera->GetPosition();
                if (options.TeleportToCruiseStart)
                {
                  pos.x = options.CruiseStartChunkX *
                              static_cast<float>(CHUNK_SIZE) +
                          8.0f;
                  pos.z = options.CruiseStartChunkZ *
                              static_cast<float>(CHUNK_SIZE) +
                          8.0f;
                }
                if (!options.BreakStandMode)
                {
                  const float min_y = sea + options.MinAltitudeAboveSea;
                  if (pos.y < min_y || options.TeleportToCruiseStart)
                  {
                    pos.y = min_y;
                  }
                }
                camera->SetPosition(pos);
                if (auto user = world->GetCurrentUser())
                {
                  user->SetPosition(pos);
                }
                autopilot_armed = true;
                std::cout << "flight-sim: autopilot armed fly="
                          << (options.Fly ? 1 : 0)
                          << " hold_forward=" << (options.HoldForward ? 1 : 0)
                          << " hold_space=" << (options.HoldSpace ? 1 : 0)
                          << " break_stand=" << (options.BreakStandMode ? 1 : 0)
                          << " teleport=" << (options.TeleportToCruiseStart ? 1 : 0)
                          << " yaw=" << options.FaceYawDeg
                          << " pitch=" << options.FacePitchDeg
                          << " idle_s=" << options.IdleBeforeFlySec
                          << std::endl;
              }
              if (autopilot_armed)
              {
                if (options.Fly && !options.BreakStandMode)
                {
                  camera->SetFreeMove(true);
                }
                camera->SetOrientation(options.FaceYawDeg, options.FacePitchDeg);
                // Keep cruise altitude (manual holds Space / levels pitch).
                if (!options.BreakStandMode &&
                    (options.HoldSpace || options.MinAltitudeAboveSea > 0.0f))
                {
                  const float sea = static_cast<float>(
                      world->GetProceduralSettings().SeaLevel);
                  const float min_y = sea + options.MinAltitudeAboveSea;
                  glm::vec3 pos = camera->GetPosition();
                  if (pos.y < min_y)
                  {
                    pos.y = min_y;
                    camera->SetPosition(pos);
                    if (auto user = world->GetCurrentUser())
                    {
                      user->SetPosition(pos);
                    }
                  }
                }
                if (options.BreakStandMode &&
                    ingame_sec >= options.IdleBeforeFlySec)
                {
                  if (ingame_sec - last_break_request_sec >=
                      options.BreakIntervalSec)
                  {
                    last_break_request_sec = ingame_sec;
                    world->RequestFlightSimBreak();
                    ++break_requests;
                    if (break_requests == 1 || (break_requests % 5) == 0)
                    {
                      std::cout << "flight-sim: break-stand request #"
                                << break_requests << " at t=" << ingame_sec
                                << "s" << std::endl;
                    }
                  }
                }
                else if (options.HoldForward &&
                         ingame_sec >= options.IdleBeforeFlySec)
                {
                  const double fly_end =
                      options.IdleBeforeFlySec +
                      (options.FlyStopMode ? options.FlyPhaseSec
                                           : in_game_seconds);
                  if (options.FlyStopMode && ingame_sec >= fly_end)
                  {
                    if (!fly_stop_released)
                    {
                      fly_stop_released = true;
                      window.SetAutopilotKey(KeyCode::Key_W, false);
                      if (options.Sprint)
                      {
                        window.SetAutopilotKey(KeyCode::Key_Ctrl, false);
                      }
                      if (options.HoldSpace)
                      {
                        window.SetAutopilotKey(KeyCode::Key_Space, false);
                      }
                      std::cout << "flight-sim: fly-stop released W at t="
                                << ingame_sec << "s" << std::endl;
                    }
                  }
                  else if (!autopilot_flying)
                  {
                    autopilot_flying = true;
                    window.SetAutopilotKey(KeyCode::Key_W, true);
                    if (options.Sprint)
                    {
                      window.SetAutopilotKey(KeyCode::Key_Ctrl, true);
                    }
                    if (options.HoldSpace)
                    {
                      window.SetAutopilotKey(KeyCode::Key_Space, true);
                    }
                    std::cout << "flight-sim: hold-forward engaged at t="
                              << ingame_sec << "s" << std::endl;
                  }
                }
              }
              if (!start_focus_captured && ingame_sec >= 2.0)
              {
                const glm::ivec3 focus_chunk = UChunkManager::WorldToChunk(
                    world->GetPreferredLoadFocusBlock());
                start_focus_cx = focus_chunk.x;
                start_focus_cz = focus_chunk.z;
                start_focus_captured = true;
              }
            }
            return ingame_sec >= in_game_seconds;
          }
          return false;
        });

    window.Run();
    window.ClearAutopilotKeys();

    // Flush perf + write report BEFORE shutdown joins so hang still leaves
    // artifacts for the harness (flight-sim exit hang is a known risk).
    UFramePerfMonitor::Shutdown();

    const std::string perf_path = UFramePerfMonitor::GetLastSessionPath();
    const std::string world_name = core->GetAppSettings().DefaultWorld;
    int exit_code = 0;
    if (!loading_seen)
    {
      std::cerr << "flight-sim: FAIL loading screen never shown" << std::endl;
      exit_code = 1;
    }
    else if (!ingame_clock_started || ingame_frames_seen < 30)
    {
      std::cerr << "flight-sim: FAIL insufficient in-game frames="
                << ingame_frames_seen << std::endl;
      exit_code = 1;
    }

    const glm::ivec3 end_focus_chunk =
        UChunkManager::WorldToChunk(world->GetPreferredLoadFocusBlock());
    const int end_focus_cx = end_focus_chunk.x;
    const int end_focus_cz = end_focus_chunk.z;
    const int focus_dx = end_focus_cx - start_focus_cx;
    const int focus_dz = end_focus_cz - start_focus_cz;
    const int chunks_traveled =
        start_focus_captured
            ? static_cast<int>(std::max(std::abs(focus_dx), std::abs(focus_dz)))
            : 0;
    if (options.HoldForward && !options.BreakStandMode &&
        chunks_traveled < 3)
    {
      std::cerr << "flight-sim: FAIL insufficient travel chunks="
                << chunks_traveled << " start=(" << start_focus_cx << ","
                << start_focus_cz << ") end=(" << end_focus_cx << ","
                << end_focus_cz << ")" << std::endl;
      exit_code = 1;
    }
    if (options.BreakStandMode && break_requests < 1)
    {
      std::cerr << "flight-sim: FAIL break-stand issued 0 break requests"
                << std::endl;
      exit_code = 1;
    }

    if (!options.PerfOutPath.empty() && !perf_path.empty())
    {
      std::error_code ec;
      std::filesystem::create_directories(
          std::filesystem::path(options.PerfOutPath).parent_path(), ec);
      std::filesystem::copy_file(
          perf_path, options.PerfOutPath,
          std::filesystem::copy_options::overwrite_existing, ec);
      if (ec)
      {
        std::cerr << "flight-sim: warn copy perf failed: " << ec.message()
                  << std::endl;
      }
    }

    const std::filesystem::path report_path =
        options.ReportPath.empty()
            ? (GetExecutableDirectory() / "flight_sim_report.json")
            : std::filesystem::path(options.ReportPath);
    {
      std::error_code ec;
      std::filesystem::create_directories(report_path.parent_path(), ec);
      std::ofstream report(report_path);
      if (report)
      {
        report << "{\n"
               << "  \"exit_code\": " << exit_code << ",\n"
               << "  \"loading_seen\": " << (loading_seen ? "true" : "false")
               << ",\n"
               << "  \"ingame_frames\": " << ingame_frames_seen << ",\n"
               << "  \"ingame_seconds_requested\": " << in_game_seconds << ",\n"
               << "  \"world\": \"" << world_name << "\",\n"
               << "  \"autopilot_armed\": "
               << (autopilot_armed ? "true" : "false") << ",\n"
               << "  \"autopilot_flying\": "
               << (autopilot_flying ? "true" : "false") << ",\n"
               << "  \"face_yaw_deg\": " << options.FaceYawDeg << ",\n"
               << "  \"idle_before_fly_sec\": " << options.IdleBeforeFlySec
               << ",\n"
               << "  \"start_focus\": [" << start_focus_cx << ", "
               << start_focus_cz << "],\n"
               << "  \"end_focus\": [" << end_focus_cx << ", " << end_focus_cz
               << "],\n"
               << "  \"chunks_traveled_cheb\": " << chunks_traveled << ",\n"
               << "  \"perf_jsonl\": \"" << perf_path << "\",\n"
               << "  \"analyze\": \"run tools/flight_sim_analyze.py on perf\"\n"
               << "}\n";
      }
    }

    std::cout << "flight-sim: report written path=" << report_path.string()
              << " — shutting down (fast)" << std::endl;
    world->PrepareForShutdownFast();

    std::cout << "flight-sim: done exit=" << exit_code
              << " world=" << world_name << " frames=" << ingame_frames_seen
              << " perf=" << perf_path << " report=" << report_path.string()
              << std::endl;
    std::cout.flush();
    std::cerr.flush();
    // Skip destructor joins (Persistence/mesh workers) — known hang after save.
    std::_Exit(exit_code);
  }
  catch (const std::exception &e)
  {
    std::cerr << "flight-sim: exception: " << e.what() << std::endl;
    std::_Exit(1);
  }
}
#endif // !__ANDROID__

} // namespace cutum
