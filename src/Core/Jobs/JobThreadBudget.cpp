#include "Core/Jobs/JobThreadBudget.h"
#include <algorithm>
#include <thread>

namespace cutum
{

std::size_t ComputeWorkerThreadCount(const JobPoolKind kind,
                                     const std::size_t overrideCount)
{
  const std::size_t pool_cap =
      kind == JobPoolKind::MeshBuild ? kMaxMeshBuildWorkers : kMaxWorkersPerPool;
  if (overrideCount > 0)
  {
    return std::max<std::size_t>(1, std::min(overrideCount, pool_cap));
  }

  const std::size_t hw = std::thread::hardware_concurrency();
  const std::size_t budget = hw > 1 ? std::min(hw - 1, pool_cap) : 1;

  switch (kind)
  {
  case JobPoolKind::CoopGeneration:
    return std::max<std::size_t>(2, std::min(budget, kMaxWorkersPerPool));
  case JobPoolKind::MeshBuild:
    return budget;
  case JobPoolKind::ChunkGeneration:
  case JobPoolKind::ChunkIo:
  case JobPoolKind::Relight:
  default:
    return std::min(budget, kMaxWorkersPerPool);
  }
}

} // namespace cutum
