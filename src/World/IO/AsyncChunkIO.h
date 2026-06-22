#pragma once

#include "Core/Jobs/JobThreadPool.h"
#include "World/Chunks/ChunkBuffer.h"
#include "World/Chunks/ChunkGenerationToken.h"
#include "World/IO/ChunkStorageTypes.h"
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
  void RequestLoad(glm::ivec3 coord, UChunkStorageService &storage,
                   const std::string &worldFolder, ChunkGenerationToken token);
  void RequestSave(glm::ivec3 coord, UChunkStorageService &storage,
                   const std::string &worldFolder, const UBlockWorld &world,
                   UBlockRegistry &registry, ChunkGenerationToken token);

  std::vector<AsyncChunkLoadResult> DrainLoads();
  std::vector<AsyncChunkSaveRequest> DrainSaves();

private:
  UJobThreadPool Pool;
  CompletedJobQueue<AsyncChunkLoadResult> CompletedLoads;
  CompletedJobQueue<AsyncChunkSaveRequest> CompletedSaves;
};

ChunkBuffer ParseChunkJsonToBuffer(const std::string &jsonText,
                                   glm::ivec3 chunkCoord,
                                   UBlockRegistry &registry);

std::string SerializeChunkToJson(glm::ivec3 chunkCoord, const UChunk &chunk,
                                 UBlockRegistry &registry);

} // namespace cutum
