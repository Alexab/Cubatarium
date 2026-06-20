#include "App/Utils.h"

#include <iostream>
#include <memory>

#include "Render/GlIncludes.h"
#include <GLFW/glfw3.h>

#include "App/Core.h"
#include "Blocks/BlockDefinition.h"
#include "Blocks/BlockDefinitionStorage.h"
#include "Render/Engine/ViewEngine.h"
#include "Render/Textures/TextureBase.h"
#include "Render/Textures/TextureCube.h"
#include "Storage/ObjectStorage.h"
#include "World/Core/World.h"
#include "World/Math/BlockTypes.h"
#include "World/Prefabs/Prefab.h"
#include "WorldGen/Core/WorldGenRefs.h"

namespace cutum
{

namespace
{

bool TierABlocksResolved(UBlockRegistry &registry)
{
  static const char *kTierA[] = {
      "bedrock", "stone",  "dirt",   "grass",   "sand",    "sandstone",
      "gravel",  "snow",   "clay",   "ice",     "hellrock",  "water",
      "lava",    "fire",   "wood",   "tree_log", "tree_leaves"};
  for (const char *name : kTierA)
  {
    if (registry.GetIdByTypeName(name) == BLOCK_AIR)
    {
      std::cerr << "validate-load: missing tier A block '" << name << "'"
                << std::endl;
      return false;
    }
  }
  return true;
}

} // namespace

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

  if (!UWorldGenRefs::LoadFromFile("content/worldgen_refs.json"))
  {
    std::cerr << "validate-load: worldgen_refs.json not loaded" << std::endl;
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
  core->LoadConfig("config.json");

  if (!TierABlocksResolved(world->GetBlockRegistry()))
  {
    glfwDestroyWindow(ctx);
    glfwTerminate();
    return 1;
  }

  const UBlockRegistry &registry = world->GetBlockRegistry();
  const auto defs = core->GetBlockDefinitionStorage();
  for (const char *fluidName : {"water", "lava", "fire", "sand"})
  {
    const BlockId id = registry.GetIdByTypeName(fluidName);
    const BlockDefinition *def = defs ? defs->GetById(id) : nullptr;
    const BlockDefinition *defByName =
        defs ? defs->GetByName(fluidName) : nullptr;
    std::cout << "validate-load: block '" << fluidName << "' id=" << id;
    if (def)
    {
      std::cout << " occupancy=" << def->Physics.Movement.Occupancy
                << " style=" << static_cast<int>(def->Render.Style)
                << " transparent=" << def->Render.Transparent;
    }
    else
    {
      std::cout << " (no definition by id)";
    }
    if (defByName && defByName != def)
    {
      std::cerr << "validate-load: id/name mismatch for '" << fluidName
                << "' id=" << id << " nameId=" << defByName->Id << std::endl;
      glfwDestroyWindow(ctx);
      glfwTerminate();
      return 1;
    }
    if (fluidName == std::string("water") || fluidName == std::string("lava") ||
        fluidName == std::string("fire"))
    {
      if (!def || def->Physics.Movement.Occupancy >= 1.0f)
      {
        std::cerr << "validate-load: fluid '" << fluidName
                  << "' has solid physics" << std::endl;
        glfwDestroyWindow(ctx);
        glfwTerminate();
        return 1;
      }
    }
    std::cout << " blocksMovement=" << (registry.BlocksMovement(id) ? "1" : "0")
              << std::endl;
  }

  const size_t prefabCount = prefab_library->ListNames().size();
  constexpr size_t kMinPrefabs = 45;
  if (prefabCount < kMinPrefabs)
  {
    std::cerr << "validate-load: loaded only " << prefabCount
              << " prefab(s), expected >= " << kMinPrefabs << std::endl;
    glfwDestroyWindow(ctx);
    glfwTerminate();
    return 1;
  }

  std::cout << "validate-load: blocks=" << world->GetCachedBlockCount()
            << " prefabs=" << prefabCount << std::endl;

  glfwDestroyWindow(ctx);
  glfwTerminate();
  return 0;
}

} // namespace cutum
