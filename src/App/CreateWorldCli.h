#pragma once

#include "WorldGen/Core/ProceduralSettings.h"
#include <cstdint>
#include <filesystem>
#include <optional>
#include <string>
#include <vector>

namespace cutum
{

struct CreateWorldCliArgs
{
  std::string WorldName;
  uint32_t Seed{42};
  ProceduralGenerator Generator{ProceduralGenerator::Overworld};
  TerrainBackend TerrainBackendMode{TerrainBackend::Heightmap};
  std::string Preset{"balanced"};
  int RadiusChunks{4};
  std::filesystem::path OutputRoot{"worlds"};
  std::optional<std::filesystem::path> ReportJsonPath;
  std::vector<std::string> PrimaryPacks;
  std::string WorldgenOwnerPack;
};

struct CreateWorldReport
{
  bool Success{false};
  std::string WorldPath;
  std::string WorldName;
  uint32_t Seed{0};
  std::string Generator;
  std::string Preset;
  int RadiusChunks{4};
  int ChunkFiles{0};
  float SpawnY{0.f};
  std::string Error;
};

bool ParseCreateWorldCliArgs(int argc, char **argv, int start_index,
                             CreateWorldCliArgs &out, std::string &error);

bool WriteCreateWorldReport(const CreateWorldReport &report,
                            const std::optional<std::filesystem::path> &path);

} // namespace cutum
