#ifndef GREEDYVERTEXPOOL_H
#define GREEDYVERTEXPOOL_H

#include "Render/Mesh/GreedyMeshBatch.h"
#include <cstddef>
#include <cstdint>

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

/// Cross-batch vertex/index arena for greedy mesh uploads (TD-CS-016).
class UGreedyVertexPool
{
public:
  GreedyGpuPoolAllocation Allocate(const GreedyMeshBatch &batch);
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
  /// Soft ceiling for vertex+index combined (0 = unbounded grow).
  void SetMaxCapacityBytes(size_t max_bytes) { MaxCapacityBytes = max_bytes; }
  size_t GetMaxCapacityBytes() const { return MaxCapacityBytes; }

private:
  /// Returns false if growth was refused/clamped by MaxCapacityBytes.
  bool EnsureCapacity(size_t vertex_bytes, size_t index_bytes);

  GLuint VertexVbo{0};
  GLuint IndexEbo{0};
  size_t VertexCapacityBytes{0};
  size_t IndexCapacityBytes{0};
  size_t VertexUsedBytes{0};
  size_t IndexUsedBytes{0};
  size_t MaxCapacityBytes{0};
};

} // namespace cutum

#endif
