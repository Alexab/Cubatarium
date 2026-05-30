#include "WorldGenerator.h"
#include "BlockRegistry.h"
#include "BlockWorld.h"
#include <iostream>

namespace cutum {

void WorldGenerator::GenerateFlat(BlockWorld& world, BlockRegistry& registry, int halfExtent, int surfaceY)
{
 const BlockId bedrock = registry.GetIdByTypeName("bedrock");
 const BlockId stone = registry.GetIdByTypeName("stone");
 const BlockId grass = registry.GetIdByTypeName("grass");

 if (bedrock == BLOCK_AIR || stone == BLOCK_AIR || grass == BLOCK_AIR) {
  std::cerr << "WorldGenerator::GenerateFlat: missing block types (bedrock/stone/grass)" << std::endl;
  return;
 }

 for (int x = -halfExtent; x <= halfExtent; ++x) {
  for (int z = -halfExtent; z <= halfExtent; ++z) {
   world.SetBlock(glm::ivec3(x, 0, z), bedrock);
   world.SetBlock(glm::ivec3(x, 1, z), stone);
   world.SetBlock(glm::ivec3(x, 2, z), stone);
   world.SetBlock(glm::ivec3(x, surfaceY, z), grass);
  }
 }
}

}
