#include "App/Utils.h"

#include "App/CreateWorldCli.h"
#include <chrono>
#include <cmath>
#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <memory>
#include <optional>
#include <sstream>
#include <thread>
#include <vector>

#include "Render/GlIncludes.h"
#include <GLFW/glfw3.h>

#include "Creatures/Core/Creature.h"
#include "Creatures/Definition/CreatureDefinitionStorage.h"
#include "Creatures/Definition/SkinDefinitionStorage.h"
#include "Creatures/Definition/CreatureDefinition.h"
#include "Creatures/Environment/CreatureEnvironment.h"
#include "Creatures/Movement/CreatureBodyProbe.h"
#include "Creatures/Movement/CreatureBodySeparation.h"
#include "Creatures/Movement/CreatureBodyStepUp.h"
#include "Creatures/Movement/CreatureMovementLog.h"
#include "Creatures/Movement/CreatureFootprint.h"
#include "Creatures/Movement/CreaturePlacement.h"
#include "Creatures/Visual/CreatureAppearance.h"
#include "Creatures/Visual/CreatureTextureStorage.h"
#include "Gui/Preview/CreaturePreviewRenderer.h"
#include "Render/Engine/ShaderManager.h"
#include "App/Core.h"
#include "App/Platform/GameDataRoot.h"
#include "App/Platform/IPlatformPaths.h"
#include "App/Platform/Log.h"
#include "Blocks/BlockDefinition.h"
#include "Blocks/BlockDefinitionStorage.h"
#include "Render/Engine/ViewEngine.h"
#include "Render/Textures/TextureBase.h"
#include "Render/Textures/TextureCube.h"
#include "World/Chunks/Chunk.h"
#include "World/Chunks/ChunkBuffer.h"
#include "World/Core/World.h"
#include "World/IO/BinaryChunkSerializer.h"
#include "World/IO/JsonChunkSerializer.h"
#include "World/Math/BlockTypes.h"
#include "World/Math/CollisionVolume.h"
#include "World/Math/GridMath.h"
#include "World/Objects/ObjectLibrary.h"
#include "WorldGen/Core/WorldGenRefs.h"
#include "WorldGen/Features/ObjectFeatureConfig.h"

namespace cutum
{

namespace
{

bool TierABlocksResolved(UBlockRegistry &registry)
{
  static const char *kTierA[] = {
      "bedrock", "stone", "dirt", "grass",    "sand",       "sandstone",
      "gravel",  "snow",  "clay", "ice",      "hellrock",   "water",
      "lava",    "fire",  "wood", "tree_log", "tree_leaves"};
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

void BuildStepUpSmokeScenario(UWorld &world)
{
  const glm::vec3 spawn = world.GetSpawnPoint();
  const int cx = WorldCoordToBlockIndex(spawn.x);
  const int cz = WorldCoordToBlockIndex(spawn.z);
  const std::optional<int> topY = world.FindHighestSolidY(cx, cz);
  const int floorY =
      topY ? *topY : static_cast<int>(std::floor(spawn.y)) - 1;

  UBlockWorld &blockWorld = world.GetBlockWorld();
  const BlockId stone = world.GetBlockRegistry().GetIdByTypeName("stone");
  if (stone == BLOCK_AIR)
  {
    return;
  }

  for (int dx = -2; dx <= 5; ++dx)
  {
    for (int dz = -2; dz <= 2; ++dz)
    {
      for (int dy = 0; dy <= 8; ++dy)
      {
        blockWorld.SetBlock(glm::ivec3(cx + dx, floorY + dy, cz + dz),
                            BLOCK_AIR);
      }
    }
  }

  for (int dx = -2; dx <= 0; ++dx)
  {
    for (int dz = -2; dz <= 2; ++dz)
    {
      blockWorld.SetBlock(glm::ivec3(cx + dx, floorY, cz + dz), stone);
    }
  }
  for (int dz = -2; dz <= 2; ++dz)
  {
    blockWorld.SetBlock(glm::ivec3(cx, floorY + 1, cz + dz), stone);
  }
  for (int dx = 1; dx <= 4; ++dx)
  {
    for (int dz = -2; dz <= 2; ++dz)
    {
      blockWorld.SetBlock(glm::ivec3(cx + dx, floorY + 1, cz + dz), stone);
    }
  }
}

std::shared_ptr<UCore> MakeSmokeCore(std::shared_ptr<UWorld> &outWorld)
{
  auto texture_base_instance = std::make_shared<UTextureBaseStorage>();
  auto texture_cube_instance =
      std::make_shared<UTextureCubeStorage>(texture_base_instance);
  auto object_library = std::make_shared<UObjectLibrary>();
  auto view_engine = std::make_shared<UViewEngine>();
  outWorld = std::make_shared<UWorld>(texture_cube_instance, view_engine);
  return std::make_shared<UCore>(texture_base_instance, texture_cube_instance,
                                 object_library, outWorld, nullptr,
                                 view_engine);
}

int RunSmokeHeadlessSetup(GLFWwindow *&ctx)
{
  if (!glfwInit())
  {
    return 1;
  }
  glfwWindowHint(GLFW_VISIBLE, GLFW_FALSE);
  glfwWindowHint(GLFW_CONTEXT_VERSION_MAJOR, 3);
  glfwWindowHint(GLFW_CONTEXT_VERSION_MINOR, 3);
  glfwWindowHint(GLFW_OPENGL_PROFILE, GLFW_OPENGL_CORE_PROFILE);
  ctx = glfwCreateWindow(64, 64, "creature-smoke", nullptr, nullptr);
  if (!ctx)
  {
    glfwTerminate();
    return 1;
  }
  glfwMakeContextCurrent(ctx);
  if (glewInit() != GLEW_OK)
  {
    glfwDestroyWindow(ctx);
    glfwTerminate();
    return 1;
  }
  if (auto *paths = IPlatformPaths::TryGet())
  {
    std::error_code ec;
    std::filesystem::current_path(paths->AssetRoot(), ec);
  }
  else
  {
    const auto exeDir = GetExecutableDirectory();
    if (auto root = TryFindProjectRoot(exeDir))
    {
      std::error_code ec;
      std::filesystem::current_path(*root, ec);
    }
  }
  return 0;
}

void BuildSpawnSmokePlatform(UWorld &world)
{
  const glm::vec3 spawn = world.GetSpawnPoint();
  const int cx = WorldCoordToBlockIndex(spawn.x);
  const int cz = WorldCoordToBlockIndex(spawn.z);
  const std::optional<int> topY = world.FindHighestSolidY(cx, cz);
  const int floorY =
      topY ? *topY : static_cast<int>(std::floor(spawn.y)) - 1;

  UBlockWorld &blockWorld = world.GetBlockWorld();
  const BlockId stone = world.GetBlockRegistry().GetIdByTypeName("stone");
  if (stone == BLOCK_AIR)
  {
    return;
  }

  for (int dx = -4; dx <= 4; ++dx)
  {
    for (int dz = -4; dz <= 4; ++dz)
    {
      const glm::ivec3 floor(cx + dx, floorY, cz + dz);
      blockWorld.SetBlock(floor, stone);
      for (int dy = 1; dy <= 6; ++dy)
      {
        blockWorld.SetBlock(glm::ivec3(cx + dx, floorY + dy, cz + dz),
                            BLOCK_AIR);
      }
    }
  }
}

void ClearNonPlayerCreatures(UWorld &world)
{
  std::vector<CreatureId> existing;
  world.ForEachCreature(
      [&](const UCreature &creature)
      {
        if (!creature.IsPlayerCharacter())
        {
          existing.push_back(creature.GetId());
        }
      });
  for (const CreatureId id : existing)
  {
    world.RemoveCreature(id);
  }
}

glm::vec3 SmokeProbeAtBlockOffset(const UWorld &world, glm::ivec2 offset)
{
  const glm::vec3 spawn = world.GetSpawnPoint();
  const int bx = WorldCoordToBlockIndex(spawn.x) + offset.x;
  const int bz = WorldCoordToBlockIndex(spawn.z) + offset.y;
  const std::optional<int> topY = world.FindHighestSolidY(bx, bz);
  const int floorY =
      topY ? *topY : static_cast<int>(std::floor(spawn.y)) - 1;
  return glm::vec3(static_cast<float>(bx) + 0.5f,
                   static_cast<float>(floorY + 1),
                   static_cast<float>(bz) + 0.5f);
}

void RelocatePlayerForSmoke(UWorld &world)
{
  if (UCreature *player = world.GetPlayerCreature())
  {
    const glm::vec3 away = SmokeProbeAtBlockOffset(world, {0, -3});
    player->SetBodyOrigin(away);
  }
}

CreatureId SmokeSpawnCreature(UWorld &world, const std::string &speciesId,
                              const glm::vec3 &probe,
                              SpawnCollisionPolicy policy)
{
  const CreatureDefinition *def = world.GetCreatureDefinition(speciesId);
  if (!def)
  {
    return 0;
  }
  const glm::vec3 snapped = SnapSpawnProbeToHabitat(world, *def, probe);
  const glm::vec3 exactOrigin = AdjustSpawnBodyOrigin(world, *def, snapped);
  if (ClassifySpawnFailureAt(world, *def, exactOrigin, policy) ==
      SpawnFailureReason::None)
  {
    return world.SpawnCreature(speciesId, exactOrigin, "", policy);
  }
  const PlacementResult placement = FindSpawnOrigin(world, *def, probe, policy);
  if (placement.failure != SpawnFailureReason::None)
  {
    return 0;
  }
  return world.SpawnCreature(speciesId, placement.bodyOrigin, "", policy);
}

CreatureId FindCreatureIdByType(const UWorld &world, const std::string &typeId)
{
  CreatureId found = 0;
  world.ForEachCreature(
      [&](const UCreature &creature)
      {
        if (found == 0 && creature.GetTypeId() == typeId)
        {
          found = creature.GetId();
        }
      });
  return found;
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
  auto object_library = std::make_shared<UObjectLibrary>();
  auto view_engine = std::make_shared<UViewEngine>();
  auto world = std::make_shared<UWorld>(texture_cube_instance, view_engine);
  auto core = std::make_shared<UCore>(
      texture_base_instance, texture_cube_instance, object_library, world,
      nullptr, view_engine);

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

  const size_t objectCount = object_library->ListNames().size();
  constexpr size_t kMinObjects = 45;
  if (objectCount < kMinObjects)
  {
    std::cerr << "validate-load: loaded only " << objectCount
              << " object(s), expected >= " << kMinObjects << std::endl;
    glfwDestroyWindow(ctx);
    glfwTerminate();
    return 1;
  }

  std::cout << "validate-load: blocks=" << world->GetCachedBlockCount()
            << " objects=" << objectCount << std::endl;

  glfwDestroyWindow(ctx);
  glfwTerminate();
  return 0;
}

namespace
{

std::shared_ptr<UCore> MakeHeadlessCore()
{
  auto texture_base_instance = std::make_shared<UTextureBaseStorage>();
  auto texture_cube_instance =
      std::make_shared<UTextureCubeStorage>(texture_base_instance);
  auto block_definitions = std::make_shared<UBlockDefinitionStorage>();
  auto object_library = std::make_shared<UObjectLibrary>();
  auto view_engine = std::make_shared<UViewEngine>();
  auto world = std::make_shared<UWorld>(texture_cube_instance, view_engine);
  auto core = std::make_shared<UCore>(
      texture_base_instance, texture_cube_instance, object_library, world,
      nullptr, view_engine);

  block_definitions->Load("models/blocks");
  texture_cube_instance->SetBlockDefinitions(block_definitions);
  world->SetBlockDefinitionStorage(block_definitions);
  return core;
}

} // namespace

int RunCreateWorld(int argc, char **argv, int create_world_index)
{
  CreateWorldCliArgs cli_args;
  std::string parse_error;
  if (!ParseCreateWorldCliArgs(argc, argv, create_world_index, cli_args,
                               parse_error))
  {
    if (parse_error == "help")
    {
      std::cout
          << "Usage: Cubatarium --console --create-world [options]\n"
          << "  --name <world>       World folder name\n"
          << "  --seed <n>           World seed (default 42)\n"
          << "  --generator <id>     overworld|flat|hills|...\n"
          << "  --preset <id>        balanced|realistic|sparse_structures\n"
          << "  --radius-chunks <n>  Generation radius (default 4)\n"
          << "  --pack <id>          Primary resource pack\n"
          << "  --output <dir>       Worlds root (default worlds)\n"
          << "  --report-json <path> Write JSON report\n";
      return 0;
    }
    std::cerr << "create-world: " << parse_error << std::endl;
    return 1;
  }

  if (!glfwInit())
  {
    std::cerr << "create-world: glfwInit failed" << std::endl;
    return 1;
  }
  glfwWindowHint(GLFW_VISIBLE, GLFW_FALSE);
  glfwWindowHint(GLFW_CONTEXT_VERSION_MAJOR, 3);
  glfwWindowHint(GLFW_CONTEXT_VERSION_MINOR, 3);
  glfwWindowHint(GLFW_OPENGL_PROFILE, GLFW_OPENGL_CORE_PROFILE);
  GLFWwindow *ctx = glfwCreateWindow(64, 64, "create-world", nullptr, nullptr);
  if (!ctx)
  {
    std::cerr << "create-world: failed to create GL context" << std::endl;
    glfwTerminate();
    return 1;
  }
  glfwMakeContextCurrent(ctx);
  if (glewInit() != GLEW_OK)
  {
    std::cerr << "create-world: glewInit failed" << std::endl;
    glfwDestroyWindow(ctx);
    glfwTerminate();
    return 1;
  }

  if (!UWorldGenRefs::LoadFromFile("content/worldgen_refs.json"))
  {
    std::cerr << "create-world: worldgen_refs.json not loaded" << std::endl;
    glfwDestroyWindow(ctx);
    glfwTerminate();
    return 1;
  }
  if (!UObjectFeatureConfigStorage::LoadFromFile(
          "content/object_features.json"))
  {
    std::cerr
        << "create-world: object_features.json not loaded — vegetation disabled"
        << std::endl;
  }

  auto core = MakeHeadlessCore();
  core->LoadConfig("config.json");

  CreateWorldReport report;
  const bool ok = core->CreateWorldHeadless(cli_args, report);
  WriteCreateWorldReport(report, cli_args.ReportJsonPath);

  glfwDestroyWindow(ctx);
  glfwTerminate();
  return ok ? 0 : 1;
}

int RunCreatureAssetSmoke()
{
  if (auto *paths = IPlatformPaths::TryGet())
  {
    std::error_code ec;
    std::filesystem::current_path(paths->AssetRoot(), ec);
  }
  else
  {
    const auto exeDir = GetExecutableDirectory();
    if (auto root = TryFindProjectRoot(exeDir))
    {
      std::error_code ec;
      std::filesystem::current_path(*root, ec);
    }
  }

  if (!glfwInit())
  {
    std::cerr << "creature-asset-smoke: glfwInit failed" << std::endl;
    return 1;
  }
  glfwWindowHint(GLFW_VISIBLE, GLFW_FALSE);
  glfwWindowHint(GLFW_CONTEXT_VERSION_MAJOR, 3);
  glfwWindowHint(GLFW_CONTEXT_VERSION_MINOR, 3);
  glfwWindowHint(GLFW_OPENGL_PROFILE, GLFW_OPENGL_CORE_PROFILE);
  GLFWwindow *ctx = glfwCreateWindow(64, 64, "creature-asset-smoke", nullptr,
                                     nullptr);
  if (!ctx)
  {
    std::cerr << "creature-asset-smoke: failed to create GL context" << std::endl;
    glfwTerminate();
    return 1;
  }
  glfwMakeContextCurrent(ctx);
  if (glewInit() != GLEW_OK)
  {
    std::cerr << "creature-asset-smoke: glewInit failed" << std::endl;
    glfwDestroyWindow(ctx);
    glfwTerminate();
    return 1;
  }

  auto creatureDefinitions = std::make_shared<UCreatureDefinitionStorage>();
  creatureDefinitions->Load("models/creatures");
  auto skinDefinitions = std::make_shared<USkinDefinitionStorage>();
  skinDefinitions->Load("models/skins");
  auto creatureTextures = std::make_shared<UCreatureTextureStorage>();
  creatureTextures->LoadFromCreatureAndSkinRoots("models/creatures",
                                                 "models/skins");

  int failures = 0;
  std::ostringstream failureLog;
  for (const std::string &speciesId : creatureDefinitions->ListSpawnable())
  {
    const CreatureDefinition *def = creatureDefinitions->Get(speciesId);
    if (!def)
    {
      failureLog << "missing definition: " << speciesId << '\n';
      ++failures;
      continue;
    }
    const ResolvedCreatureAppearance appearance = ResolveCreatureAppearance(
        *creatureDefinitions, *skinDefinitions, speciesId, "");
    int missing = 0;
    for (const ResolvedCreaturePart &part : appearance.Parts)
    {
      if (creatureTextures->GetTexture(part.textureAssetKey) == 0)
      {
        if (missing == 0)
        {
          failureLog << speciesId << " layout=" << def->visual.textureLayout
                     << " missing:";
        }
        failureLog << ' ' << part.textureAssetKey;
        ++missing;
      }
    }
    if (missing > 0)
    {
      failureLog << '\n';
      ++failures;
    }
  }

  std::cout << "creature-asset-smoke: definitions="
            << creatureDefinitions->ListAllIds().size()
            << " spawnable=" << creatureDefinitions->ListSpawnable().size()
            << " failures=" << failures << std::endl;
  if (!failureLog.str().empty())
  {
    std::cerr << failureLog.str();
  }

  {
    std::ofstream report(GetExecutableDirectory() / "creature_smoke_report.txt");
    if (report)
    {
      report << "definitions=" << creatureDefinitions->ListAllIds().size()
             << " spawnable=" << creatureDefinitions->ListSpawnable().size()
             << " failures=" << failures << '\n';
      report << failureLog.str();
    }
  }

  glfwDestroyWindow(ctx);
  glfwTerminate();
  return failures > 0 ? 1 : 0;
}

int RunCreatureSpawnSmoke()
{
  CubatariumLogInfo("Smoke", "creature-spawn-smoke: start");
  if (auto *paths = IPlatformPaths::TryGet())
  {
    std::error_code ec;
    std::filesystem::current_path(paths->AssetRoot(), ec);
  }
  else
  {
    const auto exeDir = GetExecutableDirectory();
    if (auto root = TryFindProjectRoot(exeDir))
    {
      std::error_code ec;
      std::filesystem::current_path(*root, ec);
    }
  }

  if (!glfwInit())
  {
    return 1;
  }
  glfwWindowHint(GLFW_VISIBLE, GLFW_FALSE);
  glfwWindowHint(GLFW_CONTEXT_VERSION_MAJOR, 3);
  glfwWindowHint(GLFW_CONTEXT_VERSION_MINOR, 3);
  glfwWindowHint(GLFW_OPENGL_PROFILE, GLFW_OPENGL_CORE_PROFILE);
  GLFWwindow *ctx = glfwCreateWindow(64, 64, "creature-spawn-smoke", nullptr,
                                     nullptr);
  if (!ctx)
  {
    glfwTerminate();
    return 1;
  }
  glfwMakeContextCurrent(ctx);
  if (glewInit() != GLEW_OK)
  {
    glfwDestroyWindow(ctx);
    glfwTerminate();
    return 1;
  }

  auto texture_base_instance = std::make_shared<UTextureBaseStorage>();
  auto texture_cube_instance =
      std::make_shared<UTextureCubeStorage>(texture_base_instance);
  auto object_library = std::make_shared<UObjectLibrary>();
  auto view_engine = std::make_shared<UViewEngine>();
  auto world = std::make_shared<UWorld>(texture_cube_instance, view_engine);
  auto core = std::make_shared<UCore>(
      texture_base_instance, texture_cube_instance, object_library, world,
      nullptr, view_engine);

  try
  {
    core->LoadConfig("config.json");
  }
  catch (const std::exception &e)
  {
    std::ofstream report(GetExecutableDirectory() / "creature_spawn_smoke.txt");
    report << "LoadConfig failed: " << e.what() << '\n';
    glfwDestroyWindow(ctx);
    glfwTerminate();
    return 1;
  }

  std::ostringstream out;
  const auto defs = world->GetCreatureDefinitionStorage();
  out << "spawnable_defs="
      << (defs ? defs->ListSpawnable().size() : 0) << '\n';

  const char *kSpecies[] = {"wolf", "pig", "sheep", "cow", "chicken"};
  int failures = 0;
  int requiredFailures = 0;
  for (const char *speciesId : kSpecies)
  {
    const CreatureDefinition *def = world->GetCreatureDefinition(speciesId);
    out << speciesId;
    if (!def)
    {
      out << " def=MISSING\n";
      ++failures;
      continue;
    }
    out << " layout=" << def->visual.textureLayout
        << " spawnable=" << (def->catalog.spawnable ? "yes" : "no") << '\n';
  }

  bool entered = false;
  try
  {
    core->EnterGame();
    entered = true;
  }
  catch (const std::exception &e)
  {
    out << "EnterGame skipped: " << e.what() << '\n';
  }

  if (entered)
  {
    BuildSpawnSmokePlatform(*world);
    RelocatePlayerForSmoke(*world);
    std::vector<CreatureId> existing;
    world->ForEachCreature(
        [&](const UCreature &creature)
        {
          if (!creature.IsPlayerCharacter())
          {
            existing.push_back(creature.GetId());
          }
        });
    for (const CreatureId id : existing)
    {
      world->RemoveCreature(id);
    }

    out << "creatures_in_world=";
    int creatureCount = 0;
    world->ForEachCreature([&](const UCreature &) { ++creatureCount; });
    out << creatureCount << '\n';

    for (const char *speciesId : kSpecies)
    {
      std::vector<CreatureId> stray;
      world->ForEachCreature(
          [&](const UCreature &creature)
          {
            if (!creature.IsPlayerCharacter())
            {
              stray.push_back(creature.GetId());
            }
          });
      for (const CreatureId rid : stray)
      {
        world->RemoveCreature(rid);
      }

      const CreatureDefinition *def = world->GetCreatureDefinition(speciesId);
      if (!def)
      {
        continue;
      }

      const bool canView = world->CanSpawnCreatureByView(speciesId);
      const std::string hint = world->GetCreatureSpawnBlockedHint(speciesId);
      const bool byView = world->SpawnCreatureByView(speciesId);
      const CreatureId id = byView ? FindCreatureIdByType(*world, speciesId) : 0;

      out << "spawn " << speciesId << " canView=" << (canView ? "yes" : "no")
          << " byView=" << (byView ? "yes" : "no") << " id=" << id;
      if (!hint.empty())
      {
        out << " hint=" << hint;
      }

      const bool requiredSpecies =
          std::strcmp(speciesId, "wolf") == 0 ||
          std::strcmp(speciesId, "cow") == 0 ||
          std::strcmp(speciesId, "sheep") == 0 ||
          std::strcmp(speciesId, "chicken") == 0;

      if (id == 0)
      {
        if (requiredSpecies)
        {
          ++failures;
        }
      }
      else
      {
        const UCreature *spawned = world->GetCreature(id);
        const glm::vec3 initial =
            spawned ? spawned->GetBodyOrigin() : glm::vec3(0.0f);
        constexpr int kPostSpawnTicks = 300;
        constexpr float kDt = 1.0f / 60.0f;
        for (int t = 0; t < kPostSpawnTicks; ++t)
        {
          world->TickCreatureBehaviors(kDt);
        }
        const UCreature *after = world->GetCreature(id);
        const float moved =
            after ? glm::length(after->GetBodyOrigin() - initial) : 0.0f;
        out << " post_move=" << moved;
        if (moved <= 0.3f && requiredSpecies)
        {
          ++failures;
        }
        world->RemoveCreature(id);
      }
      out << '\n';
    }

    const bool cowSpawned = world->SpawnCreatureByView("cow");
    const CreatureId cowId = cowSpawned ? FindCreatureIdByType(*world, "cow") : 0;
    const bool sheepAfterCow = world->SpawnCreatureByView("sheep");
    const CreatureId sheepId =
        sheepAfterCow ? FindCreatureIdByType(*world, "sheep") : 0;
    out << "spawn sheep_after_cow=" << (sheepAfterCow ? "yes" : "no")
        << " cow_id=" << cowId << " sheep_id=" << sheepId;
    if (cowId != 0 && sheepId != 0)
    {
      const UCreature *cow = world->GetCreature(cowId);
      const UCreature *sheep = world->GetCreature(sheepId);
      if (cow && sheep)
      {
        const float sep = glm::length(cow->GetBodyOrigin() -
                                      sheep->GetBodyOrigin());
        out << " sep=" << sep;
        if (sep <= 0.5f)
        {
          ++failures;
        }
      }
    }
    out << '\n';
    if (!cowSpawned)
    {
      ++failures;
    }
    if (sheepId != 0)
    {
      world->RemoveCreature(sheepId);
    }
    if (cowId != 0)
    {
      world->RemoveCreature(cowId);
    }
    ClearNonPlayerCreatures(*world);

    struct DirectSpawnProbe
    {
      const char *speciesId;
      glm::ivec2 offset;
    };
    const DirectSpawnProbe directProbes[] = {{"sheep", {-2, 2}},
                                             {"chicken", {0, 2}},
                                             {"cow", {2, 2}}};
    for (const DirectSpawnProbe &probe : directProbes)
    {
      const glm::vec3 body = SmokeProbeAtBlockOffset(*world, probe.offset);
      const CreatureId id =
          SmokeSpawnCreature(*world, probe.speciesId, body,
                             SpawnCollisionPolicy::Creative);
      out << "direct_spawn " << probe.speciesId << " id=" << id;
      if (id == 0)
      {
        out << " FAIL\n";
        ++failures;
        continue;
      }
      const UCreature *spawned = world->GetCreature(id);
      const glm::vec3 initial =
          spawned ? spawned->GetBodyOrigin() : glm::vec3(0.0f);
      constexpr int kPostSpawnTicks = 300;
      constexpr float kDt = 1.0f / 60.0f;
      for (int t = 0; t < kPostSpawnTicks; ++t)
      {
        world->TickCreatureBehaviors(kDt);
      }
      const UCreature *after = world->GetCreature(id);
      const float moved =
          after ? glm::length(after->GetBodyOrigin() - initial) : 0.0f;
      out << " post_move=" << moved;
      const bool requireMove = true;
      if (moved <= 0.3f && requireMove)
      {
        ++failures;
      }
      out << '\n';
      world->RemoveCreature(id);
    }
  }

  {
    std::ofstream report(GetExecutableDirectory() / "creature_spawn_smoke.txt");
    report << out.str();
    report << "failures=" << failures << '\n';
  }

  CubatariumLogInfo("Smoke",
                    std::string("creature-spawn-smoke: done failures=") +
                        std::to_string(failures));

  glfwDestroyWindow(ctx);
  glfwTerminate();
  return failures > 0 ? 1 : 0;
}

int RunCreatureMovementSmoke()
{
  CubatariumLogInfo("Smoke", "creature-movement-smoke: start");
  if (auto *paths = IPlatformPaths::TryGet())
  {
    std::error_code ec;
    std::filesystem::current_path(paths->AssetRoot(), ec);
  }
  else
  {
    const auto exeDir = GetExecutableDirectory();
    if (auto root = TryFindProjectRoot(exeDir))
    {
      std::error_code ec;
      std::filesystem::current_path(*root, ec);
    }
  }

  if (!glfwInit())
  {
    return 1;
  }
  glfwWindowHint(GLFW_VISIBLE, GLFW_FALSE);
  glfwWindowHint(GLFW_CONTEXT_VERSION_MAJOR, 3);
  glfwWindowHint(GLFW_CONTEXT_VERSION_MINOR, 3);
  glfwWindowHint(GLFW_OPENGL_PROFILE, GLFW_OPENGL_CORE_PROFILE);
  GLFWwindow *ctx = glfwCreateWindow(64, 64, "creature-movement-smoke", nullptr,
                                     nullptr);
  if (!ctx)
  {
    glfwTerminate();
    return 1;
  }
  glfwMakeContextCurrent(ctx);
  if (glewInit() != GLEW_OK)
  {
    glfwDestroyWindow(ctx);
    glfwTerminate();
    return 1;
  }

  auto texture_base_instance = std::make_shared<UTextureBaseStorage>();
  auto texture_cube_instance =
      std::make_shared<UTextureCubeStorage>(texture_base_instance);
  auto object_library = std::make_shared<UObjectLibrary>();
  auto view_engine = std::make_shared<UViewEngine>();
  auto world = std::make_shared<UWorld>(texture_cube_instance, view_engine);
  auto core = std::make_shared<UCore>(
      texture_base_instance, texture_cube_instance, object_library, world,
      nullptr, view_engine);

  int failures = 0;
  std::ostringstream out;
  try
  {
    core->LoadConfig("config.json");
    core->EnterGame();
  }
  catch (const std::exception &e)
  {
    std::ofstream report(GetExecutableDirectory() /
                         "creature_movement_smoke.txt");
    report << "setup failed: " << e.what() << '\n';
    glfwDestroyWindow(ctx);
    glfwTerminate();
    return 1;
  }

  BuildSpawnSmokePlatform(*world);
  ClearNonPlayerCreatures(*world);
  RelocatePlayerForSmoke(*world);

  struct MovementProbe
  {
    const char *speciesId;
    glm::ivec2 offset;
  };
  const MovementProbe probes[] = {{"sheep", {-2, 2}},
                                  {"chicken", {0, 2}},
                                  {"cow", {2, 2}},
                                  {"wolf", {-2, 2}}};

  constexpr int kTicks = 600;
  constexpr float kDt = 1.0f / 60.0f;
  for (const MovementProbe &probe : probes)
  {
    ClearNonPlayerCreatures(*world);
    RelocatePlayerForSmoke(*world);
    const glm::vec3 body = SmokeProbeAtBlockOffset(*world, probe.offset);
    const CreatureId id =
        SmokeSpawnCreature(*world, probe.speciesId, body,
                           SpawnCollisionPolicy::Creative);
    if (id == 0)
    {
      out << "move " << probe.speciesId << " spawn_failed\n";
      ++failures;
      continue;
    }
    const UCreature *spawned = world->GetCreature(id);
    const glm::vec3 initial =
        spawned ? spawned->GetBodyOrigin() : glm::vec3(0.0f);
    for (int i = 0; i < kTicks; ++i)
    {
      UCreature *creature = world->GetCreature(id);
      if (!creature)
      {
        break;
      }
      CreatureIntent intent;
      intent.moveDirWorld = glm::vec3(1.0f, 0.0f, 0.0f);
      intent.moveSpeed = 2.0f;
      intent.clearOnApply = false;
      creature->SetIntent(intent);
      creature->ExecuteIntent(*world, kDt);
    }
    const UCreature *after = world->GetCreature(id);
    const float moved =
        after ? glm::length(after->GetBodyOrigin() - initial) : 0.0f;
    out << "move " << probe.speciesId << " dist=" << moved << '\n';
    const bool requiredMover = true;
    if (moved <= 0.5f)
    {
      ++failures;
    }
    world->RemoveCreature(id);
  }

  {
    std::ofstream report(GetExecutableDirectory() /
                         "creature_movement_smoke.txt");
    report << out.str();
    report << "failures=" << failures << '\n';
  }

  CubatariumLogInfo("Smoke",
                    std::string("creature-movement-smoke: done failures=") +
                        std::to_string(failures));

  glfwDestroyWindow(ctx);
  glfwTerminate();
  return failures > 0 ? 1 : 0;
}

int RunCreatureWanderSmoke()
{
  CubatariumLogInfo("Smoke", "creature-wander-smoke: start");
  if (auto *paths = IPlatformPaths::TryGet())
  {
    std::error_code ec;
    std::filesystem::current_path(paths->AssetRoot(), ec);
  }
  else
  {
    const auto exeDir = GetExecutableDirectory();
    if (auto root = TryFindProjectRoot(exeDir))
    {
      std::error_code ec;
      std::filesystem::current_path(*root, ec);
    }
  }

  if (!glfwInit())
  {
    return 1;
  }
  glfwWindowHint(GLFW_VISIBLE, GLFW_FALSE);
  glfwWindowHint(GLFW_CONTEXT_VERSION_MAJOR, 3);
  glfwWindowHint(GLFW_CONTEXT_VERSION_MINOR, 3);
  glfwWindowHint(GLFW_OPENGL_PROFILE, GLFW_OPENGL_CORE_PROFILE);
  GLFWwindow *ctx = glfwCreateWindow(64, 64, "creature-wander-smoke", nullptr,
                                     nullptr);
  if (!ctx)
  {
    glfwTerminate();
    return 1;
  }
  glfwMakeContextCurrent(ctx);
  if (glewInit() != GLEW_OK)
  {
    glfwDestroyWindow(ctx);
    glfwTerminate();
    return 1;
  }

  auto texture_base_instance = std::make_shared<UTextureBaseStorage>();
  auto texture_cube_instance =
      std::make_shared<UTextureCubeStorage>(texture_base_instance);
  auto object_library = std::make_shared<UObjectLibrary>();
  auto view_engine = std::make_shared<UViewEngine>();
  auto world = std::make_shared<UWorld>(texture_cube_instance, view_engine);
  auto core = std::make_shared<UCore>(
      texture_base_instance, texture_cube_instance, object_library, world,
      nullptr, view_engine);

  int failures = 0;
  std::ostringstream out;
  try
  {
    core->LoadConfig("config.json");
    core->EnterGame();
  }
  catch (const std::exception &e)
  {
    std::ofstream report(GetExecutableDirectory() / "creature_wander_smoke.txt");
    report << "setup failed: " << e.what() << '\n';
    glfwDestroyWindow(ctx);
    glfwTerminate();
    return 1;
  }

  std::srand(42);
  BuildSpawnSmokePlatform(*world);
  ClearNonPlayerCreatures(*world);
  RelocatePlayerForSmoke(*world);

  struct WanderProbe
  {
    const char *speciesId;
    glm::ivec2 offset;
  };
  const WanderProbe probes[] = {{"sheep", {-2, 2}},
                                {"chicken", {0, 2}},
                                {"cow", {2, 2}}};

  constexpr int kTicks = 600;
  constexpr float kDt = 1.0f / 60.0f;
  for (const WanderProbe &probe : probes)
  {
    ClearNonPlayerCreatures(*world);
    RelocatePlayerForSmoke(*world);
    const glm::vec3 body = SmokeProbeAtBlockOffset(*world, probe.offset);
    const CreatureId id =
        SmokeSpawnCreature(*world, probe.speciesId, body,
                           SpawnCollisionPolicy::Creative);
    if (id == 0)
    {
      const CreatureDefinition *def = world->GetCreatureDefinition(probe.speciesId);
      SpawnFailureReason reason = SpawnFailureReason::Blocks;
      if (def)
      {
        const PlacementResult placement =
            FindSpawnOrigin(*world, *def, body, SpawnCollisionPolicy::Creative);
        reason = placement.failure;
      }
      out << "wander " << probe.speciesId
          << " spawn_failed reason=" << SpawnFailureReasonLabel(reason) << '\n';
      ++failures;
      continue;
    }
    if (UCreature *creature = world->GetCreature(id))
    {
      SeparateFromBlocksAndCreatures(*world, *creature, 8);
    }
    const UCreature *spawned = world->GetCreature(id);
    const glm::vec3 initial =
        spawned ? spawned->GetBodyOrigin() : glm::vec3(0.0f);
    const float groundY = initial.y;
    for (int i = 0; i < kTicks; ++i)
    {
      world->TickCreatureBehaviors(kDt);
    }
    const UCreature *after = world->GetCreature(id);
    if (!after)
    {
      out << "wander " << probe.speciesId << " despawned\n";
      ++failures;
      world->RemoveCreature(id);
      continue;
    }
    const glm::vec3 delta = after->GetBodyOrigin() - initial;
    const float moved = glm::length(delta);
    const float yDrift = std::abs(after->GetBodyOrigin().y - groundY);
    out << "wander " << probe.speciesId << " dist=" << moved
        << " y_drift=" << yDrift << '\n';
    const bool requiredMover = true;
    if (moved < 1.0f || yDrift > 1.5f)
    {
      ++failures;
    }
    world->RemoveCreature(id);
  }

  {
    std::ofstream report(GetExecutableDirectory() / "creature_wander_smoke.txt");
    report << out.str();
    report << "failures=" << failures << '\n';
  }

  CubatariumLogInfo("Smoke",
                    std::string("creature-wander-smoke: done failures=") +
                        std::to_string(failures));

  glfwDestroyWindow(ctx);
  glfwTerminate();
  return failures > 0 ? 1 : 0;
}

int RunCreatureStackSmoke()
{
  CubatariumLogInfo("Smoke", "creature-stack-smoke: start");
  if (auto *paths = IPlatformPaths::TryGet())
  {
    std::error_code ec;
    std::filesystem::current_path(paths->AssetRoot(), ec);
  }
  else
  {
    const auto exeDir = GetExecutableDirectory();
    if (auto root = TryFindProjectRoot(exeDir))
    {
      std::error_code ec;
      std::filesystem::current_path(*root, ec);
    }
  }

  if (!glfwInit())
  {
    return 1;
  }
  glfwWindowHint(GLFW_VISIBLE, GLFW_FALSE);
  glfwWindowHint(GLFW_CONTEXT_VERSION_MAJOR, 3);
  glfwWindowHint(GLFW_CONTEXT_VERSION_MINOR, 3);
  glfwWindowHint(GLFW_OPENGL_PROFILE, GLFW_OPENGL_CORE_PROFILE);
  GLFWwindow *ctx = glfwCreateWindow(64, 64, "creature-stack-smoke", nullptr,
                                     nullptr);
  if (!ctx)
  {
    glfwTerminate();
    return 1;
  }
  glfwMakeContextCurrent(ctx);
  if (glewInit() != GLEW_OK)
  {
    glfwDestroyWindow(ctx);
    glfwTerminate();
    return 1;
  }

  auto texture_base_instance = std::make_shared<UTextureBaseStorage>();
  auto texture_cube_instance =
      std::make_shared<UTextureCubeStorage>(texture_base_instance);
  auto object_library = std::make_shared<UObjectLibrary>();
  auto view_engine = std::make_shared<UViewEngine>();
  auto world = std::make_shared<UWorld>(texture_cube_instance, view_engine);
  auto core = std::make_shared<UCore>(
      texture_base_instance, texture_cube_instance, object_library, world,
      nullptr, view_engine);

  int failures = 0;
  std::ostringstream out;
  try
  {
    core->LoadConfig("config.json");
    core->EnterGame();
  }
  catch (const std::exception &e)
  {
    std::ofstream report(GetExecutableDirectory() / "creature_stack_smoke.txt");
    report << "setup failed: " << e.what() << '\n';
    glfwDestroyWindow(ctx);
    glfwTerminate();
    return 1;
  }

  BuildSpawnSmokePlatform(*world);
  ClearNonPlayerCreatures(*world);
  RelocatePlayerForSmoke(*world);

  const glm::vec3 body = SmokeProbeAtBlockOffset(*world, {0, -2});
  const float groundY = body.y;

  std::vector<CreatureId> stacked;
  for (int i = 0; i < 3; ++i)
  {
    const CreatureId id =
        SmokeSpawnCreature(*world, "sheep", body, SpawnCollisionPolicy::Creative);
    if (id == 0)
    {
      out << "stack spawn " << i << " failed\n";
      ++failures;
      continue;
    }
    stacked.push_back(id);
    if (UCreature *creature = world->GetCreature(id))
    {
      SeparateFromBlocksAndCreatures(*world, *creature, 8);
    }
  }
  for (int pass = 0; pass < 5; ++pass)
  {
    for (const CreatureId id : stacked)
    {
      if (UCreature *creature = world->GetCreature(id))
      {
        SeparateFromBlocksAndCreatures(*world, *creature, 4);
      }
    }
  }
  out << "stack spawned=" << stacked.size() << '\n';

  constexpr int kTicks = 300;
  constexpr float kDt = 1.0f / 60.0f;
  for (int i = 0; i < kTicks; ++i)
  {
    world->TickCreatureBehaviors(kDt);
  }

  float maxPairDist = 0.0f;
  float maxYDrift = 0.0f;
  for (size_t i = 0; i < stacked.size(); ++i)
  {
    const UCreature *a = world->GetCreature(stacked[i]);
    if (!a)
    {
      continue;
    }
    maxYDrift = std::max(maxYDrift, std::abs(a->GetBodyOrigin().y - groundY));
    for (size_t j = i + 1; j < stacked.size(); ++j)
    {
      const UCreature *b = world->GetCreature(stacked[j]);
      if (!b)
      {
        continue;
      }
      maxPairDist =
          std::max(maxPairDist,
                   glm::length(a->GetBodyOrigin() - b->GetBodyOrigin()));
    }
  }
  out << "stack max_pair_dist=" << maxPairDist << " max_y_drift=" << maxYDrift
      << '\n';
  if (maxPairDist <= 0.8f || maxYDrift > 0.5f)
  {
    ++failures;
  }

  {
    std::ofstream report(GetExecutableDirectory() / "creature_stack_smoke.txt");
    report << out.str();
    report << "failures=" << failures << '\n';
  }

  CubatariumLogInfo("Smoke",
                    std::string("creature-stack-smoke: done failures=") +
                        std::to_string(failures));

  glfwDestroyWindow(ctx);
  glfwTerminate();
  return failures > 0 ? 1 : 0;
}

int RunBenchChunkIo()
{
  auto texture_base_instance = std::make_shared<UTextureBaseStorage>();
  auto texture_cube_instance =
      std::make_shared<UTextureCubeStorage>(texture_base_instance);
  UBlockRegistry registry(texture_cube_instance, nullptr);

  UBinaryChunkSerializer binary;
  UJsonChunkSerializer json;

  UChunk chunk(glm::ivec3(0, 0, 0));
  constexpr BlockId kStone = 7;
  constexpr BlockId kDirt = 8;
  for (int z = 0; z < CHUNK_SIZE; ++z)
  {
    for (int y = 0; y < CHUNK_SIZE; ++y)
    {
      for (int x = 0; x < CHUNK_SIZE; ++x)
      {
        BlockId id = BLOCK_AIR;
        if (y < 4)
        {
          id = kStone;
        }
        else if (y < 8)
        {
          id = kDirt;
        }
        chunk.SetBlockLocal(glm::ivec3(x, y, z), id);
      }
    }
  }

  const auto bench = [](auto fn)
  {
    const auto t0 = std::chrono::high_resolution_clock::now();
    fn();
    const auto t1 = std::chrono::high_resolution_clock::now();
    return std::chrono::duration<double, std::micro>(t1 - t0).count();
  };

  SerializedChunk binaryBlob;
  const double binarySaveUs = bench(
      [&]()
      { binaryBlob = binary.Serialize(glm::ivec3(0, 0, 0), chunk, registry); });
  UChunkBuffer binaryBuf;
  const double binaryLoadUs = bench(
      [&]()
      {
        binaryBuf =
            binary.Deserialize(binaryBlob.bytes, glm::ivec3(0, 0, 0), registry);
      });

  SerializedChunk jsonBlob;
  const double jsonSaveUs = bench(
      [&]()
      { jsonBlob = json.Serialize(glm::ivec3(0, 0, 0), chunk, registry); });
  UChunkBuffer jsonBuf;
  const double jsonLoadUs = bench(
      [&]()
      {
        jsonBuf =
            json.Deserialize(jsonBlob.bytes, glm::ivec3(0, 0, 0), registry);
      });

  const bool ok = !binaryBuf.IsEmpty() && !jsonBuf.IsEmpty();
  std::cout << "BENCH_IO ok=" << (ok ? 1 : 0)
            << " binary_bytes=" << binaryBlob.bytes.size()
            << " json_bytes=" << jsonBlob.bytes.size()
            << " binary_save_us=" << binarySaveUs
            << " binary_load_us=" << binaryLoadUs
            << " json_save_us=" << jsonSaveUs << " json_load_us=" << jsonLoadUs
            << std::endl;
  return ok ? 0 : 1;
}

int RunCreatureMovementDiagnose(const char *speciesId)
{
  CubatariumLogInfo("Smoke",
                    std::string("creature-movement-diagnose: ") + speciesId);
  GLFWwindow *ctx = nullptr;
  if (RunSmokeHeadlessSetup(ctx) != 0)
  {
    return 1;
  }

  std::shared_ptr<UWorld> world;
  auto core = MakeSmokeCore(world);
  int failures = 0;
  std::ostringstream out;
  try
  {
    core->LoadConfig("config.json");
    core->EnterGame();
  }
  catch (const std::exception &e)
  {
    std::ofstream report(GetExecutableDirectory() /
                         "creature_movement_diagnose.txt");
    report << "setup failed: " << e.what() << '\n';
    glfwDestroyWindow(ctx);
    glfwTerminate();
    return 1;
  }

  BuildSpawnSmokePlatform(*world);
  BuildStepUpSmokeScenario(*world);
  ClearNonPlayerCreatures(*world);
  RelocatePlayerForSmoke(*world);

  const CreatureDefinition *def = world->GetCreatureDefinition(speciesId);
  if (!def)
  {
    out << "unknown species " << speciesId << '\n';
    failures = 1;
  }
  else
  {
    const glm::vec3 body = SmokeProbeAtBlockOffset(*world, {-1, 0});
    const CreatureId id =
        SmokeSpawnCreature(*world, speciesId, body, SpawnCollisionPolicy::Creative);
    out << "species=" << speciesId << " id=" << id << '\n';
    if (id == 0)
    {
      failures = 1;
    }
    else if (const UCreature *creature = world->GetCreature(id))
    {
      const glm::vec3 origin = creature->GetBodyOrigin();
      const glm::vec3 size = def->bounds.restSizeBlocks;
      const CollisionVolume vol = CollisionVolumeFromBody(origin, size);
      const FootprintSample footprint =
          SampleCreatureFootprint(*world, origin, size);
      out << "bodyOrigin=(" << origin.x << ',' << origin.y << ',' << origin.z
          << ") volCenter=(" << vol.center.x << ',' << vol.center.y << ','
          << vol.center.z << ") half=(" << vol.halfExtents.x << ','
          << vol.halfExtents.y << ',' << vol.halfExtents.z << ")\n";
      out << "groundSupport=" << (footprint.hasGroundSupport ? 1 : 0)
          << " solidSamples=" << footprint.solidSamples
          << " blockHit=" << (world->CheckBlockCollisionVolume(vol) ? 1 : 0)
          << '\n';

      int moveApplyOk = 0;
      const glm::vec3 dirs[] = {{1, 0, 0}, {-1, 0, 0}, {0, 0, 1}, {0, 0, -1}};
      for (HabitatContext ctxEnum :
           {HabitatContext::WanderTarget, HabitatContext::MoveApply})
      {
        out << HabitatContextLabel(ctxEnum) << ":\n";
        for (const glm::vec3 &dir : dirs)
        {
          const float dist = WanderProbeDistance(size);
          const BodyMoveResult probe = ProbeMove(
              *world, id, origin, dir * dist, def->habitat, size, ctxEnum);
          out << "  dir=(" << dir.x << ',' << dir.z
              << ") movedXZ=" << probe.movedXZ
              << " blockedGeometry=" << (probe.blockedGeometry ? 1 : 0)
              << " habitatOk=" << (probe.habitatOk ? 1 : 0) << '\n';
          if (ctxEnum == HabitatContext::MoveApply && probe.movedXZ > 0.12f)
          {
            ++moveApplyOk;
          }
        }
      }

      const CreatureStepUpProbe stepProbe = ProbeCreatureStepUp(
          *world, id, origin, glm::vec3(1.0f, 0.0f, 0.0f), size);
      out << "stepUp valid=" << (stepProbe.valid ? 1 : 0)
          << " landingY=" << stepProbe.landingBodyOrigin.y << '\n';
      if (moveApplyOk == 0 && !stepProbe.valid)
      {
        ++failures;
      }
      world->RemoveCreature(id);
    }
  }

  {
    std::ofstream report(GetExecutableDirectory() /
                         "creature_movement_diagnose.txt");
    report << out.str();
    report << "failures=" << failures << '\n';
  }

  glfwDestroyWindow(ctx);
  glfwTerminate();
  return failures > 0 ? 1 : 0;
}

int RunCreatureStepUpSmoke()
{
  CubatariumLogInfo("Smoke", "creature-step-up-smoke: start");
  GLFWwindow *ctx = nullptr;
  if (RunSmokeHeadlessSetup(ctx) != 0)
  {
    return 1;
  }

  std::shared_ptr<UWorld> world;
  auto core = MakeSmokeCore(world);
  int failures = 0;
  std::ostringstream out;
  try
  {
    core->LoadConfig("config.json");
    core->EnterGame();
  }
  catch (const std::exception &e)
  {
    std::ofstream report(GetExecutableDirectory() / "creature_step_up_smoke.txt");
    report << "setup failed: " << e.what() << '\n';
    glfwDestroyWindow(ctx);
    glfwTerminate();
    return 1;
  }

  BuildStepUpSmokeScenario(*world);
  ClearNonPlayerCreatures(*world);
  RelocatePlayerForSmoke(*world);

  const char *species[] = {"sheep", "chicken", "cow"};
  constexpr int kTicks = 300;
  constexpr float kDt = 1.0f / 60.0f;
  for (const char *speciesId : species)
  {
    ClearNonPlayerCreatures(*world);
    const glm::vec3 body = SmokeProbeAtBlockOffset(*world, {-1, 0});
    const CreatureId id =
        SmokeSpawnCreature(*world, speciesId, body, SpawnCollisionPolicy::Creative);
    if (id == 0)
    {
      out << "stepup " << speciesId << " spawn_failed\n";
      ++failures;
      continue;
    }
    UCreature *spawned = world->GetCreature(id);
    const glm::vec3 baseline =
        spawned ? spawned->GetBodyOrigin() : glm::vec3(0.0f);
    for (int i = 0; i < kTicks; ++i)
    {
      if (UCreature *creature = world->GetCreature(id))
      {
        CreatureIntent intent;
        intent.moveDirWorld = glm::vec3(1.0f, 0.0f, 0.0f);
        intent.moveSpeed = 2.5f;
        intent.clearOnApply = false;
        creature->SetIntent(intent);
        creature->ExecuteIntent(*world, kDt);
      }
    }
    const UCreature *after = world->GetCreature(id);
    const glm::vec3 delta =
        after ? after->GetBodyOrigin() - baseline : glm::vec3(0.0f);
    out << "stepup " << speciesId << " dx=" << delta.x << " dy=" << delta.y
        << '\n';
    if (delta.y < 0.8f || delta.x <= 0.5f)
    {
      ++failures;
    }
    world->RemoveCreature(id);
  }

  {
    std::ofstream report(GetExecutableDirectory() / "creature_step_up_smoke.txt");
    report << out.str();
    report << "failures=" << failures << '\n';
  }

  CubatariumLogInfo("Smoke",
                    std::string("creature-step-up-smoke: done failures=") +
                        std::to_string(failures));

  glfwDestroyWindow(ctx);
  glfwTerminate();
  return failures > 0 ? 1 : 0;
}

namespace
{

bool WriteBmpRgb(const std::filesystem::path &path, const std::vector<uint8_t> &rgba,
                 int width, int height)
{
  const int rowBytes = ((width * 3 + 3) / 4) * 4;
  std::vector<uint8_t> pixels(static_cast<size_t>(rowBytes * height), 0);
  for (int y = 0; y < height; ++y)
  {
    const int srcY = height - 1 - y;
    for (int x = 0; x < width; ++x)
    {
      const size_t src = static_cast<size_t>((srcY * width + x) * 4);
      const size_t dst = static_cast<size_t>(y * rowBytes + x * 3);
      pixels[dst + 0] = rgba[src + 0];
      pixels[dst + 1] = rgba[src + 1];
      pixels[dst + 2] = rgba[src + 2];
    }
  }
  const uint32_t fileSize =
      14u + 40u + static_cast<uint32_t>(pixels.size());
  std::ofstream out(path, std::ios::binary);
  if (!out)
  {
    return false;
  }
  auto write32 = [&](uint32_t v) {
    out.put(static_cast<char>(v & 0xff));
    out.put(static_cast<char>((v >> 8) & 0xff));
    out.put(static_cast<char>((v >> 16) & 0xff));
    out.put(static_cast<char>((v >> 24) & 0xff));
  };
  auto write16 = [&](uint16_t v) {
    out.put(static_cast<char>(v & 0xff));
    out.put(static_cast<char>((v >> 8) & 0xff));
  };
  out.put('B');
  out.put('M');
  write32(fileSize);
  write16(0);
  write16(0);
  write32(14u + 40u);
  write32(40u);
  write32(static_cast<uint32_t>(width));
  write32(static_cast<uint32_t>(height));
  write16(1);
  write16(24);
  write32(0);
  write32(static_cast<uint32_t>(pixels.size()));
  write32(0);
  write32(0);
  write32(0);
  write32(0);
  out.write(reinterpret_cast<const char *>(pixels.data()),
            static_cast<std::streamsize>(pixels.size()));
  return static_cast<bool>(out);
}

bool CapturePreviewBmp(GLuint colorTex, int size,
                       const std::filesystem::path &path)
{
  std::vector<uint8_t> rgba(static_cast<size_t>(size * size * 4));
  glBindTexture(GL_TEXTURE_2D, colorTex);
  glGetTexImage(GL_TEXTURE_2D, 0, GL_RGBA, GL_UNSIGNED_BYTE, rgba.data());
  glBindTexture(GL_TEXTURE_2D, 0);
  return WriteBmpRgb(path, rgba, size, size);
}

} // namespace

int RunCreaturePreviewSmoke(int argc, char **argv, int preview_index)
{
  if (auto *paths = IPlatformPaths::TryGet())
  {
    std::error_code ec;
    std::filesystem::current_path(paths->AssetRoot(), ec);
  }
  else
  {
    const auto exeDir = GetExecutableDirectory();
    if (auto root = TryFindProjectRoot(exeDir))
    {
      std::error_code ec;
      std::filesystem::current_path(*root, ec);
    }
  }

  std::vector<std::string> speciesFilter;
  bool tierA = false;
  std::filesystem::path outDir = GetExecutableDirectory() / "uv_preview";
  for (int i = preview_index + 1; i < argc; ++i)
  {
    if (std::strcmp(argv[i], "--species") == 0 && i + 1 < argc)
    {
      speciesFilter.push_back(argv[++i]);
    }
    else if (std::strcmp(argv[i], "--tier-a") == 0)
    {
      tierA = true;
    }
    else if (std::strcmp(argv[i], "--out-dir") == 0 && i + 1 < argc)
    {
      outDir = argv[++i];
    }
  }

  if (!glfwInit())
  {
    std::cerr << "creature-preview-smoke: glfwInit failed" << std::endl;
    return 1;
  }
  glfwWindowHint(GLFW_VISIBLE, GLFW_FALSE);
  glfwWindowHint(GLFW_CONTEXT_VERSION_MAJOR, 3);
  glfwWindowHint(GLFW_CONTEXT_VERSION_MINOR, 3);
  glfwWindowHint(GLFW_OPENGL_PROFILE, GLFW_OPENGL_CORE_PROFILE);
  GLFWwindow *ctx =
      glfwCreateWindow(256, 256, "creature-preview-smoke", nullptr, nullptr);
  if (!ctx)
  {
    std::cerr << "creature-preview-smoke: failed to create GL context" << std::endl;
    glfwTerminate();
    return 1;
  }
  glfwMakeContextCurrent(ctx);
  if (glewInit() != GLEW_OK)
  {
    std::cerr << "creature-preview-smoke: glewInit failed" << std::endl;
    glfwDestroyWindow(ctx);
    glfwTerminate();
    return 1;
  }

  auto creatureDefinitions = std::make_shared<UCreatureDefinitionStorage>();
  creatureDefinitions->Load("models/creatures");
  auto skinDefinitions = std::make_shared<USkinDefinitionStorage>();
  skinDefinitions->Load("models/skins");
  auto creatureTextures = std::make_shared<UCreatureTextureStorage>();
  creatureTextures->LoadFromCreatureAndSkinRoots("models/creatures",
                                                 "models/skins");
  auto shaderManager = std::make_shared<UShaderManager>();
  if (!shaderManager->Initialize())
  {
    std::cerr << "creature-preview-smoke: shader init failed" << std::endl;
    glfwDestroyWindow(ctx);
    glfwTerminate();
    return 1;
  }
  UCreaturePreviewRenderer preview(creatureDefinitions, skinDefinitions,
                                   creatureTextures, shaderManager);
  if (!preview.Initialize())
  {
    std::cerr << "creature-preview-smoke: preview init failed" << std::endl;
    glfwDestroyWindow(ctx);
    glfwTerminate();
    return 1;
  }

  std::vector<std::string> speciesList;
  if (!speciesFilter.empty())
  {
    speciesList = speciesFilter;
  }
  else if (tierA)
  {
    static const char *kTierA[] = {"sheep", "wolf", "pig", "cow", "chicken",
                                   "oerkki", "skeleton", "sand_monster"};
    speciesList.assign(std::begin(kTierA), std::end(kTierA));
  }
  else
  {
    speciesList = creatureDefinitions->ListSpawnable();
  }

  constexpr int kSize = 256;
  constexpr float kPitch = 15.0f;
  const float yaws[] = {0.0f, 90.0f, 180.0f, 270.0f};
  int failures = 0;
  for (const std::string &speciesId : speciesList)
  {
    if (speciesId == "human")
    {
      continue;
    }
    const auto speciesDir = outDir / speciesId;
    std::error_code ec;
    std::filesystem::create_directories(speciesDir, ec);
    for (float yaw : yaws)
    {
      const GLuint tex = preview.Render(speciesId, "", kSize, yaw, kPitch);
      if (tex == 0)
      {
        std::cerr << "creature-preview-smoke: render failed " << speciesId
                  << " yaw=" << yaw << std::endl;
        ++failures;
        continue;
      }
      const auto path =
          speciesDir / ("yaw_" + std::to_string(static_cast<int>(yaw)) + ".bmp");
      if (!CapturePreviewBmp(tex, kSize, path))
      {
        std::cerr << "creature-preview-smoke: write failed " << path << std::endl;
        ++failures;
      }
    }
  }

  std::cout << "creature-preview-smoke: species=" << speciesList.size()
            << " failures=" << failures << std::endl;
  preview.Shutdown();
  glfwDestroyWindow(ctx);
  glfwTerminate();
  return failures > 0 ? 1 : 0;
}

} // namespace cutum
