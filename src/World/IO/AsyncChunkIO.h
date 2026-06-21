#pragma once

#include "Core/Jobs/JobThreadPool.h"
#include "World/Chunks/ChunkBuffer.h"
#include "World/Chunks/ChunkGenerationToken.h"
#include <filesystem>
#include <string>
#include <vector>

namespace cutum
{

class UBlockRegistry;
class UBlockWorld;
class UChunk;

struct AsyncChunkLoadResult
{
  glm::ivec3 coord;
  ChunkGenerationToken token;
  std::string jsonText;
  bool success{false};
};

struct AsyncChunkSaveRequest
{
  glm::ivec3 coord;
  std::string jsonText;
  std::string filePath;
};

class UAsyncChunkIO
{
public:
  void RequestLoad(glm::ivec3 coord, const std::string &worldFolder,
                   ChunkGenerationToken token);
  void RequestSave(glm::ivec3 coord, const std::string &worldFolder,
                   const UBlockWorld &world, UBlockRegistry &registry,
                   ChunkGenerationToken token);

  std::vector<AsyncChunkLoadResult> DrainLoads();
  void TickSaves();

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
