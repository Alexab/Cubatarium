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

void UAsyncChunkIO::RequestDiskIndexWarmup(
    UChunkStorageService &storage, const std::string &worldFolder)
{
  if (worldFolder.empty() ||
      !DiskIndexWarmupFolders.insert(worldFolder).second)
  {
    return;
  }
  Pool.Enqueue([&storage, worldFolder]()
  { storage.PrepareHighestChunkSliceIndex(worldFolder); });
}

void UAsyncChunkIO::RequestLoad(glm::ivec3 coord, UChunkStorageService &storage,
                                UBlockRegistry &registry,
                                const std::string &worldFolder,
                                ChunkGenerationToken token,
                                std::shared_ptr<std::atomic<bool>> cancellation)
{
  const auto is_cancelled = [&]()
  {
    return cancellation &&
           cancellation->load(std::memory_order_acquire);
  };
  if (is_cancelled())
  {
    return;
  }
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
    result.cancellation = cancellation;
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
  const ChunkStorageSettings worker_storage_settings = storage.GetSettings();
  Pool.Enqueue(
      [this, coord, filePath, token, format, submitted_at,
       format_detect_ms, trace_io, cancellation, &registry,
       worker_storage_settings]()
      {
        const auto is_cancelled = [&]()
        {
          return cancellation &&
                 cancellation->load(std::memory_order_acquire);
        };
        if (is_cancelled())
        {
          return;
        }
        AsyncChunkLoadResult result;
        result.coord = coord;
        result.token = token;
        result.cancellation = cancellation;
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
          if (is_cancelled())
          {
            return;
          }
          if (trace_io)
          {
            result.workerFinishedAt = std::chrono::steady_clock::now();
          }
          CompletedLoads.Push(std::move(result));
          if (is_cancelled())
          {
            NoteLoadCancellation();
          }
          return;
        }
        if (is_cancelled())
        {
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
        if (is_cancelled())
        {
          NoteLoadCancellation();
          return;
        }
        result.success = !result.payload.empty();
        if (result.success)
        {
          const auto deserialize_started = std::chrono::steady_clock::now();
          try
          {
            UChunkStorageService worker_storage(worker_storage_settings);
            result.decodedBuffer = worker_storage.DeserializeChunk(
                result.payload, coord, format, registry);
          }
          catch (...)
          {
            result.success = false;
            result.payload.clear();
          }
          result.deserializeMs = std::chrono::duration<double, std::milli>(
                                    std::chrono::steady_clock::now() -
                                    deserialize_started)
                                    .count();
          std::vector<uint8_t>().swap(result.payload);
        }
        if (is_cancelled())
        {
          NoteLoadCancellation();
          return;
        }
        if (trace_io)
        {
          result.workerFinishedAt = std::chrono::steady_clock::now();
        }
        CompletedLoads.Push(std::move(result));
        if (is_cancelled())
        {
          NoteLoadCancellation();
        }
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
    AsyncChunkSaveRequest failed;
    failed.coord = coord;
    failed.groundCoord = glm::ivec3(coord.x, 0, coord.z);
    failed.filePath = storage.ChunkFilePath(
        worldFolder, coord, ChunkDiskFormat::Binary);
    failed.error = "chunk_missing_before_serialize";
    CompletedSaves.Push(std::move(failed));
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
        AsyncChunkSaveRequest done;
        done.coord = coord;
        done.groundCoord = ground;
        done.filePath = filePath;
        done.format = serialized.format;
        const auto finish = [this, &done](const std::string &error = {})
        {
          done.success = error.empty();
          done.error = error;
          CompletedSaves.Push(std::move(done));
        };

        std::error_code ec;
        std::filesystem::create_directories(
            std::filesystem::path(filePath).parent_path(), ec);
        if (ec)
        {
          finish("create_directories: " + ec.message());
          return;
        }
        const std::string tempPath = filePath + ".tmp";
        {
          std::ofstream file(tempPath, std::ios::binary);
          if (!file.is_open())
          {
            finish("open_temp_failed");
            return;
          }
          file.write(reinterpret_cast<const char *>(serialized.bytes.data()),
                     static_cast<std::streamsize>(serialized.bytes.size()));
          file.close();
          if (!file.good())
          {
            std::error_code cleanup_ec;
            std::filesystem::remove(tempPath, cleanup_ec);
            finish("write_temp_failed");
            return;
          }
        }
        ec.clear();
        std::filesystem::rename(tempPath, filePath, ec);
        if (ec)
        {
          std::error_code remove_ec;
          std::filesystem::remove(filePath, remove_ec);
          ec.clear();
          std::filesystem::rename(tempPath, filePath, ec);
        }
        if (ec)
        {
          std::error_code cleanup_ec;
          std::filesystem::remove(tempPath, cleanup_ec);
          finish("replace_failed: " + ec.message());
          return;
        }
        finish();
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

void UAsyncChunkIO::NoteLoadCancellation()
{
  CancelledLoadSweepPending.store(true, std::memory_order_release);
}

std::size_t UAsyncChunkIO::DiscardCancelledLoads()
{
  if (!CancelledLoadSweepPending.exchange(false, std::memory_order_acq_rel))
  {
    return 0;
  }
  return CompletedLoads.EraseIf([](const AsyncChunkLoadResult &result)
  {
    return result.cancellation &&
           result.cancellation->load(std::memory_order_acquire);
  });
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
