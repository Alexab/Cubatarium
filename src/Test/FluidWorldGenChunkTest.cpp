#include "Test/FluidTestHelpers.h"

#include "Blocks/BlockRegistry.h"
#include "ResourcePacks/ResourcePack.h"
#include "World/Chunks/ChunkBuffer.h"
#include "World/Chunks/ChunkGenerationToken.h"
#include "World/Core/BlockWorld.h"
#include "WorldGen/Core/IUChunkPopulator.h"
#include "WorldGen/Core/ProceduralSettings.h"
#include "WorldGen/Core/WorldGenPack.h"
#include "WorldGen/Core/WorldGenRefs.h"
#include "World/Objects/ObjectUtil.h"
#include "WorldGen/Stages/WorldGenStages.h"

#include <algorithm>
#include <filesystem>
#include <iostream>
#include <memory>
#include <unordered_map>
#include <vector>

namespace fs = std::filesystem;

namespace
{

constexpr const char *kTestName = "fluid_worldgen_chunk_test";

static const std::array<uint32_t, 3> kReferenceSeeds = {42u, 12354u, 20240625u};

static fs::path FindRepoRoot()
{
  fs::path dir = fs::current_path();
  for (int depth = 0; depth < 8; ++depth)
  {
    if (fs::exists(dir / "content" / "worldgen_refs.json") &&
        fs::exists(dir / "resource_packs" / "cubatarium_cc0_base" / "pack.json"))
    {
      return dir;
    }
    if (!dir.has_parent_path())
    {
      break;
    }
    dir = dir.parent_path();
  }
  FluidTest::Expect(false, kTestName, "could not find repository root");
  return fs::current_path();
}

static std::shared_ptr<cutum::UBlockDefinitionStorage>
LoadPackDefinitions(const fs::path &pack_root)
{
  const std::optional<cutum::ResourcePackManifest> manifest =
      cutum::UResourcePack::LoadManifest(pack_root);
  FluidTest::Expect(manifest.has_value(), kTestName, "pack manifest load failed");
  const std::vector<cutum::ResourcePackBlock> blocks =
      cutum::UResourcePack::LoadBlocks(*manifest);
  FluidTest::Expect(!blocks.empty(), kTestName, "pack has no blocks");

  std::unordered_map<cutum::BlockId, cutum::BlockDefinition> by_id;
  std::unordered_map<std::string, cutum::BlockId> name_to_id;
  for (const cutum::ResourcePackBlock &block : blocks)
  {
    by_id[block.Definition.Id] = block.Definition;
    name_to_id[block.Definition.Name] = block.Definition.Id;
  }
  auto storage = std::make_shared<cutum::UBlockDefinitionStorage>();
  storage->ReplaceAll(std::move(by_id), std::move(name_to_id));
  return storage;
}

static cutum::ProceduralSettings MakeBalancedOverworldSettings(uint32_t seed)
{
  cutum::ProceduralSettings settings;
  settings.Generator = cutum::ProceduralGenerator::Overworld;
  settings.Seed = seed;
  settings.FillWater = true;
  settings.EnableCaves = false;
  settings.EnableTrees = false;
  settings.EnableGroundCover = false;
  settings.EnableDecoration = false;
  settings.EnableStructures = false;
  settings.EnableOres = false;
  cutum::ApplyWorldGenPreset(settings, "balanced");
  settings.Ravines.enabled = false;
  return settings;
}

static void MergeChunksAndSeal(cutum::UBlockWorld &world,
                               cutum::UBlockRegistry &registry,
                               const cutum::ProceduralSettings &settings,
                               const std::vector<cutum::ChunkPopulateResult> &results,
                               const std::string &worldgen_owner_pack_id)
{
  for (const cutum::ChunkPopulateResult &result : results)
  {
    result.buffer.ApplyTo(world);
  }
  for (const cutum::ChunkPopulateResult &result : results)
  {
    cutum::SealFluidShoreOnChunkCommitted(world, registry, settings,
                                          worldgen_owner_pack_id, result.coord);
  }
}

static void ScanChunkSeamMesh(cutum::UBlockWorld &world,
                              cutum::UBlockRegistry &registry,
                              cutum::BlockId water_id, int sea_level)
{
  const int max_coord = cutum::CHUNK_SIZE * 2 - 1;
  for (int x = 1; x < max_coord; ++x)
  {
    for (int z = 1; z < max_coord; ++z)
    {
      const glm::ivec3 pos(x, sea_level, z);
      if (world.GetBlock(pos) != water_id)
      {
        continue;
      }
      const bool on_internal_seam =
          (x % cutum::CHUNK_SIZE == 0) || (z % cutum::CHUNK_SIZE == 0);
      if (!on_internal_seam)
      {
        continue;
      }
      FluidTest::ExpectFluidCells(
          kTestName, world, registry, water_id,
          {{pos, true, 1, 0, false}});
    }
  }
}

static void TestSeed(uint32_t seed, cutum::UPipelineChunkPopulator &populator,
                     cutum::UBlockRegistry &registry,
                     const std::string &worldgen_owner_pack_id)
{
  const cutum::ProceduralSettings settings = MakeBalancedOverworldSettings(seed);
  std::vector<cutum::ChunkPopulateResult> results;
  const std::array<glm::ivec3, 4> coords = {
      glm::ivec3(0, 0, 0), glm::ivec3(1, 0, 0), glm::ivec3(0, 0, 1),
      glm::ivec3(1, 0, 1)};
  cutum::ChunkGenerationToken token;
  for (const glm::ivec3 &coord : coords)
  {
    cutum::ChunkPopulateRequest request;
    request.chunkCoord = coord;
    request.token = token;
    request.settings = settings;
    results.push_back(populator.Populate(request));
  }

  cutum::UBlockWorld world;
  world.SetFluidDefinitions(registry.GetDefinitions());
  MergeChunksAndSeal(world, registry, settings, results, worldgen_owner_pack_id);

  const cutum::BlockId water_id =
      registry.GetIdByTypeName("cubatarium_cc0_base/water");
  const cutum::BlockId resolved_water =
      water_id != cutum::BLOCK_AIR ? water_id : registry.GetIdByTypeName("water");
  FluidTest::Expect(resolved_water != cutum::BLOCK_AIR, kTestName,
                    "water block id resolved");

  const int max_coord = cutum::CHUNK_SIZE * 2 - 1;
  const int gaps = FluidTest::CountShoreAirGaps(world, resolved_water,
                                                settings.SeaLevel, 0, max_coord,
                                                0, max_coord);
  FluidTest::Expect(gaps == 0, kTestName, "shore_air_gaps must be zero");
  ScanChunkSeamMesh(world, registry, resolved_water, settings.SeaLevel);

  int max_surface_delta = 0;
  for (int x = 0; x < max_coord; ++x)
  {
    for (int z = 0; z < max_coord; ++z)
    {
      const int y = cutum::FindTopSolidSurfaceY(
          world, registry, x, z, settings.MaxHeight);
      const int y_e = cutum::FindTopSolidSurfaceY(
          world, registry, x + 1, z, settings.MaxHeight);
      const int y_n = cutum::FindTopSolidSurfaceY(
          world, registry, x, z + 1, settings.MaxHeight);
      max_surface_delta =
          std::max({max_surface_delta, std::abs(y - y_e), std::abs(y - y_n)});
    }
  }
  FluidTest::Expect(max_surface_delta <= 6, kTestName,
                    "2x2 chunk surface neighbor delta within budget");
}

} // namespace

int main()
{
  const fs::path repo_root = FindRepoRoot();
  std::error_code ec;
  fs::current_path(repo_root, ec);

  FluidTest::Expect(cutum::UWorldGenRefs::LoadFromFile(repo_root / "content" /
                                                       "worldgen_refs.json"),
                    kTestName, "worldgen_refs load failed");
  FluidTest::Expect(cutum::UWorldGenPack::LoadPackId("default"), kTestName,
                    "worldgen pack load failed");

  const auto definitions =
      LoadPackDefinitions(repo_root / "resource_packs" / "cubatarium_cc0_base");
  cutum::UBlockRegistry registry(nullptr, definitions);
  constexpr const char *kOwnerPack = "cubatarium_cc0_base";
  cutum::UPipelineChunkPopulator populator(registry, nullptr, kOwnerPack);

  for (uint32_t seed : kReferenceSeeds)
  {
    TestSeed(seed, populator, registry, kOwnerPack);
  }

  std::cout << kTestName << ": OK" << std::endl;
  return 0;
}
