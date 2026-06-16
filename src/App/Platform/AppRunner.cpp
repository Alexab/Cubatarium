#include "App/Platform/AppRunner.h"

#include "App/Application.h"
#include "App/Core.h"
#include "App/Platform/IPlatformPaths.h"
#include "App/Platform/IPlatformWindow.h"
#include "App/Platform/Log.h"
#include "Blocks/BlockDefinitionStorage.h"
#include "Render/Engine/GeometryEngine.h"
#include "Render/Engine/TextRenderer.h"
#include "Render/Engine/ViewEngine.h"
#include "Render/Textures/TextureBase.h"
#include "Render/Textures/TextureCube.h"
#include "Storage/Object.h"
#include "Storage/ObjectStorage.h"
#include "World/Core/World.h"
#include "World/Prefabs/Prefab.h"

#include <iostream>

namespace cutum
{

int RunCubatarium(IPlatformWindow &window, IPlatformPaths &paths)
{
  try
  {
    IPlatformPaths::SetGlobal(
        std::shared_ptr<IPlatformPaths>(&paths, [](IPlatformPaths *) {}));

    const glm::ivec2 fbSize = window.GetFramebufferSize();
    const int initW = fbSize.x > 0 ? fbSize.x : 1280;
    const int initH = fbSize.y > 0 ? fbSize.y : 720;
    if (!window.Initialize(initW, initH, "Cubatarium"))
    {
      CubatariumLogError("App", "Failed to initialize platform window");
      return -1;
    }

    auto texture_base_instance = std::make_shared<UTextureBaseStorage>();
    auto texture_cube_instance =
        std::make_shared<UTextureCubeStorage>(texture_base_instance);
    auto block_definitions = std::make_shared<UBlockDefinitionStorage>();

    auto object_storage =
        std::make_shared<UObjectStorage>(texture_cube_instance);
    auto prefab_library = std::make_shared<UPrefabLibrary>();
    auto view_engine = std::make_shared<UViewEngine>();
    auto world = std::make_shared<UWorld>(object_storage, view_engine);
    auto text_renderer = std::make_shared<UTextRenderer>();

    if (!text_renderer->Initialize(16))
    {
      CubatariumLogError("App", "Failed to initialize text renderer");
      return -1;
    }

    text_renderer->SetWindowSize(initW, initH);

    auto geometry_engine = std::make_shared<UGeometryEngine>(
        object_storage, world, texture_base_instance, texture_cube_instance,
        text_renderer);
    if (!geometry_engine->InitEngine())
    {
      CubatariumLogError("App", "Failed to initialize geometry engine");
      return -1;
    }

    auto core = std::make_shared<UCore>(
        texture_base_instance, texture_cube_instance, object_storage,
        prefab_library, world, geometry_engine, view_engine);

    block_definitions->Load("models/blocks");
    texture_cube_instance->SetBlockDefinitions(block_definitions);
    world->SetBlockDefinitionStorage(block_definitions);

    window.SetInstances(core, world, geometry_engine, view_engine);
    window.SetTextRenderer(text_renderer);

    auto application = std::make_shared<UApplication>(
        core, world, geometry_engine, view_engine, text_renderer,
        geometry_engine->GetShaderManager(), block_definitions);
    window.SetApplication(application);

    application->Startup(paths.ResolveWritable("config.json").string());
    window.Run();

    core->SaveSystem(paths.ResolveWritable("config.json").string());
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
