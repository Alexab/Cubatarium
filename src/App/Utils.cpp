#include "App/Utils.h"

#include "App/CreateWorldCli.h"
#include "Core/Progress/IUProgressSink.h"
#include "World/Core/WorldLoadDiagnostics.h"
#include <chrono>
#include <cstring>
#include <fstream>
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
#include "World/Chunks/Chunk.h"
#include "World/Chunks/ChunkBuffer.h"
#include "World/Core/World.h"
#include "World/Mesh/WorldMeshService.h"
#include "World/IO/BinaryChunkSerializer.h"
#include "World/IO/JsonChunkSerializer.h"
#include "World/Math/BlockTypes.h"
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

struct HeadlessCoreSetup
{
  std::shared_ptr<UCore> Core;
  std::shared_ptr<UWorld> World;
};

HeadlessCoreSetup MakeHeadlessCore()
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
  return {core, world};
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

  auto headless = MakeHeadlessCore();
  auto core = headless.Core;
  core->LoadConfig("config.json");

  CreateWorldReport report;
  const bool ok = core->CreateWorldHeadless(cli_args, report);
  WriteCreateWorldReport(report, cli_args.ReportJsonPath);

  glfwDestroyWindow(ctx);
  glfwTerminate();
  return ok ? 0 : 1;
}

namespace
{

class ConsoleLoadProgressSink : public IUProgressSink
{
public:
  void Begin(WorldOperationKind kind) override
  {
    std::cout << "load-world: begin kind=" << static_cast<int>(kind)
              << std::endl;
  }

  void Report(const std::string &phaseId, float fraction,
              const std::string &message) override
  {
    std::cout << "load-world: " << phaseId << " " << fraction << " "
              << message << std::endl;
  }

  void End(bool success, const std::string &errorMessage) override
  {
    std::cout << "load-world: end success=" << (success ? 1 : 0);
    if (!errorMessage.empty())
    {
      std::cout << " error=" << errorMessage;
    }
    std::cout << std::endl;
  }
};

bool InitHeadlessGlContext(GLFWwindow *&out_window)
{
  if (!glfwInit())
  {
    std::cerr << "load-world: glfwInit failed" << std::endl;
    return false;
  }
  glfwWindowHint(GLFW_VISIBLE, GLFW_FALSE);
  glfwWindowHint(GLFW_CONTEXT_VERSION_MAJOR, 3);
  glfwWindowHint(GLFW_CONTEXT_VERSION_MINOR, 3);
  glfwWindowHint(GLFW_OPENGL_PROFILE, GLFW_OPENGL_CORE_PROFILE);
  out_window = glfwCreateWindow(64, 64, "load-world", nullptr, nullptr);
  if (!out_window)
  {
    std::cerr << "load-world: failed to create GL context" << std::endl;
    glfwTerminate();
    return false;
  }
  glfwMakeContextCurrent(out_window);
  if (glewInit() != GLEW_OK)
  {
    std::cerr << "load-world: glewInit failed" << std::endl;
    glfwDestroyWindow(out_window);
    glfwTerminate();
    return false;
  }
  return true;
}

} // namespace

int RunLoadWorld(int argc, char **argv, int load_world_index)
{
  if (load_world_index >= argc)
  {
    std::cerr << "load-world: missing world name" << std::endl;
    return 1;
  }

  std::string world_name = argv[load_world_index];
  int timeout_ms = 120000;
  for (int i = load_world_index + 1; i < argc; ++i)
  {
    if (std::strcmp(argv[i], "--load-timeout-ms") == 0 && i + 1 < argc)
    {
      timeout_ms = std::atoi(argv[++i]);
    }
  }

  GLFWwindow *ctx = nullptr;
  if (!InitHeadlessGlContext(ctx))
  {
    return 1;
  }

  if (!UWorldGenRefs::LoadFromFile("content/worldgen_refs.json"))
  {
    std::cerr << "load-world: worldgen_refs.json not loaded" << std::endl;
    glfwDestroyWindow(ctx);
    glfwTerminate();
    return 1;
  }

  auto headless = MakeHeadlessCore();
  auto core = headless.Core;
  core->LoadConfig("config.json");
  core->PrepareLoadWorld(world_name);
  const std::string folder = core->GetActiveWorldFolder().string();
  if (folder.empty())
  {
    std::cerr << "load-world: no folder for world '" << world_name << "'"
              << std::endl;
    glfwDestroyWindow(ctx);
    glfwTerminate();
    return 1;
  }

  UWorld &world = *headless.World;
  world.BeginCooperativeLoad(folder);
  ConsoleLoadProgressSink sink;
  const auto started = std::chrono::steady_clock::now();
  while (!world.TickCooperativeLoad(sink, 64))
  {
    const auto elapsed_ms = std::chrono::duration_cast<std::chrono::milliseconds>(
                                std::chrono::steady_clock::now() - started)
                                .count();
    if (elapsed_ms > timeout_ms)
    {
      std::cerr << "load-world: timeout after " << elapsed_ms << "ms"
                << std::endl;
      LogWorldLoadDiag("timeout", world);
      glfwDestroyWindow(ctx);
      glfwTerminate();
      return 1;
    }
  }

  LogWorldLoadDiag("cli_after_coop", world);
  WarnIfTerrainMeshesMissing(world, "load-world after cooperative");

  const size_t blocks = world.GetBlockWorld().CountNonAir();
  const size_t cache = world.GetMeshService().GetGreedyCacheSize();
  const size_t batches = world.GetMeshService().GetCache().GetGreedyBatches().size();
  const size_t vertices = world.GetMeshService().GetGreedyVertexCount();

  std::cout << "load-world: blocks=" << blocks << " greedy_cache=" << cache
            << " greedy_batches=" << batches << " greedy_vertices=" << vertices
            << std::endl;

  int exit_code = 0;
  if (blocks > 0 && cache == 0 && batches == 0)
  {
    std::cerr << "load-world: FAIL terrain meshes missing" << std::endl;
    exit_code = 1;
  }
  else
  {
    std::cout << "load-world: PASS" << std::endl;
  }

  {
    const std::filesystem::path report_path =
        GetExecutableDirectory() / "load_world_report.txt";
    std::ofstream report(report_path);
    if (report)
    {
      report << "world=" << world_name << '\n'
             << "exit_code=" << exit_code << '\n'
             << "blocks=" << blocks << '\n'
             << "greedy_cache=" << cache << '\n'
             << "greedy_batches=" << batches << '\n'
             << "greedy_vertices=" << vertices << '\n';
    }
  }

  glfwDestroyWindow(ctx);
  glfwTerminate();
  return exit_code;
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

} // namespace cutum
