#include "Blocks/BlockDefinitionStorage.h"
#include "Blocks/BlockRegistry.h"
#include "World/Chunks/Chunk.h"
#include "World/Core/BlockWorld.h"
#include "World/Lighting/ChunkLighting.h"

#include <cstdlib>
#include <iostream>
#include <memory>
#include <unordered_map>

static void Expect(bool cond, const char *message)
{
  if (!cond)
  {
    std::cerr << "chunk_bulk_blocklight_test: " << message << std::endl;
    std::exit(1);
  }
}

static std::shared_ptr<cutum::UBlockRegistry> MakeRegistry()
{
  constexpr cutum::BlockId kStone = 8;
  constexpr cutum::BlockId kTorch = 50;
  auto definitions = std::make_shared<cutum::UBlockDefinitionStorage>();
  cutum::BlockDefinition stone;
  stone.Name = "stone";
  stone.Id = kStone;
  stone.Physics = cutum::BlockPhysicsProfile::Solid();
  cutum::BlockDefinition torch;
  torch.Name = "torch";
  torch.Id = kTorch;
  torch.Physics = cutum::BlockPhysicsProfile::Solid();
  torch.Render.Style = cutum::BlockRenderStyle::Cross;
  torch.Lighting.Emission = 14;
  std::unordered_map<cutum::BlockId, cutum::BlockDefinition> by_id;
  by_id[kStone] = stone;
  by_id[kTorch] = torch;
  std::unordered_map<std::string, cutum::BlockId> name_to_id;
  name_to_id["stone"] = kStone;
  name_to_id["torch"] = kTorch;
  definitions->ReplaceAll(std::move(by_id), std::move(name_to_id));
  return std::make_shared<cutum::UBlockRegistry>(nullptr, definitions);
}

static int BlockAt(const cutum::UBlockWorld &world, glm::ivec3 pos)
{
  const glm::ivec3 chunk_coord = cutum::UChunkManager::WorldToChunk(pos);
  const cutum::UChunk *chunk = world.GetChunkManager().GetChunk(chunk_coord);
  if (!chunk)
  {
    return -1;
  }
  return chunk->GetBlockLightLocal(cutum::UChunkManager::WorldToLocal(pos));
}

int main()
{
  constexpr cutum::BlockId kStone = 8;
  constexpr cutum::BlockId kTorch = 50;
  auto registry = MakeRegistry();
  cutum::UBlockWorld world;
  const glm::ivec3 chunk_coord(0, 0, 0);

  for (int x = 0; x < cutum::CHUNK_SIZE; ++x)
  {
    for (int z = 0; z < cutum::CHUNK_SIZE; ++z)
    {
      world.SetBlock(glm::ivec3(x, 0, z), kStone);
    }
  }
  world.SetBlock(glm::ivec3(8, 5, 8), kTorch);

  cutum::RelightChunk(world, *registry, chunk_coord, false, true);
  Expect(BlockAt(world, glm::ivec3(8, 5, 9)) == 0,
         "block light absent before bulk emissive relight");

  cutum::RelightChunkBlockLight(world, *registry, chunk_coord);
  Expect(BlockAt(world, glm::ivec3(8, 5, 8)) >= 13,
         "torch emission after bulk block-light relight");
  Expect(BlockAt(world, glm::ivec3(8, 5, 9)) > 0,
         "torch spread after bulk block-light relight");

  std::cout << "chunk_bulk_blocklight_test: OK" << std::endl;
  return 0;
}
