#include "Blocks/BlockDefinitionStorage.h"
#include "Blocks/BlockRegistry.h"
#include "World/Chunks/Chunk.h"
#include "World/Core/BlockWorld.h"
#include "World/IO/BinaryChunkSerializer.h"
#include "World/IO/JsonChunkSerializer.h"
#include "World/Math/FluidCellState.h"

#include <cmath>
#include <cstdlib>
#include <iostream>
#include <memory>
#include <unordered_map>
#include <vector>

static void Expect(bool cond, const char *message)
{
  if (!cond)
  {
    std::cerr << "fluid_chunk_io_test: " << message << std::endl;
    std::exit(1);
  }
}

static std::shared_ptr<cutum::UBlockDefinitionStorage> MakeDefinitions()
{
  auto definitions = std::make_shared<cutum::UBlockDefinitionStorage>();
  cutum::BlockDefinition water;
  water.Name = "water";
  water.Physics = cutum::BlockPhysicsProfile::FromPreset("water");
  cutum::BlockDefinition stone;
  stone.Name = "stone";
  stone.Physics = cutum::BlockPhysicsProfile::Solid();
  std::unordered_map<cutum::BlockId, cutum::BlockDefinition> by_id;
  constexpr cutum::BlockId kWater = 9;
  constexpr cutum::BlockId kStone = 8;
  by_id[kWater] = water;
  by_id[kStone] = stone;
  std::unordered_map<std::string, cutum::BlockId> name_to_id;
  name_to_id["water"] = kWater;
  name_to_id["stone"] = kStone;
  definitions->ReplaceAll(std::move(by_id), std::move(name_to_id));
  return definitions;
}

int main()
{
  const auto definitions = MakeDefinitions();
  cutum::UBlockRegistry registry(nullptr, definitions);
  cutum::UBinaryChunkSerializer serializer;

  cutum::UChunk chunk(glm::ivec3(0, 0, 0));
  constexpr cutum::BlockId kWater = 9;
  chunk.SetBlockLocal(glm::ivec3(2, 3, 4), kWater);
  chunk.SetFluidLocal(glm::ivec3(2, 3, 4), cutum::FluidCellState::Flowing(3));
  chunk.SetWeatherState(0.73f, 135.25);

  const cutum::SerializedChunk serialized =
      serializer.Serialize(glm::ivec3(0, 0, 0), chunk, registry);
  const cutum::UChunkBuffer buffer =
      serializer.Deserialize(serialized.bytes, glm::ivec3(0, 0, 0), registry);
  const glm::ivec3 world_pos(2, 3, 4);
  Expect(buffer.GetBlock(world_pos) == kWater, "v4 round-trip block");
  Expect(cutum::UnpackFluidCellState(buffer.GetFluidPacked(world_pos)).Level ==
             3,
         "v4 round-trip fluid level");
  cutum::UBlockWorld apply_weather_world;
  buffer.ApplyTo(apply_weather_world);
  const cutum::UChunk *weather_chunk =
      apply_weather_world.GetChunkManager().GetChunk(glm::ivec3(0, 0, 0));
  Expect(weather_chunk &&
             std::abs(weather_chunk->GetSurfaceWetness() - 0.73f) < 0.001f &&
             std::abs(weather_chunk->GetWeatherLastUpdateSec() - 135.25) <
                 0.001,
         "v4 round-trip chunk weather state");

  chunk.SetLightLocal(glm::ivec3(2, 3, 4), 12, 5);
  const cutum::SerializedChunk lit_serialized =
      serializer.Serialize(glm::ivec3(0, 0, 0), chunk, registry);
  Expect(lit_serialized.bytes.size() > 4 &&
             lit_serialized.bytes[4] == cutum::UBinaryChunkSerializer::kVersion,
         "v4 weather/light serialize version");
  const cutum::UChunkBuffer lit_buffer = serializer.Deserialize(
      lit_serialized.bytes, glm::ivec3(0, 0, 0), registry);
  Expect(lit_buffer.HasChunkLightData(), "v3 light round-trip has light");
  Expect(lit_buffer.GetLightPacked(world_pos) ==
             chunk.GetLightPackedLocal(glm::ivec3(2, 3, 4)),
         "v4 light round-trip packed value");

  std::vector<uint8_t> version3_bytes = lit_serialized.bytes;
  version3_bytes.resize(version3_bytes.size() - 10);
  version3_bytes[4] = cutum::UBinaryChunkSerializer::kVersionLight;
  const cutum::UChunkBuffer version3_buffer =
      serializer.Deserialize(version3_bytes, glm::ivec3(0, 0, 0), registry);
  Expect(version3_buffer.HasChunkLightData(),
         "v3 legacy light remains readable");
  cutum::UBlockWorld version3_apply;
  version3_buffer.ApplyTo(version3_apply);
  const cutum::UChunk *version3_chunk =
      version3_apply.GetChunkManager().GetChunk(glm::ivec3(0, 0, 0));
  Expect(version3_chunk && version3_chunk->GetSurfaceWetness() == 0.0f,
         "v3 chunks safely default weather state");

  cutum::UBlockWorld apply_world;
  lit_buffer.ApplyTo(apply_world);
  const cutum::UChunk *applied =
      apply_world.GetChunkManager().GetChunk(glm::ivec3(0, 0, 0));
  Expect(applied != nullptr, "v4 light apply creates chunk");
  Expect(applied->GetLightPackedLocal(glm::ivec3(2, 3, 4)) ==
             chunk.GetLightPackedLocal(glm::ivec3(2, 3, 4)),
         "v4 light apply matches");

  cutum::UChunk legacy_chunk(glm::ivec3(1, 0, 0));
  legacy_chunk.SetBlockLocal(glm::ivec3(0, 0, 0), kWater);
  const cutum::SerializedChunk legacy_serialized =
      serializer.Serialize(glm::ivec3(1, 0, 0), legacy_chunk, registry);
  std::vector<uint8_t> legacy_bytes = legacy_serialized.bytes;
  legacy_bytes[4] = cutum::UBinaryChunkSerializer::kVersionLegacy;
  const cutum::UChunkBuffer legacy_buffer =
      serializer.Deserialize(legacy_bytes, glm::ivec3(1, 0, 0), registry);
  const glm::ivec3 legacy_pos(cutum::CHUNK_SIZE, 0, 0);
  Expect(legacy_buffer.GetBlock(legacy_pos) == kWater, "v1 legacy block");
  Expect(cutum::UnpackFluidCellState(legacy_buffer.GetFluidPacked(legacy_pos))
             .IsSource(),
         "v1 legacy liquid becomes source");
  Expect(!legacy_buffer.HasChunkLightData(), "v1 legacy has no light section");

  cutum::UJsonChunkSerializer json_serializer;
  const cutum::SerializedChunk json_serialized =
      json_serializer.Serialize(glm::ivec3(0, 0, 0), chunk, registry);
  const cutum::UChunkBuffer json_buffer = json_serializer.Deserialize(
      json_serialized.bytes, glm::ivec3(0, 0, 0), registry);
  cutum::UBlockWorld json_apply;
  json_buffer.ApplyTo(json_apply);
  const cutum::UChunk *json_chunk =
      json_apply.GetChunkManager().GetChunk(glm::ivec3(0, 0, 0));
  Expect(json_chunk &&
             std::abs(json_chunk->GetSurfaceWetness() - 0.73f) < 0.001f &&
             std::abs(json_chunk->GetWeatherLastUpdateSec() - 135.25) < 0.001,
         "JSON chunk weather state round-trip");
  const std::string old_json =
      R"({"format_version":3,"voxels":[{"lx":0,"ly":0,"lz":0,"id":8}]})";
  const std::vector<uint8_t> old_json_bytes(old_json.begin(), old_json.end());
  const cutum::UChunkBuffer old_json_buffer = json_serializer.Deserialize(
      old_json_bytes, glm::ivec3(2, 0, 0), registry);
  cutum::UBlockWorld old_json_apply;
  old_json_buffer.ApplyTo(old_json_apply);
  const cutum::UChunk *old_json_chunk =
      old_json_apply.GetChunkManager().GetChunk(glm::ivec3(2, 0, 0));
  Expect(old_json_chunk && old_json_chunk->GetSurfaceWetness() == 0.0f,
         "older JSON chunks default weather state to dry");

  std::cout << "fluid_chunk_io_test: OK" << std::endl;
  return 0;
}
