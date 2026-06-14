#ifndef GREEDY_TRANSPARENT_SORT_H
#define GREEDY_TRANSPARENT_SORT_H

#include "BlockDefinition.h"
#include "ChunkMeshCache.h"

#include <cstdint>
#include <vector>

#include <glm/vec3.hpp>

namespace cutum
{

class UBlockRegistry;

float GreedyBatchViewDistance(const GreedyMeshBatch &batch,
                              const glm::vec3 &cameraPos);
int TransparentBatchLayer(BlockRenderStyle style);
void SortTransparentGreedyBatches(std::vector<GreedyMeshBatch> &batches,
                                  const glm::vec3 &cameraPos,
                                  const UBlockRegistry &registry);
uint64_t GreedyTransparentSortRevision(const glm::vec3 &cameraPos);

} // namespace cutum

#endif
