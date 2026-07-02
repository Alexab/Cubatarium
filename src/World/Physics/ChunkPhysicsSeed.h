#ifndef CHUNKPHYSICSSEED_H
#define CHUNKPHYSICSSEED_H

#include <glm/glm.hpp>

namespace cutum
{

class UWorld;

struct ChunkPhysicsSeedBudgets
{
  int MaxColumnsPerCommit{32};
  int MaxLiquidEnqueuePerCommit{64};
};

void SeedPhysicsOnChunkCommitted(UWorld &world, glm::ivec3 chunk_coord,
                                 const ChunkPhysicsSeedBudgets &budgets);

} // namespace cutum

#endif // CHUNKPHYSICSSEED_H
