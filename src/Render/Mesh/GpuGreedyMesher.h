#pragma once

#include "Render/Mesh/IUChunkMesher.h"
#include "Render/Mesh/CpuGreedyMesher.h"
#include "Render/Mesh/GreedyMeshBatch.h"
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

  /// Main-thread: compute face masks → emit batches (no intermediate quads
  /// vector as the Apply product). Returns false → caller falls back to CPU.
  bool TryExtractOpaqueToBatches(const ChunkMeshSnapshot &snapshot,
                                 UBlockRegistry &registry, glm::ivec3 coord,
                                 std::vector<GreedyMeshBatch> &out_batches);

  /// True when snapshot is opaque-solid-only and GL can dispatch extract.
  bool CanDeferGpuExtract(const ChunkMeshSnapshot &snapshot,
                          UBlockRegistry &registry) const;

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
