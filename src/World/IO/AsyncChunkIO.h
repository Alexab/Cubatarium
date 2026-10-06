#pragma once

#include "Core/Jobs/JobThreadBudget.h"
#include "Core/Jobs/JobThreadPool.h"
#include "World/Chunks/ChunkBuffer.h"
#include "World/Chunks/ChunkGenerationToken.h"
#include "World/IO/ChunkStorageTypes.h"
#include <atomic>
#include <chrono>
#include <filesystem>
#include <glm/glm.hpp>
#include <memory>
#include <string>
#include <unordered_set>
#include <vector>

namespace cutum
{

class UBlockRegistry;
class UBlockWorld;
class UChunkStorageService;

struct AsyncChunkLoadResult
{
  glm::ivec3 coord;
  ChunkGenerationToken token;
  std::shared_ptr<std::atomic<bool>> cancellation;
  std::vector<uint8_t> payload;
  UChunkBuffer decodedBuffer;
  ChunkDiskFormat format{ChunkDiskFormat::Absent};
  bool success{false};
  std::chrono::steady_clock::time_point submittedAt{};
  std::chrono::steady_clock::time_point workerStartedAt{};
  std::chrono::steady_clock::time_point workerFinishedAt{};
  double formatDetectMs{0.0};
  double fileOpenMs{0.0};
  double fileReadMs{0.0};
  double deserializeMs{0.0};
};

struct AsyncChunkSaveRequest
{
  glm::ivec3 coord;
  glm::ivec3 groundCoord;
  std::string filePath;
  ChunkDiskFormat format{ChunkDiskFormat::Binary};
  bool success{false};
  std::string error;
};

struct AsyncColumnLightFlagsSaveResult
{
  std::string worldFolder;
  uint64_t revision{0};
  bool success{false};
  std::string error;
};

class UAsyncChunkIO
{
public:
  UAsyncChunkIO()
      : Pool(ComputeWorkerThreadCount(JobPoolKind::ChunkIo), "ChunkIo")
  {
  }

  void RequestLoad(glm::ivec3 coord, UChunkStorageService &storage,
                   UBlockRegistry &registry,
                   const std::string &worldFolder, ChunkGenerationToken token,
                   std::shared_ptr<std::atomic<bool>> cancellation);
  void RequestDiskIndexWarmup(UChunkStorageService &storage,
                              const std::string &worldFolder);
  void RequestSave(glm::ivec3 coord, UChunkStorageService &storage,
                   const std::string &worldFolder, const UBlockWorld &world,
                   UBlockRegistry &registry, ChunkGenerationToken token);
  void RequestSaveColumnLightFlags(
      std::string worldFolder, uint64_t revision,
      std::vector<glm::ivec2> completeColumns);

  std::vector<AsyncChunkLoadResult> DrainLoads();
  std::vector<AsyncChunkLoadResult> DrainLoadsUpTo(std::size_t max_count);
  void RequeueLoads(std::vector<AsyncChunkLoadResult> &&loads)
  {
    CompletedLoads.PushRange(std::move(loads));
  }
  template <typename Compare>
  std::vector<AsyncChunkLoadResult>
  DrainLoadsBestUpTo(std::size_t max_count, Compare &&compare)
  {
    return CompletedLoads.DrainBestUpTo(max_count, compare);
  }
  template <typename KeyFn>
  std::vector<AsyncChunkLoadResult>
  DrainLoadsBestByKeyUpTo(std::size_t max_count, KeyFn &&key_fn,
                          std::size_t *available_count = nullptr)
  {
    return CompletedLoads.DrainBestByKeyUpTo(max_count, key_fn,
                                             available_count);
  }
  std::vector<AsyncChunkSaveRequest> DrainSaves();
  std::vector<AsyncColumnLightFlagsSaveResult> DrainColumnLightFlagsSaves();
  bool WaitForColumnLightFlagsSaveIdleFor(std::chrono::milliseconds timeout);
  void WaitForColumnLightFlagsSaveIdle();
  bool CompletedColumnLightFlagsSavesEmpty() const;
  void WaitIdle();
  bool WaitIdleFor(std::chrono::milliseconds timeout);
  void CancelPending();
  void NoteLoadCancellation();
  std::size_t DiscardCancelledLoads();
  bool CompletedLoadsEmpty() const;
  bool CompletedSavesEmpty() const;
  std::size_t GetPendingJobCount() const { return Pool.GetPendingJobCount(); }
  std::size_t GetActiveJobCount() const { return Pool.GetActiveJobCount(); }
  std::size_t GetWorkerCount() const { return Pool.GetWorkerCount(); }
  std::size_t GetCompletedLoadCount() const { return CompletedLoads.Size(); }
  std::size_t GetCompletedSaveCount() const { return CompletedSaves.Size(); }

private:
  // Completion queues must outlive Pool (destroy order = reverse declaration).
  UCompletedJobQueue<AsyncChunkLoadResult> CompletedLoads;
  UCompletedJobQueue<AsyncChunkSaveRequest> CompletedSaves;
  UJobThreadPool Pool;
  // Light-completion metadata is rare and coalesced by WorldPersistence. Keep
  // it off the chunk-I/O pool so terrain cancellation cannot drop its writer.
  UCompletedJobQueue<AsyncColumnLightFlagsSaveResult>
      CompletedColumnLightFlagsSaves;
  UJobThreadPool ColumnLightFlagsPool{1, "ColumnLightFlagsSave"};
  std::atomic<bool> CancelledLoadSweepPending{false};
  std::unordered_set<std::string> DiskIndexWarmupFolders;
};

UChunkBuffer ParseChunkJsonToBuffer(const std::string &jsonText,
                                    glm::ivec3 chunkCoord,
                                    UBlockRegistry &registry);

std::string SerializeChunkToJson(glm::ivec3 chunkCoord, const UChunk &chunk,
                                 UBlockRegistry &registry);

} // namespace cutum
