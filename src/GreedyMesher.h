#ifndef GREEDYMESHER_H
#define GREEDYMESHER_H

#include "BlockTypes.h"
#include <glm/glm.hpp>
#include <vector>

namespace cutum
{

class UBlockRegistry;
class UBlockWorld;

struct GreedyQuad
{
  int axis;
  int slice;
  int u;
  int v;
  int width;
  int height;
  BlockId id;
  int faceSign;
};

class UGreedyMesher
{
public:
  static std::vector<GreedyQuad> BuildChunkMesh(const UBlockWorld &world,
                                                glm::ivec3 chunkCoord,
                                                UBlockRegistry &registry);
};

} // namespace cutum

#endif
