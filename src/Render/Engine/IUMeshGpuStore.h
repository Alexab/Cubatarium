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

/// DrawElementsIndirectCommand layout (OpenGL).
struct DrawElementsIndirectCommand
{
  uint32_t count{0};
  uint32_t instanceCount{1};
  uint32_t firstIndex{0};
  int32_t baseVertex{0};
  uint32_t baseInstance{0};
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

  virtual void *MapBucket(MeshGpuBucketHandle /*handle*/, size_t /*bytes*/)
  {
    return nullptr;
  }
  virtual void UnmapBucket(MeshGpuBucketHandle /*handle*/) {}
  virtual void FlipBucketOwnership(MeshGpuBucketHandle /*handle*/) {}

  virtual bool SupportsMultiDrawIndirect() const { return false; }

  /// Build indirect cmds for pooled batches in [begin, end). Local indices +
  /// baseVertex (vertex byte offset / stride).
  virtual size_t BuildIndirectCommandsRange(
      const GreedyGpuPassCache & /*cache*/, size_t /*begin*/, size_t /*end*/,
      std::vector<DrawElementsIndirectCommand> &out)
  {
    out.clear();
    return 0;
  }

  /// Upload cmds and MultiDrawIndirect. Caller binds VAO + attribs at origin +
  /// texture.
  virtual bool SubmitIndirectCommands(
      const std::vector<DrawElementsIndirectCommand> & /*cmds*/)
  {
    return false;
  }

  /// MultiDraw from cache.IndirectCmdsBuffer for batches [begin, end).
  virtual bool SubmitIndirectCommandsGpuRange(const GreedyGpuPassCache & /*cache*/,
                                              size_t /*begin*/, size_t /*end*/)
  {
    return false;
  }

  virtual bool TrySubmitMultiDraw(const GreedyGpuPassCache & /*cache*/)
  {
    return false;
  }

  virtual uint64_t LastSubmitDrawCmds() const { return 0; }
};

} // namespace cutum
