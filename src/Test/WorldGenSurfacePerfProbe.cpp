#include "Test/FluidTestHelpers.h"

#include "WorldGen/Core/ProceduralSettings.h"
#include "WorldGen/Core/WorldGenPack.h"
#include "WorldGen/Sampling/DensityFieldSampler.h"

#include <array>
#include <chrono>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <iomanip>
#include <iostream>
#include <nlohmann/json.hpp>
#include <set>
#include <sstream>
#include <string>
#include <vector>

namespace fs = std::filesystem;
using json = nlohmann::json;

namespace
{

constexpr const char *kTestName = "worldgen_surface_perf_probe";
constexpr int kGridSize = 16;

struct ProfileCase
{
  std::string name;
  int base_x;
  int base_z;
  bool caves_in_density;
};

const std::array<ProfileCase, 3> kProfiles{{
    {"mountain", 3072, -2048, false},
    {"coast", -2048, 3072, false},
    {"mountain_caves", 3072, -2048, true},
}};
constexpr std::array<uint32_t, 3> kSeeds{777001u, 777002u, 777003u};

fs::path FindRepoRoot()
{
  fs::path dir = fs::current_path();
  for (int depth = 0; depth < 8; ++depth)
  {
    if (fs::exists(dir / "content" / "worldgen_packs" / "default" / "pack.json"))
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

json RunCase(uint32_t seed, const ProfileCase &profile)
{
  cutum::DensityFieldParams params;
  params.routerV3 = true;
  params.cavesInDensity = profile.caves_in_density;
  cutum::CaveParams caves;
  cutum::UDensityFieldSampler sampler(seed, 63, 319, 0.62f, params, caves, -64);
  sampler.SetProfilingEnabled(true);

  uint64_t exact_hash = 1469598103934665603ull;
  uint64_t coarse_hash = 1469598103934665603ull;
  // Hash a full 16x16 chunk-surface payload in coordinate order. The name is
  // deliberately explicit: this is not a voxel/vegetation chunk-content hash.
  uint64_t chunk_surface_hash = 1469598103934665603ull;
  std::set<std::pair<int, int>> unique_columns;
  const auto wall_start = std::chrono::steady_clock::now();
  for (int z = profile.base_z; z < profile.base_z + kGridSize; ++z)
  {
    for (int x = profile.base_x; x < profile.base_x + kGridSize; ++x)
    {
      const int exact = sampler.SurfaceYAt(x, z);
      const int coarse = sampler.CoarseSurfaceYAt(x, z);
      HashU64(exact_hash, static_cast<uint32_t>(exact));
      HashU64(coarse_hash, static_cast<uint32_t>(coarse));
      HashU64(chunk_surface_hash, static_cast<uint32_t>(x));
      HashU64(chunk_surface_hash, static_cast<uint32_t>(z));
      HashU64(chunk_surface_hash, static_cast<uint32_t>(exact));
      unique_columns.emplace(x, z);
    }
  }
  const double wall_ms = std::chrono::duration<double, std::milli>(
                             std::chrono::steady_clock::now() - wall_start)
                             .count();
  const auto &stats = sampler.GetProfileStats();
  return {
      {"seed", seed},
      {"profile", profile.name},
      {"caves_in_density", profile.caves_in_density},
      {"grid_size", kGridSize},
      {"unique_columns", unique_columns.size()},
      {"surface_hash", HexHash(exact_hash)},
      {"coarse_hash", HexHash(coarse_hash)},
      {"chunk_surface_hash", HexHash(chunk_surface_hash)},
      {"wall_ms", wall_ms},
      {"counters",
       {{"surface_calls", stats.surface_calls},
        {"coarse_calls", stats.coarse_calls},
        {"route_calls", stats.route_calls},
        {"wobble_calls", stats.wobble_calls},
        {"prepare_column_calls", stats.prepare_column_calls},
        {"terrain_density_calls", stats.terrain_density_calls},
        {"cave_density_calls", stats.cave_density_calls},
        {"scan_top_calls", stats.scan_top_calls}}},
      {"phase_ms",
       {{"surface_total", stats.surface_ms},
        {"coarse_total", stats.coarse_ms},
        {"route", stats.route_ms},
        {"wobble", stats.wobble_ms},
        {"terrain_density", stats.terrain_density_ms},
        {"other", wall_ms - stats.route_ms - stats.wobble_ms -
                      stats.terrain_density_ms}}},
  };
}

void CheckParity(const json &actual, const json &expected)
{
  for (const char *key : {"seed", "profile", "caves_in_density", "grid_size",
                          "unique_columns", "surface_hash", "coarse_hash",
                          "chunk_surface_hash"})
  {
    const std::string message = std::string("parity drift: ") + key;
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
  FluidTest::Expect(cutum::UWorldGenPack::LoadPackId("default"), kTestName,
                    "worldgen pack load failed");

  json report{{"schema", "worldgen_surface_perf_probe.v1"},
              {"reference_scan", "SurfaceYAt full downward DensityScanTop"},
              {"cases", json::array()}};
  for (const ProfileCase &profile : kProfiles)
  {
    for (uint32_t seed : kSeeds)
    {
      report["cases"].push_back(RunCase(seed, profile));
    }
  }

  const fs::path baseline =
      repo_root / "src" / "Test" / "Data" / "WorldGenSurfacePerfBaseline.json";
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
