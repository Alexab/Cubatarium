#include "render/GreedyTransparentSort.h"

#include "BlockRegistry.h"

#include <algorithm>
#include <cmath>

namespace cutum
{

uint64_t GreedyTransparentSortRevision(const glm::vec3 &cameraPos)
{
  const int qx = static_cast<int>(std::floor(cameraPos.x * 4.0f));
  const int qy = static_cast<int>(std::floor(cameraPos.y * 4.0f));
  const int qz = static_cast<int>(std::floor(cameraPos.z * 4.0f));
  return (static_cast<uint64_t>(static_cast<uint32_t>(qx)) << 42) ^
         (static_cast<uint64_t>(static_cast<uint32_t>(qy)) << 21) ^
         static_cast<uint64_t>(static_cast<uint32_t>(qz));
}

float GreedyBatchViewDistance(const GreedyMeshBatch &batch,
                              const glm::vec3 &cameraPos)
{
  if (batch.vertices.empty())
  {
    return 0.0f;
  }
  glm::vec3 centroid(0.0f);
  for (const GreedyMeshVertex &v : batch.vertices)
  {
    centroid += glm::vec3(v.px, v.py, v.pz);
  }
  centroid /= static_cast<float>(batch.vertices.size());
  return glm::length(centroid - cameraPos);
}

int TransparentBatchLayer(BlockRenderStyle style)
{
  switch (style)
  {
  case BlockRenderStyle::Fluid:
    return 0;
  case BlockRenderStyle::Cross:
    return 2;
  default:
    return 1;
  }
}

void SortTransparentGreedyBatches(std::vector<GreedyMeshBatch> &batches,
                                  const glm::vec3 &cameraPos,
                                  const UBlockRegistry &registry)
{
  std::sort(batches.begin(), batches.end(),
            [&](const GreedyMeshBatch &a, const GreedyMeshBatch &b)
            {
              const float distA = GreedyBatchViewDistance(a, cameraPos);
              const float distB = GreedyBatchViewDistance(b, cameraPos);
              if (std::abs(distA - distB) > 0.25f)
              {
                return distA > distB;
              }
              const int layerA =
                  TransparentBatchLayer(registry.GetRenderStyle(a.blockId));
              const int layerB =
                  TransparentBatchLayer(registry.GetRenderStyle(b.blockId));
              return layerA < layerB;
            });
}

} // namespace cutum
