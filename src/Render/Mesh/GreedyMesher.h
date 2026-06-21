#ifndef GREEDYMESHER_H
#define GREEDYMESHER_H

#include "World/Math/BlockTypes.h"
#include <glm/glm.hpp>
#include <vector>

namespace cutum
{

class UBlockRegistry;
class UBlockWorld;
struct ChunkMeshSnapshot;

struct GreedyQuad
{
  int axis;
  int slice;
  int u;
  int v;
  int width;
  int height;
  BlockId Id;
  int faceSign;
};

class UGreedyMesher
{
public:
  static std::vector<GreedyQuad> BuildChunkMesh(const UBlockWorld &world,
                                                glm::ivec3 chunkCoord,
                                                UBlockRegistry &registry);
  static std::vector<GreedyQuad> BuildChunkMesh(const ChunkMeshSnapshot &snapshot,
                                                UBlockRegistry &registry);
};

} // namespace cutum

#endif
