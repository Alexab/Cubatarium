#include "World/IO/AsyncChunkIO.h"
#include "App/Platform/Log.h"
#include "Blocks/BlockRegistry.h"
#include "Core/Memory/CallbackStorageSource.h"
#include "Core/Memory/NameStorageSource.h"
#include "Core/Memory/RootStorageSource.h"
#include "World/Chunks/Chunk.h"
#include "World/Core/BlockWorld.h"
#include "World/IO/ChunkStorageService.h"
#include "World/IO/JsonChunkSerializer.h"
#include "World/Lighting/LightCacheCertificate.h"
#include <cstdlib>
#include <fstream>
#include <mutex>
#include <nlohmann/json.hpp>
#include <system_error>

#ifdef _WIN32
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#endif

namespace cutum
{
using json = nlohmann::json;
using Clock = std::chrono::steady_clock;

namespace
{
bool IsAsyncChunkIoTraceEnabled()
{
  const char *value = std::getenv("CUBA_WORLD_COLUMN_SOURCE_TRACE");
  return value && value[0] == '1';
}

bool IsStreamingDetailTraceEnabled()
{
  const char *value = std::getenv("CUBA_STREAMING_DETAIL_TRACE");
  return value && value[0] == '1';
}

void LogColumnLightFlagsSaveResult(
    const AsyncColumnLightFlagsSaveResult &result)
{
  const bool trace_detail =
      IsStreamingDetailTraceEnabled() &&
      (result.worker_queue_wait_ms >= 10.0 ||
       result.worker_service_ms >= 50.0);
  if (result.success && !trace_detail)
  {
    return;
  }

  struct LogThrottle
  {
    std::mutex mutex;
    std::chrono::steady_clock::time_point next_log_at{};
    uint64_t suppressed{0};
  };
  static LogThrottle throttle;

  // Result diagnostics run on the dedicated save worker, and repeated failures
  // are rate-limited so a broken target cannot flood the log or stall saves.
  uint64_t suppressed = 0;
  {
    std::lock_guard<std::mutex> lock(throttle.mutex);
    const auto now = std::chrono::steady_clock::now();
    if (now < throttle.next_log_at)
    {
      ++throttle.suppressed;
      return;
    }
    suppressed = throttle.suppressed;
    throttle.suppressed = 0;
    throttle.next_log_at = now + std::chrono::seconds(1);
  }

  const std::string message = result.success
      ? "detail=light_flags_save_result revision=" +
            std::to_string(result.revision) + " folder=" + result.worldFolder +
            " success=1 worker_queue_wait_ms=" +
            std::to_string(result.worker_queue_wait_ms) +
            " worker_service_ms=" +
            std::to_string(result.worker_service_ms) +
            " suppressed_since_previous=" + std::to_string(suppressed)
      : "outcome=light_flags_write_failed folder=" + result.worldFolder +
            " revision=" + std::to_string(result.revision) +
            " error=" + result.error + " worker_queue_wait_ms=" +
            std::to_string(result.worker_queue_wait_ms) +
            " worker_service_ms=" +
            std::to_string(result.worker_service_ms) +
            " suppressed_since_previous=" + std::to_string(suppressed);
  CubatariumLogInfo(result.success ? "StreamingDetail" : "WorldColumnSave",
                    message);
}
} // namespace

struct UAsyncChunkIO::SaveFlagsJob
{
  UAsyncChunkIO *owner;
  std::string worldFolder;
  uint64_t revision;
  std::vector<glm::ivec2> completeColumns;
  std::string lightSignature;
  Clock::time_point enqueue_started;

  void operator()();
};

void UAsyncChunkIO::SaveFlagsJob::operator()()
{
  const auto worker_started = std::chrono::steady_clock::now();
  AsyncColumnLightFlagsSaveResult result;
  result.worldFolder = worldFolder;
  result.revision = revision;
  result.worker_queue_wait_ms = std::chrono::duration<double, std::milli>(
                                    worker_started - enqueue_started)
                                    .count();
  try
  {
    const json data = EncodeColumnLightCache(std::move(completeColumns),
                                             lightSignature);
    const std::string encoded = data.dump();

    const std::filesystem::path target =
        std::filesystem::path(worldFolder) / "column_light.json";
    const std::filesystem::path temp = target.string() + ".tmp";
    std::error_code ec;
    std::filesystem::create_directories(target.parent_path(), ec);
    if (ec)
    {
      result.error = "create_directories: " + ec.message();
    }
    else
    {
      {
        std::ofstream file(temp, std::ios::binary | std::ios::trunc);
        if (!file.is_open())
        {
          result.error = "open_temp_failed";
        }
        else
        {
          file.write(encoded.data(),
                     static_cast<std::streamsize>(encoded.size()));
          file.flush();
          if (!file.good())
          {
            result.error = "write_temp_failed";
          }
          file.close();
          if (result.error.empty() && !file.good())
          {
            result.error = "close_temp_failed";
          }
        }
      }

      if (result.error.empty())
      {
#ifdef _WIN32
        if (!MoveFileExW(temp.c_str(), target.c_str(),
                         MOVEFILE_REPLACE_EXISTING |
                             MOVEFILE_WRITE_THROUGH))
        {
          result.error =
              "replace_failed: " +
              std::system_category().message(
                  static_cast<int>(GetLastError()));
        }
#else
        std::filesystem::rename(temp, target, ec);
        if (ec)
        {
          result.error = "replace_failed: " + ec.message();
        }
#endif
      }
      if (!result.error.empty())
      {
        std::error_code cleanup_ec;
        std::filesystem::remove(temp, cleanup_ec);
      }
      else
      {
        result.success = true;
      }
    }
  }
  catch (const std::exception &e)
  {
    result.error = std::string("exception: ") + e.what();
  }
  catch (...)
  {
    result.error = "unknown_exception";
  }
  result.worker_service_ms = std::chrono::duration<double, std::milli>(
                                 std::chrono::steady_clock::now() -
                                 worker_started)
                                 .count();
  LogColumnLightFlagsSaveResult(result);
  const uint64_t result_revision = result.revision;
  double publish_wait_ms = 0.0;
  double publish_held_ms = 0.0;
  owner->CompletedColumnLightFlagsSaves.Push(std::move(result),
                                       &publish_wait_ms,
                                       &publish_held_ms);
  if (IsStreamingDetailTraceEnabled() &&
      (publish_wait_ms >= 1.0 || publish_held_ms >= 1.0))
  {
    const std::string message =
        "detail=light_flags_result_publish revision=" +
        std::to_string(result_revision) + " queue_wait_ms=" +
        std::to_string(publish_wait_ms) + " queue_held_ms=" +
        std::to_string(publish_held_ms);
    CubatariumLogInfo("StreamingDetail", message);
  }
}

void UAsyncChunkIO::RequestSaveColumnLightFlags(
    std::string worldFolder, const uint64_t revision,
    std::vector<glm::ivec2> completeColumns, std::string lightSignature)
{
  const auto enqueue_started = std::chrono::steady_clock::now();
  ColumnLightFlagsPool.Enqueue(
      SaveFlagsJob{this, std::move(worldFolder), revision, std::move(completeColumns),
                   std::move(lightSignature), enqueue_started});
}

struct UAsyncChunkIO::IndexWarmupJob
{
  UChunkStorageService &storage;
  std::string worldFolder;

  void operator()() const;
};

void UAsyncChunkIO::IndexWarmupJob::operator()() const
{ storage.PrepareHighestChunkSliceIndex(worldFolder); }

void UAsyncChunkIO::RequestDiskIndexWarmup(
    UChunkStorageService &storage, const std::string &worldFolder)
{
  if (worldFolder.empty() ||
      !DiskIndexWarmupFolders.insert(worldFolder).second)
  {
    return;
  }
  EnqueueBackgroundIo(IndexWarmupJob{storage, worldFolder});
}

struct UAsyncChunkIO::LoadJob
{
  UAsyncChunkIO *owner;
  glm::ivec3 coord;
  const std::string filePath;
  ChunkGenerationToken token;
  const ChunkDiskFormat format;
  const Clock::time_point submitted_at;
  const double format_detect_ms;
  const bool trace_io;
  std::shared_ptr<std::atomic<bool>> cancellation;
  uint64_t demandId;
  UBlockRegistry &registry;
  const ChunkStorageSettings worker_storage_settings;
  bool includeColumnLight;
  std::string worldFolder;
  bool injectInvalidFrame;
  const std::shared_ptr<AsyncLoadReadProbeGate> read_probe_gate;

  void operator()() const;
};

void UAsyncChunkIO::LoadJob::operator()() const
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
  result.demandId = demandId;
  result.token = token;
  result.cancellation = cancellation;
  result.format = format;
  result.submittedAt = submitted_at;
  result.formatDetectMs = format_detect_ms;
  if (trace_io)
    result.workerStartedAt = std::chrono::steady_clock::now();
  if (includeColumnLight)
  {
    UChunkStorageService worker_storage(worker_storage_settings);
    result.columnLight =
        worker_storage.ReadColumnLight(worldFolder, coord);
  }
  if (is_cancelled())
    return;
  if (format == ChunkDiskFormat::Absent)
  {
    owner->PushCompletedLoad(std::move(result));
    return;
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
    owner->PushCompletedLoad(std::move(result));
    if (is_cancelled())
    {
      owner->NoteLoadCancellation();
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
    owner->NoteLoadCancellation();
    return;
  }
  result.success = !result.payload.empty();
  if (result.success)
  {
    // Diagnostic only: the saved file is never modified. Corrupt the
    // actual read buffer so the normal codec/commit path must reject it.
    if (injectInvalidFrame)
    {
      result.payload.push_back(format == ChunkDiskFormat::Json ? 'x' : 0);
      result.injectedInvalidFrame = true;
    }
    const auto deserialize_started = std::chrono::steady_clock::now();
    try
    {
      UChunkStorageService worker_storage(worker_storage_settings);
      result.decodedBuffer = std::make_unique<UChunkBuffer>(
          worker_storage.DeserializeChunk(result.payload, coord, format,
                                          registry));
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
  if (read_probe_gate && read_probe_gate->demandId == demandId)
  {
    read_probe_gate->entered.store(true, std::memory_order_release);
    const auto deadline = std::chrono::steady_clock::now() + read_probe_gate->limit;
    while (!read_probe_gate->released.load(std::memory_order_acquire) &&
           std::chrono::steady_clock::now() < deadline)
      std::this_thread::yield();
    if (!read_probe_gate->released.load(std::memory_order_acquire))
    {
      read_probe_gate->timedOut.store(true, std::memory_order_release);
      result.success = false;
      result.decodedBuffer.reset();
    }
  }
  if (is_cancelled())
  {
    owner->NoteLoadCancellation();
    return;
  }
  if (trace_io)
  {
    result.workerFinishedAt = std::chrono::steady_clock::now();
  }
  owner->PushCompletedLoad(std::move(result));
  if (is_cancelled())
  {
    owner->NoteLoadCancellation();
  }
}

void UAsyncChunkIO::RequestLoad(glm::ivec3 coord, UChunkStorageService &storage,
                                UBlockRegistry &registry,
                                const std::string &worldFolder,
                                ChunkGenerationToken token,
                                std::shared_ptr<std::atomic<bool>> cancellation,
                                uint64_t demandId, bool includeColumnLight,
                                bool injectInvalidFrame)
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
  if (format == ChunkDiskFormat::Absent && !includeColumnLight)
  {
    AsyncChunkLoadResult result;
    result.coord = coord;
    result.demandId = demandId;
    result.token = token;
    result.cancellation = cancellation;
    result.submittedAt = submitted_at;
    if (trace_io)
    {
      result.workerFinishedAt = std::chrono::steady_clock::now();
    }
    result.formatDetectMs = format_detect_ms;
    PushCompletedLoad(std::move(result));
    return;
  }

  const std::string filePath =
      storage.ChunkFilePath(worldFolder, coord, format);
  const ChunkStorageSettings worker_storage_settings = storage.GetSettings();
  const auto read_probe_gate = LoadReadProbeGate;
  LoadPool.Enqueue(
      LoadJob{this, coord, filePath, token, format, submitted_at, format_detect_ms,
              trace_io, cancellation, demandId, registry, worker_storage_settings,
              includeColumnLight, worldFolder, injectInvalidFrame, read_probe_gate});
}

struct UAsyncChunkIO::SaveReceiptsJob
{
  UAsyncChunkIO *owner;
  glm::ivec3 column;
  std::shared_ptr<const PersistedColumnLight> receipts;
  const ChunkStorageSettings settings;
  std::string worldFolder;
  uint64_t saveBatchId;

  void operator()() const;
};

void UAsyncChunkIO::SaveReceiptsJob::operator()() const
{
  AsyncChunkSaveRequest done;
  done.saveBatchId = saveBatchId;
  done.coord = done.groundCoord = column;
  done.worldFolder = worldFolder;
  done.filePath =
      UChunkStorageService::ColumnLightFilePath(worldFolder, column);
  // Metadata never enters the terrain disk index or changes known-AIR
  // evidence.
  done.cleanupOnly = true;
  done.cleanupOperation = "column_light_receipts";
  try
  {
    UChunkStorageService worker_storage(settings);
    done.success = receipts && worker_storage.WriteColumnLight(
                                   worldFolder, column, *receipts);
    if (!done.success)
      done.error = "column_light_write_failed";
  }
  catch (const std::exception &error)
  {
    done.error = error.what();
  }
  owner->CompletedSaves.Push(std::move(done));
}

void UAsyncChunkIO::RequestSaveColumnLight(
    glm::ivec3 column, std::shared_ptr<const PersistedColumnLight> receipts,
    UChunkStorageService &storage, const std::string &worldFolder, uint64_t saveBatchId)
{
  const auto settings = storage.GetSettings();
  column.y = 0;
  EnqueueBackgroundIo(
      SaveReceiptsJob{this, column, std::move(receipts), settings, worldFolder, saveBatchId});
}

struct UAsyncChunkIO::SaveJob
{
  UAsyncChunkIO *owner;
  const std::shared_ptr<const UChunk> snapshot;
  const ChunkStorageSettings settings;
  std::string worldFolder;
  glm::ivec3 coord;
  const glm::ivec3 ground;
  uint64_t saveBatchId;
  const std::shared_ptr<AsyncSaveWriteProbeGate> probe_gate;

  void operator()() const;
};

void UAsyncChunkIO::SaveJob::operator()() const
{
  AsyncChunkSaveRequest done;
  done.saveBatchId = saveBatchId;
  done.coord = coord;
  done.groundCoord = ground;
  done.worldFolder = worldFolder;
  const auto finish = [io = owner, &done](const std::string &error = {})
  {
    done.success = error.empty();
    done.error = error;
    io->CompletedSaves.Push(std::move(done));
  };

  try
  {
    if (probe_gate)
    {
      probe_gate->entered.store(true, std::memory_order_release);
      const auto deadline = std::chrono::steady_clock::now() + probe_gate->limit;
      while (!probe_gate->released.load(std::memory_order_acquire))
      {
        if (std::chrono::steady_clock::now() >= deadline)
        {
          finish("save_probe_gate_timeout");
          return;
        }
        std::this_thread::sleep_for(std::chrono::milliseconds(1));
      }
    }
    UChunkStorageService worker_storage(settings);
    // Current CCHK4/JSON4 serialization writes numeric block IDs and
    // does not consult registry/catalog state. Do not retain a mutable
    // main-thread registry reference across a reload/world switch.
    UBlockRegistry numeric_registry(nullptr);
    const SerializedChunk serialized =
        worker_storage.SerializeChunk(coord, *snapshot, numeric_registry);
    done.format = serialized.format;
    done.filePath = worker_storage.ChunkFilePath(
        worldFolder, coord, serialized.format);

    std::error_code ec;
    std::filesystem::create_directories(
        std::filesystem::path(done.filePath).parent_path(), ec);
    if (ec)
    {
      finish("create_directories: " + ec.message());
      return;
    }
    const std::string tempPath = done.filePath + ".tmp";
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
#ifdef _WIN32
    const std::filesystem::path targetPath(done.filePath);
    const std::filesystem::path temporaryPath(tempPath);
    if (!MoveFileExW(temporaryPath.c_str(), targetPath.c_str(),
                     MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH))
      ec = std::error_code(GetLastError(), std::system_category());
#else
    std::filesystem::rename(tempPath, done.filePath, ec);
#endif
    if (ec)
    {
      std::error_code cleanup_ec;
      std::filesystem::remove(tempPath, cleanup_ec);
      finish("replace_failed: " + ec.message());
      return;
    }

    if (serialized.format == ChunkDiskFormat::Binary &&
        settings.writeFormat == ChunkWriteFormat::Binary &&
        settings.deleteLegacyJsonOnBinarySave)
    {
      const std::string legacy_json = worker_storage.ChunkFilePath(
          worldFolder, coord, ChunkDiskFormat::Json);
      std::error_code cleanup_ec;
      std::filesystem::remove(legacy_json, cleanup_ec);
    }
    finish();
  }
  catch (const std::exception &e)
  {
    finish(std::string("serialize_or_write_exception: ") + e.what());
  }
  catch (...)
  {
    finish("serialize_or_write_unknown_exception");
  }
}

void UAsyncChunkIO::RequestSave(glm::ivec3 coord, UChunkStorageService &storage,
                                const std::string &worldFolder,
                                const UBlockWorld &world,
                                UBlockRegistry &registry,
                                ChunkGenerationToken token, uint64_t saveBatchId)
{
  auto terrain_snapshot =
      world.GetChunkManager().MakeTerrainSaveSnapshot(coord);
  if (!terrain_snapshot)
  {
    AsyncChunkSaveRequest failed;
    failed.saveBatchId = saveBatchId;
    failed.coord = coord;
    failed.groundCoord = glm::ivec3(coord.x, 0, coord.z);
    failed.worldFolder = worldFolder;
    failed.filePath = storage.ChunkFilePath(
        worldFolder, coord, ChunkDiskFormat::Binary);
    failed.error = "chunk_missing_before_serialize";
    CompletedSaves.Push(std::move(failed));
    return;
  }
  // The world owns and mutates chunks on the main thread. Take a compact,
  // immutable snapshot here, then do palette/RLE/JSON serialization on the
  // I/O worker so unloading a column does not serialize every vertical slice
  // inside the streaming update.
  const std::shared_ptr<const UChunk> snapshot(std::move(terrain_snapshot));
  const ChunkStorageSettings settings = storage.GetSettings();
  const glm::ivec3 ground(coord.x, 0, coord.z);
  (void)token;
  const auto probe_gate = SaveWriteProbeGate;
  EnqueueBackgroundIo(
      SaveJob{this, snapshot, settings, worldFolder, coord, ground, saveBatchId, probe_gate});
}

struct UAsyncChunkIO::RemoveSlices
{
  glm::ivec3 groundCoord;
  const int firstCy;
  const int lastCy;

  bool operator()(UChunkStorageService &worker_storage,
                                     const std::string &folder,
                                     std::string &error) const;
};

bool UAsyncChunkIO::RemoveSlices::operator()(UChunkStorageService &worker_storage,
                                     const std::string &folder,
                                     std::string &error) const
{
  bool success = true;
  for (int cy = firstCy; cy <= lastCy; ++cy)
  {
    std::string remove_error;
    if (!worker_storage.RemoveChunkSliceFromDisk(
            folder, glm::ivec3(groundCoord.x, cy, groundCoord.z),
            &remove_error))
    {
      success = false;
      if (!error.empty())
      {
        error += "; ";
      }
      error += remove_error;
    }
  }
  return success;
}

void UAsyncChunkIO::RequestRemoveChunkSlices(
    glm::ivec3 groundCoord, const int firstCy, const int lastCy,
    UChunkStorageService &storage, const std::string &worldFolder, uint64_t saveBatchId)
{
  if (groundCoord.y != 0)
  {
    groundCoord.y = 0;
  }
  if (firstCy > lastCy)
  {
    return;
  }
  const int sliceCount = lastCy - firstCy + 1;
  const glm::ivec3 resultCoord(groundCoord.x, firstCy, groundCoord.z);
  EnqueueStorageCleanup(
      groundCoord, resultCoord, storage, worldFolder, "slice_range",
      sliceCount,
      RemoveSlices{groundCoord, firstCy, lastCy}, saveBatchId);
}

struct UAsyncChunkIO::RemoveColumn
{
  glm::ivec3 groundCoord;
  const int maxWorldY;
  const int minWorldY;

  bool operator()(UChunkStorageService &worker_storage,
                               const std::string &folder, std::string &error) const;
};

bool UAsyncChunkIO::RemoveColumn::operator()(UChunkStorageService &worker_storage,
                               const std::string &folder, std::string &error) const
{
  return worker_storage.RemoveTerrainColumnFromDisk(
      folder, groundCoord, maxWorldY, &error, minWorldY);
}

void UAsyncChunkIO::RequestRemoveTerrainColumn(
    glm::ivec3 groundCoord, const int maxWorldY, const int minWorldY,
    UChunkStorageService &storage, const std::string &worldFolder)
{
  if (groundCoord.y != 0)
  {
    groundCoord.y = 0;
  }
  EnqueueStorageCleanup(
      groundCoord, groundCoord, storage, worldFolder, "full_column", -1,
      RemoveColumn{groundCoord, maxWorldY, minWorldY});
}

struct UAsyncChunkIO::CleanupJob
{
  UAsyncChunkIO *owner;
  UChunkStorageService &storage;
  glm::ivec3 groundCoord;
  glm::ivec3 resultCoord;
  std::string worldFolder;
  std::string cleanupOperation;
  const int cleanupSliceCount;
  StorageCleanupJob cleanup;
  uint64_t saveBatchId;

  void operator()();
};

void UAsyncChunkIO::CleanupJob::operator()()
{
  AsyncChunkSaveRequest done;
  done.saveBatchId = saveBatchId;
  done.coord = resultCoord;
  done.groundCoord = groundCoord;
  done.worldFolder = worldFolder;
  done.cleanupOperation = std::move(cleanupOperation);
  done.cleanupOnly = true;
  done.cleanupSliceCount = cleanupSliceCount;
  const auto started = std::chrono::steady_clock::now();
  try
  {
    done.success = cleanup(storage, worldFolder, done.error);
  }
  catch (const std::exception &e)
  {
    done.success = false;
    done.error = std::string("cleanup_exception: ") + e.what();
  }
  catch (...)
  {
    done.success = false;
    done.error = "cleanup_unknown_exception";
  }
  done.cleanupMs = std::chrono::duration<double, std::milli>(
                       std::chrono::steady_clock::now() - started)
                       .count();
  owner->CompletedSaves.Push(std::move(done));
}

void UAsyncChunkIO::EnqueueStorageCleanup(
    glm::ivec3 groundCoord, glm::ivec3 resultCoord,
    UChunkStorageService &storage, const std::string &worldFolder,
    std::string cleanupOperation, const int cleanupSliceCount,
    StorageCleanupJob cleanup, uint64_t saveBatchId)
{
  if (groundCoord.y != 0)
  {
    groundCoord.y = 0;
  }
  EnqueueBackgroundIo(
      CleanupJob{this, storage, groundCoord, resultCoord, worldFolder,
                 std::move(cleanupOperation), cleanupSliceCount, std::move(cleanup), saveBatchId});
}

struct AsyncIoStorageInspector
{
  using Io = UAsyncChunkIO;

  explicit AsyncIoStorageInspector(const Io &io, AsyncIoStorageIdentity *scratch,
                                   size_t scratch_count)
      : Owner(io), Scratch(scratch), ScratchCount(scratch ? scratch_count : 0)
  {
    Result.retained_supported = RootOwnerRepresentationSupported;
  }

  AsyncIoStorage Inspect()
  {
    const auto visit = [&](const auto &job) { Pending(job); };
    Owner.LoadPool.InspectPendingJobs(visit, [&](auto counts, size_t bytes) {
      Pool(0, counts, bytes);
      if (Owner.BackgroundIoPool)
      {
        Owner.BackgroundIoPool->InspectPendingJobs(visit, [&](auto background, size_t backing) {
          Pool(1, background, backing);
          FlagsAndResults(visit);
        });
      }
      else
        FlagsAndResults(visit);
    });
    Result.supported = Result.retained_supported && Result.active == 0;
    return Result;
  }

private:
  void Add(size_t &destination, size_t bytes)
  {
    NameStorageEnvelope checked{destination, true, false};
    NameStorageAdd(checked, bytes);
    destination = checked.requested;
    FlagOverflow(checked);
  }

  void FlagOverflow(const NameStorageEnvelope &checked)
  {
    if (checked.overflow)
    {
      Result.overflow = true;
      Result.retained_supported = false;
    }
  }

  void Buffer(size_t &sum, size_t count, size_t width)
  {
    NameStorageEnvelope checked{0, true, false};
    const auto request = NameStorageBufferRequest(count, width, checked);
    FlagOverflow(checked);
    Add(sum, request);
  }

  void Text(size_t &sum, const std::string &text)
  {
    Buffer(sum, NameStorageExternalBytes(text), 1);
  }

  bool NewSharedAllocation(uintptr_t address, size_t bytes)
  {
    for (size_t i = 0; i < Used; ++i)
    {
      if (Scratch[i].allocation != address)
        continue;
      if (Scratch[i].bytes != bytes)
        Result.retained_supported = false;
      ++Result.shared_aliases;
      return false;
    }
    if (Used == ScratchCount)
    {
      Result.scratch_exhausted = true;
      Result.retained_supported = false;
      return false;
    }
    Scratch[Used++] = {address, bytes};
    Add(Result.shared_bytes, bytes);
    ++Result.shared_blocks;
    return true;
  }

  // Match the actual default factory and its embedded object address before
  // traversing any payload. Const and non-const make_shared are both used.
  template<class T>
  bool Shared(const std::shared_ptr<T> &owner)
  {
#if defined(_WIN64) && defined(_MSVC_STL_VERSION) && _ITERATOR_DEBUG_LEVEL == 0
    const auto words = RootOwnerWords(owner);
    if (RootOwnerRepresentationSupported && !owner && !words[1])
      return false;
    if (words[0] && words[1] && words[0] == reinterpret_cast<uintptr_t>(owner.get()))
    {
      const auto *control = reinterpret_cast<const std::_Ref_count_base *>(words[1]);
      using Mutable = std::_Ref_count_obj2<std::remove_const_t<T>>;
      using Const = std::_Ref_count_obj2<const std::remove_const_t<T>>;
      if (typeid(*control) == typeid(Mutable) &&
          std::addressof(static_cast<const Mutable *>(control)->_Storage._Value) == owner.get())
        return NewSharedAllocation(words[1], sizeof(Mutable));
      if (typeid(*control) == typeid(Const) &&
          std::addressof(static_cast<const Const *>(control)->_Storage._Value) == owner.get())
        return NewSharedAllocation(words[1], sizeof(Const));
    }
#else
    if (!owner)
      return false;
#endif
    Result.retained_supported = false;
    return false;
  }

  void Receipts(const std::shared_ptr<const PersistedColumnLight> &column)
  {
    if (!Shared(column))
      return;
    Buffer(Result.shared_bytes, column->capacity(), sizeof(PersistedColumnLight::value_type));
    for (const auto &section : *column)
    {
      if (!Shared(section))
        continue;
      Text(Result.shared_bytes, section->signature);
      Buffer(Result.shared_bytes, section->inputs.capacity(), sizeof(PersistedSectionLight::Input));
    }
  }

  void Snapshot(const std::shared_ptr<const UChunk> &chunk)
  {
#if defined(_WIN64) && defined(_MSVC_STL_VERSION) && _ITERATOR_DEBUG_LEVEL == 0
    // SaveJob snapshots originate exclusively at MakeTerrainSaveSnapshot.
    // The shared control is constructed from its ordinary unique allocation.
    const auto words = RootOwnerWords(chunk);
    if (words[0] && words[1] && words[0] == reinterpret_cast<uintptr_t>(chunk.get()))
    {
      using Control = std::_Ref_count_resource<UChunk *, std::default_delete<UChunk>>;
      const auto *control = reinterpret_cast<const std::_Ref_count_base *>(words[1]);
      if (typeid(*control) == typeid(Control))
      {
        if (NewSharedAllocation(words[1], sizeof(Control)))
        {
          Add(Result.shared_bytes, sizeof(UChunk));
          const auto &field = chunk->Light.field;
          if (Shared(field))
          {
            Shared(field->PaddedLight);
            for (const auto &proof : field->LightProofs)
              if (proof.input || proof.owner)
                Result.retained_supported = false;
          }
        }
        return;
      }
    }
#else
    (void)chunk;
#endif
    Result.retained_supported = false;
  }

  template<class Job> const Job *Target(const std::function<void()> &fn, AsyncIoJobKind kind)
  {
    const auto *job = fn.target<Job>();
    if (job)
    {
      ++Result.queued[static_cast<size_t>(kind)];
      Add(Result.callable_bytes, KnownCallbackRequestBytes<Job>(fn));
    }
    return job;
  }

  void Pending(const std::function<void()> &fn)
  {
    if (!RootOwnerRepresentationSupported)
      return;
    if (const auto *job = Target<Io::LoadJob>(fn, AsyncIoJobKind::Load))
    {
      Text(Result.queued_backing, job->filePath);
      Text(Result.queued_backing, job->worldFolder);
      Shared(job->cancellation);
      Shared(job->read_probe_gate);
    }
    else if (const auto *job = Target<Io::SaveJob>(fn, AsyncIoJobKind::Save))
    {
      Text(Result.queued_backing, job->worldFolder);
      Snapshot(job->snapshot);
      Shared(job->probe_gate);
    }
    else if (const auto *job = Target<Io::SaveReceiptsJob>(fn, AsyncIoJobKind::Receipts))
    {
      Text(Result.queued_backing, job->worldFolder);
      Receipts(job->receipts);
    }
    else if (const auto *job = Target<Io::SaveFlagsJob>(fn, AsyncIoJobKind::Flags))
    {
      Text(Result.queued_backing, job->worldFolder);
      Text(Result.queued_backing, job->lightSignature);
      Buffer(Result.queued_backing, job->completeColumns.capacity(), sizeof(glm::ivec2));
    }
    else if (const auto *job = Target<Io::IndexWarmupJob>(fn, AsyncIoJobKind::Warmup))
      Text(Result.queued_backing, job->worldFolder);
    else if (const auto *job = Target<Io::CleanupJob>(fn, AsyncIoJobKind::Cleanup))
    {
      Text(Result.queued_backing, job->worldFolder);
      Text(Result.queued_backing, job->cleanupOperation);
      if (job->cleanup.target<Io::RemoveSlices>())
        Add(Result.callable_bytes, KnownCallbackRequestBytes<Io::RemoveSlices>(job->cleanup));
      else if (job->cleanup.target<Io::RemoveColumn>())
        Add(Result.callable_bytes, KnownCallbackRequestBytes<Io::RemoveColumn>(job->cleanup));
      else
        UnknownJob();
    }
    else
      UnknownJob();
  }

  void UnknownJob()
  {
    ++Result.unknown_jobs;
    Result.retained_supported = false;
  }

  void Pool(size_t index, JobThreadPoolSnapshot counts, size_t bytes)
  {
    Result.pools[index] = counts;
    Result.pool_queue_bytes[index] = bytes;
    Add(Result.active, counts.active);
  }

  template<class T, class Visit>
  void Completed(size_t index, const UCompletedJobQueue<T> &queue, Visit &&visit)
  {
    queue.InspectStorage([&](size_t capacity) {
      Buffer(Result.completed_slots_bytes[index], capacity, sizeof(T));
    }, [&](const T &value) {
      ++Result.completed[index];
      visit(value);
    });
  }

  template<class Visit> void FlagsAndResults(const Visit &visit)
  {
    Owner.ColumnLightFlagsPool.InspectPendingJobs(visit, [&](auto counts, size_t bytes) {
      Pool(2, counts, bytes);
      Shared(Owner.SaveWriteProbeGate);
      Shared(Owner.LoadReadProbeGate);
      Completed(0, Owner.CompletedLoads, [&](const AsyncChunkLoadResult &value) {
        Buffer(Result.completed_backing, value.payload.capacity(), sizeof(uint8_t));
        Shared(value.cancellation);
        Receipts(value.columnLight);
        if (value.decodedBuffer)
        {
          Add(Result.completed_backing, sizeof(UChunkBuffer));
          Add(Result.completed_backing, value.decodedBuffer->GetOwnedAllocationBytes());
          const auto &payload = value.decodedBuffer->Payload;
          if (payload && (payload->Budget.budget || payload->Budget.job))
            Result.retained_supported = false;
        }
      });
      Completed(1, Owner.CompletedSaves, [&](const AsyncChunkSaveRequest &value) {
        for (const auto *text : {&value.worldFolder, &value.filePath, &value.cleanupOperation, &value.error})
          Text(Result.completed_backing, *text);
      });
      Completed(2, Owner.CompletedColumnLightFlagsSaves, [&](const AsyncColumnLightFlagsSaveResult &value) {
        Text(Result.completed_backing, value.worldFolder);
        Text(Result.completed_backing, value.error);
      });
    });
  }

  const Io &Owner;
  AsyncIoStorageIdentity *Scratch;
  size_t ScratchCount;
  size_t Used{0};
  AsyncIoStorage Result;
};

AsyncIoStorage UAsyncChunkIO::InspectRetainedIo(AsyncIoStorageIdentity *scratch,
                                               size_t scratch_count) const
{
  return AsyncIoStorageInspector(*this, scratch, scratch_count).Inspect();
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

std::vector<AsyncColumnLightFlagsSaveResult>
UAsyncChunkIO::DrainColumnLightFlagsSaves(double *mutex_wait_ms,
                                          double *mutex_held_ms)
{
  return CompletedColumnLightFlagsSaves.DrainAll(mutex_wait_ms,
                                                  mutex_held_ms);
}

bool UAsyncChunkIO::WaitForColumnLightFlagsSaveIdleFor(
    const std::chrono::milliseconds timeout)
{
  return ColumnLightFlagsPool.WaitIdleFor(timeout);
}

void UAsyncChunkIO::WaitForColumnLightFlagsSaveIdle()
{
  ColumnLightFlagsPool.WaitIdle();
}

bool UAsyncChunkIO::CompletedColumnLightFlagsSavesEmpty() const
{
  return CompletedColumnLightFlagsSaves.Empty();
}

void UAsyncChunkIO::WaitIdle()
{
  LoadPool.WaitIdle();
  if (BackgroundIoPool)
  {
    BackgroundIoPool->WaitIdle();
  }
}

bool UAsyncChunkIO::WaitIdleFor(const std::chrono::milliseconds timeout)
{
  const auto deadline = std::chrono::steady_clock::now() + timeout;
  if (!LoadPool.WaitIdleFor(timeout))
  {
    return false;
  }
  if (!BackgroundIoPool)
  {
    return true;
  }
  const auto now = std::chrono::steady_clock::now();
  const auto remaining = now < deadline
                             ? std::chrono::duration_cast<
                                   std::chrono::milliseconds>(deadline - now)
                             : std::chrono::milliseconds(0);
  return BackgroundIoPool->WaitIdleFor(remaining);
}

void UAsyncChunkIO::CancelPending()
{
  LoadPool.CancelPendingJobs();
  if (BackgroundIoPool)
  {
    BackgroundIoPool->CancelPendingJobs();
  }
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
