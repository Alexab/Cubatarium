#ifndef FLUIDREFLOWSCAN_H
#define FLUIDREFLOWSCAN_H

#include <glm/glm.hpp>

namespace cutum
{

class UWorld;

// Enqueue fluid frontier cells around a block change (Luanti ReflowScan-style).
void EnqueueFluidFrontierAt(UWorld &world, glm::ivec3 block_pos);

// Scan chunk columns for active liquid/air boundaries and enqueue up to
// max_enqueue.
void ScanChunkFluidFrontier(UWorld &world, glm::ivec3 chunk_coord,
                            int max_enqueue);

} // namespace cutum

#endif // FLUIDREFLOWSCAN_H
