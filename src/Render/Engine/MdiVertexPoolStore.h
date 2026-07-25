#pragma once

#include "Render/Engine/CpuStagingGpuStore.h"
#include <cstdint>
#include <vector>

typedef unsigned int GLuint;

namespace cutum
{

/// DrawElementsIndirectCommand layout (OpenGL).
struct DrawElementsIndirectCommand
{
  uint32_t count{0};
  uint32_t instanceCount{1};
  uint32_t firstIndex{0};
  int32_t baseVertex{0};
  uint32_t baseInstance{0};
};

/// MDI-capable store: same upload as staging, DrawIndirect submit helper.
class UMdiVertexPoolStore final : public UCpuStagingGpuStore
{
public:
  ~UMdiVertexPoolStore() override;

  const char *BackendName() const override { return "mdi_vertex_pool"; }

  bool SupportsMultiDrawIndirect() const override { return true; }

  uint64_t LastSubmitDrawCmds() const override { return LastDrawCmds; }

  size_t BuildIndirectCommands(const GreedyGpuPassCache &cache,
                               std::vector<DrawElementsIndirectCommand> &out);

  /// Upload command buffer. Caller binds VAO/attrs; returns false if unavailable.
  bool TrySubmitMultiDraw(const GreedyGpuPassCache &cache);

  void *MapBucket(MeshGpuBucketHandle handle, size_t bytes) override;
  void UnmapBucket(MeshGpuBucketHandle handle) override;
  void FlipBucketOwnership(MeshGpuBucketHandle handle) override;

private:
  GLuint IndirectBuffer{0};
  size_t IndirectCapacityBytes{0};
  uint64_t LastDrawCmds{0};

  // Phase 2 staging map stubs (worker→GPU ownership flip).
  std::vector<uint8_t> StagingScratch;
  MeshGpuBucketHandle MappedHandle{};
};

} // namespace cutum
