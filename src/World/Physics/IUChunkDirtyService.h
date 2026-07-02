#ifndef IUCHUNKDIRTYSERVICE_H
#define IUCHUNKDIRTYSERVICE_H

#include <glm/glm.hpp>

namespace cutum
{

class UWorld;

class IUChunkDirtyService
{
public:
  virtual ~IUChunkDirtyService() = default;
  virtual void MarkVisualRemesh(UWorld &world, glm::ivec3 blockPos) = 0;
  virtual void MarkCollisionRebuild(UWorld &world, glm::ivec3 blockPos) = 0;
  virtual void DrainRebuildQueues(UWorld &world) = 0;
  virtual void MarkDirty(UWorld &world, glm::ivec3 blockPos);
};

} // namespace cutum

#endif // IUCHUNKDIRTYSERVICE_H
