#pragma once

#include "Render/Mesh/IUChunkMesher.h"
#include "Render/Mesh/CpuGreedyMesher.h"
#include <cstdint>
#include <memory>
#include <vector>

namespace cutum
{

class UBlockRegistry;
struct ChunkMeshSnapshot;

/// Desktop GPU greedy: compute opaque face-mask extract when the chunk is
/// opaque-solid-only; otherwise CPU greedy. Android never binds this class.
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

  /// Successful GPU extract→CPU-quad decode that will upload via pool (P5).
  static uint64_t ConsumeMeshVboDispatchCount();

private:
  bool EnsureCompute();
  std::vector<GreedyQuad> TryComputeExtract(const ChunkMeshSnapshot &snapshot,
                                            UBlockRegistry &registry);
  struct GpuState;
  UCpuGreedyMesher Cpu;
  std::unique_ptr<GpuState> State;
  uint64_t ComputeDispatches{0};
};

} // namespace cutum
