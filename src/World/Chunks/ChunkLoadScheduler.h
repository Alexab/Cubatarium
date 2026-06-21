#pragma once

#include "Core/Jobs/JobThreadPool.h"
#include "World/Chunks/ChunkBuffer.h"
#include "World/Chunks/ChunkGenerationToken.h"
#include "WorldGen/Core/IChunkPopulator.h"
#include <functional>
#include <queue>
#include <unordered_map>
#include <vector>

namespace cutum
{

class UBlockWorld;
class UChunkMeshCache;

enum class ChunkLoadState
{
  Absent,
  Requested,
  Generating,
  Ready,
  Committed
};

class UChunkLoadScheduler
{
public:
  using MarkChunkDirtyFn = std::function<void(glm::ivec3)>;
  using ColumnMeshDirtyFn =
      std::function<void(glm::ivec3 groundCoord, int minY, int maxY)>;

  UChunkLoadScheduler(IChunkPopulator &populator,
                      UChunkGenerationRegistry &tokens);

  void SetMarkDirtyFn(MarkChunkDirtyFn fn);
  void SetColumnMeshDirtyFn(ColumnMeshDirtyFn fn);
  void RequestLoad(glm::ivec3 coord, int priority,
                   const ProceduralSettings &settings);
  void Cancel(glm::ivec3 coord);
  void Invalidate(glm::ivec3 coord);
  void Tick(UBlockWorld &world, int maxCommitsPerFrame);
  bool IsCommitted(glm::ivec3 coord) const;
  bool IsPending(glm::ivec3 coord) const;
  ChunkLoadState GetState(glm::ivec3 coord) const;

private:
  struct PendingRequest
  {
    glm::ivec3 coord;
    int priority{0};
    ChunkGenerationToken token;
    ProceduralSettings settings;
  };

  struct PendingResult
  {
    ChunkPopulateResult result;
  };

  struct RequestCompare
  {
    bool operator()(const PendingRequest &a, const PendingRequest &b) const
    {
      return a.priority > b.priority;
    }
  };

  void ScheduleWorker(const PendingRequest &request);

  IChunkPopulator &Populator;
  UChunkGenerationRegistry &Tokens;
  UJobThreadPool Pool;
  CompletedJobQueue<PendingResult> Completed;
  MarkChunkDirtyFn MarkDirty;
  ColumnMeshDirtyFn ColumnMeshDirty;
  std::priority_queue<PendingRequest, std::vector<PendingRequest>,
                      RequestCompare>
      Queue;
  std::unordered_map<glm::ivec3, ChunkLoadState, IVec3Hash> States;
  std::unordered_map<glm::ivec3, ChunkGenerationToken, IVec3Hash> ActiveTokens;
};

} // namespace cutum
