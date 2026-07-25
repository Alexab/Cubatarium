#pragma once

#include "Render/Engine/IUMeshGpuStore.h"
#include "Render/Engine/GreedyGpuBackend.h"

namespace cutum
{

/// Current upload path: GreedyGpuBackend + VertexPool bump arena.
class UCpuStagingGpuStore : public IUMeshGpuStore
{
public:
  const char *BackendName() const override { return "cpu_staging"; }

  void RefreshPass(GreedyGpuPassCache &cache,
                   const std::vector<GreedyMeshBatch> &batches,
                   uint64_t mesh_revision, uint64_t cull_revision,
                   uint64_t sort_revision) override
  {
    Backend.RefreshPass(cache, batches, mesh_revision, cull_revision,
                        sort_revision);
  }

  void RefreshPassRefs(GreedyGpuPassCache &cache,
                       const UChunkMeshCache &meshCache,
                       const std::vector<GreedyBatchRef> &refs,
                       uint64_t mesh_revision, uint64_t cull_revision,
                       uint64_t sort_revision) override
  {
    Backend.RefreshPassRefs(cache, meshCache, refs, mesh_revision,
                            cull_revision, sort_revision);
  }

  void DestroyPass(GreedyGpuPassCache &cache) override
  {
    Backend.DestroyPass(cache);
  }

  void DestroyAll(GreedyGpuPassCache &opaque, GreedyGpuPassCache &cutout,
                  GreedyGpuPassCache &transparent) override
  {
    Backend.DestroyAll(opaque, cutout, transparent);
  }

  UGreedyGpuBackend &GetBackend() { return Backend; }
  const UGreedyGpuBackend &GetBackend() const { return Backend; }

private:
  UGreedyGpuBackend Backend;
};

} // namespace cutum
