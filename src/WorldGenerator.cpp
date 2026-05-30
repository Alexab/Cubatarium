#include "WorldGenerator.h"
#include "BlockRegistry.h"
#include "BlockWorld.h"
#include "Noise.h"
#include "Chunk.h"
#include <iostream>

namespace cutum {

int WorldGenerator::SurfaceYAt(int x, int z, uint32_t seed, int baseY, int maxHeight)
{
 return HeightAt(x, z, seed, baseY, maxHeight);
}

void WorldGenerator::GenerateColumn(BlockWorld& world, BlockRegistry& registry,
    int x, int z, uint32_t seed, int baseY, int maxHeight)
{
 const BlockId bedrock = registry.GetIdByTypeName("bedrock");
 const BlockId stone = registry.GetIdByTypeName("stone");
 const BlockId dirt = registry.GetIdByTypeName("dirt");
 const BlockId grass = registry.GetIdByTypeName("grass");

 if (bedrock == BLOCK_AIR || stone == BLOCK_AIR || grass == BLOCK_AIR) {
  std::cerr << "WorldGenerator::GenerateColumn: missing block types" << std::endl;
  return;
 }

 const BlockId dirtOrStone = (dirt != BLOCK_AIR) ? dirt : stone;
 const int surfaceY = SurfaceYAt(x, z, seed, baseY, maxHeight);

 for (int y = 0; y <= surfaceY; ++y) {
  BlockId id = stone;
  if (y == 0) {
   id = bedrock;
  } else if (y < surfaceY - 1) {
   id = stone;
  } else if (y == surfaceY - 1) {
   id = dirtOrStone;
  } else if (y == surfaceY) {
   id = grass;
  }
  world.SetBlock(glm::ivec3(x, y, z), id);
 }
}

void WorldGenerator::GenerateHeightmap(BlockWorld& world, BlockRegistry& registry,
    int halfExtent, uint32_t seed, int baseY, int maxHeight)
{
 for (int x = -halfExtent; x <= halfExtent; ++x) {
  for (int z = -halfExtent; z <= halfExtent; ++z) {
   GenerateColumn(world, registry, x, z, seed, baseY, maxHeight);
  }
 }
}

void WorldGenerator::GenerateSpawnArea(BlockWorld& world, BlockRegistry& registry,
    int centerX, int centerZ, int radiusChunks, uint32_t seed, int baseY, int maxHeight)
{
 const int halfBlocks = radiusChunks * CHUNK_SIZE;
 for (int x = centerX - halfBlocks; x <= centerX + halfBlocks; ++x) {
  for (int z = centerZ - halfBlocks; z <= centerZ + halfBlocks; ++z) {
   GenerateColumn(world, registry, x, z, seed, baseY, maxHeight);
  }
 }
}

glm::vec3 WorldGenerator::DefaultSpawnPosition(int x, int z, uint32_t seed,
    int baseY, int maxHeight, float eyeHeight)
{
 const int sy = SurfaceYAt(x, z, seed, baseY, maxHeight);
 return glm::vec3(static_cast<float>(x),
                  static_cast<float>(sy) + eyeHeight + 0.5f,
                  static_cast<float>(z));
}

void WorldGenerator::GenerateFlatColumn(BlockWorld& world, BlockRegistry& registry, int x, int z,
    int surfaceY)
{
 const BlockId bedrock = registry.GetIdByTypeName("bedrock");
 const BlockId stone = registry.GetIdByTypeName("stone");
 const BlockId grass = registry.GetIdByTypeName("grass");

 if (bedrock == BLOCK_AIR || stone == BLOCK_AIR || grass == BLOCK_AIR) {
  return;
 }

 world.SetBlock(glm::ivec3(x, 0, z), bedrock);
 world.SetBlock(glm::ivec3(x, 1, z), stone);
 world.SetBlock(glm::ivec3(x, 2, z), stone);
 world.SetBlock(glm::ivec3(x, surfaceY, z), grass);
}

void WorldGenerator::GenerateFlatArea(BlockWorld& world, BlockRegistry& registry, int centerX,
    int centerZ, int radiusChunks, int surfaceY)
{
 const int halfBlocks = radiusChunks * CHUNK_SIZE;
 for (int x = centerX - halfBlocks; x <= centerX + halfBlocks; ++x) {
  for (int z = centerZ - halfBlocks; z <= centerZ + halfBlocks; ++z) {
   GenerateFlatColumn(world, registry, x, z, surfaceY);
  }
 }
}

void WorldGenerator::GenerateFlat(BlockWorld& world, BlockRegistry& registry, int halfExtent, int surfaceY)
{
 for (int x = -halfExtent; x <= halfExtent; ++x) {
  for (int z = -halfExtent; z <= halfExtent; ++z) {
   GenerateFlatColumn(world, registry, x, z, surfaceY);
  }
 }
}

}
