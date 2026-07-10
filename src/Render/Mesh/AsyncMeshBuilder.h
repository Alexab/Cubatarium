#pragma once

#include "Core/Jobs/JobThreadPool.h"
#include "Render/Mesh/ChunkMeshSnapshot.h"
#include "Render/Mesh/CrossInstanceBatch.h"
#include "Render/Mesh/GreedyMeshBatch.h"
#include "World/Chunks/ChunkManager.h"
#include "World/Math/BlockTypes.h"
#include <glm/glm.hpp>
#include <unordered_map>
#include <vector>

namespace cutum
{

class UBlockRegistry;

struct MeshBuildResult
{
  glm::ivec3 coord{0};
  std::vector<GreedyMeshBatch> batches;
  std::unordered_map<BlockId, std::vector<CrossInstanceGpu>> crossCenters;
  uint64_t sourceRevision{0};
  uint64_t jobId{0};
};

class UAsyncMeshBuilder
{
public:
  void Enqueue(ChunkMeshSnapshot snapshot, UBlockRegistry &registry);
  std::vector<MeshBuildResult> DrainCompleted(int maxPerFrame);
  bool IsInFlight(glm::ivec3 coord) const;
  int GetInFlightCount() const;
  bool HasPendingWork() const;
  void WaitIdle();

private:
  UJobThreadPool Pool;
  UCompletedJobQueue<MeshBuildResult> Completed;
  mutable std::mutex InFlightMutex;
  std::unordered_map<glm::ivec3, uint64_t, IVec3Hash> InFlight;
  uint64_t NextJobId{1};
};

} // namespace cutum
