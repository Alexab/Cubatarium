#include <cstring>
#include <iostream>
#include <memory>

#ifdef _WIN32
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#endif

#include "Application.h"
#include "BlockDefinitionStorage.h"
#include "Core.h"
#include "GeometryEngine.h"
#include "Object.h"
#include "ObjectStorage.h"
#include "Prefab.h"
#include "TextRenderer.h"
#include "TextureBase.h"
#include "TextureCube.h"
#include "Utils.h"
#include "ViewEngine.h"
#include "WindowManager.h"
#include "World.h"

int main(int argc, char *argv[])
{
  using namespace cutum;

  try
  {
    for (int i = 1; i < argc; ++i)
    {
      if (std::strcmp(argv[i], "--validate-load") == 0)
      {
        return RunValidateLoad();
      }
    }

    auto windowManager = std::make_unique<UWindowManager>();

    if (!windowManager->Initialize(1280, 720, "Cubatarium"))
    {
      std::cerr << "Failed to initialize window manager" << std::endl;
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
      std::cerr << "Failed to initialize text renderer" << std::endl;
      return -1;
    }

    text_renderer->SetWindowSize(1280, 720);

    auto geometry_engine = std::make_shared<UGeometryEngine>(
        object_storage, world, texture_base_instance, texture_cube_instance,
        text_renderer);
    if (!geometry_engine->InitEngine())
    {
      std::cerr << "Failed to initialize geometry engine" << std::endl;
      return -1;
    }

    auto core = std::make_shared<UCore>(
        texture_base_instance, texture_cube_instance, object_storage,
        prefab_library, world, geometry_engine, view_engine);

    block_definitions->Load("models/blocks");
    texture_cube_instance->SetBlockDefinitions(block_definitions);
    world->SetBlockDefinitionStorage(block_definitions);

    windowManager->SetInstances(core, world, geometry_engine, view_engine);
    windowManager->SetTextRenderer(text_renderer);

    auto application = std::make_shared<UApplication>(
        core, world, geometry_engine, view_engine, text_renderer,
        geometry_engine->GetShaderManager(), block_definitions);
    windowManager->SetApplication(application);

    application->SetWindow(windowManager->GetWindow());
    application->Startup("config.json");

    windowManager->Run();

    core->SaveSystem("config.json");

    return 0;
  }
  catch (const std::exception &e)
  {
    std::cerr << "Exception: " << e.what() << std::endl;
    return -1;
  }
  catch (...)
  {
    std::cerr << "Unknown exception occurred" << std::endl;
    return -1;
  }
}

#ifdef _WIN32
int WINAPI WinMain(HINSTANCE hInstance, HINSTANCE hPrevInstance,
                   LPSTR lpCmdLine, int nCmdShow)
{
  return main(__argc, __argv);
}
#endif
