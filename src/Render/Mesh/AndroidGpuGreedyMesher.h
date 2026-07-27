#pragma once

#include "Render/Mesh/IUChunkMesher.h"
#include "Render/Mesh/CpuGreedyMesher.h"
#include <memory>

namespace cutum
{

/// Android hybrid mesher: defer eligible opaque to main thread, CPU extract +
/// merge into batches, upload via CpuStagingGpuStore (no MDI / no mask
/// readback). Selected when AllowAndroidGpu is effective.
class UAndroidGpuGreedyMesher final : public IUChunkMesher
{
public:
  UAndroidGpuGreedyMesher();
  ~UAndroidGpuGreedyMesher() override;

  const char *BackendName() const override { return "android_gpu_hybrid"; }

  std::vector<GreedyQuad> BuildChunkMesh(const UBlockWorld &world,
                                         glm::ivec3 chunk_coord,
                                         UBlockRegistry &registry) override;

  std::vector<GreedyQuad> BuildChunkMesh(const ChunkMeshSnapshot &snapshot,
                                         UBlockRegistry &registry) override;

  bool CanDeferGpuExtract(const ChunkMeshSnapshot &snapshot,
                          UBlockRegistry &registry) const override;

  bool TryExtractOpaqueToBatches(const ChunkMeshSnapshot &snapshot,
                                 UBlockRegistry &registry, glm::ivec3 coord,
                                 std::vector<GreedyMeshBatch> &out_batches,
                                 bool deferred_no_gpu_readback = false,
                                 bool greedy_merge_rects = false) override;

private:
  UCpuGreedyMesher Cpu;
};

} // namespace cutum
