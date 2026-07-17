#pragma once

#include "Core/Jobs/JobThreadPool.h"
#include "World/Chunks/ChunkBuffer.h"
#include "World/Chunks/ChunkGenerationToken.h"
#include "WorldGen/Core/IUChunkPopulator.h"
#include <chrono>
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
  using MarkChunkDirtyFn =
      std::function<void(glm::ivec3 coord, int minY, int maxY)>;
  using ColumnMeshDirtyFn =
      std::function<void(glm::ivec3 groundCoord, int minY, int maxY)>;

  UChunkLoadScheduler(IUChunkPopulator &populator,
                      UChunkGenerationRegistry &tokens);

  void SetMarkDirtyFn(MarkChunkDirtyFn fn);
  void SetColumnMeshDirtyFn(ColumnMeshDirtyFn fn);
  void RequestLoad(glm::ivec3 coord, int priority,
                   const ProceduralSettings &settings,
                   glm::ivec2 column_origin = glm::ivec2(0),
                   bool has_column_origin = false);
  void Cancel(glm::ivec3 coord);
  void CancelAllPending(std::chrono::milliseconds worker_wait =
                            std::chrono::milliseconds(2000));
  bool WaitForWorkersIdle(std::chrono::milliseconds timeout =
                               std::chrono::milliseconds(10000));
  void Invalidate(glm::ivec3 coord);
  void Tick(UBlockWorld &world, int maxCommitsPerFrame,
            int maxGenerationStartsPerFrame = 4);
  bool IsCommitted(glm::ivec3 coord) const;
  bool IsPending(glm::ivec3 coord) const;
  ChunkLoadState GetState(glm::ivec3 coord) const;
  int GetPendingQueueCount() const;
  int GetGenInFlightCount() const;
  int GetCompletedReadyCount() const;
  int GetGenBacklogTotal() const;
  double GetLastTickApplyMs() const { return LastTickApplyMs; }

private:
  struct PendingRequest
  {
    glm::ivec3 coord;
    int priority{0};
    ChunkGenerationToken token;
    ProceduralSettings settings;
    glm::ivec2 columnOrigin{0};
    bool hasColumnOrigin{false};
  };

  struct PendingResult
  {
    ChunkPopulateResult result;
    int priority{0};
    int maxHeight{256};
  };

  struct RequestCompare
  {
    bool operator()(const PendingRequest &a, const PendingRequest &b) const
    {
      return a.priority > b.priority;
    }
  };

  void ScheduleWorker(const PendingRequest &request);

  IUChunkPopulator &Populator;
  UChunkGenerationRegistry &Tokens;
  // Completed must outlive Pool (members destroy in reverse declaration order).
  UCompletedJobQueue<PendingResult> Completed;
  UJobThreadPool Pool;
  MarkChunkDirtyFn MarkDirty;
  ColumnMeshDirtyFn ColumnMeshDirty;
  std::priority_queue<PendingRequest, std::vector<PendingRequest>,
                      RequestCompare>
      Queue;
  std::unordered_map<glm::ivec3, ChunkLoadState, IVec3Hash> States;
  std::unordered_map<glm::ivec3, ChunkGenerationToken, IVec3Hash> ActiveTokens;
  std::unordered_map<glm::ivec3, int, IVec3Hash> RequestPriorities;
  double LastTickApplyMs{0.0};
};

} // namespace cutum
