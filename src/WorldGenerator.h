#ifndef WORLDGENERATOR_H
#define WORLDGENERATOR_H

namespace cutum {

class BlockRegistry;
class BlockWorld;

class WorldGenerator {
public:
 static void GenerateFlat(BlockWorld& world, BlockRegistry& registry, int halfExtent = 16, int surfaceY = 3);
};

}

#endif
