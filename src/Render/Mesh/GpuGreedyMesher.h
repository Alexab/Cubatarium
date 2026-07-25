#pragma once

#include "Render/Mesh/IUChunkMesher.h"
#include "Render/Mesh/CpuGreedyMesher.h"

namespace cutum
{

/// GPU greedy mesher backend. Compute kernels land in a follow-up; until then
/// this wraps the same greedy algorithm as CPU for interface/parity tests.
/// Factory binds this only when HasCompute && desktop (init-time).
class UGpuGreedyMesher final : public IUChunkMesher
{
public:
  const char *BackendName() const override { return "gpu_greedy"; }

  std::vector<GreedyQuad>
  BuildChunkMesh(const UBlockWorld &world, glm::ivec3 chunk_coord,
                 UBlockRegistry &registry) override
  {
    return Cpu.BuildChunkMesh(world, chunk_coord, registry);
  }

  std::vector<GreedyQuad>
  BuildChunkMesh(const ChunkMeshSnapshot &snapshot,
                 UBlockRegistry &registry) override
  {
    return Cpu.BuildChunkMesh(snapshot, registry);
  }

private:
  UCpuGreedyMesher Cpu;
};

} // namespace cutum
