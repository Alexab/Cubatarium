#pragma once

#include "Render/Mesh/GreedyMesher.h"
#include <glm/glm.hpp>
#include <vector>

namespace cutum
{

class UBlockRegistry;
class UBlockWorld;
struct ChunkMeshSnapshot;

/// Pluggable chunk mesher. Bound once at init (CPU or GPU implementation).
class IUChunkMesher
{
public:
  virtual ~IUChunkMesher() = default;

  virtual const char *BackendName() const = 0;

  virtual std::vector<GreedyQuad>
  BuildChunkMesh(const UBlockWorld &world, glm::ivec3 chunk_coord,
                 UBlockRegistry &registry) = 0;

  virtual std::vector<GreedyQuad>
  BuildChunkMesh(const ChunkMeshSnapshot &snapshot,
                 UBlockRegistry &registry) = 0;
};

} // namespace cutum
