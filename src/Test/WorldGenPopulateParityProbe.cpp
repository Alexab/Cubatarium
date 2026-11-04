#include "Test/FluidTestHelpers.h"

#include "Blocks/BlockRegistry.h"
#include "ResourcePacks/ResourcePack.h"
#include "World/Chunks/ChunkBuffer.h"
#include "World/Chunks/ChunkGenerationToken.h"
#include "WorldGen/Core/IUChunkPopulator.h"
#include "WorldGen/Core/ProceduralSettings.h"
#include "WorldGen/Core/WorldGenPack.h"
#include "WorldGen/Core/WorldGenRefs.h"
#include "WorldGen/Stages/WorldGenStages.h"

#include <array>
#include <chrono>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <iomanip>
#include <iostream>
#include <memory>
#include <nlohmann/json.hpp>
#include <optional>
#include <sstream>
#include <string>
#include <unordered_map>
#include <vector>

namespace fs = std::filesystem;
using json = nlohmann::json;

namespace
{

constexpr const char *kTestName = "worldgen_populate_parity_probe";

struct ProfileCase
{
  const char *Name;
  int ChunkX;
  int ChunkZ;
  bool Caves;
};

constexpr std::array<ProfileCase, 3> kProfiles{{
    {"mountain", 192, -128, false},
    {"coast", -128, 192, false},
    {"mountain_caves", 192, -128, true},
}};
constexpr std::array<uint32_t, 3> kSeeds{777001u, 777002u, 777003u};

fs::path FindRepoRoot()
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

std::shared_ptr<cutum::UBlockDefinitionStorage> LoadPackDefinitions(
    const fs::path &pack_root)
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

void HashU64(uint64_t &hash, uint64_t value)
{
  for (int byte = 0; byte < 8; ++byte)
  {
    hash ^= static_cast<uint8_t>(value >> (byte * 8));
    hash *= 1099511628211ull;
  }
}

std::string HexHash(uint64_t hash)
{
  std::ostringstream out;
  out << std::hex << std::setfill('0') << std::setw(16) << hash;
  return out.str();
}

cutum::ProceduralSettings MakeSettings(uint32_t seed, bool caves)
{
  cutum::ProceduralSettings settings;
  settings.Generator = cutum::ProceduralGenerator::Overworld;
  settings.Seed = seed;
  settings.WorldGenProfileVersion = 3;
  settings.TerrainBackendMode = cutum::TerrainBackend::Density3D;
  settings.EnableCaves = caves;
  cutum::ApplyWorldGenPreset(settings, "balanced");
  settings.WorldGenProfileVersion = 3;
  settings.TerrainBackendMode = cutum::TerrainBackend::Density3D;
  settings.EnableCaves = caves;
  return settings;
}

json RunCase(cutum::UPipelineChunkPopulator &populator, uint32_t seed,
             const ProfileCase &profile)
{
  cutum::ChunkPopulateRequest request;
  request.chunkCoord = glm::ivec3(profile.ChunkX, 0, profile.ChunkZ);
  request.token = cutum::ChunkGenerationToken{};
  request.settings = MakeSettings(seed, profile.Caves);
  const auto start = std::chrono::steady_clock::now();
  cutum::ChunkPopulateResult result = populator.Populate(request);
  const double wall_ms = std::chrono::duration<double, std::milli>(
                             std::chrono::steady_clock::now() - start)
                             .count();
  FluidTest::Expect(!result.discarded, kTestName, "populate was discarded");

  uint64_t block_hash = 1469598103934665603ull;
  uint64_t fluid_hash = 1469598103934665603ull;
  uint64_t light_hash = 1469598103934665603ull;
  uint64_t nonempty_blocks = 0;
  uint64_t nonempty_fluid = 0;
  const int base_x = profile.ChunkX * cutum::CHUNK_SIZE;
  const int base_z = profile.ChunkZ * cutum::CHUNK_SIZE;
  for (int z = 0; z < cutum::CHUNK_SIZE; ++z)
  {
    for (int x = 0; x < cutum::CHUNK_SIZE; ++x)
    {
      for (int y = request.settings.MinHeight; y <= request.settings.MaxHeight; ++y)
      {
        const glm::ivec3 pos(base_x + x, y, base_z + z);
        const uint32_t block = result.buffer.GetBlock(pos);
        const uint8_t fluid = result.buffer.GetFluidPacked(pos);
        const uint8_t light = result.buffer.GetLightPacked(pos);
        HashU64(block_hash, block);
        HashU64(fluid_hash, fluid);
        HashU64(light_hash, light);
        nonempty_blocks += block != cutum::BLOCK_AIR ? 1u : 0u;
        nonempty_fluid += fluid != 0 ? 1u : 0u;
      }
    }
  }
  HashU64(block_hash, result.buffer.HasYBounds() ? 1u : 0u);
  HashU64(block_hash, static_cast<uint32_t>(result.buffer.GetMinY()));
  HashU64(block_hash, static_cast<uint32_t>(result.buffer.GetMaxY()));
  const cutum::ChunkPopulateTiming timing = cutum::ChunkPopulateDiagnostics::GetLast();
  return {
      {"seed", seed},
      {"profile", profile.Name},
      {"chunk", {profile.ChunkX, 0, profile.ChunkZ}},
      {"min_y", request.settings.MinHeight},
      {"max_y", request.settings.MaxHeight},
      {"block_hash", HexHash(block_hash)},
      {"fluid_hash", HexHash(fluid_hash)},
      {"light_hash", HexHash(light_hash)},
      {"nonempty_blocks", nonempty_blocks},
      {"nonempty_fluid", nonempty_fluid},
      {"wall_ms", wall_ms},
      {"timing_ms",
       {{"total", timing.totalMs},
        {"sample", timing.sampleMs},
        {"terrain", timing.terrainMs},
        {"carve", timing.carveMs},
        {"post", timing.postMs},
        {"seal", timing.sealMs}}},
  };
}

void CheckParity(const json &actual, const json &expected)
{
  for (const char *key : {"seed", "profile", "chunk", "min_y", "max_y",
                          "block_hash", "fluid_hash", "light_hash",
                          "nonempty_blocks", "nonempty_fluid"})
  {
    const std::string message = std::string("populate parity drift: ") + key;
    FluidTest::Expect(actual.at(key) == expected.at(key), kTestName,
                      message.c_str());
  }
}

} // namespace

int main(int argc, char **argv)
{
  const fs::path repo_root = FindRepoRoot();
  std::error_code ec;
  fs::current_path(repo_root, ec);
  FluidTest::Expect(cutum::UWorldGenRefs::LoadFromFile(
                        repo_root / "content" / "worldgen_refs.json"),
                    kTestName, "worldgen_refs load failed");
  FluidTest::Expect(cutum::UWorldGenPack::LoadPackId("default"), kTestName,
                    "worldgen pack load failed");
  const auto definitions = LoadPackDefinitions(
      repo_root / "resource_packs" / "cubatarium_cc0_base");
  cutum::UBlockRegistry registry(nullptr, definitions);
  cutum::UPipelineChunkPopulator populator(registry, nullptr, "cubatarium_cc0_base");

  json report{{"schema", "worldgen_populate_parity_probe.v1"},
              {"scope", "target XZ, full MinHeight..MaxHeight block/fluid/light"},
              {"cases", json::array()}};
  for (const ProfileCase &profile : kProfiles)
  {
    for (uint32_t seed : kSeeds)
    {
      report["cases"].push_back(RunCase(populator, seed, profile));
    }
  }

  const fs::path baseline = repo_root / "src" / "Test" / "Data" /
                            "WorldGenPopulateParityBaseline.json";
  if (argc > 2 && std::string(argv[1]) == "--write-baseline")
  {
    std::ofstream out(argv[2]);
    FluidTest::Expect(out.good(), kTestName, "cannot write baseline");
    out << report.dump(2) << '\n';
  }
  else
  {
    std::ifstream input(baseline);
    FluidTest::Expect(input.good(), kTestName, "cannot read parity baseline");
    const json expected = json::parse(input);
    FluidTest::Expect(report["cases"].size() == expected["cases"].size(),
                      kTestName, "baseline case count mismatch");
    for (size_t i = 0; i < report["cases"].size(); ++i)
    {
      CheckParity(report["cases"][i], expected["cases"][i]);
    }
  }
  if (argc > 2 && std::string(argv[1]) == "--report")
  {
    std::ofstream out(argv[2]);
    FluidTest::Expect(out.good(), kTestName, "cannot write report");
    out << report.dump(2) << '\n';
  }
  std::cout << report.dump(2) << std::endl;
  std::cout << kTestName << ": all tests passed\n";
  return 0;
}
