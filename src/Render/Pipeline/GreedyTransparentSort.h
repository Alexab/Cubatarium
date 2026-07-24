#ifndef GREEDY_TRANSPARENT_SORT_H
#define GREEDY_TRANSPARENT_SORT_H

#include "Blocks/BlockDefinition.h"
#include "Render/Mesh/ChunkMeshCache.h"

#include <cstdint>
#include <vector>

#include <glm/vec3.hpp>

namespace cutum
{

class UBlockRegistry;

float GreedyBatchViewDistance(const GreedyMeshBatch &batch,
                              const glm::vec3 &cameraPos);
int TransparentBatchLayer(BlockRenderStyle Style);
void SortTransparentGreedyBatches(std::vector<GreedyMeshBatch> &batches,
                                  const glm::vec3 &cameraPos,
                                  const UBlockRegistry &registry);
void SortTransparentGreedyBatches(
    std::vector<GreedyBatchRef> &refs,
    const UChunkMeshCache &cache, const glm::vec3 &cameraPos,
    const UBlockRegistry &registry);
uint64_t GreedyTransparentSortRevision(const glm::vec3 &cameraPos);

} // namespace cutum

#endif
