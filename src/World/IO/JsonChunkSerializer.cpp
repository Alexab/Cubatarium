#include "World/IO/JsonChunkSerializer.h"
#include "Blocks/BlockRegistry.h"
#include "World/Chunks/Chunk.h"
#include <nlohmann/json.hpp>
#include <string>

namespace cutum
{

using json = nlohmann::json;

namespace
{

BlockId ResolveVoxelId(const json &voxel, UBlockRegistry &registry)
{
  if (voxel.contains("id"))
  {
    const uint16_t raw = voxel.at("id").get<uint16_t>();
    return static_cast<BlockId>(raw);
  }
  if (voxel.contains("type"))
  {
    const std::string type = voxel.at("type").get<std::string>();
    if (type.empty())
    {
      return BLOCK_AIR;
    }
    return registry.GetIdByTypeName(type);
  }
  return BLOCK_AIR;
}

} // namespace

SerializedChunk JsonChunkSerializer::Serialize(glm::ivec3 chunkCoord,
                                               const UChunk &chunk,
                                               UBlockRegistry &registry) const
{
  SerializedChunk out;
  out.format = ChunkDiskFormat::Json;

  json data;
  data["format_version"] = 3;
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
        voxels.push_back({{"lx", x}, {"ly", y}, {"lz", z}, {"id", id}});
      }
    }
  }
  data["voxels"] = voxels;
  const std::string text = data.dump();
  out.bytes.assign(text.begin(), text.end());
  return out;
}

ChunkBuffer JsonChunkSerializer::Deserialize(const std::vector<uint8_t> &bytes,
                                             glm::ivec3 chunkCoord,
                                             UBlockRegistry &registry) const
{
  ChunkBuffer buffer;
  if (bytes.empty())
  {
    return buffer;
  }
  try
  {
    const std::string text(bytes.begin(), bytes.end());
    const json data = json::parse(text);
    for (const auto &voxel : data.at("voxels"))
    {
      const int lx = voxel.at("lx").get<int>();
      const int ly = voxel.at("ly").get<int>();
      const int lz = voxel.at("lz").get<int>();
      const BlockId id = ResolveVoxelId(voxel, registry);
      if (id == BLOCK_AIR)
      {
        continue;
      }
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

} // namespace cutum
