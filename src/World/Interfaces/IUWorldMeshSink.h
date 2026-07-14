#ifndef IUWORLDMESHSINK_H
#define IUWORLDMESHSINK_H

#include <glm/glm.hpp>

namespace cutum
{

/// Notifies mesh consumers when block data changes (Render subscribes).
class IUWorldMeshSink
{
public:
  virtual ~IUWorldMeshSink() = default;

  virtual void OnChunkBlocksChanged(glm::ivec3 chunk_coord) = 0;
  virtual void OnChunkUnloaded(glm::ivec3 chunk_coord) = 0;
};

} // namespace cutum

#endif
