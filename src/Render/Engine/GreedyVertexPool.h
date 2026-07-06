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
  void Reset();
  void Destroy();

  GLuint VertexBuffer() const { return VertexVbo; }
  GLuint IndexBuffer() const { return IndexEbo; }
  bool IsActive() const { return VertexVbo != 0; }

private:
  void EnsureCapacity(size_t vertex_bytes, size_t index_bytes);

  GLuint VertexVbo{0};
  GLuint IndexEbo{0};
  size_t VertexCapacityBytes{0};
  size_t IndexCapacityBytes{0};
  size_t VertexUsedBytes{0};
  size_t IndexUsedBytes{0};
};

} // namespace cutum

#endif
