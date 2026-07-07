#ifndef RENDERMESHSINK_H
#define RENDERMESHSINK_H

#include "World/Interfaces/IUWorldMeshSink.h"

namespace cutum
{

class UGeometryEngine;

class URenderMeshSink : public IUWorldMeshSink
{
public:
  void Attach(UGeometryEngine *engine);
  void OnChunkBlocksChanged(glm::ivec3 chunk_coord) override;
  void OnChunkUnloaded(glm::ivec3 chunk_coord) override;
  uint64_t GetInvalidationCount() const { return InvalidationCount; }

private:
  UGeometryEngine *Engine{nullptr};
  uint64_t InvalidationCount{0};
};

} // namespace cutum

#endif
