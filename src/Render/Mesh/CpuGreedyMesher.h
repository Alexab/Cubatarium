#pragma once

#include "Render/Mesh/IUChunkMesher.h"

namespace cutum
{

/// CPU greedy mesher — wraps UGreedyMesher.
class UCpuGreedyMesher final : public IUChunkMesher
{
public:
  const char *BackendName() const override { return "cpu_greedy"; }

  std::vector<GreedyQuad>
  BuildChunkMesh(const UBlockWorld &world, glm::ivec3 chunk_coord,
                 UBlockRegistry &registry) override
  {
    return UGreedyMesher::BuildChunkMesh(world, chunk_coord, registry);
  }

  std::vector<GreedyQuad>
  BuildChunkMesh(const ChunkMeshSnapshot &snapshot,
                 UBlockRegistry &registry) override
  {
    return UGreedyMesher::BuildChunkMesh(snapshot, registry);
  }
};

} // namespace cutum
