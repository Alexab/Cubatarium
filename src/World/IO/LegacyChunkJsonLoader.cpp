#include "World/IO/LegacyChunkJsonLoader.h"

#include "Blocks/BlockRegistry.h"
#include "World/Chunks/ChunkManager.h"
#include "World/Core/BlockWorld.h"

#include <fstream>
#include <iostream>
#include <nlohmann/json.hpp>

namespace cutum
{

using json = nlohmann::json;

bool ULegacyChunkJsonLoader::SetBlockFromTypeName(UBlockWorld &world,
                                                  UBlockRegistry &registry,
                                                  glm::ivec3 worldPos,
                                                  const std::string &type)
{
  if (type.empty())
  {
    return false;
  }
  const BlockId id = registry.GetIdByTypeName(type);
  world.SetBlock(worldPos, id);
  return true;
}

void ULegacyChunkJsonLoader::LoadBlocksFile(UBlockWorld &world,
                                            UBlockRegistry &registry,
                                            const std::string &file_name)
{
  std::ifstream file(file_name);
  if (!file.is_open())
  {
    return;
  }
  try
  {
    const json data = json::parse(file);
    const json &blocks = data.at("blocks");
    for (const auto &entry : blocks)
    {
      const int x = entry.at("x").get<int>();
      const int y = entry.at("y").get<int>();
      const int z = entry.at("z").get<int>();
      const std::string type = entry.at("type").get<std::string>();
      SetBlockFromTypeName(world, registry, glm::ivec3(x, y, z), type);
    }
  }
  catch (const json::exception &e)
  {
    std::cerr << "JSON parsing error in LoadBlocks: " << e.what() << std::endl;
  }
}

void ULegacyChunkJsonLoader::LoadMonolithicChunksFile(
    UBlockWorld &world, UBlockRegistry &registry, const std::string &file_name)
{
  std::ifstream file(file_name);
  if (!file.is_open())
  {
    return;
  }
  try
  {
    const json data = json::parse(file);
    if (data.value("storage", "") == "per_file")
    {
      return;
    }
    if (!data.contains("chunks") || !data["chunks"].is_array())
    {
      return;
    }
    for (const auto &chunkEntry : data["chunks"])
    {
      const int cx = chunkEntry.at("cx").get<int>();
      const int cy = chunkEntry.at("cy").get<int>();
      const int cz = chunkEntry.at("cz").get<int>();
      for (const auto &voxel : chunkEntry.at("voxels"))
      {
        const int lx = voxel.at("lx").get<int>();
        const int ly = voxel.at("ly").get<int>();
        const int lz = voxel.at("lz").get<int>();
        const std::string type = voxel.at("type").get<std::string>();
        const glm::ivec3 worldPos(cx * CHUNK_SIZE + lx, cy * CHUNK_SIZE + ly,
                                  cz * CHUNK_SIZE + lz);
        SetBlockFromTypeName(world, registry, worldPos, type);
      }
    }
  }
  catch (const json::exception &e)
  {
    std::cerr << "JSON parsing error in LoadChunks: " << e.what() << std::endl;
  }
}

} // namespace cutum
