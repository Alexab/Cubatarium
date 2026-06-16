#include "App/Utils.h"

#include <iostream>
#include <memory>

#include "Render/GlIncludes.h"
#include <GLFW/glfw3.h>

#include "App/Core.h"
#include "Blocks/BlockDefinitionStorage.h"
#include "Render/Engine/ViewEngine.h"
#include "Render/Textures/TextureBase.h"
#include "Render/Textures/TextureCube.h"
#include "Storage/ObjectStorage.h"
#include "World/Core/World.h"
#include "World/Prefabs/Prefab.h"

namespace cutum
{

int RunValidateLoad()
{
  if (!glfwInit())
  {
    std::cerr << "validate-load: glfwInit failed" << std::endl;
    return 1;
  }
  glfwWindowHint(GLFW_VISIBLE, GLFW_FALSE);
  glfwWindowHint(GLFW_CONTEXT_VERSION_MAJOR, 3);
  glfwWindowHint(GLFW_CONTEXT_VERSION_MINOR, 3);
  glfwWindowHint(GLFW_OPENGL_PROFILE, GLFW_OPENGL_CORE_PROFILE);
  GLFWwindow *ctx = glfwCreateWindow(64, 64, "validate", nullptr, nullptr);
  if (!ctx)
  {
    std::cerr << "validate-load: failed to create GL context" << std::endl;
    glfwTerminate();
    return 1;
  }
  glfwMakeContextCurrent(ctx);
  if (glewInit() != GLEW_OK)
  {
    std::cerr << "validate-load: glewInit failed" << std::endl;
    glfwDestroyWindow(ctx);
    glfwTerminate();
    return 1;
  }

  auto texture_base_instance = std::make_shared<UTextureBaseStorage>();
  auto texture_cube_instance =
      std::make_shared<UTextureCubeStorage>(texture_base_instance);
  auto block_definitions = std::make_shared<UBlockDefinitionStorage>();
  auto object_storage = std::make_shared<UObjectStorage>(texture_cube_instance);
  auto prefab_library = std::make_shared<UPrefabLibrary>();
  auto view_engine = std::make_shared<UViewEngine>();
  auto world = std::make_shared<UWorld>(object_storage, view_engine);
  auto core = std::make_shared<UCore>(
      texture_base_instance, texture_cube_instance, object_storage,
      prefab_library, world, nullptr, view_engine);

  block_definitions->Load("models/blocks");
  texture_cube_instance->SetBlockDefinitions(block_definitions);
  world->SetBlockDefinitionStorage(block_definitions);
  core->LoadSystem("config.json");
  std::cout << "validate-load: blocks=" << world->GetCachedBlockCount()
            << " instances=" << world->GetRenderInstanceCount() << std::endl;

  glfwDestroyWindow(ctx);
  glfwTerminate();
  return 0;
}

} // namespace cutum
