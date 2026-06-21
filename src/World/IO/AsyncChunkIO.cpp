#include "World/IO/AsyncChunkIO.h"
#include "World/Chunks/Chunk.h"
#include "World/Core/BlockWorld.h"
#include <fstream>
#include <nlohmann/json.hpp>

namespace cutum
{

using json = nlohmann::json;

void UAsyncChunkIO::RequestLoad(glm::ivec3 coord, const std::string &worldFolder,
                                ChunkGenerationToken token)
{
  const std::string filePath =
      worldFolder + "/chunks/" + std::to_string(coord.x) + "_" +
      std::to_string(coord.y) + "_" + std::to_string(coord.z) + ".json";
  Pool.Enqueue(
      [this, coord, filePath, token]()
      {
        AsyncChunkLoadResult result;
        result.coord = coord;
        result.token = token;
        std::ifstream file(filePath);
        if (!file.is_open())
        {
          CompletedLoads.Push(std::move(result));
          return;
        }
        result.jsonText.assign(std::istreambuf_iterator<char>(file),
                               std::istreambuf_iterator<char>());
        result.success = !result.jsonText.empty();
        CompletedLoads.Push(std::move(result));
      });
}

void UAsyncChunkIO::RequestSave(glm::ivec3 coord, const std::string &worldFolder,
                                const UBlockWorld &world,
                                UBlockRegistry &registry,
                                ChunkGenerationToken token)
{
  const UChunk *chunk = world.GetChunkManager().GetChunk(coord);
  if (!chunk)
  {
    return;
  }
  const std::string jsonText = SerializeChunkToJson(coord, *chunk, registry);
  const std::string filePath =
      worldFolder + "/chunks/" + std::to_string(coord.x) + "_" +
      std::to_string(coord.y) + "_" + std::to_string(coord.z) + ".json";
  (void)token;
  Pool.Enqueue(
      [this, filePath, jsonText, coord]()
      {
        std::filesystem::create_directories(
            std::filesystem::path(filePath).parent_path());
        std::ofstream file(filePath);
        if (file.is_open())
        {
          file << jsonText;
        }
        AsyncChunkSaveRequest done;
        done.coord = coord;
        done.filePath = filePath;
        done.jsonText = jsonText;
        CompletedSaves.Push(std::move(done));
      });
}

std::vector<AsyncChunkLoadResult> UAsyncChunkIO::DrainLoads()
{
  return CompletedLoads.DrainAll();
}

void UAsyncChunkIO::TickSaves() { CompletedSaves.DrainAll(); }

ChunkBuffer ParseChunkJsonToBuffer(const std::string &jsonText,
                                   glm::ivec3 chunkCoord,
                                   UBlockRegistry &registry)
{
  ChunkBuffer buffer;
  try
  {
    const json data = json::parse(jsonText);
    for (const auto &voxel : data.at("voxels"))
    {
      const int lx = voxel.at("lx").get<int>();
      const int ly = voxel.at("ly").get<int>();
      const int lz = voxel.at("lz").get<int>();
      const std::string type = voxel.at("type").get<std::string>();
      if (type.empty())
      {
        continue;
      }
      const BlockId id = registry.GetIdByTypeName(type);
      const glm::ivec3 worldPos(chunkCoord.x * CHUNK_SIZE + lx,
                                chunkCoord.y * CHUNK_SIZE + ly,
                                chunkCoord.z * CHUNK_SIZE + lz);
      buffer.SetBlock(worldPos, id);
    }
  }
  catch (const json::exception &)
  {
    buffer.Clear();
  }
  return buffer;
}

std::string SerializeChunkToJson(glm::ivec3 chunkCoord, const UChunk &chunk,
                                 UBlockRegistry &registry)
{
  json data;
  data["format_version"] = 2;
  data["cx"] = chunkCoord.x;
  data["cy"] = chunkCoord.y;
  data["cz"] = chunkCoord.z;
  json voxels = json::array();
  for (int z = 0; z < CHUNK_SIZE; ++z)
  {
    for (int y = 0; y < CHUNK_SIZE; ++y)
    {
      for (int x = 0; x < CHUNK_SIZE; ++x)
      {
        const glm::ivec3 local(x, y, z);
        const BlockId id = chunk.GetBlockLocal(local);
        if (id == BLOCK_AIR)
        {
          continue;
        }
        const std::string &type = registry.GetTypeNameById(id);
        if (type.empty())
        {
          continue;
        }
        voxels.push_back({{"lx", x}, {"ly", y}, {"lz", z}, {"type", type}});
      }
    }
  }
  data["voxels"] = voxels;
  return data.dump(4);
}

} // namespace cutum
