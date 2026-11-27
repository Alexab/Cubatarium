#include "World/IO/AsyncChunkIO.h"
#include "App/Platform/Log.h"
#include "Blocks/BlockRegistry.h"
#include "Core/Memory/CallbackStorageSource.h"
#include "Core/Memory/NameStorageSource.h"
#include "Core/Memory/RootStorageSource.h"
#include "Core/Memory/StlSourceStorage.h"
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

struct AsyncIoWorkerLocals
{
  const AsyncChunkLoadResult *load{};
  const AsyncChunkSaveRequest *save{};
  const AsyncColumnLightFlagsSaveResult *flags{};
  const std::vector<uint8_t> *encoded_bytes{};
  const std::string *encoded_text{}, *temporary_name{}, *diagnostic{};
  const std::filesystem::path *target_path{}, *temporary_path{};
  const IoCodecStorage *codec{};
  const IoDiskIndexStorage *disk_index{};
  bool codec_output_in_outer_sample{};

  AsyncIoWorkerLocals() = default;
  explicit AsyncIoWorkerLocals(const AsyncChunkLoadResult &value) : load(&value) {}
  explicit AsyncIoWorkerLocals(const AsyncChunkSaveRequest &value) : save(&value) {}
  explicit AsyncIoWorkerLocals(const AsyncColumnLightFlagsSaveResult &value) : flags(&value) {}
};

namespace
{
template<class Call, class Observer>
decltype(auto) WithCodecProbe(bool enabled, const Call &call, Observer observe)
{
  if (!enabled)
    return call(nullptr);
  const IoCodecProbe probe{&observe, [](void *context, const IoCodecStorage &sample) noexcept {
    (*static_cast<Observer *>(context))(sample);
  }};
  return call(&probe);
}
} // namespace

struct AsyncIoCredit
{
  AsyncIoCredit(std::shared_ptr<std::atomic<size_t>> count, size_t slots) noexcept
      : outstanding(std::move(count)), slots(slots) {}
  ~AsyncIoCredit() { outstanding->fetch_sub(slots, std::memory_order_relaxed); }
  const std::shared_ptr<std::atomic<size_t>> outstanding;
  const size_t slots;
};

static std::shared_ptr<AsyncIoCredit> ReserveIoCredits(
    const std::shared_ptr<std::atomic<size_t>> &counter, size_t limit, size_t slots)
{
  if (slots > limit)
    return {};
  auto count = counter->load(std::memory_order_relaxed);
  do
  {
    if (count > limit - slots)
      return {};
  } while (!counter->compare_exchange_weak(count, count + slots, std::memory_order_relaxed));
  try
  {
    return std::make_shared<AsyncIoCredit>(counter, slots);
  }
  catch (...)
  {
    counter->fetch_sub(slots, std::memory_order_relaxed);
    throw;
  }
}

std::shared_ptr<AsyncIoCredit> UAsyncChunkIO::ReserveTerrainWrites(size_t count)
{
  return ReserveIoCredits(OutstandingWrites, kMaxOutstandingWrites, count);
}

std::shared_ptr<AsyncIoCredit> UAsyncChunkIO::ReserveFlagSave()
{
  return ReserveIoCredits(OutstandingFlagSaves, kMaxOutstandingFlagSaves, 1);
}

struct UAsyncChunkIO::SaveFlagsJob
{
  std::shared_ptr<AsyncIoCredit> credit;
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
  owner->ObserveWorker(*this, AsyncIoJobKind::Flags, AsyncIoWorkerStage::Entered, revision, {});
  const auto worker_started = std::chrono::steady_clock::now();
  AsyncColumnLightFlagsSaveResult result;
  result.writeCredit = credit;
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
    if (owner->WorkerProbe && owner->WorkerProbe->phase.IsOpen())
    {
      AsyncIoWorkerLocals locals(result);
      locals.encoded_text = &encoded;
      locals.target_path = &target;
      locals.temporary_path = &temp;
      StlSourceStorage scratch, output;
      scratch.JsonTree(data);
      output.Text(encoded);
      const IoCodecStorage codec{IoCodecKind::FlagsWrite, scratch.bytes.requested,
                                0, output.bytes.requested, 0,
                                scratch.bytes.supported && output.bytes.supported};
      locals.codec = &codec;
      locals.codec_output_in_outer_sample = true;
      owner->ObserveWorker(*this, AsyncIoJobKind::Flags, AsyncIoWorkerStage::Encoded, revision, locals);
    }
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
  owner->ObserveWorker(*this, AsyncIoJobKind::Flags, AsyncIoWorkerStage::BeforePublish, revision,
                       AsyncIoWorkerLocals(result));
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

bool UAsyncChunkIO::RequestSaveColumnLightFlags(
    std::string worldFolder, const uint64_t revision,
    std::vector<glm::ivec2> completeColumns, std::string lightSignature)
{
  auto credit = ReserveFlagSave();
  if (!credit)
    return false;
  QueueColumnLightFlagsSave(std::move(credit), std::move(worldFolder), revision,
                          std::move(completeColumns), std::move(lightSignature));
  return true;
}

void UAsyncChunkIO::QueueColumnLightFlagsSave(std::shared_ptr<AsyncIoCredit> credit,
    std::string worldFolder, uint64_t revision, std::vector<glm::ivec2> columns,
    std::string lightSignature)
{
  const auto enqueue_started = std::chrono::steady_clock::now();
  ColumnLightFlagsPool.Enqueue(
      SaveFlagsJob{std::move(credit), this, std::move(worldFolder), revision, std::move(columns),
                   std::move(lightSignature), enqueue_started});
}

struct UAsyncChunkIO::IndexWarmupJob
{
  std::shared_ptr<AsyncIoCredit> credit;
  UAsyncChunkIO *owner;
  UChunkStorageService &storage;
  std::string worldFolder;

  void operator()() const;
};

void UAsyncChunkIO::IndexWarmupJob::operator()() const
{
  owner->ObserveWorker(*this, AsyncIoJobKind::Warmup, AsyncIoWorkerStage::Entered, 0, {});
  try
  {
    WithCodecProbe(owner->WorkerProbe && owner->WorkerProbe->phase.IsOpen(),
        [&](const IoCodecProbe *probe) { storage.PrepareHighestChunkSliceIndex(worldFolder, probe); },
        [&](const IoCodecStorage &codec) noexcept {
          AsyncIoWorkerLocals locals;
          locals.codec = &codec;
          owner->ObserveWorker(*this, AsyncIoJobKind::Warmup, AsyncIoWorkerStage::Codec, 0, locals);
        });
  }
  catch (const std::filesystem::filesystem_error &error)
  {
    // Demand lookups can rescan after a failed speculative scan.
    CubatariumLogInfo("WorldColumnSource", error.what());
    return;
  }
  if (!owner->WorkerProbe || !owner->WorkerProbe->phase.IsOpen())
    return;
  AsyncIoWorkerLocals locals;
  const auto index = storage.InspectDiskIndexStorage();
  locals.disk_index = &index;
  owner->ObserveWorker(*this, AsyncIoJobKind::Warmup, AsyncIoWorkerStage::Finished, 0, locals);
}

bool UAsyncChunkIO::RequestDiskIndexWarmup(
    UChunkStorageService &storage, const std::string &worldFolder)
{
  if (worldFolder.empty())
    return false;
  auto credit = ReserveIoCredits(OutstandingIndexWarmups, 1, 1);
  if (!credit)
    return false;
  EnqueueBackgroundIo(IndexWarmupJob{std::move(credit), this, storage, worldFolder});
  return true;
}

struct UAsyncChunkIO::LoadJob
{
  std::shared_ptr<AsyncIoCredit> credit;
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
  owner->ObserveWorker(*this, AsyncIoJobKind::Load, AsyncIoWorkerStage::Entered, demandId, {});
  const auto is_cancelled = [&]()
  {
    return cancellation &&
           cancellation->load(std::memory_order_acquire);
  };
  if (is_cancelled())
  {
    owner->ObserveWorker(*this, AsyncIoJobKind::Load, AsyncIoWorkerStage::Cancelled, demandId, {});
    return;
  }
  AsyncChunkLoadResult result;
  result.readCredit = credit;
  const auto codec_observer = [&](const IoCodecStorage &codec) noexcept {
    AsyncIoWorkerLocals locals(result);
    locals.codec = &codec;
    owner->ObserveWorker(*this, AsyncIoJobKind::Load, AsyncIoWorkerStage::Codec, demandId, locals);
  };
  const auto observe = [&](AsyncIoWorkerStage stage) {
    owner->ObserveWorker(*this, AsyncIoJobKind::Load, stage, demandId, AsyncIoWorkerLocals(result));
  };
  const auto publish = [&] {
    observe(AsyncIoWorkerStage::BeforePublish);
    owner->PushCompletedLoad(std::move(result));
  };
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
    result.columnLight = WithCodecProbe(owner->WorkerProbe && owner->WorkerProbe->phase.IsOpen(),
        [&](const IoCodecProbe *probe) { return worker_storage.ReadColumnLight(worldFolder, coord, probe); },
        codec_observer);
  }
  if (is_cancelled())
  {
    observe(AsyncIoWorkerStage::Cancelled);
    return;
  }
  if (format == ChunkDiskFormat::Absent)
  {
    publish();
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
      observe(AsyncIoWorkerStage::Cancelled);
      return;
    }
    if (trace_io)
    {
      result.workerFinishedAt = std::chrono::steady_clock::now();
    }
    publish();
    if (is_cancelled())
    {
      owner->NoteLoadCancellation();
    }
    return;
  }
  if (is_cancelled())
  {
    observe(AsyncIoWorkerStage::Cancelled);
    return;
  }
  const auto read_started =
      trace_io ? std::chrono::steady_clock::now()
               : std::chrono::steady_clock::time_point{};
  result.payload = UChunkStorageService::ReadChunkBytes(file, format);
  file.close();
  observe(AsyncIoWorkerStage::ReadBytes);
  if (trace_io)
  {
    result.fileReadMs = std::chrono::duration<double, std::milli>(
                            std::chrono::steady_clock::now() -
                            read_started)
                            .count();
  }
  if (is_cancelled())
  {
    observe(AsyncIoWorkerStage::Cancelled);
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
      if (format == ChunkDiskFormat::Binary)
      {
        // Change the saved X coordinate without reallocating the input.
        constexpr size_t encoded_x = sizeof(UBinaryChunkSerializer::kMagic) + 1;
        result.payload[result.payload.size() > encoded_x ? encoded_x : 0] ^= 1;
      }
      else
        result.payload.push_back('x');
      result.injectedInvalidFrame = true;
    }
    const auto deserialize_started = std::chrono::steady_clock::now();
    try
    {
      UChunkStorageService worker_storage(worker_storage_settings);
      result.decodedBuffer = std::make_unique<UChunkBuffer>(
          WithCodecProbe(owner->WorkerProbe && owner->WorkerProbe->phase.IsOpen(), [&](const IoCodecProbe *probe) {
            return worker_storage.DeserializeChunk(result.payload, coord, format, registry, probe);
          }, codec_observer));
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
    observe(AsyncIoWorkerStage::Decoded);
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
    observe(AsyncIoWorkerStage::Cancelled);
    owner->NoteLoadCancellation();
    return;
  }
  if (trace_io)
  {
    result.workerFinishedAt = std::chrono::steady_clock::now();
  }
  publish();
  if (is_cancelled())
  {
    owner->NoteLoadCancellation();
  }
}

bool UAsyncChunkIO::RequestLoad(glm::ivec3 coord, UChunkStorageService &storage,
                                UBlockRegistry &registry,
                                const std::string &worldFolder,
                                ChunkGenerationToken token,
                                std::shared_ptr<std::atomic<bool>> cancellation,
                                uint64_t demandId, bool includeColumnLight,
                                bool injectInvalidFrame)
{
  if (cancellation && cancellation->load(std::memory_order_acquire))
    return false;
  auto credit = ReserveIoCredits(OutstandingLoads, kMaxOutstandingLoads, 1);
  if (!credit)
    return false;
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
    result.readCredit = std::move(credit);
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
    return true;
  }

  const std::string filePath =
      storage.ChunkFilePath(worldFolder, coord, format);
  const ChunkStorageSettings worker_storage_settings = storage.GetSettings();
  const auto read_probe_gate = LoadReadProbeGate;
  LoadPool.Enqueue(
      LoadJob{std::move(credit), this, coord, filePath, token, format, submitted_at, format_detect_ms,
              trace_io, cancellation, demandId, registry, worker_storage_settings,
              includeColumnLight, worldFolder, injectInvalidFrame, read_probe_gate});
  return true;
}

struct UAsyncChunkIO::SaveReceiptsJob
{
  std::shared_ptr<AsyncIoCredit> credit;
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
  owner->ObserveWorker(*this, AsyncIoJobKind::Receipts, AsyncIoWorkerStage::Entered, saveBatchId, {});
  AsyncChunkSaveRequest done;
  done.writeCredit = credit;
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
    done.success = receipts && WithCodecProbe(owner->WorkerProbe && owner->WorkerProbe->phase.IsOpen(),
        [&](const IoCodecProbe *probe) {
          return worker_storage.WriteColumnLight(worldFolder, column, *receipts, nullptr, probe);
        }, [&](const IoCodecStorage &codec) noexcept {
          AsyncIoWorkerLocals locals(done);
          locals.codec = &codec;
          owner->ObserveWorker(*this, AsyncIoJobKind::Receipts, AsyncIoWorkerStage::Codec,
                               saveBatchId, locals);
        });
    if (!done.success)
      done.error = "column_light_write_failed";
  }
  catch (const std::exception &error)
  {
    done.error = error.what();
  }
  owner->ObserveWorker(*this, AsyncIoJobKind::Receipts, AsyncIoWorkerStage::BeforePublish, saveBatchId,
                       AsyncIoWorkerLocals(done));
  owner->CompletedSaves.Push(std::move(done));
}

bool UAsyncChunkIO::RequestSaveColumnLight(
    glm::ivec3 column, std::shared_ptr<const PersistedColumnLight> receipts,
    UChunkStorageService &storage, const std::string &worldFolder, uint64_t saveBatchId)
{
  auto credit = ReserveTerrainWrites(1);
  if (!credit)
    return false;
  const auto settings = storage.GetSettings();
  column.y = 0;
  EnqueueBackgroundIo(
      SaveReceiptsJob{std::move(credit), this, column, std::move(receipts), settings,
                      worldFolder, saveBatchId});
  return true;
}

struct UAsyncChunkIO::SaveJob
{
  std::shared_ptr<AsyncIoCredit> credit;
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
  owner->ObserveWorker(*this, AsyncIoJobKind::Save, AsyncIoWorkerStage::Entered, saveBatchId, {});
  AsyncChunkSaveRequest done;
  done.writeCredit = credit;
  done.saveBatchId = saveBatchId;
  done.coord = coord;
  done.groundCoord = ground;
  done.worldFolder = worldFolder;
  const auto finish = [this, &done](const std::string &error = {},
                                    AsyncIoWorkerLocals locals = {})
  {
    done.success = error.empty();
    done.error = error;
    locals.save = &done;
    locals.diagnostic = &error;
    owner->ObserveWorker(*this, AsyncIoJobKind::Save, AsyncIoWorkerStage::BeforePublish,
                         saveBatchId, locals);
    owner->CompletedSaves.Push(std::move(done));
  };

  try
  {
    UChunkStorageService worker_storage(settings);
    if (!snapshot)
    {
      done.filePath = worker_storage.ChunkFilePath(worldFolder, coord, ChunkDiskFormat::Binary);
      finish("chunk_missing_before_serialize");
      return;
    }
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
    // Current CCHK4/JSON4 serialization writes numeric block IDs and
    // does not consult registry/catalog state. Do not retain a mutable
    // main-thread registry reference across a reload/world switch.
    UBlockRegistry numeric_registry(nullptr);
    const SerializedChunk serialized = WithCodecProbe(owner->WorkerProbe && owner->WorkerProbe->phase.IsOpen(),
        [&](const IoCodecProbe *probe) {
          return worker_storage.SerializeChunk(coord, *snapshot, numeric_registry, probe);
        }, [&](const IoCodecStorage &codec) noexcept {
          AsyncIoWorkerLocals locals(done);
          locals.codec = &codec;
          owner->ObserveWorker(*this, AsyncIoJobKind::Save, AsyncIoWorkerStage::Codec,
                               saveBatchId, locals);
        });
    done.format = serialized.format;
    done.filePath = worker_storage.ChunkFilePath(
        worldFolder, coord, serialized.format);
    AsyncIoWorkerLocals locals(done);
    locals.encoded_bytes = &serialized.bytes;

    std::error_code ec;
    std::filesystem::create_directories(
        std::filesystem::path(done.filePath).parent_path(), ec);
    if (ec)
    {
      finish("create_directories: " + ec.message(), locals);
      return;
    }
    const std::string tempPath = done.filePath + ".tmp";
    locals.temporary_name = &tempPath;
    owner->ObserveWorker(*this, AsyncIoJobKind::Save, AsyncIoWorkerStage::Encoded, saveBatchId, locals);
    {
      std::ofstream file(tempPath, std::ios::binary);
      if (!file.is_open())
      {
        finish("open_temp_failed", locals);
        return;
      }
      file.write(reinterpret_cast<const char *>(serialized.bytes.data()),
                 static_cast<std::streamsize>(serialized.bytes.size()));
      file.close();
      if (!file.good())
      {
        std::error_code cleanup_ec;
        std::filesystem::remove(tempPath, cleanup_ec);
        finish("write_temp_failed", locals);
        return;
      }
    }
    ec.clear();
#ifdef _WIN32
    const std::filesystem::path targetPath(done.filePath);
    const std::filesystem::path temporaryPath(tempPath);
    locals.target_path = &targetPath;
    locals.temporary_path = &temporaryPath;
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
      finish("replace_failed: " + ec.message(), locals);
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
    finish({}, locals);
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

bool UAsyncChunkIO::RequestSave(glm::ivec3 coord, UChunkStorageService &storage,
                               const std::string &worldFolder,
                               const UBlockWorld &world, UBlockRegistry &,
                               ChunkGenerationToken, uint64_t saveBatchId)
{
  auto credit = ReserveTerrainWrites(1);
  if (!credit)
    return false;
  auto job = MakeSaveJob(std::move(credit), coord, storage.GetSettings(), worldFolder,
                         world, saveBatchId);
  if (job.snapshot)
    EnqueueBackgroundIo(std::move(job));
  else
    job();
  return true;
}

UAsyncChunkIO::SaveJob UAsyncChunkIO::MakeSaveJob(std::shared_ptr<AsyncIoCredit> credit,
    glm::ivec3 coord, const ChunkStorageSettings &settings, const std::string &worldFolder,
    const UBlockWorld &world, uint64_t saveBatchId)
{
  // The world owns and mutates chunks on the main thread. Take a compact,
  // immutable snapshot here, then do palette/RLE/JSON serialization on the
  // I/O worker so unloading a column does not serialize every vertical slice
  // inside the streaming update.
  std::shared_ptr<const UChunk> snapshot = world.GetChunkManager().MakeTerrainSaveSnapshot(coord);
  const glm::ivec3 ground(coord.x, 0, coord.z);
  return SaveJob{std::move(credit), this, std::move(snapshot), settings, worldFolder,
                 coord, ground, saveBatchId, SaveWriteProbeGate};
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

struct UAsyncChunkIO::CleanupJob
{
  std::shared_ptr<AsyncIoCredit> credit;
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
  owner->ObserveWorker(*this, AsyncIoJobKind::Cleanup, AsyncIoWorkerStage::Entered, saveBatchId, {});
  AsyncChunkSaveRequest done;
  done.writeCredit = credit;
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
  owner->ObserveWorker(*this, AsyncIoJobKind::Cleanup, AsyncIoWorkerStage::BeforePublish, saveBatchId,
                       AsyncIoWorkerLocals(done));
  owner->CompletedSaves.Push(std::move(done));
}

UAsyncChunkIO::CleanupJob UAsyncChunkIO::MakeRemoveSlicesJob(
    std::shared_ptr<AsyncIoCredit> credit, glm::ivec3 ground, int firstCy, int lastCy,
    UChunkStorageService &storage, const std::string &worldFolder, uint64_t saveBatchId)
{
  const glm::ivec3 result(ground.x, firstCy, ground.z);
  return CleanupJob{std::move(credit), this, storage, ground, result, worldFolder, "slice_range",
                    lastCy - firstCy + 1, RemoveSlices{ground, firstCy, lastCy}, saveBatchId};
}

bool UAsyncChunkIO::RequestRemoveChunkSlices(
    glm::ivec3 groundCoord, int firstCy, int lastCy, UChunkStorageService &storage,
    const std::string &worldFolder, uint64_t saveBatchId)
{
  if (firstCy > lastCy)
    return false;
  auto credit = ReserveTerrainWrites(1);
  if (!credit)
    return false;
  groundCoord.y = 0;
  EnqueueBackgroundIo(MakeRemoveSlicesJob(std::move(credit), groundCoord, firstCy, lastCy,
                                          storage, worldFolder, saveBatchId));
  return true;
}

bool UAsyncChunkIO::RequestRemoveTerrainColumn(
    glm::ivec3 groundCoord, int maxWorldY, int minWorldY, UChunkStorageService &storage,
    const std::string &worldFolder)
{
  auto credit = ReserveTerrainWrites(1);
  if (!credit)
    return false;
  groundCoord.y = 0;
  EnqueueBackgroundIo(CleanupJob{std::move(credit), this, storage, groundCoord, groundCoord,
      worldFolder, "full_column", -1, RemoveColumn{groundCoord, maxWorldY, minWorldY}, 0});
  return true;
}

void UAsyncChunkIO::QueueTerrainColumnSave(std::shared_ptr<AsyncIoCredit> credit,
    glm::ivec3 ground, int firstCy, int highestCy, int lastCy, UChunkStorageService &storage,
    const std::string &worldFolder, const UBlockWorld &world,
    std::shared_ptr<const PersistedColumnLight> light, uint64_t saveBatchId)
{
  std::vector<std::function<void()>> jobs;
  jobs.reserve(credit->slots);
  const auto settings = storage.GetSettings();
  jobs.emplace_back(SaveReceiptsJob{credit, this, ground, std::move(light), settings,
                                     worldFolder, saveBatchId});
  for (int cy = firstCy; cy <= highestCy; ++cy)
    jobs.emplace_back(MakeSaveJob(credit, {ground.x, cy, ground.z}, settings, worldFolder,
                                  world, saveBatchId));
  if (firstCy <= highestCy && highestCy < lastCy)
    jobs.emplace_back(MakeRemoveSlicesJob(credit, ground, highestCy + 1, lastCy, storage,
                                          worldFolder, saveBatchId));
  auto &pool = BackgroundIoPool ? *BackgroundIoPool : LoadPool;
  pool.EnqueueBatch(std::move(jobs));
}

struct AsyncIoStorageInspector
{
  using Io = UAsyncChunkIO;

  explicit AsyncIoStorageInspector(const Io &io, AsyncIoStorageIdentity *scratch,
                                   size_t scratch_count)
      : Owner(io), Scratch(scratch), ScratchCount(scratch ? scratch_count : 0)
  {
    Result.io = reinterpret_cast<uintptr_t>(&Owner);
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

  template<class Job>
  static void Observe(const Io &io, const AsyncIoWorkerProbe &probe, const Job &job,
                      AsyncIoJobKind kind, AsyncIoWorkerStage stage, uint64_t key,
                      const AsyncIoWorkerLocals &locals) noexcept
  {
    AsyncIoWorkerSample sample;
    sample.job = reinterpret_cast<uintptr_t>(&job);
    sample.thread = std::this_thread::get_id();
    sample.kind = kind;
    sample.stage = stage;
    sample.key = key;
    AsyncIoStorageInspector inspector(io, sample.shared.data(), sample.shared.size());
    inspector.Result.epoch = probe.phase.epoch;
    const std::function<void()> empty;
    inspector.Result.callable_bytes = KnownCallbackRequestBytes<Job>(empty);
    inspector.Capture(job);
    if (locals.load)
      inspector.LoadResult(*locals.load, &sample.input_bytes, &sample.decoded_bytes);
    if (locals.save)
      inspector.SaveResult(*locals.save);
    if (locals.flags)
      inspector.FlagsResult(*locals.flags);
    if (locals.diagnostic)
      inspector.Text(inspector.Result.completed_backing, *locals.diagnostic);
    if (locals.encoded_bytes)
      inspector.Buffer(sample.encoded_bytes, locals.encoded_bytes->capacity(), sizeof(uint8_t));
    if (locals.encoded_text)
      inspector.Text(sample.encoded_bytes, *locals.encoded_text);
    if (locals.temporary_name)
      inspector.Text(sample.path_bytes, *locals.temporary_name);
    if (locals.target_path)
      inspector.Text(sample.path_bytes, locals.target_path->native());
    if (locals.temporary_path)
      inspector.Text(sample.path_bytes, locals.temporary_path->native());
    inspector.Add(inspector.Result.completed_backing, sample.encoded_bytes);
    inspector.Add(inspector.Result.completed_backing, sample.path_bytes);
    if (locals.codec)
    {
      sample.codec = *locals.codec;
      inspector.Result.retained_supported &= sample.codec.supported;
      // A directory scan high point was collected under the index lock and
      // exported after the iterator retired. Only live samples join here.
      if (sample.codec.kind != IoCodecKind::DiskDirectoryEntry)
      {
        inspector.Add(inspector.Result.completed_backing, sample.codec.scratch_bytes);
        // Read inputs are already included from locals.load. Flags encoding
        // likewise supplies encoded_text at the outer checkpoint.
        if (!locals.codec_output_in_outer_sample)
          inspector.Add(inspector.Result.completed_backing, sample.codec.output_bytes);
      }
    }
    if (locals.disk_index)
      sample.disk_index = *locals.disk_index;
    sample.sources = inspector.Result;
    probe.sample(probe.context, sample);
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

  template<class Char> void Text(size_t &sum, const std::basic_string<Char> &text)
  {
    const auto address = reinterpret_cast<uintptr_t>(text.data());
    const auto object = reinterpret_cast<uintptr_t>(&text);
    if (address < object || address - object >= sizeof(text))
      Buffer(sum, text.capacity() + 1, sizeof(Char));
  }

  void AttachSharedBacking(size_t bytes)
  {
    // Called immediately after admitting the owning control, before visiting
    // any of its separate child controls.
    Scratch[Used - 1].backing = bytes;
    Add(Result.shared_bytes, bytes);
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
    size_t column_backing = 0;
    Buffer(column_backing, column->capacity(), sizeof(PersistedColumnLight::value_type));
    AttachSharedBacking(column_backing);
    for (const auto &section : *column)
    {
      if (!Shared(section))
        continue;
      size_t section_backing = 0;
      Text(section_backing, section->signature);
      Buffer(section_backing, section->inputs.capacity(), sizeof(PersistedSectionLight::Input));
      AttachSharedBacking(section_backing);
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
          AttachSharedBacking(sizeof(UChunk));
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
      Capture(*job);
    else if (const auto *job = Target<Io::SaveJob>(fn, AsyncIoJobKind::Save))
      Capture(*job);
    else if (const auto *job = Target<Io::SaveReceiptsJob>(fn, AsyncIoJobKind::Receipts))
      Capture(*job);
    else if (const auto *job = Target<Io::SaveFlagsJob>(fn, AsyncIoJobKind::Flags))
      Capture(*job);
    else if (const auto *job = Target<Io::IndexWarmupJob>(fn, AsyncIoJobKind::Warmup))
      Capture(*job);
    else if (const auto *job = Target<Io::CleanupJob>(fn, AsyncIoJobKind::Cleanup))
      Capture(*job);
    else
      UnknownJob();
  }

  void Borrow(AsyncIoBorrowedRoot &root, const void *object, const void *lifetime = nullptr)
  {
    const auto address = reinterpret_cast<uintptr_t>(object);
    const auto identity = reinterpret_cast<uintptr_t>(lifetime);
    if (!root.references)
    {
      root.object = address;
      root.lifetime = identity;
    }
    root.mixed |= root.object != address || root.lifetime != identity;
    Add(root.references, 1);
  }

  void Capture(const Io::LoadJob &job)
  {
    Borrow(Result.registry_root, &job.registry, job.registry.GetLifetimeIdentity().get());
    Text(Result.queued_backing, job.filePath);
    Text(Result.queued_backing, job.worldFolder);
    Shared(job.cancellation);
    Shared(job.read_probe_gate);
    Credit(job.credit);
  }

  void Credit(const std::shared_ptr<AsyncIoCredit> &credit)
  {
    if (Shared(credit))
      Shared(credit->outstanding);
  }
  void Capture(const Io::SaveJob &job)
  {
    Credit(job.credit);
    Text(Result.queued_backing, job.worldFolder);
    Snapshot(job.snapshot);
    Shared(job.probe_gate);
  }
  void Capture(const Io::SaveReceiptsJob &job)
  {
    Credit(job.credit);
    Text(Result.queued_backing, job.worldFolder);
    Receipts(job.receipts);
  }
  void Capture(const Io::SaveFlagsJob &job)
  {
    Credit(job.credit);
    Text(Result.queued_backing, job.worldFolder);
    Text(Result.queued_backing, job.lightSignature);
    Buffer(Result.queued_backing, job.completeColumns.capacity(), sizeof(glm::ivec2));
  }
  void Capture(const Io::IndexWarmupJob &job)
  {
    Credit(job.credit);
    Borrow(Result.storage_root, &job.storage);
    Text(Result.queued_backing, job.worldFolder);
  }
  void Capture(const Io::CleanupJob &job)
  {
    Credit(job.credit);
    Borrow(Result.storage_root, &job.storage);
    Text(Result.queued_backing, job.worldFolder);
    Text(Result.queued_backing, job.cleanupOperation);
    if (job.cleanup.target<Io::RemoveSlices>())
      Add(Result.callable_bytes, KnownCallbackRequestBytes<Io::RemoveSlices>(job.cleanup));
    else if (job.cleanup.target<Io::RemoveColumn>())
      Add(Result.callable_bytes, KnownCallbackRequestBytes<Io::RemoveColumn>(job.cleanup));
    else
      UnknownJob();
  }

  void LoadResult(const AsyncChunkLoadResult &value, size_t *input = nullptr,
                  size_t *decoded = nullptr)
  {
    size_t input_backing = 0, decoded_backing = 0;
    Buffer(input_backing, value.payload.capacity(), sizeof(uint8_t));
    Shared(value.cancellation);
    Credit(value.readCredit);
    Receipts(value.columnLight);
    if (value.decodedBuffer)
    {
      Add(decoded_backing, sizeof(UChunkBuffer));
      Add(decoded_backing, value.decodedBuffer->GetOwnedAllocationBytes());
      const auto &payload = value.decodedBuffer->Payload;
      if (payload && (payload->Budget.budget || payload->Budget.job))
        Result.retained_supported = false;
    }
    if (input)
      *input = input_backing;
    if (decoded)
      *decoded = decoded_backing;
    Add(Result.completed_backing, input_backing);
    Add(Result.completed_backing, decoded_backing);
  }

  void SaveResult(const AsyncChunkSaveRequest &value)
  {
    Credit(value.writeCredit);
    for (const auto *text : {&value.worldFolder, &value.filePath, &value.cleanupOperation, &value.error})
      Text(Result.completed_backing, *text);
  }

  void FlagsResult(const AsyncColumnLightFlagsSaveResult &value)
  {
    Credit(value.writeCredit);
    Text(Result.completed_backing, value.worldFolder);
    Text(Result.completed_backing, value.error);
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
      Shared(Owner.OutstandingLoads);
      Shared(Owner.OutstandingWrites);
      Shared(Owner.OutstandingFlagSaves);
      Shared(Owner.OutstandingIndexWarmups);
      Completed(0, Owner.CompletedLoads, [&](const AsyncChunkLoadResult &value) {
        LoadResult(value);
      });
      Completed(1, Owner.CompletedSaves, [&](const AsyncChunkSaveRequest &value) {
        SaveResult(value);
      });
      Completed(2, Owner.CompletedColumnLightFlagsSaves, [&](const AsyncColumnLightFlagsSaveResult &value) {
        FlagsResult(value);
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
                                               size_t scratch_count,
                                               const AllocatorSourcePhase *phase) const
{
  auto result = AsyncIoStorageInspector(*this, scratch, scratch_count).Inspect();
  if (phase && phase->IsOpen())
    result.epoch = phase->epoch;
  return result;
}

AsyncIoRootBinding UAsyncChunkIO::InspectBorrowedIoRoots(const AllocatorSourcePhase &phase,
                                                        const UChunkStorageService &storage,
                                                        const UBlockRegistry &registry) const
{
  if (!phase.IsOpen() || !phase.epoch)
    return {};
  return {reinterpret_cast<uintptr_t>(this), reinterpret_cast<uintptr_t>(&storage),
          reinterpret_cast<uintptr_t>(&registry),
          reinterpret_cast<uintptr_t>(registry.GetLifetimeIdentity().get()),
          phase.epoch, storage.InspectDiskIndexStorage()};
}

template<class Job>
void UAsyncChunkIO::ObserveWorker(const Job &job, AsyncIoJobKind kind, AsyncIoWorkerStage stage,
                                 uint64_t key, const AsyncIoWorkerLocals &locals) const noexcept
{
  const auto *probe = WorkerProbe;
  if (probe && probe->sample && probe->phase.IsOpen() && probe->phase.epoch)
    AsyncIoStorageInspector::Observe(*this, *probe, job, kind, stage, key, locals);
}

AsyncIoExecutionStorage JoinAsyncIoWorkers(
    const AllocatorSourcePhase &phase, const AsyncIoRootBinding &roots,
    const AsyncIoStorage &queues,
    const AsyncIoStorageIdentity *queue_shared, size_t queue_shared_count,
    const AsyncIoWorkerSample *const *workers, size_t worker_count,
    AsyncIoStorageIdentity *scratch, size_t scratch_count) noexcept
{
  AsyncIoExecutionStorage result;
  if (!phase.IsOpen() || !phase.epoch || !phase.participants ||
      queues.epoch != phase.epoch || !queues.io || !queues.retained_supported ||
      roots.epoch != phase.epoch || roots.io != queues.io ||
      !roots.storage || !roots.registry || !roots.registry_lifetime ||
      !workers || worker_count != phase.participants || worker_count != queues.active ||
      queue_shared_count != queues.shared_blocks || (queue_shared_count && !queue_shared) ||
      !scratch)
    return result;

  result.supported = true;
  result.callable_bytes = queues.callable_bytes;
  result.captured_backing = queues.queued_backing;
  result.local_backing = queues.completed_backing;
  const auto add = [&](size_t &total, size_t amount) {
    if (amount > SIZE_MAX - total)
    {
      result.overflow = true;
      result.supported = false;
      total = SIZE_MAX;
    }
    else
      total += amount;
  };
  const auto shared = [&](const AsyncIoStorageIdentity *nodes, size_t count, size_t expected) {
    size_t bytes = 0;
    for (size_t i = 0; i < count; ++i)
    {
      const auto &node = nodes[i];
      if (!node.allocation || !node.bytes)
      {
        result.supported = false;
        continue;
      }
      add(bytes, node.bytes);
      add(bytes, node.backing);
      const auto *end = scratch + result.shared_blocks;
      const auto *match = std::find_if(scratch, scratch + result.shared_blocks,
          [&](const auto &seen) { return seen.allocation == node.allocation; });
      if (match != end)
      {
        if (match->bytes != node.bytes || match->backing != node.backing)
          result.supported = false;
        ++result.shared_aliases;
        continue;
      }
      if (result.shared_blocks == scratch_count)
      {
        result.supported = false;
        continue;
      }
      scratch[result.shared_blocks++] = node;
      add(result.shared_bytes, node.bytes);
      add(result.shared_bytes, node.backing);
    }
    if (bytes != expected)
      result.supported = false;
  };
  const auto borrow = [&](size_t &total, const AsyncIoBorrowedRoot &root, size_t expected,
                          uintptr_t object, uintptr_t lifetime) {
    if (root.mixed || root.references != expected ||
        root.object != (expected ? object : 0) || root.lifetime != (expected ? lifetime : 0))
      result.supported = false;
    add(total, root.references);
  };
  add(result.local_backing, queues.warmup_requested);
  shared(queue_shared, queue_shared_count, queues.shared_bytes);
  const auto queued = [&](AsyncIoJobKind kind) { return queues.queued[static_cast<size_t>(kind)]; };
  size_t queued_storage = queued(AsyncIoJobKind::Warmup);
  add(queued_storage, queued(AsyncIoJobKind::Cleanup));
  borrow(result.storage_references, queues.storage_root, queued_storage, roots.storage, 0);
  borrow(result.registry_references, queues.registry_root, queued(AsyncIoJobKind::Load),
         roots.registry, roots.registry_lifetime);
  for (size_t i = 0; i < worker_count; ++i)
  {
    const auto *sample = workers[i];
    if (!sample || sample->sources.io != queues.io || sample->sources.epoch != phase.epoch ||
        !sample->sources.retained_supported || !sample->job || sample->thread == std::thread::id{} ||
        sample->kind >= AsyncIoJobKind::Count || sample->sources.shared_blocks > sample->shared.size())
    {
      result.supported = false;
      continue;
    }
    bool duplicate = false;
    for (size_t j = 0; j < i; ++j)
      if (workers[j] && (workers[j]->job == sample->job || workers[j]->thread == sample->thread))
        duplicate = true;
    if (duplicate)
    {
      result.supported = false;
      continue;
    }
    ++result.participants;
    add(result.callable_bytes, sample->sources.callable_bytes);
    add(result.captured_backing, sample->sources.queued_backing);
    add(result.local_backing, sample->sources.completed_backing);
    shared(sample->shared.data(), sample->sources.shared_blocks, sample->sources.shared_bytes);
    borrow(result.storage_references, sample->sources.storage_root,
           sample->kind == AsyncIoJobKind::Warmup || sample->kind == AsyncIoJobKind::Cleanup,
           roots.storage, 0);
    borrow(result.registry_references, sample->sources.registry_root,
           sample->kind == AsyncIoJobKind::Load, roots.registry, roots.registry_lifetime);
  }
  if (result.storage_references)
  {
    result.disk_index_requested = roots.disk_index.requested;
    result.supported &= roots.disk_index.supported;
  }
  size_t total = 0;
  for (const auto bytes : {result.callable_bytes, result.captured_backing,
                           result.local_backing, result.shared_bytes, result.disk_index_requested})
    add(total, bytes);
  if (!phase.IsOpen() || result.participants != worker_count)
    result.supported = false;
  return result;
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
