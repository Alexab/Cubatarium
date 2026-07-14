#pragma once

#include <cstddef>

namespace cutum
{

enum class JobPoolKind
{
  ChunkGeneration,
  ChunkIo,
  MeshBuild,
  Relight,
  CoopGeneration
};

constexpr std::size_t kMaxWorkersPerPool = 4;

std::size_t ComputeWorkerThreadCount(JobPoolKind kind,
                                     std::size_t overrideCount = 0);

} // namespace cutum
