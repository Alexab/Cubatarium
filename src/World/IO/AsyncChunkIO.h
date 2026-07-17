#pragma once

#include "Core/Jobs/JobThreadBudget.h"
#include "Core/Jobs/JobThreadPool.h"
#include "World/Chunks/ChunkBuffer.h"
#include "World/Chunks/ChunkGenerationToken.h"
#include "World/IO/ChunkStorageTypes.h"
#include <chrono>
#include <filesystem>
#include <string>
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
  std::vector<uint8_t> payload;
  ChunkDiskFormat format{ChunkDiskFormat::Absent};
  bool success{false};
};

struct AsyncChunkSaveRequest
{
  glm::ivec3 coord;
  glm::ivec3 groundCoord;
  std::vector<uint8_t> payload;
  std::string filePath;
  ChunkDiskFormat format{ChunkDiskFormat::Binary};
};

class UAsyncChunkIO
{
public:
  UAsyncChunkIO()
      : Pool(ComputeWorkerThreadCount(JobPoolKind::ChunkIo), "ChunkIo")
  {
  }

  void RequestLoad(glm::ivec3 coord, UChunkStorageService &storage,
                   const std::string &worldFolder, ChunkGenerationToken token);
  void RequestSave(glm::ivec3 coord, UChunkStorageService &storage,
                   const std::string &worldFolder, const UBlockWorld &world,
                   UBlockRegistry &registry, ChunkGenerationToken token);

  std::vector<AsyncChunkLoadResult> DrainLoads();
  std::vector<AsyncChunkLoadResult> DrainLoadsUpTo(std::size_t max_count);
  std::vector<AsyncChunkSaveRequest> DrainSaves();
  void WaitIdle();
  bool WaitIdleFor(std::chrono::milliseconds timeout);
  void CancelPending();
  bool CompletedLoadsEmpty() const;
  bool CompletedSavesEmpty() const;

private:
  // Completion queues must outlive Pool (destroy order = reverse declaration).
  UCompletedJobQueue<AsyncChunkLoadResult> CompletedLoads;
  UCompletedJobQueue<AsyncChunkSaveRequest> CompletedSaves;
  UJobThreadPool Pool;
};

UChunkBuffer ParseChunkJsonToBuffer(const std::string &jsonText,
                                    glm::ivec3 chunkCoord,
                                    UBlockRegistry &registry);

std::string SerializeChunkToJson(glm::ivec3 chunkCoord, const UChunk &chunk,
                                 UBlockRegistry &registry);

} // namespace cutum
