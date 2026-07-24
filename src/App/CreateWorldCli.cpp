#include "App/CreateWorldCli.h"

#include "WorldGen/Core/ProceduralSettings.h"

#include <chrono>
#include <cstring>
#include <fstream>
#include <iostream>
#include <nlohmann/json.hpp>

namespace cutum
{

namespace
{

bool ParseUInt32(const char *text, uint32_t &out)
{
  if (!text || !*text)
  {
    return false;
  }
  try
  {
    const unsigned long value = std::stoul(text);
    out = static_cast<uint32_t>(value);
    return true;
  }
  catch (const std::exception &)
  {
    return false;
  }
}

bool ParseInt(const char *text, int &out)
{
  if (!text || !*text)
  {
    return false;
  }
  try
  {
    out = std::stoi(text);
    return true;
  }
  catch (const std::exception &)
  {
    return false;
  }
}

std::string DefaultWorldName()
{
  const auto now = std::chrono::system_clock::now().time_since_epoch();
  const auto seconds =
      std::chrono::duration_cast<std::chrono::seconds>(now).count();
  return "World_CI_" + std::to_string(seconds);
}

} // namespace

bool ParseCreateWorldCliArgs(int argc, char **argv, int start_index,
                             CreateWorldCliArgs &out, std::string &error)
{
  out = CreateWorldCliArgs{};
  for (int i = start_index; i < argc; ++i)
  {
    const char *arg = argv[i];
    if (std::strcmp(arg, "--help") == 0 || std::strcmp(arg, "-h") == 0)
    {
      error = "help";
      return false;
    }
    if (std::strcmp(arg, "--name") == 0)
    {
      if (i + 1 >= argc)
      {
        error = "--name requires a value";
        return false;
      }
      out.WorldName = argv[++i];
      continue;
    }
    if (std::strcmp(arg, "--seed") == 0)
    {
      if (i + 1 >= argc || !ParseUInt32(argv[i + 1], out.Seed))
      {
        error = "--seed requires a numeric value";
        return false;
      }
      ++i;
      continue;
    }
    if (std::strcmp(arg, "--generator") == 0)
    {
      if (i + 1 >= argc)
      {
        error = "--generator requires a value";
        return false;
      }
      out.Generator = ProceduralGeneratorFromString(argv[++i]);
      continue;
    }
    if (std::strcmp(arg, "--preset") == 0)
    {
      if (i + 1 >= argc)
      {
        error = "--preset requires a value";
        return false;
      }
      out.Preset = argv[++i];
      continue;
    }
    if (std::strcmp(arg, "--terrain-backend") == 0)
    {
      if (i + 1 >= argc)
      {
        error = "--terrain-backend requires a value";
        return false;
      }
      out.TerrainBackendMode = TerrainBackendFromString(argv[++i]);
      continue;
    }
    if (std::strcmp(arg, "--radius-chunks") == 0)
    {
      if (i + 1 >= argc || !ParseInt(argv[i + 1], out.RadiusChunks))
      {
        error = "--radius-chunks requires an integer";
        return false;
      }
      ++i;
      continue;
    }
    if (std::strcmp(arg, "--pack") == 0)
    {
      if (i + 1 >= argc)
      {
        error = "--pack requires a value";
        return false;
      }
      out.PrimaryPacks.push_back(argv[++i]);
      continue;
    }
    if (std::strcmp(arg, "--output") == 0)
    {
      if (i + 1 >= argc)
      {
        error = "--output requires a path";
        return false;
      }
      out.OutputRoot = argv[++i];
      continue;
    }
    if (std::strcmp(arg, "--report-json") == 0)
    {
      if (i + 1 >= argc)
      {
        error = "--report-json requires a path";
        return false;
      }
      out.ReportJsonPath = argv[++i];
      continue;
    }
    if (arg[0] == '-')
    {
      error = std::string("unknown option: ") + arg;
      return false;
    }
  }

  if (out.WorldName.empty())
  {
    out.WorldName = DefaultWorldName();
  }
  if (out.RadiusChunks < 1)
  {
    out.RadiusChunks = 1;
  }
  return true;
}

bool WriteCreateWorldReport(const CreateWorldReport &report,
                            const std::optional<std::filesystem::path> &path)
{
  nlohmann::json json_report;
  json_report["success"] = report.Success;
  json_report["world_path"] = report.WorldPath;
  json_report["world_name"] = report.WorldName;
  json_report["seed"] = report.Seed;
  json_report["generator"] = report.Generator;
  json_report["preset"] = report.Preset;
  json_report["radius_chunks"] = report.RadiusChunks;
  json_report["chunk_files"] = report.ChunkFiles;
  json_report["spawn_y"] = report.SpawnY;
  json_report["error"] = report.Error;

  const std::string text = json_report.dump(2);
  if (!path.has_value())
  {
    std::cout << text << std::endl;
    return true;
  }

  std::ofstream out(path->string(), std::ios::trunc);
  if (!out.is_open())
  {
    std::cerr << "create-world: failed to write report: " << path->string()
              << std::endl;
    return false;
  }
  out << text;
  return true;
}

} // namespace cutum
