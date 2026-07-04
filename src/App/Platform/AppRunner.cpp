#include "App/Platform/AppRunner.h"

#include "App/Application.h"
#include "App/Core.h"
#include "App/Platform/IUPlatformPaths.h"
#include "App/Platform/IUPlatformWindow.h"
#include "App/Platform/Log.h"
#include "Blocks/BlockDefinitionStorage.h"
#include "Gui/Core/GuiMetrics.h"
#include "Render/Engine/GeometryEngine.h"
#include "Render/Engine/TextRenderer.h"
#include "Render/Engine/ViewEngine.h"
#include "Render/Textures/TextureBase.h"
#include "Render/Textures/TextureCube.h"
#include "World/Core/World.h"
#include "World/Objects/ObjectLibrary.h"

#include <iostream>

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
    window.Shutdown();
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

} // namespace cutum
