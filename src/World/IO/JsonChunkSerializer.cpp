#include "World/IO/JsonChunkSerializer.h"
#include "Core/Memory/StlSourceStorage.h"
#include "Blocks/BlockRegistry.h"
#include "World/Chunks/Chunk.h"
#include "World/Math/FluidCellState.h"
#include <algorithm>
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

SerializedChunk UJsonChunkSerializer::Serialize(glm::ivec3 chunkCoord,
                                                const UChunk &chunk,
                                                UBlockRegistry &registry,
                                                const IoCodecProbe *probe) const
{
  SerializedChunk out;
  out.format = ChunkDiskFormat::Json;

  json data;
  data["format_version"] = 4;
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
        json voxel = {{"lx", x}, {"ly", y}, {"lz", z}, {"id", id}};
        const uint8_t fluid_packed =
            PackFluidCellState(chunk.GetFluidLocal(local));
        if (fluid_packed != 0)
        {
          voxel["fluid"] = fluid_packed;
        }
        voxels.push_back(std::move(voxel));
      }
    }
  }
  data["voxels"] = voxels;
  data["weather_state"] = {
      {"surface_wetness", std::clamp(chunk.GetSurfaceWetness(), 0.0f, 1.0f)},
      {"last_update_sec", std::max(0.0, chunk.GetWeatherLastUpdateSec())}};
  const auto text = RunIoCodecWindow(probe, IoCodecWindow::JsonDump, [&] { return data.dump(); });
  out.bytes.assign(text.begin(), text.end());
  if (probe)
  {
    StlSourceStorage scratch, output;
    scratch.JsonTree(voxels);
    scratch.JsonTree(data);
    scratch.Text(text);
    output.Buffer(out.bytes.capacity(), sizeof(uint8_t));
    probe->sample(probe->context, {IoCodecKind::JsonWrite, scratch.bytes.requested,
                                  0, output.bytes.requested, 0,
                                  scratch.bytes.supported && output.bytes.supported});
  }
  return out;
}

UChunkBuffer
UJsonChunkSerializer::Deserialize(const std::vector<uint8_t> &bytes,
                                  glm::ivec3 chunkCoord,
                                  UBlockRegistry &registry,
                                  const IoCodecProbe *probe) const
{
  UChunkBuffer buffer;
  if (bytes.empty())
  {
    return buffer;
  }
  try
  {
    const std::string text(bytes.begin(), bytes.end());
    const auto data = RunIoCodecWindow(probe, IoCodecWindow::JsonParse, [&] { return json::parse(text); });
    bool frame_valid = data.is_object() && data.contains("voxels") &&
                       data.at("voxels").is_array();
    if (data.contains("format_version"))
      frame_valid &= data.at("format_version").is_number_integer() &&
                     data.at("format_version") >= 1 &&
                     data.at("format_version") <= 4;
    const char *keys[] = {"cx", "cy", "cz"};
    const bool has_coord =
        data.contains("cx") || data.contains("cy") || data.contains("cz");
    if (has_coord)
      for (int i = 0; i < 3; ++i)
        frame_valid &= data.contains(keys[i]) &&
                       data.at(keys[i]).is_number_integer() &&
                       data.at(keys[i]) == chunkCoord[i];
    if (data.contains("weather_state") && data["weather_state"].is_object())
    {
      const json &weather = data["weather_state"];
      buffer.SetChunkWeatherState(chunkCoord,
                                  weather.value("surface_wetness", 0.0f),
                                  weather.value("last_update_sec", 0.0));
    }
    if (data.contains("weather_state"))
    {
      const auto &weather = data.at("weather_state");
      frame_valid &= weather.is_object();
      if (weather.is_object())
        for (const char *key : {"surface_wetness", "last_update_sec"})
          if (weather.contains(key))
            frame_valid &= weather.at(key).is_number();
    }
    for (const auto &voxel : data.at("voxels"))
    {
      const int lx = voxel.at("lx").get<int>();
      const int ly = voxel.at("ly").get<int>();
      const int lz = voxel.at("lz").get<int>();
      frame_valid &= voxel.at("lx").is_number_integer() &&
                     voxel.at("ly").is_number_integer() &&
                     voxel.at("lz").is_number_integer() && lx >= 0 &&
                     lx < CHUNK_SIZE && ly >= 0 && ly < CHUNK_SIZE && lz >= 0 &&
                     lz < CHUNK_SIZE;
      if (voxel.contains("id"))
        frame_valid &= voxel.at("id").is_number_integer() &&
                       voxel.at("id") >= 0 && voxel.at("id") <= 65535;
      else
        frame_valid &= voxel.contains("type") && voxel.at("type").is_string();
      if (voxel.contains("fluid"))
        frame_valid &=
            voxel.at("fluid").is_number_integer() && voxel.at("fluid") == 0;
      const BlockId id = ResolveVoxelId(voxel, registry);
      if (!voxel.contains("id") && id == BLOCK_AIR && voxel.contains("type"))
        frame_valid &= voxel.at("type") == "air";
      if (id == BLOCK_AIR)
      {
        continue;
      }
      const glm::ivec3 worldPos(chunkCoord.x * CHUNK_SIZE + lx,
                                chunkCoord.y * CHUNK_SIZE + ly,
                                chunkCoord.z * CHUNK_SIZE + lz);
      buffer.SetBlock(worldPos, id);
      if (voxel.contains("fluid"))
      {
        buffer.SetFluidPacked(
            worldPos, static_cast<uint8_t>(voxel.at("fluid").get<int>()));
      }
      else if (registry.IsLiquid(id))
      {
        buffer.SetFluidPacked(worldPos,
                              PackFluidCellState(FluidCellState::Source()));
      }
    }
    if (frame_valid)
      buffer.MarkDecodedFrame(chunkCoord);
    if (probe)
    {
      StlSourceStorage scratch, input;
      scratch.Text(text);
      scratch.JsonTree(data);
      input.Buffer(bytes.capacity(), sizeof(uint8_t));
      probe->sample(probe->context, {IoCodecKind::JsonRead, scratch.bytes.requested,
                                    input.bytes.requested, buffer.GetOwnedAllocationBytes(), 0,
                                    scratch.bytes.supported && input.bytes.supported});
    }
  }
  catch (const json::exception &)
  {
    buffer.Clear();
  }
  return buffer;
}

} // namespace cutum
