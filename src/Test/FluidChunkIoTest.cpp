#include "Blocks/BlockDefinitionStorage.h"
#include "Blocks/BlockRegistry.h"
#include "World/Chunks/Chunk.h"
#include "World/IO/BinaryChunkSerializer.h"
#include "World/Math/FluidCellState.h"

#include <cstdlib>
#include <iostream>
#include <memory>
#include <unordered_map>

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

  const cutum::SerializedChunk serialized =
      serializer.Serialize(glm::ivec3(0, 0, 0), chunk, registry);
  const cutum::UChunkBuffer buffer =
      serializer.Deserialize(serialized.bytes, glm::ivec3(0, 0, 0), registry);
  const glm::ivec3 world_pos(2, 3, 4);
  Expect(buffer.GetBlock(world_pos) == kWater, "v2 round-trip block");
  Expect(cutum::UnpackFluidCellState(buffer.GetFluidPacked(world_pos)).Level == 3,
         "v2 round-trip fluid level");

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

  std::cout << "fluid_chunk_io_test: OK" << std::endl;
  return 0;
}
