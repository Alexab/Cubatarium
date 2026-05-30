#ifndef GREEDYMESHER_H
#define GREEDYMESHER_H

#include <vector>
#include <glm/glm.hpp>
#include "BlockTypes.h"

namespace cutum {

class BlockRegistry;
class BlockWorld;

struct GreedyQuad {
 int axis;
 int slice;
 int u;
 int v;
 int width;
 int height;
 BlockId id;
 int faceSign;
};

class GreedyMesher {
public:
 static std::vector<GreedyQuad> BuildChunkMesh(
     const BlockWorld& world, glm::ivec3 chunkCoord, BlockRegistry& registry);
};

}

#endif
