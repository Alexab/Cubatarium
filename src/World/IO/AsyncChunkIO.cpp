#include "World/IO/AsyncChunkIO.h"
#include "Blocks/BlockRegistry.h"
#include "World/Chunks/Chunk.h"
#include "World/Core/BlockWorld.h"
#include "World/IO/ChunkStorageService.h"
#include "World/IO/JsonChunkSerializer.h"
#include <cstdlib>
#include <fstream>

namespace cutum
{
namespace
{
bool IsAsyncChunkIoTraceEnabled()
{
  const char *value = std::getenv("CUBA_WORLD_COLUMN_SOURCE_TRACE");
  return value && value[0] == '1';
}
} // namespace

void UAsyncChunkIO::RequestLoad(glm::ivec3 coord, UChunkStorageService &storage,
                                const std::string &worldFolder,
                                ChunkGenerationToken token)
{
  const bool trace_io = IsAsyncChunkIoTraceEnabled();
  const auto detect_started = trace_io ? std::chrono::steady_clock::now()
                                       : std::chrono::steady_clock::time_point{};
  const ChunkDiskFormat format = storage.DetectFormatOnDisk(worldFolder, coord);
  const double format_detect_ms =
      trace_io ? std::chrono::duration<double, std::milli>(
                     std::chrono::steady_clock::now() - detect_started)
                     .count()
               : 0.0;
  const auto submitted_at = trace_io ? std::chrono::steady_clock::now()
                                     : std::chrono::steady_clock::time_point{};
  if (format == ChunkDiskFormat::Absent)
  {
    AsyncChunkLoadResult result;
    result.coord = coord;
    result.token = token;
    result.submittedAt = submitted_at;
    if (trace_io)
    {
      result.workerFinishedAt = std::chrono::steady_clock::now();
    }
    result.formatDetectMs = format_detect_ms;
    CompletedLoads.Push(std::move(result));
    return;
  }

  const std::string filePath =
      storage.ChunkFilePath(worldFolder, coord, format);
  Pool.Enqueue(
      [this, coord, filePath, token, format, submitted_at,
       format_detect_ms, trace_io]()
      {
        AsyncChunkLoadResult result;
        result.coord = coord;
        result.token = token;
        result.format = format;
        result.submittedAt = submitted_at;
        result.formatDetectMs = format_detect_ms;
        if (trace_io)
        {
          result.workerStartedAt = std::chrono::steady_clock::now();
        }
        const auto open_started = result.workerStartedAt;
        std::ifstream file(filePath, std::ios::binary);
        if (trace_io)
        {
          result.fileOpenMs = std::chrono::duration<double, std::milli>(
                                  std::chrono::steady_clock::now() -
                                  open_started)
                                  .count();
        }
        if (!file.is_open())
        {
          if (trace_io)
          {
            result.workerFinishedAt = std::chrono::steady_clock::now();
          }
          CompletedLoads.Push(std::move(result));
          return;
        }
        const auto read_started =
            trace_io ? std::chrono::steady_clock::now()
                     : std::chrono::steady_clock::time_point{};
        result.payload.assign(std::istreambuf_iterator<char>(file),
                              std::istreambuf_iterator<char>());
        if (trace_io)
        {
          result.fileReadMs = std::chrono::duration<double, std::milli>(
                                  std::chrono::steady_clock::now() -
                                  read_started)
                                  .count();
        }
        result.success = !result.payload.empty();
        if (trace_io)
        {
          result.workerFinishedAt = std::chrono::steady_clock::now();
        }
        CompletedLoads.Push(std::move(result));
      });
}

void UAsyncChunkIO::RequestSave(glm::ivec3 coord, UChunkStorageService &storage,
                                const std::string &worldFolder,
                                const UBlockWorld &world,
                                UBlockRegistry &registry,
                                ChunkGenerationToken token)
{
  const UChunk *chunk = world.GetChunkManager().GetChunk(coord);
  if (!chunk)
  {
    return;
  }
  const SerializedChunk serialized =
      storage.SerializeChunk(coord, *chunk, registry);
  const std::string filePath =
      storage.ChunkFilePath(worldFolder, coord, serialized.format);
  const glm::ivec3 ground(coord.x, 0, coord.z);
  (void)token;
  Pool.Enqueue(
      [this, filePath, serialized, coord, ground]()
      {
        std::filesystem::create_directories(
            std::filesystem::path(filePath).parent_path());
        const std::string tempPath = filePath + ".tmp";
        {
          std::ofstream file(tempPath, std::ios::binary);
          if (!file.is_open())
          {
            return;
          }
          file.write(reinterpret_cast<const char *>(serialized.bytes.data()),
                     static_cast<std::streamsize>(serialized.bytes.size()));
          if (!file.good())
          {
            std::filesystem::remove(tempPath);
            return;
          }
        }
        std::error_code ec;
        std::filesystem::rename(tempPath, filePath, ec);
        if (ec)
        {
          std::filesystem::remove(filePath, ec);
          ec.clear();
          std::filesystem::rename(tempPath, filePath, ec);
        }
        AsyncChunkSaveRequest done;
        done.coord = coord;
        done.groundCoord = ground;
        done.payload = serialized.bytes;
        done.filePath = filePath;
        done.format = serialized.format;
        CompletedSaves.Push(std::move(done));
      });
}

std::vector<AsyncChunkLoadResult> UAsyncChunkIO::DrainLoads()
{
  return CompletedLoads.DrainAll();
}

std::vector<AsyncChunkLoadResult> UAsyncChunkIO::DrainLoadsUpTo(
    std::size_t max_count)
{
  return CompletedLoads.DrainUpTo(max_count);
}

std::vector<AsyncChunkSaveRequest> UAsyncChunkIO::DrainSaves()
{
  return CompletedSaves.DrainAll();
}

void UAsyncChunkIO::WaitIdle()
{
  Pool.WaitIdle();
}

bool UAsyncChunkIO::WaitIdleFor(const std::chrono::milliseconds timeout)
{
  return Pool.WaitIdleFor(timeout);
}

void UAsyncChunkIO::CancelPending()
{
  Pool.CancelPendingJobs();
}

bool UAsyncChunkIO::CompletedLoadsEmpty() const
{
  return CompletedLoads.Empty();
}

bool UAsyncChunkIO::CompletedSavesEmpty() const
{
  return CompletedSaves.Empty();
}

UChunkBuffer ParseChunkJsonToBuffer(const std::string &jsonText,
                                    glm::ivec3 chunkCoord,
                                    UBlockRegistry &registry)
{
  UJsonChunkSerializer serializer;
  std::vector<uint8_t> bytes(jsonText.begin(), jsonText.end());
  return serializer.Deserialize(bytes, chunkCoord, registry);
}

std::string SerializeChunkToJson(glm::ivec3 chunkCoord, const UChunk &chunk,
                                 UBlockRegistry &registry)
{
  UJsonChunkSerializer serializer;
  const SerializedChunk serialized =
      serializer.Serialize(chunkCoord, chunk, registry);
  return std::string(serialized.bytes.begin(), serialized.bytes.end());
}

} // namespace cutum
