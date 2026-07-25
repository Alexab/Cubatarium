#pragma once

#include "Render/Engine/GreedyGpuBackend.h"
#include "Render/Mesh/GreedyMeshBatch.h"
#include <cstddef>
#include <cstdint>
#include <vector>

namespace cutum
{

class UChunkMeshCache;

struct MeshGpuBucketHandle
{
  size_t index{0};
  bool valid{false};
};

/// GPU mesh storage + opaque submit. Bound once at init.
class IUMeshGpuStore
{
public:
  virtual ~IUMeshGpuStore() = default;

  virtual const char *BackendName() const = 0;

  virtual void RefreshPass(GreedyGpuPassCache &cache,
                           const std::vector<GreedyMeshBatch> &batches,
                           uint64_t mesh_revision, uint64_t cull_revision,
                           uint64_t sort_revision) = 0;

  virtual void RefreshPassRefs(GreedyGpuPassCache &cache,
                               const UChunkMeshCache &meshCache,
                               const std::vector<GreedyBatchRef> &refs,
                               uint64_t mesh_revision, uint64_t cull_revision,
                               uint64_t sort_revision) = 0;

  virtual void DestroyPass(GreedyGpuPassCache &cache) = 0;
  virtual void DestroyAll(GreedyGpuPassCache &opaque, GreedyGpuPassCache &cutout,
                          GreedyGpuPassCache &transparent) = 0;

  /// Optional staging map for worker→GPU upload (Phase 2). Default: unsupported.
  virtual void *MapBucket(MeshGpuBucketHandle /*handle*/, size_t /*bytes*/)
  {
    return nullptr;
  }
  virtual void UnmapBucket(MeshGpuBucketHandle /*handle*/) {}
  virtual void FlipBucketOwnership(MeshGpuBucketHandle /*handle*/) {}

  /// True when store can submit via MultiDrawIndirect.
  virtual bool SupportsMultiDrawIndirect() const { return false; }

  /// Submit pooled pass via DrawIndirect when supported. Returns false → caller
  /// falls back to per-batch glDrawElements. Caller must bind shader/VAO;
  /// implementation binds pool buffers and issues draws (texture bind still
  /// per-batch unless grouped).
  virtual bool TrySubmitMultiDraw(const GreedyGpuPassCache & /*cache*/)
  {
    return false;
  }

  virtual uint64_t LastSubmitDrawCmds() const { return 0; }
};

} // namespace cutum
