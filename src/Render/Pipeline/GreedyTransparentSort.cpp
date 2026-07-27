#include "Render/Pipeline/GreedyTransparentSort.h"

#include "Blocks/BlockRegistry.h"
#if !defined(__ANDROID__) && !defined(CUBATARIUM_GLES)
#include "Render/Backend/GpuHotPathFallback.h"
#include "Render/Pipeline/GpuTransparentSort.h"
#endif

#include <algorithm>
#include <cmath>
#include <utility>
#include <vector>

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

/// View distance via AABB center (cullSphere-compatible key for GPU sort).
float GreedyBatchViewDistance(const GreedyMeshBatch &batch,
                              const glm::vec3 &cameraPos)
{
  if (batch.vertices.empty())
  {
    return 0.0f;
  }
  // Fast path: average of first/last vertex approximates AABB center well
  // enough for back-to-front order (±0.25 hysteresis in sort).
  const GreedyMeshVertex &a = batch.vertices.front();
  const GreedyMeshVertex &b = batch.vertices.back();
  const glm::vec3 center(0.5f * (a.px + b.px), 0.5f * (a.py + b.py),
                         0.5f * (a.pz + b.pz));
  return glm::length(center - cameraPos);
}

int TransparentBatchLayer(BlockRenderStyle Style)
{
  switch (Style)
  {
  case BlockRenderStyle::Fluid:
    return 0;
  case BlockRenderStyle::Cross:
    return 2;
  default:
    return 1;
  }
}

namespace
{
struct SortKey
{
  float dist;
  int layer;
  BlockId id;
  size_t index;
};

bool SortKeyLess(const SortKey &a, const SortKey &b)
{
  if (std::abs(a.dist - b.dist) > 0.25f)
  {
    return a.dist > b.dist;
  }
  if (a.layer != b.layer)
  {
    return a.layer < b.layer;
  }
  return a.id < b.id;
}
} // namespace

void SortTransparentGreedyBatches(std::vector<GreedyMeshBatch> &batches,
                                  const glm::vec3 &cameraPos,
                                  const UBlockRegistry &registry)
{
  std::vector<SortKey> keys;
  keys.reserve(batches.size());
  for (size_t i = 0; i < batches.size(); ++i)
  {
    const GreedyMeshBatch &batch = batches[i];
    keys.push_back(
        SortKey{GreedyBatchViewDistance(batch, cameraPos),
                TransparentBatchLayer(registry.GetRenderStyle(batch.blockId)),
                batch.blockId, i});
  }
  std::sort(keys.begin(), keys.end(), SortKeyLess);
  std::vector<GreedyMeshBatch> ordered;
  ordered.reserve(batches.size());
  for (const SortKey &k : keys)
  {
    ordered.push_back(std::move(batches[k.index]));
  }
  batches = std::move(ordered);
}

void SortTransparentGreedyBatches(
    std::vector<GreedyBatchRef> &refs,
    const UChunkMeshCache &cache, const glm::vec3 &cameraPos,
    const UBlockRegistry &registry)
{
#if !defined(__ANDROID__) && !defined(CUBATARIUM_GLES)
  if (TryGpuSortTransparentGreedyBatches(refs, cache, cameraPos, registry))
  {
    return;
  }
  NoteGpuHotPathFallback();
#endif
  std::vector<SortKey> keys;
  keys.reserve(refs.size());
  for (size_t i = 0; i < refs.size(); ++i)
  {
    const GreedyMeshBatch *batch = cache.TryGetGreedyBatch(refs[i]);
    if (!batch)
    {
      keys.push_back(SortKey{-1.0f, 99, 0, i});
      continue;
    }
    keys.push_back(SortKey{
        GreedyBatchViewDistance(*batch, cameraPos),
        TransparentBatchLayer(registry.GetRenderStyle(batch->blockId)),
        batch->blockId, i});
  }
  std::sort(keys.begin(), keys.end(), SortKeyLess);
  std::vector<GreedyBatchRef> ordered;
  ordered.reserve(refs.size());
  for (const SortKey &k : keys)
  {
    ordered.push_back(refs[k.index]);
  }
  refs = std::move(ordered);
}

} // namespace cutum
