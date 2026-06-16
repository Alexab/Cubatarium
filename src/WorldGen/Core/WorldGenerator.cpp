#include "WorldGen/Core/WorldGenerator.h"
#include "Blocks/BlockRegistry.h"
#include "World/Chunks/Chunk.h"
#include "World/Core/BlockWorld.h"
#include "WorldGen/Core/Noise.h"
#include <iostream>

namespace cutum
{

int UWorldGenerator::SurfaceYAt(int x, int z, uint32_t Seed, int baseY,
                                int MaxHeight)
{
  return HeightAt(x, z, Seed, baseY, MaxHeight);
}

void UWorldGenerator::GenerateColumn(UBlockWorld &world,
                                     UBlockRegistry &registry, int x, int z,
                                     uint32_t Seed, int baseY, int MaxHeight)
{
  const BlockId bedrock = registry.GetIdByTypeName("bedrock");
  const BlockId stone = registry.GetIdByTypeName("stone");
  const BlockId dirt = registry.GetIdByTypeName("dirt");
  const BlockId grass = registry.GetIdByTypeName("grass");

  if (bedrock == BLOCK_AIR || stone == BLOCK_AIR || grass == BLOCK_AIR)
  {
    std::cerr << "UWorldGenerator::GenerateColumn: missing block Types"
              << std::endl;
    return;
  }

  const BlockId dirtOrStone = (dirt != BLOCK_AIR) ? dirt : stone;
  const int surfaceY = SurfaceYAt(x, z, Seed, baseY, MaxHeight);

  for (int y = 0; y <= surfaceY; ++y)
  {
    BlockId Id = stone;
    if (y == 0)
    {
      Id = bedrock;
    }
    else if (y < surfaceY - 1)
    {
      Id = stone;
    }
    else if (y == surfaceY - 1)
    {
      Id = dirtOrStone;
    }
    else if (y == surfaceY)
    {
      Id = grass;
    }
    world.SetBlock(glm::ivec3(x, y, z), Id);
  }
}

void UWorldGenerator::GenerateHeightmap(UBlockWorld &world,
                                        UBlockRegistry &registry,
                                        int halfExtent, uint32_t Seed,
                                        int baseY, int MaxHeight)
{
  for (int x = -halfExtent; x <= halfExtent; ++x)
  {
    for (int z = -halfExtent; z <= halfExtent; ++z)
    {
      GenerateColumn(world, registry, x, z, Seed, baseY, MaxHeight);
    }
  }
}

void UWorldGenerator::GenerateSpawnArea(UBlockWorld &world,
                                        UBlockRegistry &registry, int centerX,
                                        int centerZ, int radiusChunks,
                                        uint32_t Seed, int baseY, int MaxHeight)
{
  const int halfBlocks = radiusChunks * CHUNK_SIZE;
  for (int x = centerX - halfBlocks; x <= centerX + halfBlocks; ++x)
  {
    for (int z = centerZ - halfBlocks; z <= centerZ + halfBlocks; ++z)
    {
      GenerateColumn(world, registry, x, z, Seed, baseY, MaxHeight);
    }
  }
}

glm::vec3 UWorldGenerator::DefaultSpawnPosition(int x, int z, uint32_t Seed,
                                                int baseY, int MaxHeight,
                                                float eyeHeight)
{
  const int sy = SurfaceYAt(x, z, Seed, baseY, MaxHeight);
  return glm::vec3(static_cast<float>(x),
                   static_cast<float>(sy) + eyeHeight + 0.5f,
                   static_cast<float>(z));
}

void UWorldGenerator::GenerateFlatColumn(UBlockWorld &world,
                                         UBlockRegistry &registry, int x, int z,
                                         int surfaceY)
{
  const BlockId bedrock = registry.GetIdByTypeName("bedrock");
  const BlockId stone = registry.GetIdByTypeName("stone");
  const BlockId grass = registry.GetIdByTypeName("grass");

  if (bedrock == BLOCK_AIR || stone == BLOCK_AIR || grass == BLOCK_AIR)
  {
    return;
  }

  world.SetBlock(glm::ivec3(x, 0, z), bedrock);
  world.SetBlock(glm::ivec3(x, 1, z), stone);
  world.SetBlock(glm::ivec3(x, 2, z), stone);
  world.SetBlock(glm::ivec3(x, surfaceY, z), grass);
}

void UWorldGenerator::GenerateFlatArea(UBlockWorld &world,
                                       UBlockRegistry &registry, int centerX,
                                       int centerZ, int radiusChunks,
                                       int surfaceY)
{
  const int halfBlocks = radiusChunks * CHUNK_SIZE;
  for (int x = centerX - halfBlocks; x <= centerX + halfBlocks; ++x)
  {
    for (int z = centerZ - halfBlocks; z <= centerZ + halfBlocks; ++z)
    {
      GenerateFlatColumn(world, registry, x, z, surfaceY);
    }
  }
}

void UWorldGenerator::GenerateFlat(UBlockWorld &world, UBlockRegistry &registry,
                                   int halfExtent, int surfaceY)
{
  for (int x = -halfExtent; x <= halfExtent; ++x)
  {
    for (int z = -halfExtent; z <= halfExtent; ++z)
    {
      GenerateFlatColumn(world, registry, x, z, surfaceY);
    }
  }
}

} // namespace cutum
