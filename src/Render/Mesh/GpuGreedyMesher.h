#pragma once

#include "Render/Mesh/IUChunkMesher.h"
#include "Render/Mesh/CpuGreedyMesher.h"
#include <cstdint>
#include <memory>

namespace cutum
{

/// GPU greedy mesher. Runs a Desktop compute warm/dispatch then produces
/// quads via the CPU greedy algorithm (full GPU surface extract is G5+).
class UGpuGreedyMesher final : public IUChunkMesher
{
public:
  UGpuGreedyMesher();
  ~UGpuGreedyMesher() override;

  const char *BackendName() const override { return "gpu_greedy"; }

  std::vector<GreedyQuad>
  BuildChunkMesh(const UBlockWorld &world, glm::ivec3 chunk_coord,
                 UBlockRegistry &registry) override;

  std::vector<GreedyQuad>
  BuildChunkMesh(const ChunkMeshSnapshot &snapshot,
                 UBlockRegistry &registry) override;

  uint64_t GetComputeDispatchCount() const { return ComputeDispatches; }

private:
  void WarmCompute();
  struct GpuState;
  UCpuGreedyMesher Cpu;
  std::unique_ptr<GpuState> State;
  uint64_t ComputeDispatches{0};
};

} // namespace cutum
