#include "Blocks/BlockDefinitionStorage.h"
#include "Blocks/BlockRegistry.h"
#include "World/Chunks/Chunk.h"
#include "World/Core/BlockWorld.h"
#include "World/Lighting/ChunkLighting.h"
#include "World/Lighting/LightUtil.h"

#include <cstdlib>
#include <iostream>
#include <memory>
#include <unordered_map>

static void Expect(bool cond, const char *message)
{
  if (!cond)
  {
    std::cerr << "chunk_lighting_propagation_test: " << message << std::endl;
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
  std::unordered_map<cutum::BlockId, cutum::BlockDefinition> by_id;
  by_id[kStone] = stone;
  by_id[kTorch] = torch;
  std::unordered_map<std::string, cutum::BlockId> name_to_id;
  name_to_id["stone"] = kStone;
  name_to_id["torch"] = kTorch;
  definitions->ReplaceAll(std::move(by_id), std::move(name_to_id));
  return std::make_shared<cutum::UBlockRegistry>(nullptr, definitions);
}

static int SkyAt(const cutum::UBlockWorld &world, glm::ivec3 pos)
{
  const glm::ivec3 chunk_coord = cutum::UChunkManager::WorldToChunk(pos);
  const cutum::UChunk *chunk = world.GetChunkManager().GetChunk(chunk_coord);
  if (!chunk)
  {
    return -1;
  }
  return chunk->GetSkyLightLocal(
      cutum::UChunkManager::WorldToLocal(pos));
}

static int BlockAt(const cutum::UBlockWorld &world, glm::ivec3 pos)
{
  const glm::ivec3 chunk_coord = cutum::UChunkManager::WorldToChunk(pos);
  const cutum::UChunk *chunk = world.GetChunkManager().GetChunk(chunk_coord);
  if (!chunk)
  {
    return -1;
  }
  return chunk->GetBlockLightLocal(
      cutum::UChunkManager::WorldToLocal(pos));
}

static void BuildEnclosedRoom(cutum::UBlockWorld &world, cutum::BlockId stone,
                              int floor_y)
{
  for (int x = 4; x <= 11; ++x)
  {
    for (int z = 4; z <= 11; ++z)
    {
      world.SetBlock(glm::ivec3(x, floor_y, z), stone);
    }
  }
  for (int x = 4; x <= 11; ++x)
  {
    for (int y = floor_y + 1; y <= 15; ++y)
    {
      world.SetBlock(glm::ivec3(x, y, 4), stone);
      world.SetBlock(glm::ivec3(x, y, 11), stone);
    }
  }
  for (int z = 4; z <= 11; ++z)
  {
    for (int y = floor_y + 1; y <= 15; ++y)
    {
      world.SetBlock(glm::ivec3(4, y, z), stone);
      world.SetBlock(glm::ivec3(11, y, z), stone);
    }
  }
}

int main()
{
  constexpr cutum::BlockId kStone = 8;
  constexpr cutum::BlockId kTorch = 50;
  constexpr int kMaxWorldY = cutum::CHUNK_SIZE - 1;
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

  cutum::RelightChunk(world, *registry, chunk_coord);
  Expect(SkyAt(world, glm::ivec3(8, 15, 8)) == cutum::kMaxLightLevel,
         "open sky at chunk top");

  for (int x = 4; x <= 11; ++x)
  {
    for (int z = 4; z <= 11; ++z)
    {
      world.SetBlock(glm::ivec3(x, 8, z), kStone);
    }
  }
  for (int x = 4; x <= 11; ++x)
  {
    for (int y = 1; y <= 15; ++y)
    {
      world.SetBlock(glm::ivec3(x, y, 4), kStone);
      world.SetBlock(glm::ivec3(x, y, 11), kStone);
    }
  }
  for (int z = 4; z <= 11; ++z)
  {
    for (int y = 1; y <= 15; ++y)
    {
      world.SetBlock(glm::ivec3(4, y, z), kStone);
      world.SetBlock(glm::ivec3(11, y, z), kStone);
    }
  }

  cutum::RelightChunk(world, *registry, chunk_coord);
  Expect(SkyAt(world, glm::ivec3(8, 5, 8)) == 0, "enclosed room skylight");

  world.SetBlock(glm::ivec3(8, 5, 8), kTorch);
  cutum::RelightChunksAround(world, *registry, glm::ivec3(8, 5, 8), kMaxWorldY);
  Expect(BlockAt(world, glm::ivec3(8, 5, 8)) >= 13, "torch emission");
  Expect(BlockAt(world, glm::ivec3(8, 5, 9)) > 0, "torch blocklight spread");

  world.SetBlock(glm::ivec3(8, 5, 8), cutum::BLOCK_AIR);
  cutum::RelightChunksAround(world, *registry, glm::ivec3(8, 5, 8), kMaxWorldY);
  Expect(BlockAt(world, glm::ivec3(8, 5, 9)) == 0,
         "torch blocklight removed after break");

  for (int x = 0; x < cutum::CHUNK_SIZE; ++x)
  {
    for (int z = 0; z < cutum::CHUNK_SIZE; ++z)
    {
      world.SetBlock(glm::ivec3(x, 0, z), kStone);
    }
  }
  for (int y = 1; y <= 15; ++y)
  {
    world.SetBlock(glm::ivec3(8, y, 8), cutum::BLOCK_AIR);
    world.SetBlock(glm::ivec3(7, y, 8), kStone);
    world.SetBlock(glm::ivec3(9, y, 8), kStone);
    world.SetBlock(glm::ivec3(8, y, 7), kStone);
    world.SetBlock(glm::ivec3(8, y, 9), kStone);
  }
  cutum::RelightChunk(world, *registry, chunk_coord);
  Expect(SkyAt(world, glm::ivec3(8, 1, 8)) > 0, "skylight enters open shaft");
  world.SetBlock(glm::ivec3(8, 2, 8), kStone);
  cutum::RelightChunksAround(world, *registry, glm::ivec3(8, 2, 8), kMaxWorldY);
  Expect(SkyAt(world, glm::ivec3(8, 1, 8)) == 0,
         "skylight blocked by second shaft block");
  world.SetBlock(glm::ivec3(8, 2, 8), cutum::BLOCK_AIR);
  cutum::RelightChunksAround(world, *registry, glm::ivec3(8, 2, 8), kMaxWorldY);
  Expect(SkyAt(world, glm::ivec3(8, 1, 8)) > 0,
         "skylight restored after shaft reopen");

  for (int x = 0; x < cutum::CHUNK_SIZE; ++x)
  {
    for (int z = 0; z < cutum::CHUNK_SIZE; ++z)
    {
      world.SetBlock(glm::ivec3(x, 0, z), kStone);
    }
  }
  BuildEnclosedRoom(world, kStone, 5);
  for (int y = 6; y <= 15; ++y)
  {
    world.SetBlock(glm::ivec3(8, y, 8), kStone);
    world.SetBlock(glm::ivec3(7, y, 8), kStone);
    world.SetBlock(glm::ivec3(9, y, 8), kStone);
    world.SetBlock(glm::ivec3(8, y, 7), kStone);
    world.SetBlock(glm::ivec3(8, y, 9), kStone);
  }
  cutum::RelightChunk(world, *registry, chunk_coord);
  Expect(SkyAt(world, glm::ivec3(8, 6, 8)) == 0,
         "deep room dark before shaft opens");
  for (int y = 6; y <= 15; ++y)
  {
    world.SetBlock(glm::ivec3(8, y, 8), cutum::BLOCK_AIR);
  }
  cutum::RelightBlocksAroundAll(world, *registry, {glm::ivec3(8, 15, 8)},
                                kMaxWorldY);
  Expect(SkyAt(world, glm::ivec3(8, 6, 8)) > 0,
         "deep shaft to surface relights room");

  for (int x = 0; x < cutum::CHUNK_SIZE; ++x)
  {
    for (int z = 0; z < cutum::CHUNK_SIZE; ++z)
    {
      world.SetBlock(glm::ivec3(x, 0, z), kStone);
    }
  }
  for (int y = 6; y <= 15; ++y)
  {
    world.SetBlock(glm::ivec3(8, y, 8), kStone);
    world.SetBlock(glm::ivec3(7, y, 8), kStone);
    world.SetBlock(glm::ivec3(9, y, 8), kStone);
    world.SetBlock(glm::ivec3(8, y, 7), kStone);
    world.SetBlock(glm::ivec3(8, y, 9), kStone);
  }
  world.SetBlock(glm::ivec3(8, 5, 8), kStone);
  for (int x = 8; x <= 17; ++x)
  {
    world.SetBlock(glm::ivec3(x, 5, 8), kStone);
    world.SetBlock(glm::ivec3(x, 6, 8), kStone);
    world.SetBlock(glm::ivec3(x, 4, 8), kStone);
    world.SetBlock(glm::ivec3(x, 5, 7), kStone);
    world.SetBlock(glm::ivec3(x, 5, 9), kStone);
  }
  for (int x = 11; x <= 13; ++x)
  {
    for (int z = 7; z <= 9; ++z)
    {
      world.SetBlock(glm::ivec3(x, 4, z), kStone);
    }
  }
  for (int x = 11; x <= 13; ++x)
  {
    for (int z = 7; z <= 9; ++z)
    {
      for (int y = 6; y <= 8; ++y)
      {
        if (x == 11 || x == 13 || z == 7 || z == 9)
        {
          world.SetBlock(glm::ivec3(x, y, z), kStone);
        }
      }
    }
  }
  for (int x = 11; x <= 13; ++x)
  {
    for (int z = 7; z <= 9; ++z)
    {
      world.SetBlock(glm::ivec3(x, 9, z), kStone);
    }
  }
  cutum::RelightChunk(world, *registry, chunk_coord);
  Expect(SkyAt(world, glm::ivec3(12, 5, 8)) == 0,
         "offset cave dark before shaft opens");
  for (int y = 6; y <= 15; ++y)
  {
    world.SetBlock(glm::ivec3(8, y, 8), cutum::BLOCK_AIR);
  }
  world.SetBlock(glm::ivec3(8, 5, 8), cutum::BLOCK_AIR);
  for (int x = 9; x <= 16; ++x)
  {
    world.SetBlock(glm::ivec3(x, 5, 8), cutum::BLOCK_AIR);
  }
  cutum::RelightBlocksAroundAll(world, *registry, {glm::ivec3(8, 15, 8)},
                                kMaxWorldY);
  Expect(SkyAt(world, glm::ivec3(12, 5, 8)) > 0,
         "offset cave lit after shaft and tunnel open");

  std::cout << "chunk_lighting_propagation_test: OK" << std::endl;
  return 0;
}
