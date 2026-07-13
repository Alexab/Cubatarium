#include "Core/Jobs/JobThreadBudget.h"
#include <algorithm>
#include <thread>

namespace cutum
{

std::size_t ComputeWorkerThreadCount(const JobPoolKind kind,
                                     const std::size_t overrideCount)
{
  if (overrideCount > 0)
  {
    return std::max<std::size_t>(1, std::min(overrideCount, kMaxWorkersPerPool));
  }

  const std::size_t hw = std::thread::hardware_concurrency();
  const std::size_t budget = hw > 1 ? std::min(hw - 1, kMaxWorkersPerPool) : 1;

  switch (kind)
  {
  case JobPoolKind::CoopGeneration:
    return std::max<std::size_t>(2, std::min(budget, kMaxWorkersPerPool));
  case JobPoolKind::ChunkGeneration:
  case JobPoolKind::ChunkIo:
  case JobPoolKind::MeshBuild:
  case JobPoolKind::Relight:
  default:
    return budget;
  }
}

} // namespace cutum
