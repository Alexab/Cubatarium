#include "Render/Mesh/AndroidGpuGreedyMesher.h"
#include "Render/Mesh/GpuGreedyFaceExtract.h"
#include "Render/Mesh/GreedyMeshEmitter.h"
#include "Render/Mesh/MeshLightSampling.h"
#include "Render/Backend/RenderBackendCaps.h"
#include <unordered_map>

namespace cutum
{

UAndroidGpuGreedyMesher::UAndroidGpuGreedyMesher() = default;
UAndroidGpuGreedyMesher::~UAndroidGpuGreedyMesher() = default;

std::vector<GreedyQuad>
UAndroidGpuGreedyMesher::BuildChunkMesh(const UBlockWorld &world,
                                        glm::ivec3 chunk_coord,
                                        UBlockRegistry &registry)
{
  return Cpu.BuildChunkMesh(world, chunk_coord, registry);
}

std::vector<GreedyQuad>
UAndroidGpuGreedyMesher::BuildChunkMesh(const ChunkMeshSnapshot &snapshot,
                                        UBlockRegistry &registry)
{
  return Cpu.BuildChunkMesh(snapshot, registry);
}

bool UAndroidGpuGreedyMesher::CanDeferGpuExtract(
    const ChunkMeshSnapshot &snapshot, UBlockRegistry &registry) const
{
  const RenderBackendCaps &caps = GetActiveRenderBackendCaps();
  if (!caps.AllowAndroidGpu)
  {
    return false;
  }
  return SnapshotIsGpuExtractEligible(snapshot, registry);
}

bool UAndroidGpuGreedyMesher::TryExtractOpaqueToBatches(
    const ChunkMeshSnapshot &snapshot, UBlockRegistry &registry,
    glm::ivec3 coord, std::vector<GreedyMeshBatch> &out_batches,
    bool /*deferred_no_gpu_readback*/, bool greedy_merge_rects)
{
  out_batches.clear();
  if (!SnapshotIsGpuExtractEligible(snapshot, registry))
  {
    return false;
  }
  // Hybrid A2: main-thread CPU face extract + strict merge → batches.
  // No mask SSBO readback (gpu_mask_readback_med stays 0). Staging store
  // uploads batches; GLES mask compute can replace this without changing the
  // IUChunkMesher contract.
  std::vector<GreedyQuad> quads = ExtractOpaqueFacesCpu(snapshot, registry);
  (void)greedy_merge_rects;
  quads = MergeOpaqueQuadsStrict(quads);
  if (quads.empty())
  {
    return false;
  }
  std::unordered_map<BlockId, GreedyMeshBatch> byBlockId;
  for (const GreedyQuad &q : quads)
  {
    GreedyMeshBatch &batch = byBlockId[q.Id];
    batch.blockId = q.Id;
    batch.Transparent = registry.IsTransparent(q.Id);
    batch.AlphaCutout =
        registry.GetRenderStyle(q.Id) == BlockRenderStyle::Cutout;
    const size_t base_vertex = batch.vertices.size();
    AppendGreedyQuad(q, coord, batch.vertices, batch.indices);
    for (size_t i = base_vertex; i < batch.vertices.size(); ++i)
    {
      ApplyVertexLight(batch.vertices[i], q.LightPacked);
    }
  }
  out_batches.reserve(byBlockId.size());
  for (auto &entry : byBlockId)
  {
    entry.second.blockId = entry.first;
    out_batches.push_back(std::move(entry.second));
  }
  return !out_batches.empty();
}

} // namespace cutum
