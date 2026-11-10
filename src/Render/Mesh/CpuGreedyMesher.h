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
                 UBlockRegistry &registry,
                 const BlockDefinitionCatalog *catalog = nullptr) override
  {
    return UGreedyMesher::BuildChunkMesh(world, chunk_coord, registry, catalog);
  }

  std::vector<GreedyQuad>
  BuildChunkMesh(const ChunkMeshSnapshot &snapshot, UBlockRegistry &registry,
                 const BlockDefinitionCatalog *catalog = nullptr) override
  {
    return UGreedyMesher::BuildChunkMesh(snapshot, registry, catalog);
  }
};

} // namespace cutum
