#ifndef GREEDYVERTEXPOOL_H
#define GREEDYVERTEXPOOL_H

#include "Render/Mesh/GreedyMeshBatch.h"
#include <cstddef>
#include <cstdint>
#include <vector>

typedef unsigned int GLuint;
typedef int GLsizei;

namespace cutum
{

struct GreedyGpuPoolAllocation
{
  size_t vertexByteOffset{0};
  size_t indexByteOffset{0};
  size_t vertexCount{0};
  size_t indexCount{0};
  GLsizei indexCountGl{0};
};

struct GreedyGpuPoolFreeSlot
{
  size_t vertexByteOffset{0};
  size_t indexByteOffset{0};
  size_t vertexBytes{0};
  size_t indexBytes{0};
};

/// Cross-batch vertex/index arena for greedy mesh uploads (TD-CS-016).
/// Grow-only GL buffers + free-list reuse for released slots.
class UGreedyVertexPool
{
public:
  GreedyGpuPoolAllocation Allocate(const GreedyMeshBatch &batch);
  /// Return a prior allocation to the free-list (does not shrink GL buffers).
  void Free(const GreedyGpuPoolAllocation &alloc);
  /// Grow GPU buffers once per pass before batch uploads (avoids mid-pass orphan).
  /// Returns false if request was clamped by MaxCapacity (partial/no grow).
  bool Reserve(size_t vertex_bytes, size_t index_bytes);
  /// Grow to at least these sizes without resetting used counters.
  bool EnsureMinCapacity(size_t vertex_bytes, size_t index_bytes);
  void Reset();
  void Destroy();

  GLuint VertexBuffer() const { return VertexVbo; }
  GLuint IndexBuffer() const { return IndexEbo; }
  bool IsActive() const { return VertexVbo != 0; }

  size_t UsedBytes() const { return VertexUsedBytes + IndexUsedBytes; }
  size_t CapacityBytes() const
  {
    return VertexCapacityBytes + IndexCapacityBytes;
  }
  size_t VertexUsedBytesValue() const { return VertexUsedBytes; }
  size_t VertexCapacityBytesValue() const { return VertexCapacityBytes; }
  size_t FreeSlotCount() const { return FreeList.size(); }
  /// Soft ceiling for vertex+index combined (0 = unbounded grow).
  void SetMaxCapacityBytes(size_t max_bytes) { MaxCapacityBytes = max_bytes; }
  size_t GetMaxCapacityBytes() const { return MaxCapacityBytes; }
  /// M3: per-frame unsync upload budget (0 = unlimited).
  void SetMaxUnsyncUploadsPerFrame(int n)
  {
    MaxUnsyncUploadsPerFrame = std::max(0, n);
  }
  void BeginUploadFrame() { FrameUnsyncUploads = 0; }
  /// Opt-in (CUBATARIUM_POOL_SYNC=1): fence after a full upload pass.
  void SignalUploadComplete();
  uint64_t ConsumeUnsyncUploads()
  {
    const uint64_t v = UnsyncUploads;
    UnsyncUploads = 0;
    return v;
  }
  double ConsumeFenceWaitMs()
  {
    const double v = FenceWaitMs;
    FenceWaitMs = 0.0;
    return v;
  }

private:
  /// Returns false if growth was refused/clamped by MaxCapacityBytes.
  bool EnsureCapacity(size_t vertex_bytes, size_t index_bytes);
  bool TryAllocateFromFreeList(size_t vertex_bytes, size_t index_bytes,
                               GreedyGpuPoolAllocation &out);

  GLuint VertexVbo{0};
  GLuint IndexEbo{0};
  size_t VertexCapacityBytes{0};
  size_t IndexCapacityBytes{0};
  size_t VertexUsedBytes{0};
  size_t IndexUsedBytes{0};
  size_t MaxCapacityBytes{0};
  std::vector<GreedyGpuPoolFreeSlot> FreeList;
  void *UploadFence{nullptr};
  uint64_t UnsyncUploads{0};
  double FenceWaitMs{0.0};
  int MaxUnsyncUploadsPerFrame{64};
  int FrameUnsyncUploads{0};
};

} // namespace cutum

#endif
