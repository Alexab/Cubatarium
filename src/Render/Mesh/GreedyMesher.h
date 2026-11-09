#ifndef GREEDYMESHER_H
#define GREEDYMESHER_H

#include "World/Math/BlockTypes.h"
#include <glm/glm.hpp>
#include <vector>

namespace cutum
{

class UBlockRegistry;
class UBlockWorld;
struct BlockDefinitionCatalog;
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
  uint8_t LightPacked{0};
  uint8_t FluidPacked{0};
  /// 0 = not a fluid surface cell. 1 = surface cell with a real packed
  /// fluid state (height from FluidCellHeight). 2 = surface cell whose
  /// fluid state packs to 0 (legacy source without a Kind) — assume the
  /// source 7/8 height. Minecraft-style truncated surface (W4/TD-FL-012).
  uint8_t FluidSurface{0};
};

class UGreedyMesher
{
public:
  /// Optional `catalog`: when non-null, ALL face/liquid/movement decisions
  /// read the pinned catalog (Q4 WorkerCompute). Registry is fallback only.
  static std::vector<GreedyQuad> BuildChunkMesh(
      const UBlockWorld &world, glm::ivec3 chunkCoord, UBlockRegistry &registry,
      const BlockDefinitionCatalog *catalog = nullptr);
  static std::vector<GreedyQuad> BuildChunkMesh(
      const ChunkMeshSnapshot &snapshot, UBlockRegistry &registry,
      const BlockDefinitionCatalog *catalog = nullptr);
};

} // namespace cutum

#endif
