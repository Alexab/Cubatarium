#include "World/Persistence/WorldPersistence.h"
#include "Core/Environment.h"
#include "Blocks/BlockRegistry.h"
#include "Creatures/Core/Creature.h"
#include "Creatures/Core/CreatureInventory.h"
#include "Creatures/Definition/CreatureDefinitionStorage.h"
#include "Creatures/Player/Player.h"
#include "Creatures/Player/PlayerCapsule.h"
#include "Creatures/Player/User.h"
#include "Creatures/Stats/CreatureStatsJson.h"
#include "Creatures/Visual/CreaturePartMeshData.h"
#include "Creatures/Visual/CreatureVisualFactory.h"
#include "Game/WorldDifficulty.h"
#include "Game/ModePolicy.h"
#include "Game/WorldGameMode.h"
#include "Render/Camera/Camera.h"
#include "World/Chunks/Chunk.h"
#include "World/Chunks/ChunkBuffer.h"
#include "World/Chunks/ChunkManager.h"
#include "World/Chunks/ChunkStreamer.h"
#include "World/Core/BlockWorld.h"
#include "World/Streaming/EnterVisualWarmupPolicy.h"
#include "World/Streaming/NearFovWorkPriority.h"
#include "World/Streaming/RelightFifoPolicy.h"
#include "World/Streaming/SoftDeferEmptyPolicy.h"
#include "World/Core/RuntimeTuning.h"
#include "World/Core/World.h"
#include "World/Diagnostics/JobStageTrace.h"
#include "World/Mesh/WorldMeshService.h"
#include "World/Streaming/ChunkRenderDemand.h"
#include "World/Chunks/Chunk.h"
#include "World/Chunks/TerrainColumnUtil.h"
#include "World/Lighting/ChunkLighting.h"
#include "App/Platform/Log.h"
#include "World/Streaming/ColumnEmergeState.h"
#include "World/Streaming/ColumnFlowExecutor.h"
#include "World/Streaming/ColumnFlowScheduler.h"
#include "World/Math/GridMath.h"
#include "World/Math/BlockTypes.h"
#include <algorithm>
#include <cstdlib>
#include <queue>
#include <tuple>
#include <unordered_set>
#include <utility>

#include "World/Environment/EnvironmentConfig.h"
#include "World/Math/GridMath.h"
#include "World/View/WorldViewSettings.h"
#include "World/Streaming/WorldStreaming.h"
#include "WorldGen/Core/ProceduralConfigIO.h"
#include "WorldGen/Core/ProceduralSettings.h"
#include "WorldGen/Core/WorldGenSets.h"
#include "WorldGen/Features/ObjectFeatureConfig.h"
#include <algorithm>
#include <chrono>
#include <cmath>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <nlohmann/json.hpp>
#include <sstream>
#include <thread>
#include <vector>

using json = nlohmann::json;

namespace cutum
{

namespace
{

constexpr int kVisibleDrawGateRelightTargetLimit = 8;

int ColumnTopBlockY(const UWorld &world, glm::ivec2 ground_xz, int max_y)
{
  const glm::ivec3 ground(ground_xz.x, 0, ground_xz.y);
  const int top_cy =
      GetHighestNonAirChunkSlice(world.GetBlockWorld(), ground, max_y);
  if (top_cy < 0)
  {
    return -1;
  }
  return std::min(max_y, (top_cy + 1) * CHUNK_SIZE - 1);
}

bool ColumnSurfaceBandNeedsRelight(const UWorld &world, glm::ivec2 ground_xz,
                                   int band_min, int band_max)
{
  const UWorldMeshService &mesh = world.GetMeshService();
  const int cy0 = FloorDiv(band_min, CHUNK_SIZE);
  const int cy1 = FloorDiv(band_max, CHUNK_SIZE);
  for (int cy = cy0; cy <= cy1; ++cy)
  {
    const glm::ivec3 coord(ground_xz.x, cy, ground_xz.y);
    if (!mesh.HasGreedyMesh(coord))
    {
      continue;
    }
    if (mesh.GetCache().ChunkHasFullyDarkFace(coord))
    {
      return true;
    }
  }
  return world.IsPendingLightBeforeMesh(ground_xz);
}

constexpr float kMaxReasonablePlayerY = 512.0f;
constexpr float kMinReasonablePlayerY = -32.0f;

bool IsWorldColumnSourceTraceEnabled()
{
  const char *value = std::getenv("CUBA_WORLD_COLUMN_SOURCE_TRACE");
  return value && value[0] == '1';
}

bool IsWorldColumnSaveTraceEnabled()
{
  const char *value = std::getenv("CUBA_WORLD_COLUMN_SAVE_TRACE");
  return value && value[0] == '1';
}

bool IsStreamingDetailTraceEnabled()
{
  const char *value = std::getenv("CUBA_STREAMING_DETAIL_TRACE");
  return value && value[0] == '1';
}

void LogWorldColumnSource(const char *source, const char *outcome,
                          glm::ivec3 ground, const std::string &details)
{
  if (!IsWorldColumnSourceTraceEnabled())
  {
    return;
  }
  const std::string message =
      std::string("source=") + source + " outcome=" + outcome + " coord=(" +
      std::to_string(ground.x) + ",0," + std::to_string(ground.z) + ") " +
      details;
  CubatariumLogInfo("WorldColumnSource", message);
}

void LogWorldColumnSave(const char *outcome, glm::ivec3 coord,
                        const std::string &details, bool always = false)
{
  if (!always && !IsWorldColumnSaveTraceEnabled())
  {
    return;
  }
  const std::string message =
      std::string("outcome=") + outcome + " coord=(" +
      std::to_string(coord.x) + "," + std::to_string(coord.y) + "," +
      std::to_string(coord.z) + ") " + details;
  if (always)
  {
    CubatariumLogError("WorldColumnSave", message);
  }
  else
  {
    CubatariumLogInfo("WorldColumnSave", message);
  }
}

bool HasChunkDataFiles(const std::string &chunks_dir)
{
  if (!std::filesystem::exists(chunks_dir) ||
      !std::filesystem::is_directory(chunks_dir))
  {
    return false;
  }
  for (const auto &entry : std::filesystem::directory_iterator(chunks_dir))
  {
    const auto ext = entry.path().extension();
    if (ext == ".json" || ext == ".cchunk")
    {
      return true;
    }
  }
  return false;
}

} // namespace

UWorldPersistence::UWorldPersistence()
{
  ChunkStorage = std::make_unique<UChunkStorageService>();
}

void UWorldPersistence::SetWorldFolderPath(const std::string &path)
{
  if (WorldFolderPath == path)
  {
    return;
  }

  SaveColumnLightFlagsIfDirty();
  if (!FlushColumnLightFlagsForWorldSwitch())
  {
    const std::string message =
        "outcome=light_flags_flush_timeout folder=" + WorldFolderPath +
        " revision=" + std::to_string(LightCompleteRevision);
    CubatariumLogInfo("WorldColumnSave", message);
  }

  WorldFolderPath = path;
  LightCompleteColumns.clear();
  LightCompleteDirty = false;
  LightCompleteLoaded = false;
  ++LightCompleteRevision;
  LightCompleteSaveFailures = 0;
  LightCompleteSaveRetryAt = {};
  if (!WorldFolderPath.empty())
  {
    EnsureChunkIoInitialized();
    AsyncChunkIo->RequestDiskIndexWarmup(*ChunkStorage, WorldFolderPath);
    LoadColumnLightFlags();
  }
}

bool UWorldPersistence::HasPersistedTerrainOnDisk(
    const std::string &world_folder_path)
{
  const std::string chunks_dir = world_folder_path + "/chunks";
  if (HasChunkDataFiles(chunks_dir) ||
      UChunkStorageService::HasChunkFilesOnDisk(world_folder_path))
  {
    return true;
  }

  const std::string chunks_file = world_folder_path + "/chunks.json";
  if (!std::filesystem::exists(chunks_file))
  {
    return false;
  }

  try
  {
    std::ifstream file(chunks_file);
    if (!file.is_open())
    {
      return false;
    }
    const json data = json::parse(file);
    const std::string storage = data.value("storage", "");
    return storage == "per_file" || storage == "binary" || storage == "json";
  }
  catch (const json::exception &)
  {
    return false;
  }
}

void UWorldPersistence::EnsureChunkIoInitialized()
{
  if (!AsyncChunkIo)
  {
    AsyncChunkIo = std::make_unique<UAsyncChunkIO>();
  }
  if (!ChunkStorage)
  {
    ChunkStorage = std::make_unique<UChunkStorageService>();
  }
}

void UWorldPersistence::SetChunkWriteFormat(ChunkWriteFormat format)
{
  if (!ChunkStorage)
  {
    ChunkStorage = std::make_unique<UChunkStorageService>();
  }
  ChunkStorage->SetWriteFormat(format);
}

ChunkWriteFormat UWorldPersistence::GetChunkWriteFormat() const
{
  return ChunkStorage ? ChunkStorage->GetSettings().writeFormat
                      : ChunkWriteFormat::Binary;
}

bool UWorldPersistence::IsColumnLightComplete(glm::ivec2 ground_xz) const
{
  return LightCompleteColumns.count(ground_xz) != 0;
}

void UWorldPersistence::SetColumnLightComplete(glm::ivec2 ground_xz,
                                               bool complete)
{
  if (complete)
  {
    if (LightCompleteColumns.insert(ground_xz).second)
    {
      LightCompleteDirty = true;
      ++LightCompleteRevision;
    }
  }
  else
  {
    ClearColumnLightComplete(ground_xz);
  }
}

void UWorldPersistence::ClearColumnLightComplete(glm::ivec2 ground_xz)
{
  if (LightCompleteColumns.erase(ground_xz) > 0)
  {
    LightCompleteDirty = true;
    ++LightCompleteRevision;
  }
}

void UWorldPersistence::LoadColumnLightFlags()
{
  LightCompleteColumns.clear();
  LightCompleteDirty = false;
  LightCompleteLoaded = true;
  if (WorldFolderPath.empty())
  {
    return;
  }
  const std::string path = WorldFolderPath + "/column_light.json";
  if (!std::filesystem::exists(path))
  {
    return;
  }
  try
  {
    std::ifstream file(path);
    if (!file.is_open())
    {
      return;
    }
    const json data = json::parse(file);
    if (!data.contains("complete") || !data["complete"].is_array())
    {
      return;
    }
    for (const auto &entry : data["complete"])
    {
      if (!entry.is_array() || entry.size() < 2)
      {
        continue;
      }
      LightCompleteColumns.emplace(entry[0].get<int>(), entry[1].get<int>());
    }
  }
  catch (const json::exception &)
  {
  }
}

void UWorldPersistence::SaveColumnLightFlagsIfDirty()
{
  if (!LightCompleteDirty || WorldFolderPath.empty() ||
      LightCompleteSaveInFlight ||
      std::chrono::steady_clock::now() < LightCompleteSaveRetryAt)
  {
    return;
  }

  EnsureChunkIoInitialized();
  if (!AsyncChunkIo)
  {
    return;
  }

  std::vector<glm::ivec2> complete_columns;
  complete_columns.reserve(LightCompleteColumns.size());
  for (const glm::ivec2 &col : LightCompleteColumns)
  {
    complete_columns.push_back(col);
  }
  LightCompleteSaveWorldFolder = WorldFolderPath;
  LightCompleteSaveRevision = LightCompleteRevision;
  LightCompleteSaveInFlight = true;
  AsyncChunkIo->RequestSaveColumnLightFlags(
      LightCompleteSaveWorldFolder, LightCompleteSaveRevision,
      std::move(complete_columns));
}

void UWorldPersistence::ProcessColumnLightFlagSaveResults(
    double *queue_mutex_wait_ms, double *queue_mutex_held_ms,
    std::size_t *result_count)
{
  if (queue_mutex_wait_ms)
  {
    *queue_mutex_wait_ms = 0.0;
  }
  if (queue_mutex_held_ms)
  {
    *queue_mutex_held_ms = 0.0;
  }
  if (result_count)
  {
    *result_count = 0;
  }
  if (!AsyncChunkIo)
  {
    return;
  }
  std::vector<AsyncColumnLightFlagsSaveResult> results =
      AsyncChunkIo->DrainColumnLightFlagsSaves(queue_mutex_wait_ms,
                                               queue_mutex_held_ms);
  if (result_count)
  {
    *result_count = results.size();
  }
  for (AsyncColumnLightFlagsSaveResult &result : results)
  {
    LightCompleteSaveInFlight = false;
    LightCompleteSaveWorldFolder.clear();
    LightCompleteSaveRevision = 0;
    LightCompleteSaveRetryAt = {};

    if (result.success)
    {
      LightCompleteSaveFailures = 0;
      if (result.worldFolder == WorldFolderPath &&
          result.revision == LightCompleteRevision)
      {
        LightCompleteDirty = false;
      }
      continue;
    }

    if (result.worldFolder == WorldFolderPath)
    {
      LightCompleteDirty = true;
      ++LightCompleteSaveFailures;
      const unsigned int exponent =
          std::min<unsigned int>(LightCompleteSaveFailures - 1, 6);
      const auto retry_delay = std::chrono::milliseconds(
          std::min<int>(30000, 250 * (1 << exponent)));
      LightCompleteSaveRetryAt =
          std::chrono::steady_clock::now() + retry_delay;
    }
  }
}

bool UWorldPersistence::FlushColumnLightFlagsForWorldSwitch()
{
  if (!LightCompleteDirty || WorldFolderPath.empty())
  {
    return true;
  }
  EnsureChunkIoInitialized();
  if (!AsyncChunkIo)
  {
    return false;
  }

  constexpr auto kFlushTimeout = std::chrono::seconds(10);
  const auto deadline = std::chrono::steady_clock::now() + kFlushTimeout;
  while (std::chrono::steady_clock::now() < deadline)
  {
    ProcessColumnLightFlagSaveResults();
    SaveColumnLightFlagsIfDirty();
    if (!LightCompleteDirty && !LightCompleteSaveInFlight)
    {
      return true;
    }

    const auto now = std::chrono::steady_clock::now();
    if (LightCompleteSaveInFlight)
    {
      const auto remaining = std::chrono::duration_cast<
          std::chrono::milliseconds>(deadline - now);
      (void)AsyncChunkIo->WaitForColumnLightFlagsSaveIdleFor(
          std::min(remaining, std::chrono::milliseconds(100)));
    }
    else if (LightCompleteSaveRetryAt > now)
    {
      const auto remaining = std::chrono::duration_cast<
          std::chrono::milliseconds>(deadline - now);
      const auto retry_wait = std::chrono::duration_cast<
          std::chrono::milliseconds>(LightCompleteSaveRetryAt - now);
      std::this_thread::sleep_for(std::min(
          remaining, std::min(retry_wait, std::chrono::milliseconds(100))));
    }
  }
  ProcessColumnLightFlagSaveResults();
  return !LightCompleteDirty && !LightCompleteSaveInFlight;
}

void UWorldPersistence::EnqueueTerrainColumnRelight(int world_x, int world_z,
                                                    const bool priority,
                                                    int min_y, int max_y)
{
  EnqueueTerrainColumnRelightImpl(world_x, world_z, priority, min_y, max_y,
                                  /*visible_admission=*/false);
}

void UWorldPersistence::EnqueueTerrainColumnRelightImpl(
    int world_x, int world_z, const bool priority, int min_y, int max_y,
    const bool visible_admission)
{
  const glm::ivec2 key(world_x, world_z);
  // Block-space key → column xz for light_complete invalidation.
  const glm::ivec2 ground_xz(FloorDiv(world_x, CHUNK_SIZE),
                             FloorDiv(world_z, CHUNK_SIZE));
  // Phase 5.7R5: unified FIFO admit — new enqueues outside nh≤1 blocked under BP.
  // Already-keyed columns may still Promote (drain-owner / pin repair).
  if (PendingTerrainColumnRelightKeys.count(key) == 0)
  {
    const int fifo_n =
        static_cast<int>(PendingTerrainColumnRelightKeys.size());
    int horiz = 0;
    if (RelightFifoTrimFocusValid)
    {
      horiz = std::max(std::abs(ground_xz.x - RelightFifoTrimFocusCx),
                       std::abs(ground_xz.y - RelightFifoTrimFocusCz));
    }
    if (!visible_admission &&
        !ShouldAdmitRelightFifoEnqueue(fifo_n, horiz))
    {
      // A FIFO admission denial is backpressure, not completion. Keep the
      // exact column/band as durable work so PendingLight cannot outlive every
      // executable or retryable owner.
      DeferFarRelightColumn(ground_xz, min_y, max_y, priority);
      return;
    }
  }
  ClearColumnLightComplete(ground_xz);
  if (max_y >= min_y)
  {
    auto &band = PendingTerrainColumnRelightYBands[key];
    if (PendingTerrainColumnRelightKeys.count(key) == 0)
    {
      band = glm::ivec2(min_y, max_y);
    }
    else
    {
      band.x = std::min(band.x, min_y);
      band.y = std::max(band.y, max_y);
    }
  }
  if (!PendingTerrainColumnRelightKeys.insert(key).second)
  {
    // Already keyed: promote-from-far or repair Keys-without-deque ghosts.
    // Always run Promote so non-priority requeues cannot leave ghosts stuck
    // (those blocked MarkRelit forever with pending_light≈30, relight_drain≈0).
    PromoteTerrainColumnRelight(key);
    return;
  }
  const int pin_cx = RelightFifoPinCx;
  const int pin_cz = RelightFifoPinCz;
  const bool is_pin = ShouldProtectRelightFifoPinKey(
      ground_xz.x, ground_xz.y, RelightFifoPinValid, pin_cx, pin_cz);
  const bool force_priority =
      priority ||
      ShouldForcePinColumnPriority(is_pin, RelightEnqueueMissHoriz_,
                                   RelightEnqueueWitnessHoldN_);
  if (force_priority)
  {
    if (is_pin)
    {
      PendingTerrainColumnRelightsPriority.insert(
          PendingTerrainColumnRelightsPriority.begin(), key);
      ++RelightFifoPriorityInsertN;
    }
    else
    {
      PendingTerrainColumnRelightsPriority.push_back(key);
    }
  }
  else
  {
    PendingTerrainColumnRelights.push_back(key);
  }
  const int soft_cap = URuntimeTuning::Get().RelightFifoSoftCap;
  // Bound far FIFO growth: drop oldest far entries (priority deque untouched).
  // P1: never pop the pinned miss / hold key — scan for the next victim.
  while (soft_cap > 0 &&
         static_cast<int>(PendingTerrainColumnRelights.size()) > soft_cap)
  {
    auto victim_it = PendingTerrainColumnRelights.end();
    for (auto it = PendingTerrainColumnRelights.begin();
         it != PendingTerrainColumnRelights.end(); ++it)
    {
      const int cx = FloorDiv(it->x, CHUNK_SIZE);
      const int cz = FloorDiv(it->y, CHUNK_SIZE);
      if (ShouldProtectRelightFifoTrimVictim(
              cx, cz, RelightFifoPinValid, pin_cx, pin_cz,
              RelightFifoTrimFocusValid, RelightFifoTrimFocusCx,
              RelightFifoTrimFocusCz))
      {
        ++RelightFifoPinSavedN;
        continue;
      }
      victim_it = it;
      break;
    }
    if (victim_it == PendingTerrainColumnRelights.end())
    {
      ++RelightFifoProtectBlockN;
      break;
    }
    const glm::ivec2 victim = *victim_it;
    PreserveRelightFifoVictimAsDeferred(victim, /*priority=*/false);
    PendingTerrainColumnRelights.erase(victim_it);
    PendingTerrainColumnRelightKeys.erase(victim);
    PendingTerrainColumnRelightYBands.erase(victim);
    ++RelightFifoOverflowDroppedN;
  }
}

bool UWorldPersistence::EnqueueVisibleRelight(
    int world_x, int world_z, int min_y, int max_y, glm::ivec3 focus_ground,
    int max_horiz,
    const std::vector<glm::ivec2> &protected_visible_columns,
    uint8_t *outcome, int *out_victim_horiz)
{
  // Outcome: 1=normal admission, 2=no replaceable victim,
  // 3=victim replaced and target queued, 4=target failed after victim removal,
  // 5=already queued, 6=outside horizon, 7=normal admission failed,
  // 8=already in flight, 9=admitted into the bounded visible-repair reserve,
  // 10=visible-repair reserve insertion failed, 14=nearer exact target
  // replaced a farther, not-yet-promoted deferred target.
  if (outcome)
  {
    *outcome = 0;
  }
  if (out_victim_horiz)
  {
    *out_victim_horiz = -1;
  }
  const glm::ivec2 key(world_x, world_z);
  const glm::ivec2 ground_xz(FloorDiv(world_x, CHUNK_SIZE),
                             FloorDiv(world_z, CHUNK_SIZE));
  const int horiz = std::max(std::abs(ground_xz.x - focus_ground.x),
                             std::abs(ground_xz.y - focus_ground.z));
  if (max_horiz < 0 || horiz > max_horiz)
  {
    if (outcome)
    {
      *outcome = 6;
    }
    return false;
  }

  // A visible first-mesh request may be retrying a durable far-deferred
  // relight for the same column. Transfer that band into the visible owner so
  // accepting this request cannot leave a duplicate deferred record behind.
  const auto deferred_far = DeferredFarRelightColumns.find(ground_xz);
  if (deferred_far != DeferredFarRelightColumns.end() &&
      deferred_far->second.y_band.y >= deferred_far->second.y_band.x)
  {
    min_y = std::min(min_y, deferred_far->second.y_band.x);
    max_y = std::max(max_y, deferred_far->second.y_band.y);
  }
  const auto clear_deferred_far = [&]()
  { DeferredFarRelightColumns.erase(ground_xz); };

  if (auto deferred = DeferredVisibleDrawGateRelightYBands.find(key);
      deferred != DeferredVisibleDrawGateRelightYBands.end())
  {
    deferred->second.x = std::min(deferred->second.x, min_y);
    deferred->second.y = std::max(deferred->second.y, max_y);
    clear_deferred_far();
    if (outcome)
    {
      *outcome = 9;
    }
    return true;
  }

  const auto prioritize_visible_repair = [&]()
  {
    auto remove_key = [&](std::deque<glm::ivec2> &queue)
    {
      const auto it = std::find(queue.begin(), queue.end(), key);
      if (it != queue.end())
      {
        queue.erase(it);
      }
    };
    remove_key(PendingTerrainColumnRelights);
    remove_key(PendingTerrainColumnRelightsPriority);

    // Preserve the active miss witness at the front, including when it was
    // still in the far deque. Exact renderer rejects follow it immediately,
    // ahead of generic focus work that can otherwise starve visible repair.
    const glm::ivec2 pin_key(RelightFifoPinCx * CHUNK_SIZE,
                             RelightFifoPinCz * CHUNK_SIZE);
    if (RelightFifoPinValid && pin_key == key)
    {
      PendingTerrainColumnRelightsPriority.push_front(key);
      return;
    }
    bool pin_front = false;
    if (RelightFifoPinValid && pin_key != key &&
        PendingTerrainColumnRelightKeys.count(pin_key) != 0)
    {
      auto pin_it = std::find(PendingTerrainColumnRelightsPriority.begin(),
                              PendingTerrainColumnRelightsPriority.end(),
                              pin_key);
      if (pin_it == PendingTerrainColumnRelightsPriority.end())
      {
        pin_it = std::find(PendingTerrainColumnRelights.begin(),
                           PendingTerrainColumnRelights.end(), pin_key);
        if (pin_it != PendingTerrainColumnRelights.end())
        {
          PendingTerrainColumnRelights.erase(pin_it);
          PendingTerrainColumnRelightsPriority.push_front(pin_key);
          pin_front = true;
        }
      }
      else
      {
        if (pin_it != PendingTerrainColumnRelightsPriority.begin())
        {
          PendingTerrainColumnRelightsPriority.erase(pin_it);
          PendingTerrainColumnRelightsPriority.push_front(pin_key);
        }
        pin_front = true;
      }
    }

    auto insert_it = PendingTerrainColumnRelightsPriority.begin();
    if (pin_front)
    {
      ++insert_it;
    }
    PendingTerrainColumnRelightsPriority.insert(insert_it, key);
  };

  if (PendingTerrainColumnRelightKeys.count(key) != 0)
  {
    EnqueueTerrainColumnRelight(world_x, world_z, /*priority=*/true, min_y,
                                max_y);
    prioritize_visible_repair();
    clear_deferred_far();
    if (outcome)
    {
      *outcome = 5;
    }
    return true;
  }

  const int fifo_n = GetPendingTerrainColumnRelightCount();
  if (ShouldAdmitRelightFifoEnqueue(fifo_n, horiz))
  {
    // This branch has already admitted against the caller's current focus.
    // Bypass EnqueueTerrainColumnRelight's second pressure check, which uses
    // the FIFO trim focus and can reject the same visible target while moving.
    EnqueueTerrainColumnRelightImpl(world_x, world_z, /*priority=*/true, min_y,
                                    max_y, /*visible_admission=*/true);
    const bool admitted = PendingTerrainColumnRelightKeys.count(key) != 0;
    if (admitted)
    {
      prioritize_visible_repair();
      clear_deferred_far();
    }
    if (outcome)
    {
      *outcome = admitted ? 1 : 7;
    }
    return admitted;
  }

  // Keep total relight work bounded: replace one queued entry outside the
  // current visible repair ring. The queue can be priority-heavy, so inspect
  // both deques. Preserve the active miss pin; the wider focus trim halo is
  // too broad here and can otherwise block every draw-gate repair.
  std::deque<glm::ivec2> *victim_queue = nullptr;
  auto victim_it = PendingTerrainColumnRelights.end();
  int victim_horiz = horiz;
  const auto find_farthest_unpinned_victim =
      [&](std::deque<glm::ivec2> &queue)
  {
    for (auto it = queue.begin(); it != queue.end(); ++it)
    {
      const int cx = FloorDiv(it->x, CHUNK_SIZE);
      const int cz = FloorDiv(it->y, CHUNK_SIZE);
      const int candidate_horiz =
          std::max(std::abs(cx - focus_ground.x),
                   std::abs(cz - focus_ground.z));
      if (candidate_horiz <= victim_horiz ||
          ShouldProtectRelightFifoPinKey(cx, cz, RelightFifoPinValid,
                                         RelightFifoPinCx, RelightFifoPinCz) ||
          std::find(protected_visible_columns.begin(),
                    protected_visible_columns.end(),
                    glm::ivec2(cx, cz)) != protected_visible_columns.end())
      {
        continue;
      }
      victim_queue = &queue;
      victim_it = it;
      victim_horiz = candidate_horiz;
    }
  };
  find_farthest_unpinned_victim(PendingTerrainColumnRelights);
  find_farthest_unpinned_victim(PendingTerrainColumnRelightsPriority);
  if (!victim_queue)
  {
    // Keep the existing shared-FIFO reserve first. If no safe victim exists,
    // retain a small number of exact draw-gate targets outside the shared FIFO;
    // the capture dequeue promotes these directly when it is ready to consume
    // another item. This preserves visible work without growing/churning the
    // priority deque.
    constexpr int kVisibleRelightReserve =
        kVisibleDrawGateRelightTargetLimit;
    if (ShouldAdmitRelightFifoEnqueue(
            fifo_n, horiz, /*fifo_backpressure=*/16 + kVisibleRelightReserve))
    {
      EnqueueTerrainColumnRelightImpl(world_x, world_z, /*priority=*/true,
                                      min_y, max_y,
                                      /*visible_admission=*/true);
      const bool admitted = PendingTerrainColumnRelightKeys.count(key) != 0;
      if (admitted)
      {
        prioritize_visible_repair();
      }
      if (outcome)
      {
        *outcome = admitted ? 9 : 10;
      }
      return admitted;
    }
    if (DeferredVisibleDrawGateRelightYBands.size() <
        static_cast<size_t>(kVisibleRelightReserve))
    {
      DeferredVisibleDrawGateRelightYBands.emplace(
          key, glm::ivec2(min_y, max_y));
      clear_deferred_far();
      if (outcome)
      {
        *outcome = 9;
      }
      return true;
    }

    // The exact-target side lane is intentionally bounded, but retaining its
    // first eight requests forever can starve a newly visible near target.
    // Replace only an unpromoted entry and only when the incoming target is
    // strictly closer. Keep both the active miss pin and targets already
    // promoted into the shared FIFO out of the victim set.
    auto farthest_deferred = DeferredVisibleDrawGateRelightYBands.end();
    int farthest_deferred_horiz = horiz;
    for (auto it = DeferredVisibleDrawGateRelightYBands.begin();
         it != DeferredVisibleDrawGateRelightYBands.end(); ++it)
    {
      const glm::ivec2 deferred_key = it->first;
      const int deferred_cx = FloorDiv(deferred_key.x, CHUNK_SIZE);
      const int deferred_cz = FloorDiv(deferred_key.y, CHUNK_SIZE);
      if (ShouldProtectRelightFifoPinKey(
              deferred_cx, deferred_cz, RelightFifoPinValid,
              RelightFifoPinCx, RelightFifoPinCz) ||
          PendingTerrainColumnRelightKeys.count(deferred_key) != 0)
      {
        continue;
      }
      const int deferred_horiz =
          std::max(std::abs(deferred_cx - focus_ground.x),
                   std::abs(deferred_cz - focus_ground.z));
      if (deferred_horiz > farthest_deferred_horiz)
      {
        farthest_deferred = it;
        farthest_deferred_horiz = deferred_horiz;
      }
    }
    if (farthest_deferred != DeferredVisibleDrawGateRelightYBands.end())
    {
      const glm::ivec2 victim = farthest_deferred->first;
      DeferredVisibleDrawGateRelightYBands.erase(farthest_deferred);
      PendingVisibleDrawGateRelightYBands.erase(victim);
      DeferredVisibleDrawGateRelightYBands.emplace(
          key, glm::ivec2(min_y, max_y));
      clear_deferred_far();
      if (outcome)
      {
        *outcome = 14;
      }
      return true;
    }
    if (outcome)
    {
      *outcome = 2;
    }
    return false;
  }

  if (out_victim_horiz)
  {
    *out_victim_horiz = victim_horiz;
  }
  const glm::ivec2 victim = *victim_it;
  PreserveRelightFifoVictimAsDeferred(
      victim, victim_queue == &PendingTerrainColumnRelightsPriority);
  victim_queue->erase(victim_it);
  PendingTerrainColumnRelightKeys.erase(victim);
  PendingTerrainColumnRelightYBands.erase(victim);
  ++RelightFifoOverflowDroppedN;
  EnqueueTerrainColumnRelightImpl(world_x, world_z, /*priority=*/true, min_y,
                                  max_y, /*visible_admission=*/true);
  const bool admitted = PendingTerrainColumnRelightKeys.count(key) != 0;
  if (admitted)
  {
    prioritize_visible_repair();
    clear_deferred_far();
  }
  if (outcome)
  {
    *outcome = admitted ? 3 : 4;
  }
  return admitted;
}

bool UWorldPersistence::EnqueueVisibleDrawGateRelight(
    int world_x, int world_z, int min_y, int max_y, glm::ivec3 focus_ground,
    int max_horiz,
    const std::vector<glm::ivec2> &protected_visible_columns,
    uint8_t *outcome, int *out_victim_horiz)
{
  return EnqueueVisibleRelight(world_x, world_z, min_y, max_y, focus_ground,
                               max_horiz, protected_visible_columns, outcome,
                               out_victim_horiz);
}

void UWorldPersistence::NoteVisibleFirstMeshRelight(
    glm::ivec2 world_block_key, int min_y, int max_y)
{
  if (max_y < min_y)
  {
    return;
  }
  auto [it, inserted] = PendingVisibleFirstMeshRelightYBands.try_emplace(
      world_block_key, glm::ivec2(std::max(0, min_y), max_y));
  if (!inserted)
  {
    it->second.x = std::min(it->second.x, std::max(0, min_y));
    it->second.y = std::max(it->second.y, max_y);
  }
}

void UWorldPersistence::ClearVisibleFirstMeshRelightIfNotQueued(
    glm::ivec2 world_block_key)
{
  const glm::ivec2 ground_xz(FloorDiv(world_block_key.x, CHUNK_SIZE),
                             FloorDiv(world_block_key.y, CHUNK_SIZE));
  if (PendingTerrainColumnRelightKeys.count(world_block_key) == 0 &&
      DeferredFarRelightColumns.count(ground_xz) == 0)
  {
    PendingVisibleFirstMeshRelightYBands.erase(world_block_key);
  }
}

bool UWorldPersistence::TryEnqueueTerrainColumnRelight(UWorld &world, int world_x,
                                                       int world_z,
                                                       const bool priority,
                                                       int min_y, int max_y)
{
  const glm::ivec2 ground_xz(FloorDiv(world_x, CHUNK_SIZE),
                               FloorDiv(world_z, CHUNK_SIZE));
  const int max_y_eff =
      max_y >= 0 ? max_y : world.ProceduralTemplate.MaxHeight;
  const int band_min = std::max(0, min_y);
  const int band_max = std::max(band_min, max_y_eff);
  if (ShouldSkipNoOpTerrainRelightEnqueue(
          world.IsPendingLightBeforeMesh(ground_xz),
          world.IsColumnLitReady(glm::ivec3(ground_xz.x, 0, ground_xz.y)),
          ColumnSurfaceBandNeedsRelight(world, ground_xz, band_min, band_max)))
  {
    ++world.GetPhysicsTelemetryMutable().RelightSkippedNoOpEnqueueN;
    return false;
  }
  EnqueueTerrainColumnRelight(world_x, world_z, priority, min_y, max_y);
  return true;
}

void UWorldPersistence::DeferFarRelightColumn(glm::ivec2 ground_xz, int min_y,
                                              int max_y, bool priority)
{
  auto it = DeferredFarRelightColumns.find(ground_xz);
  if (it == DeferredFarRelightColumns.end())
  {
    DeferredFarRelightEntry entry{};
    entry.y_band = glm::ivec2(min_y, max_y);
    entry.priority = priority;
    DeferredFarRelightColumns.emplace(ground_xz, entry);
    return;
  }
  if (max_y >= min_y)
  {
    if (it->second.y_band.y < it->second.y_band.x)
    {
      it->second.y_band = glm::ivec2(min_y, max_y);
    }
    else
    {
      it->second.y_band.x = std::min(it->second.y_band.x, min_y);
      it->second.y_band.y = std::max(it->second.y_band.y, max_y);
    }
  }
  it->second.priority = it->second.priority || priority;
}

void UWorldPersistence::ClearDeferredFarRelightColumn(glm::ivec2 ground_xz)
{
  DeferredFarRelightColumns.erase(ground_xz);
}

void UWorldPersistence::PreserveRelightFifoVictimAsDeferred(
    glm::ivec2 world_block_key, bool priority)
{
  int min_y = 0;
  int max_y = -1;
  const auto merge_band = [&](glm::ivec2 band)
  {
    if (band.y < band.x)
    {
      return;
    }
    if (max_y < min_y)
    {
      min_y = band.x;
      max_y = band.y;
    }
    else
    {
      min_y = std::min(min_y, band.x);
      max_y = std::max(max_y, band.y);
    }
  };
  if (const auto it = PendingTerrainColumnRelightYBands.find(world_block_key);
      it != PendingTerrainColumnRelightYBands.end())
  {
    merge_band(it->second);
  }
  if (const auto it = PendingVisibleDrawGateRelightYBands.find(world_block_key);
      it != PendingVisibleDrawGateRelightYBands.end())
  {
    merge_band(it->second);
  }
  if (const auto it = PendingVisibleFirstMeshRelightYBands.find(world_block_key);
      it != PendingVisibleFirstMeshRelightYBands.end())
  {
    merge_band(it->second);
  }
  const glm::ivec2 ground_xz(FloorDiv(world_block_key.x, CHUNK_SIZE),
                             FloorDiv(world_block_key.y, CHUNK_SIZE));
  DeferFarRelightColumn(ground_xz, min_y, max_y, priority);
}

int UWorldPersistence::AdmitDeferredFarRelightColumns(UWorld &world,
                                                      glm::ivec3 focus_ground,
                                                      int pin_horiz)
{
  if (DeferredFarRelightColumns.empty())
  {
    world.GetPhysicsTelemetryMutable().RelightDeferredFarPendingN = 0;
    return 0;
  }
  const size_t deferred_before = DeferredFarRelightColumns.size();
  const int fifo_before = GetPendingTerrainColumnRelightCount();
  int admitted = 0;
  int in_range = 0;
  int outside_range = 0;
  int fifo_blocked = 0;
  int already_queued = 0;
  int already_inflight = 0;
  int enqueue_attempts = 0;
  int enqueue_rejected = 0;
  int enqueue_trimmed = 0;
  int nearest_horiz = -1;
  glm::ivec2 nearest_coord{};
  std::vector<glm::ivec2> to_erase;
  to_erase.reserve(DeferredFarRelightColumns.size());
  for (const auto &kv : DeferredFarRelightColumns)
  {
    const glm::ivec2 ground_xz = kv.first;
    const int horiz = std::max(std::abs(ground_xz.x - focus_ground.x),
                               std::abs(ground_xz.y - focus_ground.z));
    if (nearest_horiz < 0 || horiz < nearest_horiz)
    {
      nearest_horiz = horiz;
      nearest_coord = ground_xz;
    }
    if (horiz > pin_horiz)
    {
      ++outside_range;
      continue;
    }
    ++in_range;
    // Use the same live-FIFO predicate as EnqueueTerrainColumnRelightImpl.
    // The wider deferral pin (currently four) is a service horizon, not the
    // FIFO's tighter nh<=1 pressure admission rule.
    const int fifo_n = GetPendingTerrainColumnRelightCount();
    if (!ShouldAdmitRelightFifoEnqueue(fifo_n, horiz))
    {
      ++fifo_blocked;
      continue;
    }
    const glm::ivec2 band = kv.second.y_band;
    const glm::ivec2 world_key(ground_xz.x * CHUNK_SIZE,
                               ground_xz.y * CHUNK_SIZE);
    const bool async_owned = world.IsAsyncRelightColumnInFlight(ground_xz);
    const bool queued_before = IsTerrainColumnRelightQueued(world_key);
    bool enqueue_caused_fifo_drop = false;
    already_queued += queued_before ? 1 : 0;
    already_inflight += async_owned ? 1 : 0;
    if (!async_owned && !queued_before)
    {
      ++enqueue_attempts;
      // This entry already passed the live FIFO admission check above using
      // focus_ground. Match EnqueueVisibleRelight: skip the second pressure
      // check in EnqueueTerrainColumnRelightImpl, which recomputes distance
      // against the independently stored trim focus and can reject this same
      // work after the outer admission decision.
      const int fifo_dropped_before = RelightFifoOverflowDroppedN;
      EnqueueTerrainColumnRelightImpl(world_key.x, world_key.y,
                                      kv.second.priority, band.x, band.y,
                                      /*visible_admission=*/true);
      enqueue_caused_fifo_drop =
          RelightFifoOverflowDroppedN > fifo_dropped_before;
    }
    const bool accepted = IsTerrainColumnRelightQueued(world_key) || async_owned;
    if (!accepted)
    {
      ++enqueue_rejected;
      enqueue_trimmed += enqueue_caused_fifo_drop ? 1 : 0;
      // If the bounded FIFO does not retain the key, keep this deferred
      // record and let a later admission pass retry it.
      continue;
    }
    world.TryNotePendingLightBeforeMesh(glm::ivec3(ground_xz.x, 0, ground_xz.y),
                                     band.x, band.y, __FUNCTION__);
    to_erase.push_back(ground_xz);
    ++admitted;
  }
  for (const glm::ivec2 &key : to_erase)
  {
    DeferredFarRelightColumns.erase(key);
  }
  auto &telem = world.GetPhysicsTelemetryMutable();
  telem.RelightDeferredFarPendingN =
      static_cast<int>(DeferredFarRelightColumns.size());
  static const bool audit_admission =
      IsEnvironmentFlagEnabled("CUBATARIUM_RELIGHT_AUDIT");
  static auto last_admission_audit = std::chrono::steady_clock::time_point{};
  const auto audit_now = std::chrono::steady_clock::now();
  if (audit_admission &&
      (last_admission_audit == std::chrono::steady_clock::time_point{} ||
       audit_now - last_admission_audit >= std::chrono::seconds(1)))
  {
    last_admission_audit = audit_now;
    CubatariumLogInfo(
        "RelightAudit",
        std::string("deferred_far_admit focus=(") +
            std::to_string(focus_ground.x) + "," +
            std::to_string(focus_ground.z) + ") pending=" +
            std::to_string(deferred_before) + "->" +
            std::to_string(DeferredFarRelightColumns.size()) + " fifo=" +
            std::to_string(fifo_before) + "->" +
            std::to_string(GetPendingTerrainColumnRelightCount()) +
            " pin=" + std::to_string(pin_horiz) +
            " in_range=" + std::to_string(in_range) +
            " outside=" + std::to_string(outside_range) +
            " fifo_blocked=" + std::to_string(fifo_blocked) +
            " queued=" + std::to_string(already_queued) +
            " inflight=" + std::to_string(already_inflight) +
            " enqueue=" + std::to_string(enqueue_attempts) +
            " rejected=" + std::to_string(enqueue_rejected) +
            " trimmed=" + std::to_string(enqueue_trimmed) +
            " admitted=" + std::to_string(admitted) + " nearest=(" +
            std::to_string(nearest_coord.x) + "," +
            std::to_string(nearest_coord.y) + ")/h=" +
            std::to_string(nearest_horiz));
  }
  return admitted;
}

void UWorldPersistence::SetRelightFifoPin(glm::ivec2 chunk_xz, bool valid)
{
  RelightFifoPinValid = valid;
  RelightFifoPinCx = chunk_xz.x;
  RelightFifoPinCz = chunk_xz.y;
}

int UWorldPersistence::TakeRelightFifoOverflowDropped()
{
  const int n = RelightFifoOverflowDroppedN;
  RelightFifoOverflowDroppedN = 0;
  return n;
}

int UWorldPersistence::TakeRelightFifoPinSaved()
{
  const int n = RelightFifoPinSavedN;
  RelightFifoPinSavedN = 0;
  return n;
}

int UWorldPersistence::TakeRelightFifoPriorityInsert()
{
  const int n = RelightFifoPriorityInsertN;
  RelightFifoPriorityInsertN = 0;
  return n;
}

int UWorldPersistence::TakeRelightFifoProtectBlock()
{
  const int n = RelightFifoProtectBlockN;
  RelightFifoProtectBlockN = 0;
  return n;
}

void UWorldPersistence::PromoteTerrainColumnRelight(
    glm::ivec2 world_block_key)
{
  // Queue keys are world-block coordinates; the protected pin is a chunk
  // coordinate. Compare in one coordinate space or every non-origin column
  // is incorrectly treated as a different pinned target.
  const glm::ivec2 column(FloorDiv(world_block_key.x, CHUNK_SIZE),
                          FloorDiv(world_block_key.y, CHUNK_SIZE));
  if (RelightFifoPinValid &&
      column != glm::ivec2(RelightFifoPinCx, RelightFifoPinCz))
  {
    ++RelightFifoProtectBlockN;
    return;
  }
  for (const glm::ivec2 &queued : PendingTerrainColumnRelightsPriority)
  {
    if (queued == world_block_key)
    {
      return;
    }
  }
  auto it = std::find(PendingTerrainColumnRelights.begin(),
                      PendingTerrainColumnRelights.end(), world_block_key);
  if (it != PendingTerrainColumnRelights.end())
  {
    PendingTerrainColumnRelights.erase(it);
    PendingTerrainColumnRelightsPriority.push_back(world_block_key);
    return;
  }
  // Keys-without-deque ghost: Drain used to re-Enqueue in-flight columns and
  // leave Keys set with no FIFO entry — pending_light then stuck forever.
  if (PendingTerrainColumnRelightKeys.count(world_block_key) != 0)
  {
    PendingTerrainColumnRelightsPriority.push_back(world_block_key);
  }
}

bool UWorldPersistence::PrioritizeTerrainColumnRelight(
    glm::ivec2 world_block_key, bool pin_in_flight)
{
  if (PendingTerrainColumnRelightKeys.count(world_block_key) == 0)
  {
    return false;
  }

  auto erase_key = [&](std::deque<glm::ivec2> &queue)
  {
    const auto it = std::find(queue.begin(), queue.end(), world_block_key);
    if (it == queue.end())
    {
      return false;
    }
    queue.erase(it);
    return true;
  };

  const glm::ivec2 pin_key(RelightFifoPinCx * CHUNK_SIZE,
                            RelightFifoPinCz * CHUNK_SIZE);
  const bool target_is_pin = RelightFifoPinValid && pin_key == world_block_key;
  const bool pin_queued = RelightFifoPinValid && !pin_in_flight &&
                          !target_is_pin &&
                          PendingTerrainColumnRelightKeys.count(pin_key) != 0;
  if (pin_queued)
  {
    // The witness owns the first slot even if it was left in the far deque.
    erase_key(PendingTerrainColumnRelightsPriority);
    erase_key(PendingTerrainColumnRelights);
    PendingTerrainColumnRelightsPriority.push_front(pin_key);
  }

  erase_key(PendingTerrainColumnRelightsPriority);
  erase_key(PendingTerrainColumnRelights);
  auto insert_at = PendingTerrainColumnRelightsPriority.begin();
  if (pin_queued && !target_is_pin)
  {
    ++insert_at;
  }
  PendingTerrainColumnRelightsPriority.insert(insert_at, world_block_key);
  return true;
}

bool UWorldPersistence::PrioritizeNearestTerrainColumnRelight(
    UWorld &world, glm::ivec3 focus_ground, int radius_chunks,
    int scan_cap)
{
  if (radius_chunks < 0 || scan_cap <= 0)
  {
    return false;
  }
  const auto target_service_radius = [&](glm::ivec2 world_key)
  {
    return PendingVisibleFirstMeshRelightYBands.count(world_key) != 0
               ? std::max(radius_chunks,
                          kVisualStageFirstMeshRelightForwardHoriz)
               : radius_chunks;
  };

  // Exact renderer rejections that could not safely enter the shared FIFO stay
  // in a bounded side lane. Promote one just-in-time, ahead of ordinary work
  // but immediately behind the active miss pin. Keep the reservation until the
  // corresponding capture is submitted.
  bool have_visible_target = false;
  bool visible_target_is_deferred = false;
  glm::ivec2 visible_target{};
  glm::ivec2 visible_band{};
  int visible_distance = radius_chunks + 1;
  std::vector<glm::ivec2> expired_visible;
  for (const auto &entry : DeferredVisibleDrawGateRelightYBands)
  {
    const glm::ivec2 world_key = entry.first;
    const glm::ivec2 column(FloorDiv(world_key.x, CHUNK_SIZE),
                            FloorDiv(world_key.y, CHUNK_SIZE));
    const int distance =
        std::max(std::abs(column.x - focus_ground.x),
                 std::abs(column.y - focus_ground.z));
    const int service_radius = target_service_radius(world_key);
    const bool already_queued =
        PendingTerrainColumnRelightKeys.count(world_key) != 0;
    if (!already_queued && distance > service_radius)
    {
      DeferFarRelightColumn(column, entry.second.x, entry.second.y,
                            /*priority=*/true);
      PendingVisibleDrawGateRelightYBands.erase(world_key);
      expired_visible.push_back(world_key);
      continue;
    }
    if (distance > service_radius ||
        world.IsAsyncRelightColumnInFlight(column))
    {
      continue;
    }
    if (!have_visible_target || distance < visible_distance ||
        (distance == visible_distance &&
         (world_key.x < visible_target.x ||
          (world_key.x == visible_target.x && world_key.y < visible_target.y))))
    {
      have_visible_target = true;
      visible_target_is_deferred = true;
      visible_target = world_key;
      visible_band = entry.second;
      visible_distance = distance;
    }
  }
  for (const glm::ivec2 &world_key : expired_visible)
  {
    DeferredVisibleDrawGateRelightYBands.erase(world_key);
  }

  // A draw-gate rejection that was admitted to the shared FIFO keeps its
  // exact Y-band in PendingVisibleDrawGateRelightYBands. Re-run that exact
  // lane at dequeue time as well: generic nearest-column sorting below can
  // otherwise replace the earlier admission pin with unrelated PendingLight
  // work on every Capture iteration.
  for (const auto &entry : PendingVisibleDrawGateRelightYBands)
  {
    const glm::ivec2 world_key = entry.first;
    if (PendingTerrainColumnRelightKeys.count(world_key) == 0)
    {
      continue;
    }
    const glm::ivec2 column(FloorDiv(world_key.x, CHUNK_SIZE),
                            FloorDiv(world_key.y, CHUNK_SIZE));
    const int distance =
        std::max(std::abs(column.x - focus_ground.x),
                 std::abs(column.y - focus_ground.z));
    if (distance > target_service_radius(world_key) ||
        world.IsAsyncRelightColumnInFlight(column))
    {
      continue;
    }
    if (!have_visible_target || distance < visible_distance ||
        (distance == visible_distance &&
         (world_key.x < visible_target.x ||
          (world_key.x == visible_target.x && world_key.y < visible_target.y))))
    {
      have_visible_target = true;
      visible_target_is_deferred = false;
      visible_target = world_key;
      visible_band = entry.second;
      visible_distance = distance;
    }
  }

  // FirstMesh relights can be reserved one column beyond the ordinary
  // renderer-repair ring. Let those exact bands reach the front of the same
  // bounded queue lane once they are inside that approach horizon.
  for (const auto &entry : PendingVisibleFirstMeshRelightYBands)
  {
    const glm::ivec2 world_key = entry.first;
    if (PendingTerrainColumnRelightKeys.count(world_key) == 0)
    {
      continue;
    }
    const glm::ivec2 column(FloorDiv(world_key.x, CHUNK_SIZE),
                            FloorDiv(world_key.y, CHUNK_SIZE));
    const int distance =
        std::max(std::abs(column.x - focus_ground.x),
                 std::abs(column.y - focus_ground.z));
    if (distance > target_service_radius(world_key) ||
        world.IsAsyncRelightColumnInFlight(column))
    {
      continue;
    }
    if (!have_visible_target || distance < visible_distance ||
        (distance == visible_distance &&
         (world_key.x < visible_target.x ||
          (world_key.x == visible_target.x && world_key.y < visible_target.y))))
    {
      have_visible_target = true;
      visible_target_is_deferred = false;
      visible_target = world_key;
      visible_band = entry.second;
      visible_distance = distance;
    }
  }

  if (have_visible_target)
  {
    if (visible_target_is_deferred &&
        PendingTerrainColumnRelightKeys.count(visible_target) == 0)
    {
      EnqueueTerrainColumnRelightImpl(
          visible_target.x, visible_target.y, /*priority=*/true,
          visible_band.x, visible_band.y, /*visible_admission=*/true);
      if (PendingTerrainColumnRelightKeys.count(visible_target) != 0)
      {
        ClearDeferredFarRelightColumn(glm::ivec2(
            FloorDiv(visible_target.x, CHUNK_SIZE),
            FloorDiv(visible_target.y, CHUNK_SIZE)));
      }
      if (PendingVisibleFirstMeshRelightYBands.count(visible_target) == 0)
      {
        PendingVisibleDrawGateRelightYBands[visible_target] = visible_band;
      }
    }
    const bool pin_in_flight =
        RelightFifoPinValid && world.IsAsyncRelightColumnInFlight(
                                   glm::ivec2(RelightFifoPinCx,
                                              RelightFifoPinCz));
    return PrioritizeTerrainColumnRelight(visible_target, pin_in_flight);
  }

  if (PendingTerrainColumnRelightsPriority.empty())
  {
    return false;
  }

  const int scan_n = std::min(
      static_cast<int>(PendingTerrainColumnRelightsPriority.size()), scan_cap);
  int best_index = -1;
  int best_distance = radius_chunks + 1;
  for (int i = 0; i < scan_n; ++i)
  {
    const glm::ivec2 key = PendingTerrainColumnRelightsPriority[i];
    const glm::ivec2 column(FloorDiv(key.x, CHUNK_SIZE),
                           FloorDiv(key.y, CHUNK_SIZE));
    if (RelightFifoPinValid &&
        column == glm::ivec2(RelightFifoPinCx, RelightFifoPinCz))
    {
      continue;
    }
    if (world.IsAsyncRelightColumnInFlight(column))
    {
      continue;
    }
    const int distance =
        std::max(std::abs(column.x - focus_ground.x),
                 std::abs(column.y - focus_ground.z));
    if (distance > radius_chunks)
    {
      continue;
    }
    // Prefer nearer targets; break ties in favor of the oldest queued entry.
    if (best_index < 0 || distance < best_distance ||
        (distance == best_distance && i > best_index))
    {
      best_index = i;
      best_distance = distance;
    }
  }
  if (best_index < 0)
  {
    return false;
  }

  const glm::ivec2 target = PendingTerrainColumnRelightsPriority[best_index];
  const bool pin_in_flight =
      RelightFifoPinValid && world.IsAsyncRelightColumnInFlight(
                                 glm::ivec2(RelightFifoPinCx,
                                            RelightFifoPinCz));
  return PrioritizeTerrainColumnRelight(target, pin_in_flight);
}

int UWorldPersistence::PromoteNearTerrainColumnRelights(glm::ivec3 focus_ground,
                                                        int radius_chunks)
{
  if (radius_chunks < 0 || PendingTerrainColumnRelights.empty())
  {
    return 0;
  }
  int promoted = 0;
  for (auto it = PendingTerrainColumnRelights.begin();
       it != PendingTerrainColumnRelights.end();)
  {
    // Keys are block-space column origins (world_x, world_z).
    const int cx = FloorDiv(it->x, CHUNK_SIZE);
    const int cz = FloorDiv(it->y, CHUNK_SIZE);
    const int dist = std::max(std::abs(cx - focus_ground.x),
                              std::abs(cz - focus_ground.z));
    if (dist > radius_chunks)
    {
      ++it;
      continue;
    }
    PendingTerrainColumnRelightsPriority.push_back(*it);
    it = PendingTerrainColumnRelights.erase(it);
    ++promoted;
  }
  return promoted;
}

void UWorldPersistence::EnqueuePlayerRelight(
    const std::vector<glm::ivec3> &block_positions)
{
  if (block_positions.empty())
  {
    return;
  }
  int min_y = block_positions.front().y;
  for (const glm::ivec3 &pos : block_positions)
  {
    min_y = std::min(min_y, pos.y);
  }
  min_y = std::max(0, min_y - CHUNK_SIZE);
  PendingPlayerRelights.push_back(
      PlayerRelightRequest{block_positions, min_y});
}

void UWorldPersistence::DrainRelightQueues(UWorld &world, int max_player_jobs,
                                           int max_bg_columns)
{
  auto &capture_telem = world.GetPhysicsTelemetryMutable();
  const bool audit_relight =
      IsEnvironmentFlagEnabled("CUBATARIUM_RELIGHT_AUDIT");
  auto log_capture_state = [&](const char *phase, int cap, int drained)
  {
    if (!audit_relight)
    {
      return;
    }
    const glm::ivec3 focus =
        UChunkManager::WorldToChunk(world.GetPreferredLoadFocusBlock());
    const glm::ivec2 front =
        !PendingTerrainColumnRelightsPriority.empty()
            ? PendingTerrainColumnRelightsPriority.front()
            : (!PendingTerrainColumnRelights.empty()
                   ? PendingTerrainColumnRelights.front()
                   : glm::ivec2(-1));
    CubatariumLogInfo(
        "RelightAudit",
        std::string("capture phase=") + phase + " drained=" +
            std::to_string(drained) + " cap=" + std::to_string(cap) +
            " stop=" + std::to_string(capture_telem.RelightCaptureStopReason) +
            " fifo=" + std::to_string(GetPendingTerrainColumnRelightCount()) +
            " fifo_front=(" + std::to_string(front.x) + "," +
            std::to_string(front.y) + ") focus=(" +
            std::to_string(focus.x) + "," + std::to_string(focus.z) +
            ") pending_light=" +
            std::to_string(world.GetPendingLightBeforeMeshCount()) +
            " inflight=" + std::to_string(world.GetAsyncRelightInFlightCount()) +
            " completed=" + std::to_string(world.GetRelightCompletedSize()));
  };
  capture_telem.RelightCaptureStopReason = 0;
  capture_telem.RelightCaptureInFlightN = world.GetAsyncRelightInFlightCount();
  capture_telem.RelightCaptureInFlightLimit = 0;
  capture_telem.RelightCaptureInflightScanN = 0;
  if (world.BlocksAsyncRelightDrain())
  {
    capture_telem.RelightCaptureStopReason = 1;
    log_capture_state("blocked", max_bg_columns, 0);
    return;
  }
  capture_telem.RelightCaptureHotSkipDrawGate = 0;
  {
    const glm::ivec3 focus_chunk =
        UChunkManager::WorldToChunk(world.GetPreferredLoadFocusBlock());
    RelightFifoTrimFocusValid = true;
    RelightFifoTrimFocusCx = focus_chunk.x;
    RelightFifoTrimFocusCz = focus_chunk.z;
    const glm::ivec3 focus_horiz(focus_chunk.x, 0, focus_chunk.z);
    AdmitDeferredFarRelightColumns(world, focus_horiz,
                                   RelightMissPinMaxHoriz());
  }
  {
    const auto &phys = world.GetPhysicsTelemetry();
    const auto &exec = GetColumnFlowExecutor();
    if (exec.HasPromoteRelightHold())
    {
      SetRelightFifoPin(exec.GetPromoteRelightHoldColumn(), true);
    }
    else
    {
      SetRelightFifoPin(glm::ivec2(phys.MissCx, phys.MissCz),
                        phys.FocusMissingMesh > 0 &&
                            ShouldHoldPinnedRelightWitness(
                                phys.MissHoriz, true, true));
    }
  }
  auto harvest_fifo_overflow = [&world, this]()
  {
    auto &telem = world.GetPhysicsTelemetryMutable();
    const int overflow = TakeRelightFifoOverflowDropped();
    const int saved = TakeRelightFifoPinSaved();
    const int priority_insert = TakeRelightFifoPriorityInsert();
    const int protect_block = TakeRelightFifoProtectBlock();
    telem.RelightFifoDropN += overflow;
    telem.RelightFifoOverflowDropN += overflow;
    telem.RelightFifoPinSavedN += saved;
    telem.RelightFifoPriorityInsertN += priority_insert;
    telem.RelightFifoProtectBlockN += protect_block;
    telem.RelightFifoDropped += static_cast<uint64_t>(std::max(0, overflow));
  };
  int drained_player = 0;
  while (!PendingPlayerRelights.empty() && drained_player < max_player_jobs)
  {
    const PlayerRelightRequest request = std::move(PendingPlayerRelights.front());
    PendingPlayerRelights.pop_front();
    world.RelightPlayerEdit(request.block_positions, request.min_world_y);
    ++drained_player;
  }

  if (max_bg_columns <= 0)
  {
    capture_telem.RelightCaptureStopReason = 2;
    log_capture_state("no_budget", max_bg_columns, 0);
    harvest_fifo_overflow();
    return;
  }
  world.ReconcileAsyncRelightColumnInFlight();
  const int max_y = world.ProceduralTemplate.MaxHeight;
  const glm::ivec3 focus_block = world.GetPreferredLoadFocusBlock();
  const int surface_band_min =
      RelightSurfaceBandMinY(focus_block.y, CHUNK_SIZE, 0);
  const int surface_band_max =
      RelightSurfaceBandMaxY(focus_block.y, CHUNK_SIZE, max_y, max_y);
  const bool async_bg =
      world.ProceduralTemplate.AsyncRelight &&
      !world.IsLightingRelightDeferred() && world.AllowsAsyncLighting();
  const glm::ivec3 focus_chunk =
      UChunkManager::WorldToChunk(world.GetPreferredLoadFocusBlock());
  const glm::ivec3 focus_horiz(focus_chunk.x, 0, focus_chunk.z);
  const int focus_radius = world.GetStreamingFocusRadius();
  const int pending_light_focus_n =
      world.CountPendingLightBeforeMeshNear(focus_horiz, focus_radius);
  const int near_fov_pending_light_n = world.CountPendingLightBeforeMeshNear(
      focus_horiz, kVisualStageNearFovHoriz);
  const bool focus_pending_high = pending_light_focus_n > 15;
  const bool focus_pending_mid = pending_light_focus_n > 0;
  const bool visual_holes =
      world.MeshService &&
      world.MeshService->HasMissingGreedyMeshInHorizontalRadius(
          world.GetBlockWorld(), focus_horiz, focus_radius);
  const bool idle_recovery =
      world.GetLastMovementSpeed() <=
          world.ProceduralTemplate.MovementPrefetchThreshold &&
      (focus_pending_mid || visual_holes);
  const URuntimeTuning &tune = URuntimeTuning::Get();
  const bool enter_fov_lit = world.IsEnterFovLitPassActive();
  const int vb_no_ticket_n = world.GetPhysicsTelemetry().VisibleBlackNoTicketN;
  const int vb_focus_n = world.GetPhysicsTelemetry().VisibleBlackFocusN;
  // MultHigh was loaded from tune but unused — use it for idle/mid pending so
  // stop can drain light debt without waiting for holes.
  int inflight_mult = 2;
  if (enter_fov_lit)
  {
    inflight_mult = std::max(3, tune.EnterFovLitInflightMult);
  }
  else if (focus_pending_high || visual_holes)
  {
    inflight_mult = std::max(3, tune.RelightInflightMultHoles);
  }
  else if (idle_recovery || focus_pending_mid)
  {
    inflight_mult = std::max(2, tune.RelightInflightMultHigh);
  }
  const int max_inflight =
      async_bg ? std::clamp(world.ProceduralTemplate.RelightThreadCount, 1, 8) *
                     inflight_mult
               : 0;
  capture_telem.RelightCaptureInFlightN =
      world.GetAsyncRelightInFlightCount();
  capture_telem.RelightCaptureInFlightLimit = max_inflight;

  // Continuously re-order priority FIFO by effective distance + forward bias.
  if (PendingTerrainColumnRelightsPriority.size() > 1)
  {
    const glm::vec2 fwd = world.GetLastMovementDirXz();
    const float bias_k = tune.MeshForwardBiasK;
    auto effective = [&](glm::ivec2 col) -> float
    {
      const int cx = FloorDiv(col.x, CHUNK_SIZE);
      const int cz = FloorDiv(col.y, CHUNK_SIZE);
      float d = static_cast<float>(
          std::max(std::abs(cx - focus_horiz.x), std::abs(cz - focus_horiz.z)));
      if (bias_k <= 0.0f)
      {
        return d;
      }
      const float flen = glm::length(fwd);
      if (flen < 0.01f)
      {
        return d;
      }
      const float dx = static_cast<float>(cx - focus_horiz.x);
      const float dz = static_cast<float>(cz - focus_horiz.z);
      const float clen = std::sqrt(dx * dx + dz * dz);
      if (clen < 0.01f)
      {
        return d;
      }
      const float bias =
          std::max(0.0f, (dx / clen) * (fwd.x / flen) +
                             (dz / clen) * (fwd.y / flen));
      return d - bias_k * bias;
    };
    std::stable_sort(PendingTerrainColumnRelightsPriority.begin(),
                     PendingTerrainColumnRelightsPriority.end(),
                     [&](const glm::ivec2 &a, const glm::ivec2 &b)
                     { return effective(a) < effective(b); });
  }

  // SoftDefer hole: pin nearest missing column to front so the hot-frame
  // Capture bypass clears the lit gate for the visible hole first. If the hole
  // is PendingLight but missing from FIFO (Keys ghost / far-only), enqueue it.
  // P1: nh≤2 pending witness hold — do not hop to a new nearest miss.
  glm::ivec3 soft_defer_hole{};
  bool soft_defer_hole_valid = false;
  const auto &phys_pin = world.GetPhysicsTelemetry();
  RelightEnqueueMissHoriz_ = phys_pin.MissHoriz;
  RelightEnqueueWitnessHoldN_ = phys_pin.RelightWitnessHoldN;
  const glm::ivec2 miss_xz(phys_pin.MissCx, phys_pin.MissCz);
  const bool hold_nh2 =
      visual_holes && phys_pin.FocusMissingMesh > 0 &&
      ShouldHoldPinnedRelightWitness(
          phys_pin.MissHoriz, world.IsPendingLightBeforeMesh(miss_xz),
          phys_pin.FocusMissingMesh > 0);
  if (hold_nh2)
  {
    soft_defer_hole = glm::ivec3(miss_xz.x, 0, miss_xz.y);
    soft_defer_hole_valid = true;
  }
  else if (visual_holes && world.MeshService)
  {
    soft_defer_hole_valid = world.MeshService->FindNearestMissingGreedyMesh(
        world.GetBlockWorld(), focus_horiz, focus_radius, soft_defer_hole);
  }
  if (soft_defer_hole_valid)
  {
    const glm::ivec2 hole_key(soft_defer_hole.x * CHUNK_SIZE,
                              soft_defer_hole.z * CHUNK_SIZE);
    const glm::ivec2 hole_xz(soft_defer_hole.x, soft_defer_hole.z);
    auto &prio = PendingTerrainColumnRelightsPriority;
    auto &far = PendingTerrainColumnRelights;
    const auto prio_it = std::find(prio.begin(), prio.end(), hole_key);
    if (prio_it != prio.end())
    {
      if (prio_it != prio.begin())
      {
        prio.erase(prio_it);
        prio.push_front(hole_key);
      }
    }
    else
    {
      const auto far_it = std::find(far.begin(), far.end(), hole_key);
      if (far_it != far.end())
      {
        far.erase(far_it);
        prio.push_front(hole_key);
      }
      else
      {
        // Era40: force Enqueue for miss/SoftDefer hole even if not yet in FIFO
        // (PendingLight, or SoftDefer-empty HasGreedy∧!Drawable).
        bool undrawn = false;
        if (world.MeshService)
        {
          const int max_cy_hole = std::max(0, FloorDiv(max_y, CHUNK_SIZE));
          for (int cy = 0; cy <= max_cy_hole; ++cy)
          {
            const glm::ivec3 coord(soft_defer_hole.x, cy, soft_defer_hole.z);
            if (world.MeshService->HasGreedyMesh(coord) &&
                !world.MeshService->HasDrawableGreedyMesh(coord))
            {
              undrawn = true;
              break;
            }
          }
        }
        const bool pending_or_void =
            world.IsPendingLightBeforeMesh(hole_xz) || undrawn;
        if (ShouldForceMissColumnFifoEnqueue(/*miss=*/true, pending_or_void,
                                             /*already_in_fifo=*/false,
                                             phys_pin.RelightWitnessHoldN / 60))
        {
          EnqueueTerrainColumnRelight(hole_key.x, hole_key.y, /*priority=*/true,
                                      0, max_y);
          const auto again = std::find(prio.begin(), prio.end(), hole_key);
          if (again != prio.end() && again != prio.begin())
          {
            prio.erase(again);
            prio.push_front(hole_key);
          }
        }
      }
    }
  }

  // Era18: while VisibleBlack, pin nearest focus PendingLight column to FIFO
  // front so far orphans do not starve Capture (manual 165953 fifo frozen,
  // pending_light outside focus).
  if (world.GetPhysicsTelemetry().VisibleBlackFocusN > 0)
  {
    std::vector<glm::ivec2> pending_cols;
    world.CollectPendingLightFocusColumns(focus_horiz, focus_radius,
                                          pending_cols, /*max_cols=*/4);
    if (!pending_cols.empty())
    {
      const glm::ivec2 nearest = pending_cols.front();
      const glm::ivec2 nearest_key(nearest.x * CHUNK_SIZE,
                                   nearest.y * CHUNK_SIZE);
      auto &prio = PendingTerrainColumnRelightsPriority;
      auto &far = PendingTerrainColumnRelights;
      const auto prio_it = std::find(prio.begin(), prio.end(), nearest_key);
      if (prio_it != prio.end())
      {
        if (prio_it != prio.begin())
        {
          prio.erase(prio_it);
          prio.push_front(nearest_key);
        }
      }
      else
      {
        const auto far_it = std::find(far.begin(), far.end(), nearest_key);
        if (far_it != far.end())
        {
          far.erase(far_it);
          prio.push_front(nearest_key);
        }
        else
        {
          EnqueueTerrainColumnRelight(nearest_key.x, nearest_key.y,
                                      /*priority=*/true, 0, max_y);
          const auto again = std::find(prio.begin(), prio.end(), nearest_key);
          if (again != prio.end() && again != prio.begin())
          {
            prio.erase(again);
            prio.push_front(nearest_key);
          }
        }
      }
    }
  }

  // Era38 A3 / Era40: pin SoftDefer-empty / miss rim (horiz<=LitDrawable ring)
  // PendingLight columns so Capture clears lit gate before hinterland trees.
  if (world.MeshService)
  {
    auto &prio = PendingTerrainColumnRelightsPriority;
    auto &far = PendingTerrainColumnRelights;
    auto pin_key = [&](glm::ivec2 nearest_key)
    {
      const auto prio_it = std::find(prio.begin(), prio.end(), nearest_key);
      if (prio_it != prio.end())
      {
        if (prio_it != prio.begin())
        {
          prio.erase(prio_it);
          prio.push_front(nearest_key);
        }
        return;
      }
      const auto far_it = std::find(far.begin(), far.end(), nearest_key);
      if (far_it != far.end())
      {
        far.erase(far_it);
        prio.push_front(nearest_key);
        return;
      }
      EnqueueTerrainColumnRelight(nearest_key.x, nearest_key.y,
                                  /*priority=*/true, 0, max_y);
      const auto again = std::find(prio.begin(), prio.end(), nearest_key);
      if (again != prio.end() && again != prio.begin())
      {
        prio.erase(again);
        prio.push_front(nearest_key);
      }
    };
    const int pin_max_horiz = RelightMissPinMaxHoriz();
    const int max_cy_pin =
        std::max(0, FloorDiv(max_y, CHUNK_SIZE));
    int pinned = 0;
    std::vector<std::pair<int, glm::ivec2>> near_empty;
    near_empty.reserve(static_cast<size_t>((pin_max_horiz * 2 + 1) *
                                           (pin_max_horiz * 2 + 1)));
    for (int dx = -pin_max_horiz; dx <= pin_max_horiz; ++dx)
    {
      for (int dz = -pin_max_horiz; dz <= pin_max_horiz; ++dz)
      {
        const int horiz = std::max(std::abs(dx), std::abs(dz));
        if (horiz > pin_max_horiz)
        {
          continue;
        }
        const glm::ivec2 col(focus_horiz.x + dx, focus_horiz.z + dz);
        if (!world.IsPendingLightBeforeMesh(col))
        {
          continue;
        }
        bool softdefer_empty = false;
        for (int cy = 0; cy <= max_cy_pin; ++cy)
        {
          const glm::ivec3 coord(col.x, cy, col.y);
          if (world.MeshService->HasGreedyMesh(coord) &&
              !world.MeshService->HasDrawableGreedyMesh(coord))
          {
            softdefer_empty = true;
            break;
          }
        }
        if (!softdefer_empty)
        {
          continue;
        }
        near_empty.emplace_back(horiz, col);
      }
    }
    std::stable_sort(near_empty.begin(), near_empty.end(),
                     [](const auto &a, const auto &b)
                     { return a.first > b.first; });
    // Pin farthest first so nearest ends at front after push_front.
    if (!hold_nh2)
    {
    for (const auto &entry : near_empty)
    {
      if (pinned >= 4)
      {
        break;
      }
      const glm::ivec2 key(entry.second.x * CHUNK_SIZE,
                           entry.second.y * CHUNK_SIZE);
      pin_key(key);
      ++pinned;
    }
    }
    // Era40 / P1: pin FOV miss witness. Hold nh≤2 pending until MarkRelit.
    const auto &phys = world.GetPhysicsTelemetry();
    if (phys.FocusMissingMesh > 0 &&
        (hold_nh2 || ShouldPreferMissFinalizeBand(phys.MissHoriz) ||
         phys.MissHoriz <= RelightMissPinMaxHoriz()))
    {
      const glm::ivec2 miss_col(phys.MissCx, phys.MissCz);
      bool undrawn = false;
      for (int cy = 0; cy <= max_cy_pin; ++cy)
      {
        const glm::ivec3 coord(miss_col.x, cy, miss_col.y);
        if (world.MeshService->HasGreedyMesh(coord) &&
            !world.MeshService->HasDrawableGreedyMesh(coord))
        {
          undrawn = true;
          break;
        }
      }
      const bool pending_or_void =
          world.IsPendingLightBeforeMesh(miss_col) || undrawn;
      const glm::ivec2 miss_key(miss_col.x * CHUNK_SIZE,
                                miss_col.y * CHUNK_SIZE);
      const bool already_in_fifo =
          PendingTerrainColumnRelightKeys.count(miss_key) != 0;
      if (pending_or_void &&
          (already_in_fifo ||
           ShouldForceMissColumnFifoEnqueue(/*miss=*/true, pending_or_void,
                                            already_in_fifo,
                                            phys.RelightWitnessHoldN / 60)))
      {
        pin_key(miss_key);
      }
    }
  }

  int drained_bg = 0;
  int skipped_inflight = 0;
  const bool moving =
      world.GetLastMovementSpeed() >
      world.ProceduralTemplate.MovementPrefetchThreshold;
  // FP-E2.3: post-stop VB stuck — escalate priority insert after 20s no progress.
  if (!moving && (vb_no_ticket_n > 0 || vb_focus_n >= 20))
  {
    static int stop_vb_stuck_frames = 0;
    static int last_stop_vb_no_ticket = -1;
    static int stop_vb_focus_plateau_frames = 0;
    static int last_stop_vb_focus = -1;
    if (last_stop_vb_no_ticket >= 0 && vb_no_ticket_n >= last_stop_vb_no_ticket)
    {
      ++stop_vb_stuck_frames;
    }
    else
    {
      stop_vb_stuck_frames = 0;
    }
    last_stop_vb_no_ticket = vb_no_ticket_n;
    if (last_stop_vb_focus >= 0 &&
        vb_focus_n >= std::max(20, last_stop_vb_focus - 2))
    {
      ++stop_vb_focus_plateau_frames;
    }
    else
    {
      stop_vb_focus_plateau_frames = 0;
    }
    last_stop_vb_focus = vb_focus_n;
    world.GetPhysicsTelemetryMutable().StopVbStuckFrames =
        std::max(stop_vb_stuck_frames, stop_vb_focus_plateau_frames);
    if (stop_vb_stuck_frames > 1200 || stop_vb_focus_plateau_frames > 600)
    {
      std::vector<glm::ivec2> stuck_cols;
      world.CollectPendingLightFocusColumns(focus_horiz, focus_radius, stuck_cols,
                                            /*max_cols=*/6);
      for (const glm::ivec2 &col : stuck_cols)
      {
        const glm::ivec2 col_key(col.x * CHUNK_SIZE, col.y * CHUNK_SIZE);
        if (!PendingTerrainColumnRelightKeys.count(col_key))
        {
          EnqueueTerrainColumnRelight(col_key.x, col_key.y, /*priority=*/true, 0,
                                      max_y);
        }
        else
        {
          PromoteTerrainColumnRelight(col_key);
        }
        ++RelightFifoPriorityInsertN;
      }
    }
  }

  bool draw_gate_target_pinned = false;
  std::vector<glm::ivec2> draw_gate_target_keys;
  // RecentRendererDrawGateRejections is the direct view-level observation.
  // Do not require the separately sampled VisibleBlack census as a second
  // gate: its update cadence can leave zero here while real draw candidates
  // were rejected in the previous render frame.
  if (async_bg && world.MeshService)
  {
    std::vector<DrawGateRelightTarget> draw_gate_targets;
    const int draw_gate_radius = std::min(focus_radius,
                                          RelightMissPinMaxHoriz());
    world.CollectDrawGateRelightTargets(focus_chunk, draw_gate_radius,
                                        draw_gate_targets,
                                        kVisibleDrawGateRelightTargetLimit);
    std::vector<glm::ivec2> protected_visible_columns;
    protected_visible_columns.reserve(draw_gate_targets.size());
    for (const DrawGateRelightTarget &target : draw_gate_targets)
    {
      if (!target.settled_mesh_repair)
      {
        protected_visible_columns.emplace_back(target.column.x,
                                               target.column.y);
      }
    }
    for (auto target_it = draw_gate_targets.rbegin();
         target_it != draw_gate_targets.rend(); ++target_it)
    {
      const DrawGateRelightTarget &target = *target_it;
      const glm::ivec2 draw_gate_target_key{
          target.column.x * CHUNK_SIZE, target.column.y * CHUNK_SIZE};
      // Avoid duplicate capture debt while the column is already being
      // repaired. A queued entry still receives the witness band so it can
      // survive the top-down remainder.
      const bool already_queued =
          IsTerrainColumnRelightQueued(draw_gate_target_key);
      const glm::ivec2 target_column(target.column.x, target.column.y);
      const bool already_inflight =
          world.IsAsyncRelightColumnInFlight(target_column);
      uint8_t draw_gate_admission_outcome = 0;
      int draw_gate_victim_horiz = -1;
      bool target_pinned = false;
      if (target.settled_mesh_repair)
      {
        bool repair_queued = false;
        target_pinned = world.QueueSettledDrawGateMeshRepair(
            target.rejected_slice, &repair_queued);
        // 11=queued an exact mesh-only repair, 12=not admitted, 13=kept an
        // existing repair ticket pinned while its mesh owner/backoff settles.
        draw_gate_admission_outcome =
            !target_pinned ? 12 : (repair_queued ? 11 : 13);
      }
      else if (already_queued || !already_inflight)
      {
        target_pinned = EnqueueVisibleDrawGateRelight(
            draw_gate_target_key.x, draw_gate_target_key.y,
            target.min_world_y, target.max_world_y, focus_chunk,
            draw_gate_radius, protected_visible_columns,
            &draw_gate_admission_outcome, &draw_gate_victim_horiz);
      }
      else
      {
        draw_gate_admission_outcome = 8;
      }
      if (target_pinned)
      {
        draw_gate_target_pinned = true;
        if (std::find(draw_gate_target_keys.begin(),
                      draw_gate_target_keys.end(), draw_gate_target_key) ==
            draw_gate_target_keys.end())
        {
          draw_gate_target_keys.push_back(draw_gate_target_key);
        }
        auto [band_it, inserted] = PendingVisibleDrawGateRelightYBands.try_emplace(
            draw_gate_target_key,
            glm::ivec2(target.min_world_y, target.max_world_y));
        if (!inserted)
        {
          band_it->second.x = std::min(band_it->second.x, target.min_world_y);
          band_it->second.y = std::max(band_it->second.y, target.max_world_y);
        }
      }
      if (UJobStageTrace::VisualBlackTraceEnabled())
      {
        VisualBlackTraceRecord trace{};
        trace.sample_kind = 3;
        trace.cx = target.rejected_slice.x;
        trace.cy = target.rejected_slice.y;
        trace.cz = target.rejected_slice.z;
        trace.focus_cx = focus_chunk.x;
        trace.focus_cz = focus_chunk.z;
        trace.frame_epoch = world.GetStreamingFrameEpoch();
        trace.camera_x = focus_block.x;
        trace.camera_y = focus_block.y;
        trace.camera_z = focus_block.z;
        trace.draw_gate_ready = 0;
        trace.draw_gate_repair_mode =
            target.settled_mesh_repair ? 1u : 0u;
        trace.flags = target_pinned ? 4u
                      : (already_inflight && !already_queued ? 1u : 2u);
        trace.cause = draw_gate_admission_outcome;
        trace.face_debt_mask = static_cast<uint8_t>(std::clamp(
            draw_gate_victim_horiz, 0, static_cast<int>(UINT8_MAX)));
        trace.relight_y_band_defined = 1;
        trace.relight_band_min_y = target.min_world_y;
        trace.relight_band_max_y = target.max_world_y;
        if (const ChunkRenderDemandRecord *demand =
                UChunkRenderDemandStore::Get().Find(target.rejected_slice))
        {
          trace.world_epoch = demand->world_epoch;
          trace.demand_incarnation = demand->incarnation;
          trace.attempt_id = demand->has_active_attempt
                                 ? demand->active_attempt_id
                                 : 0;
          trace.desired_geom_rev = demand->desired_geom_rev;
          trace.desired_light_rev = demand->desired_light_rev;
          trace.demand_published_geom_rev = demand->published_geom_rev;
          trace.demand_published_light_rev = demand->published_light_rev;
          trace.settled_light_rev = demand->settled_light_rev;
          trace.has_settled_light = demand->has_settled_light ? 1u : 0u;
          trace.active_stage = static_cast<uint8_t>(demand->active_stage);
        }
        if (const UChunk *chunk = world.GetBlockWorld()
                                      .GetChunkManager()
                                      .GetChunk(target.rejected_slice))
        {
          trace.non_air_blocks = chunk->GetNonAirCount();
          trace.chunk_content_revision = chunk->GetContentRevision();
          trace.incarnation = chunk->GetIncarnation();
          trace.field_light_rev = chunk->GetLightFieldRevision();
        }
        if (world.MeshService)
        {
          const auto &cache = world.MeshService->GetCache();
          trace.mesh_work_owner_flags =
              (cache.IsChunkMeshDirty(target.rejected_slice) ? 1u << 0 : 0u) |
              (world.MeshService->HasInflightMeshBuild(target.rejected_slice)
                   ? 1u << 1
                   : 0u) |
              (cache.IsRemeshAfterApplyPending(target.rejected_slice)
                   ? 1u << 2
                   : 0u) |
              (cache.IsPendingGpuApply(target.rejected_slice) ? 1u << 3
                                                               : 0u) |
              (cache.IsPendingGpuQueued(target.rejected_slice) ? 1u << 4
                                                                : 0u) |
              (cache.IsPendingGpuKickedOrDispatched(target.rejected_slice)
                   ? 1u << 5
                   : 0u) |
              (cache.IsGpuExtractInFlight(target.rejected_slice) ? 1u << 6
                                                                  : 0u) |
              (cache.HasPendingCaptureWork(target.rejected_slice) ? 1u << 7
                                                                   : 0u);
          trace.mesh_dirty_queue_kind = cache.GetDirtyQueueTrace(
              target.rejected_slice, trace.mesh_dirty_queue_index,
              trace.mesh_dirty_queue_size);
          trace.mesh_revision =
              cache.GetChunkMeshRevision(target.rejected_slice);
          const MeshPublishRevs published =
              cache.GetMeshPublishRevs(target.rejected_slice);
          trace.published_geom_rev = published.geom_rev;
          trace.published_light_rev = published.light_rev;
          trace.meshed_light_rev =
              cache.GetMeshedLightRevision(target.rejected_slice);
        }
        const TerrainColumnRelightQueueInfo queue_info =
            GetTerrainColumnRelightQueueInfo(draw_gate_target_key);
        trace.relight_queue_kind =
            queue_info.deferred_visible && !queue_info.keyed
                ? 6
                : (queue_info.deferred_far && !queue_info.keyed
                       ? 7
                       : (!queue_info.keyed
                              ? 0
                              : (!queue_info.in_deque
                                     ? 3
                                     : (queue_info.priority ? 1 : 2))));
        trace.relight_queue_index = queue_info.queue_index;
        trace.relight_queue_size = queue_info.queue_size;
        UJobStageTrace::NoteVisualBlack(trace);
      }
    }
  }

  // Capture() is main-thread and copies a 3x3 column band. Idle used to allow
  // 48–56 Captures/frame with no wall budget → 15–52s spikes and multi-GB
  // snapshot high-water (manual 220018). Always bound Capture wall time.
  // Manual 102936: full-column Capture still ~1.6s — split into top-down Y
  // bands (RelightCaptureBandCy). SoftDefer keeps PendingLight until the
  // final band (finalize_pending_gate=false on partial).
  // Phase B: budgets from RuntimeTuning / streaming_tune.json.
  // Era41b: enter FOV lit pass uses elevated Capture wall to feed workers.
  double capture_drain_budget_ms =
      enter_fov_lit
          ? static_cast<double>(tune.EnterFovLitCaptureDrainMs)
          : (moving ? static_cast<double>(tune.CaptureDrainMovingMs)
                    : static_cast<double>(tune.CaptureDrainIdleMs));
  // Narrow PendingLight bands are cheaper now; when focus still has missing
  // mesh plus light debt, allow a bit more Capture time so relight can clear
  // the gate instead of holding mesh_async at 0 for many seconds.
  if (!enter_fov_lit && async_bg && visual_holes && focus_pending_mid)
  {
    capture_drain_budget_ms =
        moving ? static_cast<double>(tune.CaptureDrainHolesMovingMs)
               : static_cast<double>(tune.CaptureDrainHolesIdleMs);
    if (focus_pending_high)
    {
      capture_drain_budget_ms =
          moving ? static_cast<double>(tune.CaptureDrainHighPendingMovingMs)
                 : static_cast<double>(tune.CaptureDrainHighPendingIdleMs);
    }
  }
  const double capture_hot_mult =
      std::max(1.0, static_cast<double>(tune.CaptureHotFrameMult));
  const double capture_hot_skip_ms = capture_drain_budget_ms * capture_hot_mult;
  const double frame_ms_so_far = world.GetWallFrameDelta() * 1000.0;
  // Hot SoftDefer bypass: at most one Capture so cruise hitch stays bounded.
  // Async SoftDefer hole may enqueue even when wall is high (see drain_one).
  int bg_cap = max_bg_columns;
  if (enter_fov_lit && vb_no_ticket_n >= 10)
  {
    bg_cap = std::max(bg_cap, 3);
  }
  else if (enter_fov_lit && vb_no_ticket_n > 0)
  {
    bg_cap = std::max(bg_cap, 2);
  }
  // S2 step A: cruise ≤CaptureMovingBgCap (worker Capture hung — TD-ARCH-015).
  // Era36 B2: dynamic cap based on pending light pressure.
  // Era41b: enter FOV lit keeps caller Capture budget (feed async workers).
  // Cruise SOTA: moving+holes never raise bg above dynamic cap; narrow band_cy.
  int band_cy = std::max(0, tune.RelightCaptureBandCy);
  if (moving && !enter_fov_lit)
  {
    const auto &telem = world.GetPhysicsTelemetry();
    int dynamic_cap = tune.CaptureMovingBgCap;
    // P5 / ColdSupply S1: raise above base 1 when Apply unit cheap; high PL
    // ignores one-frame fifo_drop if pin stable. Do not lift RuntimeTuning base.
    if (ShouldAllowDynamicCaptureMovingBgCap(
            telem.RelightFifoDropNPrev, telem.RelightFifoPinDropNPrev,
            telem.RelightApplyMsPrev, telem.RelightApplyNPrev,
            pending_light_focus_n))
    {
      dynamic_cap =
          DynamicCaptureMovingBgCap(pending_light_focus_n, tune.CaptureMovingBgCap);
    }
    bg_cap = ClampCaptureMovingBgCapWithHoles(bg_cap, moving, visual_holes,
                                              dynamic_cap);
  }
  // ColdFix P1 / RateMatch R1: admit depth = Apply capacity (apply_n_prev+1),
  // not raw worker max_inflight — SoftDefer/miss keep floor of 1.
  if (!enter_fov_lit)
  {
    const auto &telem = world.GetPhysicsTelemetry();
    const int completed_n =
        static_cast<int>(world.GetRelightCompletedSize());
    const int inflight_n = world.GetAsyncRelightInFlightCount();
    const bool consume_mode = IsTicketedVbConsumeMode(
        vb_no_ticket_n, telem.VisibleBlackFocusN,
        telem.VisibleBlackStalledN, moving);
    const double unit_ms_prev =
        telem.RelightApplyNPrev > 0
            ? (telem.RelightApplyMsPrev /
               static_cast<double>(telem.RelightApplyNPrev))
            : telem.RelightApplyMsPrev;
    const double light_unit =
        telem.RelightApplyNPrev > 0
            ? telem.RelightApplyLightMsPrev /
                  static_cast<double>(telem.RelightApplyNPrev)
            : 0.0;
    const double install_unit =
        telem.RelightApplyNPrev > 0
            ? telem.RelightApplyInstallMsPrev /
                  static_cast<double>(telem.RelightApplyNPrev)
            : 0.0;
    const int depth_cap = RelightCapturePipelineDepthCap(
        telem.RelightApplyNPrev, max_inflight,
        static_cast<double>(tune.MissReservedMs), unit_ms_prev, light_unit,
        install_unit, completed_n, telem.RelightFifoN, tune.RelightFifoSoftCap,
        telem.PendingLightN, consume_mode);
    // I15-A2: stop consume_mode keeps Capture refill floor.
    if (consume_mode && !moving)
    {
      bg_cap = std::max(bg_cap, 3);
    }
    // G1: moving cruise with FullyDarkPendingRepair debt — raise Capture so
    // Apply has completed work (stall→repair left consume on but bg stayed 1).
    if (consume_mode && moving &&
        telem.VisibleBlackFullyDarkRepairN >= 20)
    {
      bg_cap = std::max(bg_cap, 2);
    }
    const int fifo_soft_cap = tune.RelightFifoSoftCap;
    const bool fifo_starve =
        telem.RelightFifoN >= 50 ||
        (fifo_soft_cap > 0 && telem.RelightFifoN >= fifo_soft_cap / 2);
    if (!ShouldAdmitRelightCapture(completed_n, inflight_n, depth_cap))
    {
      const bool soft_defer_or_miss =
          (visual_holes && focus_pending_mid) ||
          (visual_holes &&
           ShouldPreferMissFinalizeBand(
               world.GetPhysicsTelemetry().MissHoriz));
      bg_cap = SoftDeferCaptureFloorWhenDepthFull(
          soft_defer_or_miss, 0, completed_n, fifo_starve, telem.RelightFifoN,
          telem.RelightApplyNPrev);
    }
    if (fifo_soft_cap > 0 &&
        telem.RelightFifoN >= (fifo_soft_cap * 2) / 3 &&
        completed_n < 2)
    {
      const double light_unit =
          telem.RelightApplyNPrev > 0
              ? telem.RelightApplyLightMsPrev /
                    static_cast<double>(telem.RelightApplyNPrev)
              : 0.0;
      if (ShouldSuppressProducerBoostWhenConsumerBoundP9(
              telem.RelightApplyNPrev, telem.RelightFifoN, fifo_soft_cap,
              static_cast<ApplyBinding>(telem.ApplyBindingPrev), light_unit,
              static_cast<double>(tune.MissReservedMs), completed_n))
      {
        bg_cap = std::min(bg_cap, 1);
      }
    }
    bg_cap = RelightCaptureBgFloorForFifoStarve(
        bg_cap, telem.RelightFifoN, fifo_soft_cap, completed_n, inflight_n,
        RelightApplyCapUnitMs(unit_ms_prev, light_unit, install_unit));
    // P10: sim kill clamps boosts only — keep Completed-empty refill ≤3.
    bg_cap = ClampCaptureBgAfterSimKill(
        bg_cap, ShouldKillProducerBoostOnSimHot(telem.SimMsPrev), completed_n,
        telem.RelightFifoN);
  }
  if (!enter_fov_lit && frame_ms_so_far >= capture_hot_skip_ms &&
      visual_holes && focus_pending_mid)
  {
    const auto &telem_hot = world.GetPhysicsTelemetry();
    const int fifo_soft = tune.RelightFifoSoftCap;
    const int completed_hot =
        static_cast<int>(world.GetRelightCompletedSize());
    if (!ShouldBypassCaptureHotSoftDeferClamp(telem_hot.RelightFifoN, fifo_soft,
                                              completed_hot))
    {
      bg_cap = std::min(bg_cap, 1);
    }
  }
  if (enter_fov_lit && vb_no_ticket_n >= 10)
  {
    bg_cap = std::max(bg_cap, 2);
  }
  // FZ2.4-P0b: plateau after nt=0 — raise Capture admit to feed Apply.
  if (!enter_fov_lit &&
      ShouldSuppressPendingLightNote(vb_no_ticket_n, pending_light_focus_n,
                                     world.GetPhysicsTelemetry().VisibleBlackFocusN))
  {
    const auto &telem_pl = world.GetPhysicsTelemetry();
    const int fifo_soft = tune.RelightFifoSoftCap;
    if (!ShouldSuppressProducerBoostWhenConsumerBoundP9(
            telem_pl.RelightApplyNPrev, telem_pl.RelightFifoN, fifo_soft,
            static_cast<ApplyBinding>(telem_pl.ApplyBindingPrev),
            telem_pl.RelightApplyNPrev > 0
                ? telem_pl.RelightApplyLightMsPrev /
                      static_cast<double>(telem_pl.RelightApplyNPrev)
                : 0.0,
            static_cast<double>(tune.MissReservedMs),
            static_cast<int>(world.GetRelightCompletedSize())) &&
        !ShouldKillProducerBoostOnSimHot(telem_pl.SimMsPrev))
    {
      bg_cap = std::max(bg_cap, std::max(2, EnterFovRelightCaptureBudget() / 2));
    }
  }
  // P11: final floor after all late clamps (hot SoftDefer / plateau).
  if (!enter_fov_lit)
  {
    const auto &telem_final = world.GetPhysicsTelemetry();
    const int completed_final =
        static_cast<int>(world.GetRelightCompletedSize());
    const int fifo_soft_final = tune.RelightFifoSoftCap;
    const double unit_ms_final =
        telem_final.RelightApplyNPrev > 0
            ? (telem_final.RelightApplyMsPrev /
               static_cast<double>(telem_final.RelightApplyNPrev))
            : telem_final.RelightApplyMsPrev;
    const double light_unit_final =
        telem_final.RelightApplyNPrev > 0
            ? telem_final.RelightApplyLightMsPrev /
                  static_cast<double>(telem_final.RelightApplyNPrev)
            : 0.0;
    const double install_unit_final =
        telem_final.RelightApplyNPrev > 0
            ? telem_final.RelightApplyInstallMsPrev /
                  static_cast<double>(telem_final.RelightApplyNPrev)
            : 0.0;
    bg_cap = RelightCaptureBgFloorForFifoStarve(
        bg_cap, telem_final.RelightFifoN, fifo_soft_final, completed_final,
        world.GetAsyncRelightInFlightCount(),
        RelightApplyCapUnitMs(unit_ms_final, light_unit_final,
                              install_unit_final));
    bg_cap = ClampCaptureBgAfterSimKill(
        bg_cap, ShouldKillProducerBoostOnSimHot(telem_final.SimMsPrev),
        completed_final, telem_final.RelightFifoN);
  }
  band_cy = EffectiveRelightCaptureBandCy(band_cy, moving && !enter_fov_lit,
                                          visual_holes);
  // G1: sticky hitch + FullyDark repair — clamp Capture flood (193536 wall~113
  // @cap=8). 195525: wall>50@cap≤3 cut Apply too hard (miss_stuck↑); only clamp
  // past ~80ms when thrash is clear.
  {
    const auto &telem_hitch = world.GetPhysicsTelemetry();
    if (!enter_fov_lit && frame_ms_so_far > 80.0 &&
        telem_hitch.VisibleBlackFullyDarkRepairN >= 20)
    {
      bg_cap = std::min(bg_cap, 3);
    }
  }
  // Visible FirstMesh slices can be held behind PendingLight debt even after
  // they have an executable FIFO owner. Streaming may pass a one-column
  // budget here, which leaves the relight worker underfed despite an active
  // near-FOV repair target. Keep this near-FOV boost limited to a short
  // in-flight queue: m13 showed that filling the full worker allowance moved
  // the bottleneck into first-mesh scheduling. Exact draw-gate work retains
  // its existing exception. Time budget, completed-queue backpressure and
  // worker limit still bound the capture loop.
  // M120 observed stale vertex-light debt with a one-column capture cap while
  // async inflight stayed near two of forty slots, the completion queue was
  // empty, and memory pressure was absent. Let that measured debt use the same
  // small refill headroom as a near-FOV pending-light target.
  const bool stale_vertex_light_capture_headroom =
      world.GetPhysicsTelemetry().DrawOracleStaleVertexLightN > 0 &&
      world.GetAsyncRelightInFlightCount() < std::min(max_inflight, 2);
  const bool near_fov_capture_headroom =
      near_fov_pending_light_n > 0 &&
      world.GetAsyncRelightInFlightCount() < std::min(max_inflight, 2);
  if (async_bg &&
      (draw_gate_target_pinned || near_fov_capture_headroom ||
       stale_vertex_light_capture_headroom) &&
      world.GetPhysicsTelemetry().MemoryPressure == 0 &&
      world.GetAsyncRelightInFlightCount() < max_inflight &&
      world.GetRelightCompletedSize() < 2)
  {
    bg_cap = std::max(bg_cap, 2);
  }
  {
    auto &telem = world.GetPhysicsTelemetryMutable();
    telem.CaptureBgCapN = bg_cap;
    telem.CaptureBandCy = band_cy;
  }
  const auto drain_loop_t0 = std::chrono::high_resolution_clock::now();
  auto drain_one = [&]()
  {
    if (drained_bg >= bg_cap)
    {
      capture_telem.RelightCaptureStopReason = 3;
      return false;
    }
    const double elapsed_ms =
        std::chrono::duration<double, std::milli>(
            std::chrono::high_resolution_clock::now() - drain_loop_t0)
            .count();
    if (elapsed_ms >= capture_drain_budget_ms)
    {
      capture_telem.RelightCaptureStopReason = 4;
      return false;
    }
    // Frame already far over Capture budget (sticky hitch) — skip this frame.
    // SoftDefer hole exception: cruise wall often 40–200ms from stream, so the
    // hot-frame skip starved Capture (fifo~96, rd≈0, miss=1 16s+).
    // SoftDefer hole: async enqueue is cheap — always allow one even on hot
    // frames (startup wall 280–450 blocked @sync_skip and plated cold=6). Sync
    // Capture still skips when wall ≥ CaptureSyncSkipWallMs.
    if (drained_bg == 0 && frame_ms_so_far >= capture_hot_skip_ms)
    {
      const bool soft_defer_hole = visual_holes && focus_pending_mid;
      // Era40: miss rim (horiz<=4) also bypasses hot-frame Capture skip.
      const bool miss_rim_pin =
          visual_holes &&
          ShouldPreferMissFinalizeBand(
              world.GetPhysicsTelemetry().MissHoriz,
              moving ? kVisualStageNearFovHoriz : kVisualStageLitDrawableHoriz);
      // Idle PendingLight progress (TD-ARCH-010): when holes=0 the SoftDefer
      // exception never fired and FIFO stalled. Allow one Capture/enqueue if
      // inflight is empty and wall is not catastrophic.
      const bool idle_pending_progress =
          !moving && focus_pending_mid &&
          world.GetAsyncRelightInFlightCount() == 0 &&
          frame_ms_so_far <
              static_cast<double>(tune.CaptureIdlePendingMaxWallMs);
      const bool draw_gate_hot_bypass =
          async_bg && draw_gate_target_pinned &&
          std::any_of(draw_gate_target_keys.begin(),
                      draw_gate_target_keys.end(),
                      [&](glm::ivec2 target_key)
                      {
                        return PendingTerrainColumnRelightKeys.count(
                                   target_key) != 0 ||
                               DeferredVisibleDrawGateRelightYBands.count(
                                   target_key) != 0;
                      });
      if (!enter_fov_lit && !soft_defer_hole && !miss_rim_pin &&
          !idle_pending_progress && !draw_gate_hot_bypass)
      {
        capture_telem.RelightCaptureStopReason = 5;
        return false;
      }
      if (!async_bg &&
          frame_ms_so_far >= static_cast<double>(tune.CaptureSyncSkipWallMs) &&
          !idle_pending_progress)
      {
        capture_telem.RelightCaptureStopReason = 6;
        return false;
      }
    }
    const int inflight_now = world.GetAsyncRelightInFlightCount();
    capture_telem.RelightCaptureInFlightN = inflight_now;
    if (async_bg && inflight_now >= max_inflight)
    {
      capture_telem.RelightCaptureStopReason = 7;
      return false;
    }
    PrioritizeNearestTerrainColumnRelight(
        world, focus_chunk, RelightMissPinMaxHoriz(), /*scan_cap=*/64);
    glm::ivec2 col;
    if (!PendingTerrainColumnRelightsPriority.empty())
    {
      col = PendingTerrainColumnRelightsPriority.front();
      PendingTerrainColumnRelightsPriority.pop_front();
    }
    else if (!PendingTerrainColumnRelights.empty())
    {
      col = PendingTerrainColumnRelights.front();
      PendingTerrainColumnRelights.pop_front();
    }
    else
    {
      capture_telem.RelightCaptureStopReason = 8;
      return false;
    }
    int relight_min = 0;
    int relight_max = max_y;
    const auto band_it = PendingTerrainColumnRelightYBands.find(col);
    bool queued_band_defined = band_it != PendingTerrainColumnRelightYBands.end();
    glm::ivec2 queued_band(0, max_y);
    if (band_it != PendingTerrainColumnRelightYBands.end())
    {
      queued_band = band_it->second;
      relight_min = std::max(0, band_it->second.x);
      relight_max = std::min(max_y, band_it->second.y);
      PendingTerrainColumnRelightYBands.erase(band_it);
      if (relight_max < relight_min)
      {
        relight_min = 0;
        relight_max = max_y;
      }
    }
    const glm::ivec2 ground_xz(FloorDiv(col.x, CHUNK_SIZE),
                               FloorDiv(col.y, CHUNK_SIZE));
    bool exact_draw_gate_band = false;
    bool exact_first_mesh_band = false;
    int retained_band_min = -1;
    int retained_band_max = -1;
    if (async_bg)
    {
      const auto visible_band_it =
          PendingVisibleDrawGateRelightYBands.find(col);
      if (visible_band_it != PendingVisibleDrawGateRelightYBands.end())
      {
        const glm::ivec2 visible_band = visible_band_it->second;
        const int exact_min = std::max(0, visible_band.x);
        const int exact_max = std::min(max_y, visible_band.y);
        if (exact_max >= exact_min)
        {
          exact_draw_gate_band = true;
          if (queued_band_defined && queued_band != visible_band)
          {
            retained_band_min = std::max(0, queued_band.x);
            retained_band_max = std::min(max_y, queued_band.y);
          }
          else if (queued_band_defined &&
                   world.IsPendingLightBeforeMesh(ground_xz))
          {
            retained_band_min = std::max(0, queued_band.x);
            retained_band_max = std::min(max_y, queued_band.y);
          }
          else if (!queued_band_defined)
          {
            retained_band_min = 0;
            retained_band_max = max_y;
          }
          relight_min = exact_min;
          relight_max = exact_max;
        }
        else
        {
          PendingVisibleDrawGateRelightYBands.erase(visible_band_it);
          DeferredVisibleDrawGateRelightYBands.erase(col);
        }
      }
    }
    if (!exact_draw_gate_band)
    {
      const auto first_mesh_band_it =
          PendingVisibleFirstMeshRelightYBands.find(col);
      if (first_mesh_band_it != PendingVisibleFirstMeshRelightYBands.end())
      {
        const int exact_min = std::max(0, first_mesh_band_it->second.x);
        const int exact_max = std::min(max_y, first_mesh_band_it->second.y);
        if (exact_max >= exact_min)
        {
          exact_first_mesh_band = true;
          // Multiple visible slice demands can coalesce under one column key.
          // Preserve the full owned interval; the surface clamp would otherwise
          // silently replace a missing FirstMesh slice with the surface slice.
          relight_min = queued_band_defined
                            ? std::min(std::max(0, queued_band.x), exact_min)
                            : exact_min;
          relight_max = queued_band_defined
                            ? std::max(std::min(max_y, queued_band.y), exact_max)
                            : exact_max;
        }
        else
        {
          PendingVisibleFirstMeshRelightYBands.erase(first_mesh_band_it);
        }
      }
    }
    // Era36/37 B1: clamp Capture Y-band to visible surface — drop underground.
    const int requested_relight_min = relight_min;
    const int requested_relight_max = relight_max;
    bool surface_band_adjusted = false;
    const int col_top_y = ColumnTopBlockY(world, ground_xz, max_y);
    if (!exact_draw_gate_band && !exact_first_mesh_band)
    {
      const auto col_band = RelightSurfaceBandForColumn(
          focus_block.y, col_top_y, CHUNK_SIZE, max_y, relight_min, relight_max);
      surface_band_adjusted = col_band.first != relight_min ||
                              col_band.second != relight_max;
      relight_min = col_band.first;
      relight_max = col_band.second;
    }
    if (relight_max < relight_min)
    {
      auto &telem = world.GetPhysicsTelemetryMutable();
      if (ColumnSurfaceBandNeedsRelight(world, ground_xz, surface_band_min,
                                        surface_band_max))
      {
        relight_min = surface_band_min;
        relight_max = surface_band_max;
        surface_band_adjusted = true;
        ++telem.RelightSkippedUndergroundN;
        world.TryNotePendingLightBeforeMesh(glm::ivec3(ground_xz.x, 0, ground_xz.y),
                                         relight_min, relight_max, __FUNCTION__);
      }
      else
      {
        world.ClearPendingLightBeforeMesh(ground_xz);
        ++telem.RelightFalseClearN;
        PendingTerrainColumnRelightKeys.erase(col);
        PendingVisibleDrawGateRelightYBands.erase(col);
        PendingVisibleFirstMeshRelightYBands.erase(col);
        DeferredVisibleDrawGateRelightYBands.erase(col);
        return skipped_inflight < std::max(8, max_bg_columns * 4);
      }
    }
    const int horiz_dist =
        std::max(std::abs(ground_xz.x - focus_horiz.x),
                 std::abs(ground_xz.y - focus_horiz.z));
    // Top-down Y-band: Capture sky first; requeue remainder after enqueue.
    // SoftDefer keeps PendingLight until the final band — cold hole first-mesh
    // is unblocked via MeshLitGate hole-preview in TickMeshEmerge (not here).
    int remainder_min = -1;
    int remainder_max = -1;
    bool finalize_gate = true;
    if (exact_draw_gate_band)
    {
      // This capture settles only the rejected slice. Preserve the coalesced
      // column work and never clear its wider PendingLight obligation here.
      remainder_min = retained_band_min;
      remainder_max = retained_band_max;
      finalize_gate = false;
    }
    // P2: miss nh≤2 prefers one surface finalize Capture (no partial Y-band).
    // Rim nh=3–4 keeps split. Era41b: enter FOV lit always finalizes.
    const int vb_focus_n = world.GetPhysicsTelemetry().VisibleBlackFocusN;
    const auto &phys_miss = world.GetPhysicsTelemetry();
    static int telemetry_mismatch_frames = 0;
    if (phys_miss.FocusMissingMesh > 0 && phys_miss.VisualHoles == 0 &&
        phys_miss.MissHoriz >= 3 && phys_miss.MissHoriz <= 4)
    {
      ++telemetry_mismatch_frames;
    }
    else
    {
      telemetry_mismatch_frames = 0;
    }
    const bool mismatch_finalize_kick = ShouldForceMissFinalizeOnTelemetryMismatch(
        phys_miss.FocusMissingMesh > 0, phys_miss.VisualHoles > 0,
        phys_miss.MissHoriz, telemetry_mismatch_frames) ||
        ShouldForceMissFinalizeOnStandWitnessStuck(
            !moving, phys_miss.FocusMissingMesh > 0, phys_miss.MissWitnessAgeFramesReport);
    const int finalize_ring =
        moving ? kVisualStageNearFovHoriz : kVisualStageLitDrawableHoriz;
    const bool miss_finalize_band =
        enter_fov_lit ||
        mismatch_finalize_kick ||
        (visual_holes &&
         ShouldPreferMissFinalizeBand(horiz_dist, finalize_ring)) ||
        ShouldFinalizeRelightUnderPlPressure(pending_light_focus_n, horiz_dist,
                                             focus_radius) ||
        ShouldFinalizeRelightUnderVbPressure(vb_no_ticket_n, horiz_dist) ||
        ShouldFinalizeRelightUnderVbSteadyPressure(
            vb_focus_n, pending_light_focus_n, horiz_dist);
    if (!exact_draw_gate_band && !exact_first_mesh_band && async_bg &&
        band_cy > 0 &&
        !miss_finalize_band)
    {
      const int band_h = band_cy * CHUNK_SIZE;
      const int span = relight_max - relight_min;
      if (span > band_h)
      {
        const int band_min =
            std::max(relight_min, relight_max - band_h + 1);
        if (band_min > relight_min)
        {
          remainder_min = relight_min;
          remainder_max = band_min - 1;
          relight_min = band_min;
          finalize_gate = false;
        }
      }
    }
    {
      auto &telem = world.GetPhysicsTelemetryMutable();
      telem.RelightCaptureColHoriz = horiz_dist;
      if (finalize_gate)
      {
        const uint64_t epoch = world.GetStreamingFrameEpoch();
        const auto fit = RelightLastFinalizeEpoch_.find(ground_xz);
        const bool band_needs =
            ColumnSurfaceBandNeedsRelight(world, ground_xz, relight_min,
                                          relight_max);
        const auto &cap_telem = world.GetPhysicsTelemetry();
        const bool consume_mode = IsTicketedVbConsumeMode(
            cap_telem.VisibleBlackNoTicketN, cap_telem.VisibleBlackFocusN,
            cap_telem.VisibleBlackStalledN);
        const bool plateau_suppress =
            ShouldSuppressPendingLightNote(
                cap_telem.VisibleBlackNoTicketN, pending_light_focus_n,
                cap_telem.VisibleBlackFocusN);
        const bool consume_finalize_carve = consume_mode && band_needs;
        if (!exact_first_mesh_band && plateau_suppress &&
            cap_telem.RelightApplyNPrev == 0 &&
            world.IsAsyncRelightColumnInFlight(ground_xz) &&
            !consume_finalize_carve)
        {
          finalize_gate = false;
          ++telem.RelightFinalizeDedupN;
        }
        else if (!exact_first_mesh_band &&
                 fit != RelightLastFinalizeEpoch_.end() && epoch >= fit->second &&
                 (epoch - fit->second) < 4 &&
                 world.IsPendingLightBeforeMesh(ground_xz) && !band_needs &&
                 !consume_finalize_carve)
        {
          finalize_gate = false;
          ++telem.RelightFinalizeDedupN;
        }
        else if (finalize_gate)
        {
          RelightLastFinalizeEpoch_[ground_xz] = epoch;
        }
      }
      telem.RelightCaptureFinalize = finalize_gate ? 1 : 0;
      const int span_y = std::max(0, relight_max - relight_min + 1);
      telem.RelightCaptureBandCySpan =
          (span_y + CHUNK_SIZE - 1) / CHUNK_SIZE;
    }
    if (async_bg && world.IsAsyncRelightColumnInFlight(ground_xz))
    {
      // Ghost InFlight (worker count 0) — reconcile then fall through.
      // Live in-flight: requeue at end; never erase Keys (Keys-without-deque
      // ghosts blocked MarkRelit with pendf plateau / relight_drain≈0).
      world.ReconcileAsyncRelightColumnInFlight();
      if (world.IsAsyncRelightColumnInFlight(ground_xz) &&
          world.GetAsyncRelightInFlightCount() > 0)
      {
        if (remainder_min >= 0)
        {
          PendingTerrainColumnRelightYBands[col] =
              glm::ivec2(remainder_min, remainder_max);
        }
        else if (relight_min > 0 || relight_max < max_y)
        {
          PendingTerrainColumnRelightYBands[col] =
              glm::ivec2(relight_min, relight_max);
        }
        // ColdApply A5: do not push_back a second deque entry if already keyed.
        if (PendingTerrainColumnRelightKeys.insert(col).second)
        {
          PendingTerrainColumnRelightsPriority.push_back(col);
        }
        else
        {
          PromoteTerrainColumnRelight(col);
        }
        ++skipped_inflight;
        capture_telem.RelightCaptureStopReason = 9;
        ++capture_telem.RelightCaptureInflightScanN;
        return skipped_inflight < std::max(8, max_bg_columns * 4);
      }
    }
    if (frame_ms_so_far >= capture_hot_skip_ms && async_bg &&
        draw_gate_target_pinned &&
        std::find(draw_gate_target_keys.begin(),
                  draw_gate_target_keys.end(), col) !=
            draw_gate_target_keys.end())
    {
      world.GetPhysicsTelemetryMutable().RelightCaptureHotSkipDrawGate = 1;
    }
    if (exact_draw_gate_band)
    {
      // Keep the precise rejected slice attached while the request is merely
      // dequeued or requeued behind in-flight work. It is consumed only when
      // the corresponding relight job is actually submitted below.
      PendingVisibleDrawGateRelightYBands.erase(col);
    }
    if (exact_first_mesh_band)
    {
      PendingVisibleFirstMeshRelightYBands.erase(col);
    }
    DeferredVisibleDrawGateRelightYBands.erase(col);
    PendingTerrainColumnRelightKeys.erase(col);
    const auto capture_t0 = std::chrono::high_resolution_clock::now();
    if (audit_relight)
    {
      CubatariumLogInfo(
          "RelightAudit",
          "capture submit column=(" + std::to_string(ground_xz.x) + "," +
              std::to_string(ground_xz.y) + ") band=" +
              std::to_string(relight_min) + ":" + std::to_string(relight_max) +
              " queued=" +
              (queued_band_defined
                   ? (std::to_string(queued_band.x) + ":" +
                      std::to_string(queued_band.y))
                   : std::string("none")) +
              " requested=" + std::to_string(requested_relight_min) + ":" +
              std::to_string(requested_relight_max) +
              " surface_adjusted=" +
              std::to_string(surface_band_adjusted) +
              " focus_y=" + std::to_string(focus_block.y) +
              " column_top=" + std::to_string(col_top_y) +
              " finalize=" + std::to_string(finalize_gate) +
              " draw_gate=" + std::to_string(exact_draw_gate_band) +
              " first_mesh_exact=" +
              std::to_string(exact_first_mesh_band) +
              " horiz=" + std::to_string(horiz_dist) +
              " fifo=" + std::to_string(GetPendingTerrainColumnRelightCount()) +
              " inflight=" + std::to_string(world.GetAsyncRelightInFlightCount()));
    }
    if (async_bg)
    {
      world.EnqueueAsyncTerrainColumnRelight(col.x, col.y, relight_min,
                                             relight_max, true, true,
                                             finalize_gate,
                                             exact_draw_gate_band);
    }
    else
    {
      world.RelightTerrainColumn(col.x, col.y, relight_min, relight_max, false);
    }
    if (remainder_min >= 0)
    {
      // Era36 B1: underground remainder is invisible — do not requeue.
      if (remainder_max < surface_band_min)
      {
        remainder_min = -1;
      }
      else
      {
        remainder_min = std::max(remainder_min, surface_band_min);
      }
    }
    if (remainder_min >= 0)
    {
      // SoftDefer: remainder band must stay hot within focus, otherwise
      // finalize_pending_gate=true can be starved and PendingLight keeps
      // rising while mesh_async stays at 0.
      const bool remainder_priority = horiz_dist <= focus_radius;
      // ColdFix / RateMatch R1: stash when Apply-capacity depth is full.
      const auto &telem = world.GetPhysicsTelemetry();
      const bool consume_mode = IsTicketedVbConsumeMode(
          telem.VisibleBlackNoTicketN, telem.VisibleBlackFocusN,
          telem.VisibleBlackStalledN, moving);
      const double unit_ms_prev =
          telem.RelightApplyNPrev > 0
              ? (telem.RelightApplyMsPrev /
                 static_cast<double>(telem.RelightApplyNPrev))
              : telem.RelightApplyMsPrev;
      const double light_unit =
          telem.RelightApplyNPrev > 0
              ? telem.RelightApplyLightMsPrev /
                    static_cast<double>(telem.RelightApplyNPrev)
              : 0.0;
      const double install_unit =
          telem.RelightApplyNPrev > 0
              ? telem.RelightApplyInstallMsPrev /
                    static_cast<double>(telem.RelightApplyNPrev)
              : 0.0;
      const int depth_cap = RelightCapturePipelineDepthCap(
          telem.RelightApplyNPrev, max_inflight,
          static_cast<double>(tune.MissReservedMs), unit_ms_prev, light_unit,
          install_unit, static_cast<int>(world.GetRelightCompletedSize()),
          telem.RelightFifoN, tune.RelightFifoSoftCap, telem.PendingLightN,
          consume_mode);
      const bool depth_full = !ShouldAdmitRelightCapture(
          static_cast<int>(world.GetRelightCompletedSize()),
          world.GetAsyncRelightInFlightCount(), depth_cap);
      if (depth_full)
      {
        PendingTerrainColumnRelightYBands[col] =
            glm::ivec2(remainder_min, remainder_max);
        if (PendingTerrainColumnRelightKeys.insert(col).second)
        {
          if (remainder_priority)
          {
            PendingTerrainColumnRelightsPriority.push_back(col);
          }
          else
          {
            PendingTerrainColumnRelights.push_back(col);
          }
        }
        else
        {
          PromoteTerrainColumnRelight(col);
        }
      }
      else
      {
        EnqueueTerrainColumnRelight(col.x, col.y, remainder_priority,
                                    remainder_min, remainder_max);
      }
    }
    const double capture_ms =
        std::chrono::duration<double, std::milli>(
            std::chrono::high_resolution_clock::now() - capture_t0)
            .count();
    ++drained_bg;
    capture_telem.RelightCaptureStopReason = 10;
    // One expensive Capture consumes the frame budget — stop the loop.
    if (capture_ms >= capture_drain_budget_ms)
    {
      return false;
    }
    return true;
  };
  while (drain_one())
  {
  }
  log_capture_state("end", bg_cap, drained_bg);
  harvest_fifo_overflow();
}

void UWorldPersistence::DrainTerrainColumnRelights(UWorld &world, int max_columns)
{
  DrainRelightQueues(world, 0, max_columns);
}

int UWorldPersistence::GetPendingTerrainColumnRelightCount() const
{
  return static_cast<int>(PendingTerrainColumnRelights.size() +
                          PendingTerrainColumnRelightsPriority.size());
}

bool UWorldPersistence::IsTerrainColumnRelightQueued(
    glm::ivec2 world_block_key) const
{
  return PendingTerrainColumnRelightKeys.count(world_block_key) != 0;
}

int UWorldPersistence::CancelTerrainColumnRelight(
    glm::ivec2 world_block_key)
{
  const glm::ivec2 ground_xz(FloorDiv(world_block_key.x, CHUNK_SIZE),
                             FloorDiv(world_block_key.y, CHUNK_SIZE));
  world_block_key = glm::ivec2(ground_xz.x * CHUNK_SIZE,
                               ground_xz.y * CHUNK_SIZE);
  int removed = 0;
  const auto remove_from_queue = [&](std::deque<glm::ivec2> &queue)
  {
    for (auto it = queue.begin(); it != queue.end();)
    {
      if (it->x == world_block_key.x && it->y == world_block_key.y)
      {
        it = queue.erase(it);
        ++removed;
      }
      else
      {
        ++it;
      }
    }
  };
  remove_from_queue(PendingTerrainColumnRelightsPriority);
  remove_from_queue(PendingTerrainColumnRelights);
  removed += PendingTerrainColumnRelightKeys.erase(world_block_key);
  removed += PendingTerrainColumnRelightYBands.erase(world_block_key);
  removed += PendingVisibleDrawGateRelightYBands.erase(world_block_key);
  removed += PendingVisibleFirstMeshRelightYBands.erase(world_block_key);
  removed += DeferredVisibleDrawGateRelightYBands.erase(world_block_key);
  removed += RelightLastFinalizeEpoch_.erase(world_block_key);
  removed += DeferredFarRelightColumns.erase(ground_xz);
  if (RelightFifoPinValid && RelightFifoPinCx == ground_xz.x &&
      RelightFifoPinCz == ground_xz.y)
  {
    RelightFifoPinValid = false;
    ++removed;
  }
  return removed;
}

UWorldPersistence::TerrainColumnRelightQueueInfo
UWorldPersistence::GetTerrainColumnRelightQueueInfo(
    glm::ivec2 world_block_key) const
{
  TerrainColumnRelightQueueInfo out{};
  out.keyed = PendingTerrainColumnRelightKeys.count(world_block_key) != 0;
  out.deferred_visible =
      DeferredVisibleDrawGateRelightYBands.count(world_block_key) != 0;
  const glm::ivec2 ground_xz(FloorDiv(world_block_key.x, CHUNK_SIZE),
                             FloorDiv(world_block_key.y, CHUNK_SIZE));
  const auto far_it = DeferredFarRelightColumns.find(ground_xz);
  out.deferred_far = far_it != DeferredFarRelightColumns.end();
  if (out.deferred_far)
  {
    out.priority = far_it->second.priority;
    out.y_band_defined = far_it->second.y_band.y >= far_it->second.y_band.x;
    out.min_world_y = far_it->second.y_band.x;
    out.max_world_y = far_it->second.y_band.y;
  }

  auto find_position = [&](const std::deque<glm::ivec2> &queue) -> int
  {
    const auto it = std::find(queue.begin(), queue.end(), world_block_key);
    return it == queue.end() ? -1 : static_cast<int>(it - queue.begin());
  };
  const int priority_index = find_position(PendingTerrainColumnRelightsPriority);
  if (priority_index >= 0)
  {
    out.priority = true;
    out.in_deque = true;
    out.queue_index = priority_index;
    out.queue_size = static_cast<int>(PendingTerrainColumnRelightsPriority.size());
  }
  else
  {
    const int far_index = find_position(PendingTerrainColumnRelights);
    if (far_index >= 0)
    {
      out.in_deque = true;
      out.queue_index = far_index;
      out.queue_size = static_cast<int>(PendingTerrainColumnRelights.size());
    }
  }

  const auto band_it = PendingTerrainColumnRelightYBands.find(world_block_key);
  if (band_it != PendingTerrainColumnRelightYBands.end())
  {
    out.y_band_defined = true;
    out.min_world_y = band_it->second.x;
    out.max_world_y = band_it->second.y;
  }
  else if (const auto visible_it =
               DeferredVisibleDrawGateRelightYBands.find(world_block_key);
           visible_it != DeferredVisibleDrawGateRelightYBands.end())
  {
    out.y_band_defined = true;
    out.min_world_y = visible_it->second.x;
    out.max_world_y = visible_it->second.y;
  }
  else if (const auto first_mesh_it =
               PendingVisibleFirstMeshRelightYBands.find(world_block_key);
           first_mesh_it != PendingVisibleFirstMeshRelightYBands.end())
  {
    out.y_band_defined = true;
    out.min_world_y = first_mesh_it->second.x;
    out.max_world_y = first_mesh_it->second.y;
  }
  return out;
}

int UWorldPersistence::TrimFarRelightFifoFarthest(glm::ivec3 focus_ground,
                                                  int soft_cap,
                                                  int protect_horiz)
{
  RelightFifoTrimFocusValid = true;
  RelightFifoTrimFocusCx = focus_ground.x;
  RelightFifoTrimFocusCz = focus_ground.z;
  const int protect =
      protect_horiz >= 0 ? protect_horiz : RelightFifoTrimProtectHoriz();
  auto total_fifo = [this]()
  {
    return static_cast<int>(PendingTerrainColumnRelights.size() +
                            PendingTerrainColumnRelightsPriority.size());
  };
  if (soft_cap <= 0 || total_fifo() <= soft_cap)
  {
    return 0;
  }
  const int need = total_fifo() - soft_cap;
  // Phase 5.7.5 / 5.7R: max-heap farthest + survivor rebuild — O(N + D log N)
  // (no per-drop std::find/erase O(N·D)).
  struct HeapEntry
  {
    int dist{0};
    glm::ivec2 key{0};
    bool operator<(const HeapEntry &o) const { return dist < o.dist; }
  };
  auto push_unprotected = [&](const std::deque<glm::ivec2> &q,
                              std::priority_queue<HeapEntry> &heap)
  {
    for (const glm::ivec2 &key : q)
    {
      const int cx = FloorDiv(key.x, CHUNK_SIZE);
      const int cz = FloorDiv(key.y, CHUNK_SIZE);
      const int dist =
          std::max(std::abs(cx - focus_ground.x), std::abs(cz - focus_ground.z));
      if (ShouldProtectRelightFifoTrimVictim(
              cx, cz, RelightFifoPinValid, RelightFifoPinCx, RelightFifoPinCz,
              true, focus_ground.x, focus_ground.z, protect) ||
          dist <= RelightMissPinMaxHoriz())
      {
        continue;
      }
      heap.push(HeapEntry{dist, key});
    }
  };
  std::priority_queue<HeapEntry> far_heap;
  std::priority_queue<HeapEntry> prio_heap;
  push_unprotected(PendingTerrainColumnRelights, far_heap);
  push_unprotected(PendingTerrainColumnRelightsPriority, prio_heap);
  std::unordered_set<glm::ivec2, IVec2Hash> victims;
  victims.reserve(static_cast<size_t>(std::max(0, need)));
  auto take_victim = [&](std::priority_queue<HeapEntry> &heap) -> bool
  {
    while (!heap.empty())
    {
      const HeapEntry top = heap.top();
      heap.pop();
      const glm::ivec2 victim = top.key;
      const int cx = FloorDiv(victim.x, CHUNK_SIZE);
      const int cz = FloorDiv(victim.y, CHUNK_SIZE);
      const int dist =
          std::max(std::abs(cx - focus_ground.x), std::abs(cz - focus_ground.z));
      if (ShouldProtectRelightFifoTrimVictim(
              cx, cz, RelightFifoPinValid, RelightFifoPinCx, RelightFifoPinCz,
              true, focus_ground.x, focus_ground.z, protect) ||
          dist <= RelightMissPinMaxHoriz())
      {
        continue;
      }
      if (!victims.insert(victim).second)
      {
        continue;
      }
      return true;
    }
    return false;
  };
  while (static_cast<int>(victims.size()) < need)
  {
    if (!PendingTerrainColumnRelights.empty() && take_victim(far_heap))
    {
      continue;
    }
    if (!PendingTerrainColumnRelightsPriority.empty() && take_victim(prio_heap))
    {
      continue;
    }
    ++RelightFifoProtectBlockN;
    break;
  }
  if (victims.empty())
  {
    return 0;
  }
  auto rebuild = [&](std::deque<glm::ivec2> &q, bool priority)
  {
    std::deque<glm::ivec2> kept;
    for (const glm::ivec2 &key : q)
    {
      if (victims.count(key) == 0)
      {
        kept.push_back(key);
      }
      else
      {
        PreserveRelightFifoVictimAsDeferred(key, priority);
        PendingTerrainColumnRelightKeys.erase(key);
        PendingTerrainColumnRelightYBands.erase(key);
      }
    }
    q.swap(kept);
  };
  rebuild(PendingTerrainColumnRelights, /*priority=*/false);
  rebuild(PendingTerrainColumnRelightsPriority, /*priority=*/true);
  return static_cast<int>(victims.size());
}

void UWorldPersistence::ClearPendingRelights()
{
  PendingPlayerRelights.clear();
  PendingTerrainColumnRelights.clear();
  PendingTerrainColumnRelightsPriority.clear();
  PendingTerrainColumnRelightKeys.clear();
  PendingTerrainColumnRelightYBands.clear();
  PendingVisibleDrawGateRelightYBands.clear();
  PendingVisibleFirstMeshRelightYBands.clear();
  DeferredVisibleDrawGateRelightYBands.clear();
  DeferredFarRelightColumns.clear();
}

int UWorldPersistence::GetPendingPlayerRelightCount() const
{
  return static_cast<int>(PendingPlayerRelights.size());
}

void UWorldPersistence::FinalizeAsyncTerrainColumnLoad(
    UWorld &world, glm::ivec3 ground_coord,
    PendingAsyncColumnLoadState state)
{
  const auto finalize_started = std::chrono::steady_clock::now();
  if (ground_coord.y != 0)
  {
    ground_coord.y = 0;
  }
  const auto async_io_trace_details = [&]()
  {
    if (!IsWorldColumnSourceTraceEnabled() || !AsyncChunkIo)
    {
      return std::string{};
    }
    const double finalize_prelog_ms = std::chrono::duration<double, std::milli>(
                                          std::chrono::steady_clock::now() -
                                          finalize_started)
                                          .count();
    return " timing_slices=" + std::to_string(state.timing_slice_count) +
           " disk_discovery_ms=" +
           std::to_string(state.disk_discovery_ms) +
           " format_detect_ms=" + std::to_string(state.format_detect_ms) +
           " worker_queue_ms=" + std::to_string(state.worker_queue_ms) +
           " worker_queue_max_ms=" +
           std::to_string(state.worker_queue_max_ms) +
           " file_open_ms=" + std::to_string(state.file_open_ms) +
           " file_read_ms=" + std::to_string(state.file_read_ms) +
           " file_read_max_ms=" + std::to_string(state.file_read_max_ms) +
           " disk_probe_ms=" + std::to_string(state.disk_probe_ms) +
           " disk_probe_max_ms=" +
           std::to_string(state.disk_probe_max_ms) +
           " result_wait_ms=" + std::to_string(state.result_wait_ms) +
           " result_wait_max_ms=" +
           std::to_string(state.result_wait_max_ms) +
           " deserialize_ms=" + std::to_string(state.deserialize_ms) +
           " deserialize_max_ms=" +
           std::to_string(state.deserialize_max_ms) +
           " apply_ms=" + std::to_string(state.apply_ms) +
           " apply_max_ms=" + std::to_string(state.apply_max_ms) +
           " deserialize_apply_ms=" +
           std::to_string(state.deserialize_apply_ms) +
           " deserialize_apply_max_ms=" +
           std::to_string(state.deserialize_apply_max_ms) +
           " finalize_prelog_ms=" + std::to_string(finalize_prelog_ms) +
           " chunkio_workers=" +
           std::to_string(AsyncChunkIo->GetWorkerCount()) +
           " chunkio_load_workers=" +
           std::to_string(AsyncChunkIo->GetLoadWorkerCount()) +
           " chunkio_background_workers=" +
           std::to_string(AsyncChunkIo->GetBackgroundWorkerCount()) +
           " chunkio_pending_jobs=" +
           std::to_string(AsyncChunkIo->GetPendingJobCount()) +
           " chunkio_active_jobs=" +
           std::to_string(AsyncChunkIo->GetActiveJobCount()) +
           " chunkio_load_pending_jobs=" +
           std::to_string(AsyncChunkIo->GetLoadPendingJobCount()) +
           " chunkio_background_pending_jobs=" +
           std::to_string(AsyncChunkIo->GetBackgroundPendingJobCount()) +
           " chunkio_load_active_jobs=" +
           std::to_string(AsyncChunkIo->GetLoadActiveJobCount()) +
           " chunkio_background_active_jobs=" +
           std::to_string(AsyncChunkIo->GetBackgroundActiveJobCount()) +
           " chunkio_ready_loads=" +
           std::to_string(AsyncChunkIo->GetLoadResultQueueDepth()) +
           " chunkio_ready_saves=" +
           std::to_string(AsyncChunkIo->GetSaveResultQueueDepth()) +
           " pending_disk_columns=" +
           std::to_string(PendingAsyncColumnLoadSlices.size());
  };
  const int max_height = world.ProceduralTemplate.MaxHeight;
  MaterializeRequiredTerrainColumnSlices(world.BlockWorld, ground_coord,
                                         max_height, state.highest_cy_on_disk);

  const bool has_disk = state.highest_cy_on_disk >= 0;
  const bool complete = IsTerrainChunkComplete(
      world.BlockWorld, ground_coord, max_height, state.highest_cy_on_disk);
  const bool should_retry =
      has_disk &&
      (state.had_disk_read_failure || state.had_invalid_token || !complete);
  const double load_ms =
      state.requested_at == std::chrono::steady_clock::time_point{}
          ? 0.0
          : std::chrono::duration<double, std::milli>(
                std::chrono::steady_clock::now() - state.requested_at)
                .count();

  if (should_retry && state.retry_generation < kMaxAsyncColumnLoadRetries)
  {
    LogWorldColumnSource(
        "disk", "retry",
        ground_coord,
        "highest_cy=" + std::to_string(state.highest_cy_on_disk) +
            " retry=" + std::to_string(state.retry_generation + 1) +
            " read_failed=" + (state.had_disk_read_failure ? "1" : "0") +
            " token_invalid=" + (state.had_invalid_token ? "1" : "0") +
            " complete=" + (complete ? "1" : "0") +
            " elapsed_ms=" + std::to_string(load_ms) +
            async_io_trace_details());
    ClearTerrainColumnChunks(world.BlockWorld, ground_coord, max_height);
    PendingAsyncColumnLoadState retry_state;
    const int load_to_cy = state.highest_cy_on_disk;
    retry_state.remaining_results = load_to_cy + 1;
    retry_state.highest_cy_on_disk = state.highest_cy_on_disk;
    retry_state.cancellation = state.cancellation;
    retry_state.requested_at = state.requested_at;
    retry_state.disk_discovery_ms = state.disk_discovery_ms;
    retry_state.retry_generation = state.retry_generation + 1;
    retry_state.timing_slice_count = state.timing_slice_count;
    retry_state.format_detect_ms = state.format_detect_ms;
    retry_state.worker_queue_ms = state.worker_queue_ms;
    retry_state.worker_queue_max_ms = state.worker_queue_max_ms;
    retry_state.file_open_ms = state.file_open_ms;
    retry_state.file_read_ms = state.file_read_ms;
    retry_state.file_read_max_ms = state.file_read_max_ms;
    retry_state.disk_probe_ms = state.disk_probe_ms;
    retry_state.disk_probe_max_ms = state.disk_probe_max_ms;
    retry_state.result_wait_ms = state.result_wait_ms;
    retry_state.result_wait_max_ms = state.result_wait_max_ms;
    retry_state.deserialize_ms = state.deserialize_ms;
    retry_state.deserialize_max_ms = state.deserialize_max_ms;
    retry_state.apply_ms = state.apply_ms;
    retry_state.apply_max_ms = state.apply_max_ms;
    retry_state.deserialize_apply_ms = state.deserialize_apply_ms;
    retry_state.deserialize_apply_max_ms = state.deserialize_apply_max_ms;
    PendingAsyncColumnLoadSlices[ground_coord] = retry_state;
    const glm::ivec3 focus =
        UChunkManager::WorldToChunk(world.GetPreferredLoadFocusBlock());
    const int sea_cy =
        FloorDiv(world.ProceduralTemplate.SeaLevel, CHUNK_SIZE);
    std::vector<int> cy_order;
    cy_order.reserve(static_cast<size_t>(load_to_cy + 1));
    auto push_cy = [&](int cy)
    {
      if (cy < 0 || cy > load_to_cy)
      {
        return;
      }
      if (std::find(cy_order.begin(), cy_order.end(), cy) == cy_order.end())
      {
        cy_order.push_back(cy);
      }
    };
    push_cy(focus.y);
    if (world.ProceduralTemplate.FillWater)
    {
      push_cy(sea_cy);
    }
    for (int d = 1; d <= load_to_cy; ++d)
    {
      push_cy(focus.y - d);
      push_cy(focus.y + d);
    }
    for (int cy = 0; cy <= load_to_cy; ++cy)
    {
      push_cy(cy);
    }
    const auto token =
        world.Streaming->GetChunkGenTokens().Current(ground_coord);
    for (int cy : cy_order)
    {
      AsyncChunkIo->RequestLoad(glm::ivec3(ground_coord.x, cy, ground_coord.z),
                                *ChunkStorage, *world.BlockRegistry,
                                WorldFolderPath, token,
                                retry_state.cancellation);
    }
    return;
  }

  if (!complete)
  {
    LogWorldColumnSource(
        "disk", "incomplete", ground_coord,
        "highest_cy=" + std::to_string(state.highest_cy_on_disk) +
            " retry=" + std::to_string(state.retry_generation) +
            " read_failed=" + (state.had_disk_read_failure ? "1" : "0") +
            " token_invalid=" + (state.had_invalid_token ? "1" : "0") +
            " elapsed_ms=" + std::to_string(load_ms) +
            async_io_trace_details());
    if (has_disk)
    {
      ClearTerrainColumnChunks(world.BlockWorld, ground_coord, max_height);
      RemoveTerrainColumnFromDisk(ground_coord, max_height);
    }
    return;
  }

  LogWorldColumnSource(
      has_disk ? "disk" : "empty-disk-column", "complete", ground_coord,
      "highest_cy=" + std::to_string(state.highest_cy_on_disk) +
          " disk_light=" + (state.had_disk_light ? "1" : "0") +
          " retry=" + std::to_string(state.retry_generation) +
          " elapsed_ms=" + std::to_string(load_ms) +
          async_io_trace_details());

  if (!world.Streaming || !world.Streaming->GetStreamer())
  {
    return;
  }

  if (has_disk || complete)
  {
    const glm::ivec3 focus_ground =
        UChunkManager::WorldToChunk(world.GetPreferredLoadFocusBlock());
    const int focus_radius = world.GetRenderDistanceChunks() + 1;
    const bool near_focus =
        std::abs(ground_coord.x - focus_ground.x) <= focus_radius &&
        std::abs(ground_coord.z - focus_ground.z) <= focus_radius;
    const ProceduralSettings &settings = world.GetProceduralSettings();

    // Default relight range (used when the mesh gate isn't blocking yet).
    const int relight_min_full = std::max(0, settings.SeaLevel - CHUNK_SIZE * 2);
    const int relight_max_full = settings.MaxHeight;

    if (!world.IsLightingRelightDeferred() && !state.had_disk_light)
    {
      // Mesh gate band = sea±2 CHUNK ∪ player when near (same as commit).
      const int sea = settings.SeaLevel;
      int dirty_min = std::max(0, sea - CHUNK_SIZE);
      int dirty_max =
          std::min(settings.MaxHeight, sea + CHUNK_SIZE * 2);
      if (near_focus)
      {
        const glm::ivec3 focus_block = world.GetPreferredLoadFocusBlock();
        dirty_min =
            std::min(dirty_min, std::max(0, focus_block.y - CHUNK_SIZE));
        dirty_max = std::max(
            dirty_max,
            std::min(settings.MaxHeight, focus_block.y + CHUNK_SIZE * 2));
      }

      // SoftDefer: finalize_pending_gate must line up with the mesh-gate band.
      const int horiz =
          std::max(std::abs(ground_coord.x - focus_ground.x),
                   std::abs(ground_coord.z - focus_ground.z));
      const glm::ivec2 col_xz(ground_coord.x, ground_coord.z);
      const auto note_commit_pl = [&](int dmin, int dmax) {
        // FZ2.2-C1b/C1c: FullyDark-only + idempotent Note.
        if (!world.ColumnFullyDarkSolidDrawable(col_xz))
        {
          return;
        }
        world.TryNotePendingLightBeforeMesh(ground_coord, dmin, dmax,
                                            __FUNCTION__);
      };
      const int fifo_n = GetPendingTerrainColumnRelightCount();
      const int soft_cap = URuntimeTuning::Get().RelightFifoSoftCap;
      const float fifo_frac = URuntimeTuning::Get().RelightFifoAdmitFrac;
      if (ShouldDeferFarRelightEnqueueOnFifoPressure(
              horiz, RelightMissPinMaxHoriz(), fifo_n, soft_cap, fifo_frac))
      {
        DeferFarRelightColumn(col_xz, dirty_min, dirty_max, near_focus);
        note_commit_pl(dirty_min, dirty_max);
        ++world.GetPhysicsTelemetryMutable().RelightDeferredFarEnqueueN;
      }
      else
      {
        EnqueueTerrainColumnRelight(ground_coord.x * CHUNK_SIZE,
                                    ground_coord.z * CHUNK_SIZE, near_focus,
                                    dirty_min, dirty_max);
        note_commit_pl(dirty_min, dirty_max);
      }
      // FZ2-T2 / FZ2.2-C1a: lit-ring seed — off by default (Fz2LitRingSeed).
      if (URuntimeTuning::Get().Fz2LitRingSeed && near_focus &&
          horiz <= kVisualStageLitDrawableHoriz && world.IsEnterLitGateActive())
      {
        const glm::ivec2 world_key(col_xz.x * CHUNK_SIZE, col_xz.y * CHUNK_SIZE);
        if (!IsTerrainColumnRelightQueued(world_key) &&
            !world.IsAsyncRelightColumnInFlight(col_xz) &&
            !world.IsPendingLightBeforeMesh(col_xz) &&
            world.ColumnFullyDarkSolidDrawable(col_xz))
        {
          EnqueueTerrainColumnRelight(ground_coord.x * CHUNK_SIZE,
                                      ground_coord.z * CHUNK_SIZE,
                                      /*priority=*/true, dirty_min, dirty_max);
          world.TryNotePendingLightBeforeMesh(ground_coord, dirty_min,
                                              dirty_max, __FUNCTION__);
        }
      }
      // Focus: first-mesh Dirty immediately (preview). Far waits MarkRelit
      // under Yellow/Red via commit path; disk-load always Dirty near.
      if (near_focus)
      {
        world.GetMeshService().MarkTerrainChunkMeshDirtySeamed(
            ground_coord, dirty_min, dirty_max, false,
            MeshRevisionBumpReason::PersistenceTerrainLoad);
        // TD-ARCH-015: warm Capture store on first-mesh admit (not remesh).
        world.GetMeshService().PrefetchMeshCaptureBand(
            world.GetBlockWorld(), ground_coord, dirty_min, dirty_max);
      }
    }
    else if (ShouldTrustDiskLightmap(
                 state.had_disk_light,
                 IsColumnLightComplete(glm::ivec2(ground_coord.x, ground_coord.z)),
                 world.IsLightingRelightDeferred()))
    {
      // Trusted disk lightmap: remesh only — no Capture FIFO refeed.
      // Bake-before-present: Dirty without LitReady until non-FullyDark
      // drawable settle (no near_focus LitReady bypass — that caused VB/flicker).
      // Seamed neighbors off: avoid 3x3 Dirty dual with neighbor FirstMesh/RAA.
      ++world.GetPhysicsTelemetryMutable().DiskLightTrustedN;
      const glm::ivec3 focus_block = world.GetPreferredLoadFocusBlock();
      int dirty_min = std::max(0, focus_block.y - CHUNK_SIZE);
      int dirty_max =
          std::min(settings.MaxHeight, focus_block.y + CHUNK_SIZE * 2);
      if (settings.FillWater)
      {
        dirty_min =
            std::min(dirty_min, std::max(0, settings.SeaLevel - CHUNK_SIZE));
        dirty_max = std::max(
            dirty_max,
            std::min(settings.MaxHeight, settings.SeaLevel + CHUNK_SIZE * 2));
      }
      world.GetMeshService().MarkTerrainChunkMeshDirtySeamed(
          ground_coord, dirty_min, dirty_max, false,
          MeshRevisionBumpReason::PersistenceTerrainLoad);
      const int cy0 = FloorDiv(dirty_min, CHUNK_SIZE);
      const int cy1 = FloorDiv(dirty_max, CHUNK_SIZE);
      bool has_lit_drawable = false;
      bool remesh_in_flight = false;
      for (int cy = cy0; cy <= cy1; ++cy)
      {
        const glm::ivec3 coord(ground_coord.x, cy, ground_coord.z);
        if (world.GetMeshService().HasDrawableGreedyMesh(coord) &&
            !world.GetMeshService().GetCache().ChunkHasFullyDarkFace(coord))
        {
          has_lit_drawable = true;
        }
        if (ColumnHasRemeshOwner(
                world.GetMeshService().IsChunkMeshDirty(coord),
                world.GetMeshService().IsRemeshAfterApplyPending(coord),
                world.GetMeshService().IsPendingGpuApply(coord),
                world.GetMeshService().HasInflightMeshBuild(coord)))
        {
          remesh_in_flight = true;
        }
      }
      if (ShouldSetLitReadyOnTrustedDisk(has_lit_drawable, remesh_in_flight))
      {
        world.SetColumnEmergeState(ground_coord, ColumnEmergeState::LitReady);
      }
      else
      {
        world.SetColumnEmergeState(ground_coord, ColumnEmergeState::Meshing);
        world.NoteStickyRemeshAfterLight(
            glm::ivec2(ground_coord.x, ground_coord.z));
      }
      // Seam repair: at most one incomplete cardinal neighbor near focus.
      if (near_focus)
      {
        static const glm::ivec2 kCardinals[] = {
            {1, 0}, {-1, 0}, {0, 1}, {0, -1}};
        for (const glm::ivec2 &d : kCardinals)
        {
          const glm::ivec2 n(ground_coord.x + d.x, ground_coord.z + d.y);
          if (IsColumnLightComplete(n))
          {
            continue;
          }
          GetColumnFlowExecutor().Enqueue(n, ColumnWorkKind::RelightThenMesh,
                                          /*priority=*/50);
          ++world.GetPhysicsTelemetryMutable().DiskLightRepairedN;
          break;
        }
      }
    }
    else
    {
      // Disk light present but not complete, or relight deferred: Capture path.
      TryEnqueueTerrainColumnRelight(world, ground_coord.x * CHUNK_SIZE,
                                     ground_coord.z * CHUNK_SIZE, near_focus,
                                     relight_min_full, relight_max_full);

      // Disk already lit: remesh visible band only (player ∪ sea), not full
      // 0..MaxHeight (that flooded Dirty on every column load).
      const glm::ivec3 focus_block = world.GetPreferredLoadFocusBlock();
      int dirty_min = std::max(0, focus_block.y - CHUNK_SIZE);
      int dirty_max =
          std::min(settings.MaxHeight, focus_block.y + CHUNK_SIZE * 2);
      if (settings.FillWater)
      {
        dirty_min =
            std::min(dirty_min, std::max(0, settings.SeaLevel - CHUNK_SIZE));
        dirty_max = std::max(
            dirty_max,
            std::min(settings.MaxHeight, settings.SeaLevel + CHUNK_SIZE * 2));
      }
      world.GetMeshService().MarkTerrainChunkMeshDirtySeamed(
          ground_coord, dirty_min, dirty_max, near_focus,
          MeshRevisionBumpReason::PersistenceTerrainLoad);
    }
  }
  world.Streaming->GetStreamer()->NotifyChunkCommitted(ground_coord);
}

AsyncChunkIoTickMetrics UWorldPersistence::TickAsyncChunkIo(
    UWorld &world, std::size_t max_slice_applies_override,
    double max_apply_ms)
{
  const auto tick_started = std::chrono::steady_clock::now();
  AsyncChunkIoTickMetrics metrics;
  if (!ChunkStorage)
  {
    metrics.tick_wall_ms = std::chrono::duration<double, std::milli>(
                               std::chrono::steady_clock::now() - tick_started)
                               .count();
    metrics.unattributed_ms = metrics.tick_wall_ms;
    return metrics;
  }

  const auto light_flags_result_drain_started =
      std::chrono::steady_clock::now();
  ProcessColumnLightFlagSaveResults(
      &metrics.light_flags_result_queue_mutex_wait_ms,
      &metrics.light_flags_result_queue_mutex_held_ms,
      &metrics.light_flags_result_count);
  metrics.light_flags_result_drain_ms =
      std::chrono::duration<double, std::milli>(
          std::chrono::steady_clock::now() - light_flags_result_drain_started)
          .count();
  if (IsStreamingDetailTraceEnabled() &&
      (metrics.light_flags_result_drain_ms >= 25.0 ||
       metrics.light_flags_result_queue_mutex_wait_ms >= 2.0 ||
       metrics.light_flags_result_queue_mutex_held_ms >= 2.0))
  {
    const double other_ms = (std::max)(
        0.0, metrics.light_flags_result_drain_ms -
                 metrics.light_flags_result_queue_mutex_wait_ms -
                 metrics.light_flags_result_queue_mutex_held_ms);
    const std::string message =
        "detail=light_flags_result_drain elapsed_ms=" +
        std::to_string(metrics.light_flags_result_drain_ms) +
        " queue_wait_ms=" +
        std::to_string(metrics.light_flags_result_queue_mutex_wait_ms) +
        " queue_held_ms=" +
        std::to_string(metrics.light_flags_result_queue_mutex_held_ms) +
        " result_processing_ms=" + std::to_string(other_ms) +
        " results_n=" + std::to_string(metrics.light_flags_result_count);
    CubatariumLogInfo("StreamingDetail", message);
  }
  if (AsyncChunkIo && world.ProceduralTemplate.AsyncChunkIo)
  {
    const auto discard_started = std::chrono::steady_clock::now();
    metrics.cancelled_discard_n = AsyncChunkIo->DiscardCancelledLoads();
    metrics.discard_cancelled_ms =
        std::chrono::duration<double, std::milli>(
            std::chrono::steady_clock::now() - discard_started)
            .count();
    const bool trace_async_io = IsWorldColumnSourceTraceEnabled();
    const double frame_ms = world.GetLastMovementFrameMs();
    std::size_t max_slice_applies = max_slice_applies_override;
    if (max_slice_applies == 0)
    {
      max_slice_applies = 10;
      if (frame_ms > 24.0)
      {
        max_slice_applies = 4;
      }
      else if (frame_ms > 16.0)
      {
        max_slice_applies = 6;
      }
    }
    const double effective_apply_budget_ms =
        max_apply_ms > 0.0
            ? max_apply_ms
            : (frame_ms > 24.0 ? 4.0 : (frame_ms > 16.0 ? 5.0 : 6.0));
    const auto apply_started = std::chrono::steady_clock::now();
    std::size_t applied_slices = 0;
    const auto apply_budget_expired = [&]()
    {
      const bool expired =
          applied_slices > 0 &&
          std::chrono::duration<double, std::milli>(
              std::chrono::steady_clock::now() - apply_started)
                  .count() >= effective_apply_budget_ms;
      metrics.apply_time_budget_hit = metrics.apply_time_budget_hit || expired;
      return expired;
    };
    const glm::ivec3 focus_chunk =
        UChunkManager::WorldToChunk(world.GetPreferredLoadFocusBlock());
    const auto result_selection_time = std::chrono::steady_clock::now();
    const auto result_rank = [&](const AsyncChunkLoadResult &load)
    {
      const glm::ivec3 ground(load.coord.x, 0, load.coord.z);
      const bool has_pending_column =
          PendingAsyncColumnLoadSlices.find(ground) !=
          PendingAsyncColumnLoadSlices.end();
      const int distance = (std::max)(
          std::abs(load.coord.x - focus_chunk.x),
          std::abs(load.coord.z - focus_chunk.z));
      int age_bonus = 0;
      if (load.submittedAt != std::chrono::steady_clock::time_point{} &&
          result_selection_time > load.submittedAt)
      {
        const double age_sec = std::chrono::duration<double>(
                                   result_selection_time - load.submittedAt)
                                   .count();
        age_bonus = (std::min)(256, static_cast<int>(age_sec / 5.0));
      }
      return std::tuple<int, int, std::chrono::steady_clock::time_point>{
          has_pending_column ? 0 : 1, (std::max)(0, distance - age_bonus),
          load.submittedAt};
    };
    // The load queue can contain hundreds of ready results. Select a small
    // near-focus batch once per tick, then apply under the existing time and
    // slice limits instead of rescanning the full queue after each slice.
    constexpr std::size_t kMaxRankedLoadsPerTick = 4;
    const std::size_t max_ranked_loads =
        std::min(max_slice_applies, kMaxRankedLoadsPerTick);
    auto completed_loads = AsyncChunkIo->DrainLoadsBestByKeyUpTo(
        max_ranked_loads, result_rank, &metrics.ready_loads_before_n,
        &metrics.result_selection_mutex_wait_ms,
        &metrics.result_selection_mutex_held_ms);
    metrics.result_selection_ms =
        std::chrono::duration<double, std::milli>(
            std::chrono::steady_clock::now() - result_selection_time)
            .count();
    metrics.selected_loads_n = completed_loads.size();
    const auto result_apply_started = std::chrono::steady_clock::now();
    std::size_t next_load = 0;
    while (next_load < completed_loads.size() &&
           applied_slices < max_slice_applies)
    {
      if (apply_budget_expired())
      {
        break;
      }
      AsyncChunkLoadResult &load = completed_loads[next_load++];
      ++applied_slices;
      const glm::ivec3 ground(load.coord.x, 0, load.coord.z);
      auto pending_it = PendingAsyncColumnLoadSlices.find(ground);
      if (pending_it == PendingAsyncColumnLoadSlices.end())
      {
        if (apply_budget_expired())
        {
          break;
        }
        continue;
      }

      PendingAsyncColumnLoadState &state = pending_it->second;
      if (trace_async_io)
      {
        ++state.timing_slice_count;
        state.format_detect_ms += load.formatDetectMs;
        if (load.workerStartedAt != std::chrono::steady_clock::time_point{} &&
            load.submittedAt != std::chrono::steady_clock::time_point{})
        {
          const double worker_queue_ms =
              std::chrono::duration<double, std::milli>(load.workerStartedAt -
                                                        load.submittedAt)
                  .count();
          state.worker_queue_ms += worker_queue_ms;
          state.worker_queue_max_ms =
              std::max(state.worker_queue_max_ms, worker_queue_ms);
        }
        state.file_open_ms += load.fileOpenMs;
        state.file_read_ms += load.fileReadMs;
        state.file_read_max_ms =
            std::max(state.file_read_max_ms, load.fileReadMs);
        state.deserialize_ms += load.deserializeMs;
        state.deserialize_max_ms =
            std::max(state.deserialize_max_ms, load.deserializeMs);
        if (load.workerFinishedAt != std::chrono::steady_clock::time_point{})
        {
          const double result_wait_ms =
              std::chrono::duration<double, std::milli>(
                  std::chrono::steady_clock::now() - load.workerFinishedAt)
                  .count();
          state.result_wait_ms += result_wait_ms;
          state.result_wait_max_ms =
              std::max(state.result_wait_max_ms, result_wait_ms);
        }
      }
      // The requested format is already carried with a successful worker
      // result. Re-check the filesystem only after a read failure, where the
      // file may have disappeared between request and open.
      ChunkDiskFormat disk_format = load.format;
      if (!load.success && load.format != ChunkDiskFormat::Absent)
      {
        const auto probe_started =
            trace_async_io ? std::chrono::steady_clock::now()
                           : std::chrono::steady_clock::time_point{};
        disk_format =
            ChunkStorage->DetectFormatOnDisk(WorldFolderPath, load.coord);
        if (trace_async_io)
        {
          const double probe_ms = std::chrono::duration<double, std::milli>(
                                      std::chrono::steady_clock::now() -
                                      probe_started)
                                      .count();
          state.disk_probe_ms += probe_ms;
          state.disk_probe_max_ms =
              std::max(state.disk_probe_max_ms, probe_ms);
        }
      }
      const uint64_t current_sequence =
          world.Streaming
              ? world.Streaming->GetChunkGenTokens().Current(ground).sequence
              : load.token.sequence;
      const bool token_valid = load.token.IsValidFor(ground, current_sequence);

      const auto world_apply_started = std::chrono::steady_clock::now();
      const auto apply_started_at =
          trace_async_io ? std::chrono::steady_clock::now()
                         : std::chrono::steady_clock::time_point{};
      if (load.success && token_valid && world.BlockRegistry)
      {
        const UChunkBuffer *buffer = load.decodedBuffer.get();
        if (buffer && !buffer->IsEmpty())
        {
          buffer->ApplyToChunk(world.BlockWorld, load.coord);
          if (buffer->HasChunkLightData())
          {
            state.had_disk_light = true;
          }
        }
        else
        {
          world.BlockWorld.GetChunkManager().EnsureChunk(load.coord);
        }
      }
      else if (disk_format == ChunkDiskFormat::Absent)
      {
        world.BlockWorld.GetChunkManager().EnsureChunk(load.coord);
      }
      else if (!token_valid)
      {
        state.had_invalid_token = true;
      }
      else
      {
        state.had_disk_read_failure = true;
      }
      metrics.world_apply_ms +=
          std::chrono::duration<double, std::milli>(
              std::chrono::steady_clock::now() - world_apply_started)
              .count();
      if (trace_async_io)
      {
        const double apply_ms =
            std::chrono::duration<double, std::milli>(
                std::chrono::steady_clock::now() - apply_started_at)
                .count();
        state.apply_ms += apply_ms;
        state.apply_max_ms = std::max(state.apply_max_ms, apply_ms);
        const double deserialize_apply_ms = load.deserializeMs + apply_ms;
        state.deserialize_apply_ms += deserialize_apply_ms;
        state.deserialize_apply_max_ms =
            std::max(state.deserialize_apply_max_ms, deserialize_apply_ms);
      }

      --state.remaining_results;
      if (state.remaining_results > 0)
      {
        if (apply_budget_expired())
        {
          break;
        }
        continue;
      }

      const PendingAsyncColumnLoadState finished = state;
      PendingAsyncColumnLoadSlices.erase(pending_it);
      const auto finalize_started = std::chrono::steady_clock::now();
      FinalizeAsyncTerrainColumnLoad(world, ground, finished);
      metrics.column_finalize_ms +=
          std::chrono::duration<double, std::milli>(
              std::chrono::steady_clock::now() - finalize_started)
              .count();

      if (apply_budget_expired())
      {
        break;
      }
    }
    metrics.processed_loads_n = next_load;
    metrics.applied_slices_n = applied_slices;
    metrics.requeued_loads_n = completed_loads.size() - next_load;
    metrics.result_processing_ms =
        std::chrono::duration<double, std::milli>(
            std::chrono::steady_clock::now() - result_apply_started)
            .count();
    if (next_load < completed_loads.size())
    {
      const auto requeue_started = std::chrono::steady_clock::now();
      std::vector<AsyncChunkLoadResult> deferred;
      deferred.reserve(completed_loads.size() - next_load);
      for (std::size_t i = next_load; i < completed_loads.size(); ++i)
      {
        deferred.push_back(std::move(completed_loads[i]));
      }
      AsyncChunkIo->RequeueLoads(
          std::move(deferred), &metrics.result_requeue_mutex_wait_ms,
          &metrics.result_requeue_mutex_held_ms);
      metrics.result_requeue_ms =
          std::chrono::duration<double, std::milli>(
              std::chrono::steady_clock::now() - requeue_started)
              .count();
    }

    const auto save_drain_started = std::chrono::steady_clock::now();
    for (AsyncChunkSaveRequest &save : AsyncChunkIo->DrainSaves())
    {
      if (!save.cleanupOnly)
      {
        ++metrics.saves_processed_n;
      }
      if (save.success && !save.cleanupOnly)
      {
        ChunkStorage->RecordChunkSliceSaved(save.worldFolder, save.coord);
      }
      auto pending_it = PendingAsyncColumnSaveSlices.find(save.groundCoord);
      if (pending_it != PendingAsyncColumnSaveSlices.end())
      {
        --pending_it->second;
        if (pending_it->second <= 0)
        {
          PendingAsyncColumnSaveSlices.erase(pending_it);
          ChunkStorage->ClearColumnSavePending(save.groundCoord);
        }
      }
      else
      {
        ChunkStorage->ClearColumnSavePending(save.groundCoord);
      }
      std::string outcome;
      std::string details;
      if (save.cleanupOnly)
      {
        outcome = save.success ? "stale_cleanup_done" : "stale_cleanup_failed";
        details = "operation=" + save.cleanupOperation +
                  " slices=" + std::to_string(save.cleanupSliceCount) +
                  " duration_ms=" + std::to_string(save.cleanupMs);
      }
      else
      {
        outcome = save.success ? "slice_written" : "slice_failed";
        details = "file=" + save.filePath;
      }
      if (!save.error.empty())
      {
        details += " error=" + save.error;
      }
      details += " pending_columns=" +
                 std::to_string(PendingAsyncColumnSaveSlices.size()) +
                 " io_jobs=" +
                 std::to_string(AsyncChunkIo->GetPendingJobCount()) +
                 " io_active=" +
                 std::to_string(AsyncChunkIo->GetActiveJobCount()) +
                 " io_load_jobs=" +
                 std::to_string(AsyncChunkIo->GetLoadPendingJobCount()) +
                 " io_background_jobs=" +
                 std::to_string(AsyncChunkIo->GetBackgroundPendingJobCount());
      LogWorldColumnSave(outcome.c_str(), save.coord, details,
                         !save.success);
    }
    metrics.save_drain_ms =
        std::chrono::duration<double, std::milli>(
            std::chrono::steady_clock::now() - save_drain_started)
            .count();
  }
  const auto light_flags_save_started = std::chrono::steady_clock::now();
  SaveColumnLightFlagsIfDirty();
  metrics.light_flags_save_ms =
      std::chrono::duration<double, std::milli>(
          std::chrono::steady_clock::now() - light_flags_save_started)
          .count();
  const auto queue_snapshot_started = std::chrono::steady_clock::now();
  if (AsyncChunkIo)
  {
    const JobThreadPoolSnapshot load = AsyncChunkIo->GetLoadPoolSnapshot();
    const JobThreadPoolSnapshot background =
        AsyncChunkIo->GetBackgroundPoolSnapshot();
    metrics.load_pending_jobs_n = load.pending;
    metrics.load_active_jobs_n = load.active;
    metrics.load_workers_n = load.workers;
    metrics.background_pending_jobs_n = background.pending;
    metrics.background_active_jobs_n = background.active;
    metrics.background_workers_n = background.workers;
    metrics.load_result_queue_depth_n =
        AsyncChunkIo->GetLoadResultQueueDepth();
    metrics.save_result_queue_depth_n =
        AsyncChunkIo->GetSaveResultQueueDepth();
    const AsyncChunkLoadQueuePushMetrics push_metrics =
        AsyncChunkIo->TakeLoadResultPushMetrics();
    metrics.load_result_push_mutex_wait_ms = push_metrics.mutex_wait_ms;
    metrics.load_result_push_mutex_held_ms = push_metrics.mutex_held_ms;
    metrics.load_result_push_mutex_wait_max_ms =
        push_metrics.mutex_wait_max_ms;
    metrics.load_result_push_mutex_held_max_ms =
        push_metrics.mutex_held_max_ms;
    metrics.load_result_push_n = push_metrics.push_count;
  }
  metrics.queue_snapshot_ms = std::chrono::duration<double, std::milli>(
                                  std::chrono::steady_clock::now() -
                                  queue_snapshot_started)
                                  .count();
  metrics.tick_wall_ms = std::chrono::duration<double, std::milli>(
                             std::chrono::steady_clock::now() - tick_started)
                             .count();
  const double attributed_ms =
      metrics.light_flags_result_drain_ms + metrics.discard_cancelled_ms +
      metrics.result_selection_ms + metrics.result_processing_ms +
      metrics.result_requeue_ms + metrics.save_drain_ms +
      metrics.light_flags_save_ms + metrics.queue_snapshot_ms;
  metrics.unattributed_ms =
      (std::max)(0.0, metrics.tick_wall_ms - attributed_ms);
  return metrics;
}

bool UWorldPersistence::IsAsyncChunkIoQuiescent() const
{
  if (!PendingAsyncColumnLoadSlices.empty() ||
      !PendingAsyncColumnSaveSlices.empty() || LightCompleteSaveInFlight ||
      (!WorldFolderPath.empty() && LightCompleteDirty))
  {
    return false;
  }
  if (!AsyncChunkIo)
  {
    return true;
  }
  return AsyncChunkIo->CompletedLoadsEmpty() &&
         AsyncChunkIo->CompletedSavesEmpty() &&
         AsyncChunkIo->CompletedColumnLightFlagsSavesEmpty();
}

void UWorldPersistence::TraceAsyncChunkIoShutdownState() const
{
  if (!IsWorldColumnSourceTraceEnabled())
  {
    return;
  }
  const std::string message =
      "outcome=shutdown_state pending_save_columns=" +
      std::to_string(PendingAsyncColumnSaveSlices.size()) +
      " pending_disk_columns=" +
      std::to_string(PendingAsyncColumnLoadSlices.size()) +
      " io_jobs=" +
      std::to_string(AsyncChunkIo ? AsyncChunkIo->GetPendingJobCount() : 0) +
      " io_active=" +
      std::to_string(AsyncChunkIo ? AsyncChunkIo->GetActiveJobCount() : 0) +
      " ready_saves=" +
      std::to_string(AsyncChunkIo
                         ? AsyncChunkIo->GetSaveResultQueueDepth()
                         : 0) +
      " ready_loads=" +
      std::to_string(AsyncChunkIo
                         ? AsyncChunkIo->GetLoadResultQueueDepth()
                         : 0) +
      " light_flags_in_flight=" +
      std::to_string(LightCompleteSaveInFlight ? 1 : 0) +
      " light_flags_dirty=" +
      std::to_string(LightCompleteDirty ? 1 : 0);
  CubatariumLogInfo("WorldColumnSave", message);
}

bool UWorldPersistence::TickDrainAsyncChunkIo(UWorld &world, int max_iterations)
{
  const int iterations = std::max(1, max_iterations);
  for (int i = 0; i < iterations; ++i)
  {
    TickAsyncChunkIo(world);
  }
  if (!IsAsyncChunkIoQuiescent())
  {
    return false;
  }
  if (AsyncChunkIo)
  {
    (void)AsyncChunkIo->DrainLoads();
    (void)AsyncChunkIo->DrainSaves();
  }
  return IsAsyncChunkIoQuiescent();
}

void UWorldPersistence::FlushAsyncChunkIo(UWorld &world)
{
  if (AsyncChunkIo)
  {
    constexpr int kMaxDrainIterations = 4096;
    for (int i = 0; i < kMaxDrainIterations; ++i)
    {
      if (TickDrainAsyncChunkIo(world, 1))
      {
        AsyncChunkIo->WaitIdle();
        if (IsAsyncChunkIoQuiescent())
        {
          break;
        }
      }
    }
  }
  if (!FlushColumnLightFlagsForWorldSwitch())
  {
    const std::string message =
        "outcome=light_flags_flush_timeout folder=" + WorldFolderPath +
        " revision=" + std::to_string(LightCompleteRevision);
    CubatariumLogInfo("WorldColumnSave", message);
  }
}

void UWorldPersistence::AbortAsyncChunkIo()
{
  (void)AbortAsyncChunkIoFor(std::chrono::milliseconds(200));
}

bool UWorldPersistence::AbortAsyncChunkIoFor(
    const std::chrono::milliseconds timeout)
{
  bool cancelled_loads = false;
  for (auto &entry : PendingAsyncColumnLoadSlices)
  {
    if (entry.second.cancellation)
    {
      entry.second.cancellation->store(true, std::memory_order_release);
      cancelled_loads = true;
    }
  }
  if (cancelled_loads && AsyncChunkIo)
  {
    AsyncChunkIo->NoteLoadCancellation();
    (void)AsyncChunkIo->DiscardCancelledLoads();
  }
  PendingAsyncColumnLoadSlices.clear();
  PendingAsyncColumnSaveSlices.clear();
  if (!AsyncChunkIo)
  {
    return true;
  }
  (void)AsyncChunkIo->DiscardCancelledLoads();
  (void)AsyncChunkIo->DrainLoads();
  (void)AsyncChunkIo->DrainSaves();
  AsyncChunkIo->CancelPending();
  if (timeout.count() <= 0)
  {
    return true;
  }
  const bool idle = AsyncChunkIo->WaitIdleFor(timeout);
  (void)AsyncChunkIo->DiscardCancelledLoads();
  (void)AsyncChunkIo->DrainLoads();
  (void)AsyncChunkIo->DrainSaves();
  return idle;
}

void UWorldPersistence::RequestAsyncTerrainColumnLoad(UWorld &world,
                                                      glm::ivec3 ground_coord)
{
  EnsureChunkIoInitialized();
  if (!AsyncChunkIo || !ChunkStorage || !world.BlockRegistry)
  {
    return;
  }
  if (ground_coord.y != 0)
  {
    ground_coord.y = 0;
  }
  if (ChunkStorage->IsColumnSavePending(ground_coord) ||
      PendingAsyncColumnLoadSlices.count(ground_coord) > 0)
  {
    return;
  }
  PendingAsyncColumnLoadState state;
  const bool trace_async_io = IsWorldColumnSourceTraceEnabled();
  const auto disk_discovery_started =
      trace_async_io ? std::chrono::steady_clock::now()
                     : std::chrono::steady_clock::time_point{};
  state.highest_cy_on_disk =
      ChunkStorage->GetHighestChunkSliceOnDisk(WorldFolderPath, ground_coord);
  if (trace_async_io)
  {
    state.disk_discovery_ms = std::chrono::duration<double, std::milli>(
                                  std::chrono::steady_clock::now() -
                                  disk_discovery_started)
                                  .count();
  }
  if (state.highest_cy_on_disk < 0)
  {
    LogWorldColumnSource("procedural", "disk_miss", ground_coord,
                         "highest_cy=-1 pending_save=0 discovery_ms=" +
                             std::to_string(state.disk_discovery_ms));
    return;
  }
  state.requested_at = std::chrono::steady_clock::now();
  state.remaining_results = state.highest_cy_on_disk + 1;
  state.cancellation = std::make_shared<std::atomic<bool>>(false);
  PendingAsyncColumnLoadSlices[ground_coord] = state;
  LogWorldColumnSource(
      "disk", "queued", ground_coord,
      "highest_cy=" + std::to_string(state.highest_cy_on_disk) +
          " slices=" + std::to_string(state.remaining_results) +
          " discovery_ms=" + std::to_string(state.disk_discovery_ms));

  // I/O order: player cy / sea surface first, then expand. Finalize still waits
  // for the full column, but near-surface slices land in RAM sooner and mesh
  // can start as soon as light finishes after finalize.
  const glm::ivec3 focus =
      UChunkManager::WorldToChunk(world.GetPreferredLoadFocusBlock());
  const int sea_cy =
      FloorDiv(world.ProceduralTemplate.SeaLevel, CHUNK_SIZE);
  std::vector<int> cy_order;
  cy_order.reserve(static_cast<size_t>(state.highest_cy_on_disk + 1));
  auto push_cy = [&](int cy)
  {
    if (cy < 0 || cy > state.highest_cy_on_disk)
    {
      return;
    }
    if (std::find(cy_order.begin(), cy_order.end(), cy) == cy_order.end())
    {
      cy_order.push_back(cy);
    }
  };
  push_cy(focus.y);
  if (world.ProceduralTemplate.FillWater)
  {
    push_cy(sea_cy);
  }
  for (int d = 1; d <= state.highest_cy_on_disk; ++d)
  {
    push_cy(focus.y - d);
    push_cy(focus.y + d);
  }
  for (int cy = 0; cy <= state.highest_cy_on_disk; ++cy)
  {
    push_cy(cy);
  }
  const auto token = world.Streaming->GetChunkGenTokens().Current(ground_coord);
  for (int cy : cy_order)
  {
    AsyncChunkIo->RequestLoad(glm::ivec3(ground_coord.x, cy, ground_coord.z),
                              *ChunkStorage, *world.BlockRegistry,
                              WorldFolderPath, token, state.cancellation);
  }
}

void UWorldPersistence::RequestAsyncTerrainColumnSave(UWorld &world,
                                                      glm::ivec3 ground_coord)
{
  const auto request_started = std::chrono::steady_clock::now();
  if (!AsyncChunkIo || !ChunkStorage || !world.BlockRegistry)
  {
    return;
  }
  if (ground_coord.y != 0)
  {
    ground_coord.y = 0;
  }
  if (ChunkStorage->IsColumnSavePending(ground_coord) ||
      PendingAsyncColumnSaveSlices.count(ground_coord) > 0)
  {
    return;
  }
  const int max_height = world.ProceduralTemplate.MaxHeight;
  const auto completeness_started = std::chrono::steady_clock::now();
  const bool complete =
      IsTerrainChunkComplete(world.BlockWorld, ground_coord, max_height);
  const double completeness_ms =
      std::chrono::duration<double, std::milli>(
          std::chrono::steady_clock::now() - completeness_started)
          .count();
  if (!complete)
  {
    ChunkStorage->MarkColumnSavePending(ground_coord);
    PendingAsyncColumnSaveSlices[ground_coord] = 1;
    AsyncChunkIo->RequestRemoveTerrainColumn(
        ground_coord, max_height, *ChunkStorage, WorldFolderPath);
    if (IsWorldColumnSaveTraceEnabled())
    {
      LogWorldColumnSave("discard_incomplete_queued", ground_coord,
                         "max_height=" + std::to_string(max_height) +
                             " complete_ms=" +
                             std::to_string(completeness_ms) +
                             " cleanup_operation=full_column" +
                             " total_ms=" +
                             std::to_string(
                                 std::chrono::duration<double, std::milli>(
                                     std::chrono::steady_clock::now() -
                                     request_started)
                                     .count()) +
                             " world_folder=" + WorldFolderPath);
    }
    return;
  }
  const int max_cy = (max_height + CHUNK_SIZE - 1) / CHUNK_SIZE;
  const auto disk_index_started = std::chrono::steady_clock::now();
  const int highest_on_disk =
      ChunkStorage->GetHighestChunkSliceOnDisk(WorldFolderPath, ground_coord);
  const double disk_index_ms =
      std::chrono::duration<double, std::milli>(
          std::chrono::steady_clock::now() - disk_index_started)
          .count();
  const auto highest_non_air_started = std::chrono::steady_clock::now();
  const int highest_non_air =
      GetHighestNonAirChunkSlice(world.BlockWorld, ground_coord, max_height);
  const double highest_non_air_ms =
      std::chrono::duration<double, std::milli>(
          std::chrono::steady_clock::now() - highest_non_air_started)
          .count();
  int highest_to_save = std::max(highest_on_disk, highest_non_air);
  if (highest_to_save < 0)
  {
    LogWorldColumnSave("skip_empty", ground_coord,
                       "world_folder=" + WorldFolderPath);
    return;
  }
  highest_to_save = std::min(highest_to_save, max_cy);

  const auto materialize_started = std::chrono::steady_clock::now();
  int save_count = 0;
  for (int cy = 0; cy <= highest_to_save; ++cy)
  {
    const glm::ivec3 slice(ground_coord.x, cy, ground_coord.z);
    if (!world.BlockWorld.GetChunkManager().HasChunk(slice))
    {
      world.BlockWorld.GetChunkManager().EnsureChunk(slice);
    }
    ++save_count;
  }
  const double materialize_ms =
      std::chrono::duration<double, std::milli>(
          std::chrono::steady_clock::now() - materialize_started)
          .count();
  const int stale_slice_count =
      std::max(0, max_cy - highest_to_save);
  const int cleanup_operation_count = stale_slice_count > 0 ? 1 : 0;
  const auto pending_started = std::chrono::steady_clock::now();
  ChunkStorage->MarkColumnSavePending(ground_coord);
  PendingAsyncColumnSaveSlices[ground_coord] =
      save_count + cleanup_operation_count;
  const double pending_setup_ms =
      std::chrono::duration<double, std::milli>(
          std::chrono::steady_clock::now() - pending_started)
          .count();
  const auto enqueue_started = std::chrono::steady_clock::now();
  for (int cy = 0; cy <= highest_to_save; ++cy)
  {
    const glm::ivec3 slice(ground_coord.x, cy, ground_coord.z);
    AsyncChunkIo->RequestSave(
        slice, *ChunkStorage, WorldFolderPath, world.BlockWorld,
        *world.BlockRegistry,
        world.Streaming->GetChunkGenTokens().Current(ground_coord));
  }
  const double snapshot_enqueue_ms =
      std::chrono::duration<double, std::milli>(
          std::chrono::steady_clock::now() - enqueue_started)
          .count();
  const auto cleanup_enqueue_started = std::chrono::steady_clock::now();
  if (cleanup_operation_count > 0)
  {
    AsyncChunkIo->RequestRemoveChunkSlices(
        ground_coord, highest_to_save + 1, max_cy, *ChunkStorage,
        WorldFolderPath);
  }
  const double cleanup_enqueue_ms =
      std::chrono::duration<double, std::milli>(
          std::chrono::steady_clock::now() - cleanup_enqueue_started)
          .count();
  if (IsWorldColumnSaveTraceEnabled())
  {
    LogWorldColumnSave(
        "queued", ground_coord,
        "slices=" + std::to_string(save_count) +
            " highest_cy=" + std::to_string(highest_to_save) +
            " stale_slices=" + std::to_string(stale_slice_count) +
            " complete_ms=" + std::to_string(completeness_ms) +
            " disk_index_ms=" + std::to_string(disk_index_ms) +
            " highest_non_air_ms=" + std::to_string(highest_non_air_ms) +
            " materialize_ms=" + std::to_string(materialize_ms) +
            " snapshot_enqueue_ms=" + std::to_string(snapshot_enqueue_ms) +
            " pending_setup_ms=" + std::to_string(pending_setup_ms) +
            " cleanup_enqueue_ms=" + std::to_string(cleanup_enqueue_ms) +
            " total_ms=" +
            std::to_string(std::chrono::duration<double, std::milli>(
                               std::chrono::steady_clock::now() - request_started)
                               .count()) +
            " world_folder=" + WorldFolderPath);
  }
}

void UWorldPersistence::CancelAsyncTerrainColumnLoad(glm::ivec3 ground_coord)
{
  if (ground_coord.y != 0)
  {
    ground_coord.y = 0;
  }
  const auto pending = PendingAsyncColumnLoadSlices.find(ground_coord);
  if (pending == PendingAsyncColumnLoadSlices.end())
  {
    return;
  }
  const PendingAsyncColumnLoadState state = pending->second;
  if (state.cancellation)
  {
    state.cancellation->store(true, std::memory_order_release);
  }
  PendingAsyncColumnLoadSlices.erase(pending);
  if (AsyncChunkIo)
  {
    AsyncChunkIo->NoteLoadCancellation();
    (void)AsyncChunkIo->DiscardCancelledLoads();
  }
  const double elapsed_ms =
      state.requested_at == std::chrono::steady_clock::time_point{}
          ? 0.0
          : std::chrono::duration<double, std::milli>(
                std::chrono::steady_clock::now() - state.requested_at)
                .count();
  LogWorldColumnSource(
      "disk", "cancelled_unloaded", ground_coord,
      "remaining_slices=" + std::to_string(state.remaining_results) +
          " elapsed_ms=" + std::to_string(elapsed_ms));
}

int UWorldPersistence::CancelAsyncTerrainColumnLoadsOutsideRadius(
    UWorld &world, glm::ivec3 center, int radius_chunks)
{
  center.y = 0;
  const int radius = std::max(0, radius_chunks);
  int cancelled = 0;
  for (auto pending = PendingAsyncColumnLoadSlices.begin();
       pending != PendingAsyncColumnLoadSlices.end();)
  {
    const glm::ivec3 ground = pending->first;
    const int distance =
        std::max(std::abs(ground.x - center.x), std::abs(ground.z - center.z));
    if (distance <= radius)
    {
      ++pending;
      continue;
    }

    const PendingAsyncColumnLoadState state = pending->second;
    if (state.cancellation)
    {
      state.cancellation->store(true, std::memory_order_release);
    }
    pending = PendingAsyncColumnLoadSlices.erase(pending);
    if (world.Streaming)
    {
      world.Streaming->GetChunkGenTokens().Bump(ground);
    }
    const double elapsed_ms =
        state.requested_at == std::chrono::steady_clock::time_point{}
            ? 0.0
            : std::chrono::duration<double, std::milli>(
                  std::chrono::steady_clock::now() - state.requested_at)
                  .count();
    LogWorldColumnSource(
        "disk", "cancelled_out_of_range", ground,
        "distance_chunks=" + std::to_string(distance) +
            " radius_chunks=" + std::to_string(radius) +
            " remaining_slices=" + std::to_string(state.remaining_results) +
            " elapsed_ms=" + std::to_string(elapsed_ms));
    ++cancelled;
  }
  if (cancelled > 0 && AsyncChunkIo)
  {
    AsyncChunkIo->NoteLoadCancellation();
    (void)AsyncChunkIo->DiscardCancelledLoads();
  }
  return cancelled;
}

bool UWorldPersistence::IsTerrainColumnDiskLoadPending(
    glm::ivec3 ground_coord) const
{
  if (ground_coord.y != 0)
  {
    ground_coord.y = 0;
  }
  return PendingAsyncColumnLoadSlices.count(ground_coord) > 0;
}

int UWorldPersistence::LoadTerrainColumn(glm::ivec3 coord,
                                         UBlockWorld &block_world,
                                         UBlockRegistry &registry,
                                         int max_height)
{
  if (!ChunkStorage)
  {
    return 0;
  }
  return ChunkStorage->LoadTerrainColumn(coord, block_world, WorldFolderPath,
                                         registry, max_height);
}

void UWorldPersistence::SaveTerrainColumn(glm::ivec3 ground_coord,
                                          UBlockWorld &block_world,
                                          UBlockRegistry &registry,
                                          int max_height)
{
  if (!ChunkStorage)
  {
    return;
  }
  if (ground_coord.y != 0)
  {
    ground_coord.y = 0;
  }
  if (!IsTerrainChunkComplete(block_world, ground_coord, max_height))
  {
    // Incomplete in RAM must not preserve a stale complete column on disk
    // (e.g. ocean file left after quit mid-land-gen → straight coast cut).
    RemoveTerrainColumnFromDisk(ground_coord, max_height);
    return;
  }
  ChunkStorage->SaveTerrainColumn(ground_coord, block_world, WorldFolderPath,
                                  registry, max_height);
}

void UWorldPersistence::RemoveTerrainColumnFromDisk(glm::ivec3 ground_coord,
                                                    int max_height)
{
  if (!ChunkStorage)
  {
    return;
  }
  if (ground_coord.y != 0)
  {
    ground_coord.y = 0;
  }
  ChunkStorage->RemoveTerrainColumnFromDisk(WorldFolderPath, ground_coord,
                                            max_height);
}

void UWorldPersistence::PurgeIncompleteTerrainColumn(UBlockWorld &block_world,
                                                     glm::ivec3 ground_coord,
                                                     int max_height)
{
  if (ground_coord.y != 0)
  {
    ground_coord.y = 0;
  }
  ClearTerrainColumnChunks(block_world, ground_coord, max_height);
  RemoveTerrainColumnFromDisk(ground_coord, max_height);
}

void UWorldPersistence::LoadInitialTerrainColumns(UWorld &world,
                                                  glm::vec3 spawn_point,
                                                  int render_distance_chunks)
{
  if (!ChunkStorage || !world.BlockRegistry)
  {
    return;
  }
  const glm::ivec3 spawn_block = WorldPosToBlock(spawn_point);
  const glm::ivec3 center_chunk = UChunkManager::WorldToChunk(spawn_block);
  const int radius = render_distance_chunks + 1;
  for (int dx = -radius; dx <= radius; ++dx)
  {
    for (int dz = -radius; dz <= radius; ++dz)
    {
      const glm::ivec3 ground(center_chunk.x + dx, 0, center_chunk.z + dz);
      LoadTerrainColumn(ground, world.BlockWorld, *world.BlockRegistry,
                        world.ProceduralTemplate.MaxHeight);
      if (!IsTerrainChunkComplete(world.BlockWorld, ground,
                                  world.ProceduralTemplate.MaxHeight))
      {
        PurgeIncompleteTerrainColumn(world.BlockWorld, ground,
                                     world.ProceduralTemplate.MaxHeight);
      }
    }
  }
}

void UWorldPersistence::LoadUsers(UWorld &world, const std::string &file_name)
{
  std::string val;
  std::ifstream file(file_name);
  if (file.is_open())
  {
    std::stringstream buffer;
    buffer << file.rdbuf();
    val = buffer.str();
    file.close();
  }
  else
  {
    std::cerr << "Failed to open users file: " << file_name << std::endl;
    return;
  }

  try
  {
    world.Users.clear();
    json d = json::parse(val);
    for (auto i = d.begin(); i != d.end(); ++i)
    {
      const auto user_name = i.key();
      const auto user_data = i.value();

      world.AddUser(user_name);
      auto user = world.GetUser(user_name);
      if (!user)
      {
        continue;
      }

      glm::vec3 position = world.SpawnPoint;
      const auto position_value = user_data.value("position", json::array());
      if (position_value.is_array() && position_value.size() == 3)
      {
        position = glm::vec3(position_value[0].get<float>(),
                             position_value[1].get<float>(),
                             position_value[2].get<float>());
      }
      user->SetPosition(position);
      world.SanitizeUserPosition(user);

      if (user_data.contains("player_creature_id"))
      {
        const CreatureId saved_id =
            user_data["player_creature_id"].get<CreatureId>();
        if (world.GetCreature(saved_id))
        {
          user->SetPlayerCreatureId(saved_id);
          world.Environment.SetPlayerCreatureId(saved_id);
        }
      }
      if (user_data.contains("selected_skin_id"))
      {
        user->SetSelectedSkinId(
            user_data["selected_skin_id"].get<std::string>());
      }
      else if (user_data.contains("selected_appearance_type"))
      {
        user->SetSelectedAppearanceTypeId(
            user_data["selected_appearance_type"].get<std::string>());
        user->SetSelectedSkinId(
            user_data["selected_appearance_type"].get<std::string>());
      }
      UCreature *player_creature =
          world.GetCreature(user->GetPlayerCreatureId());
      if (!player_creature && world.Environment.GetPlayerCreatureId() != 0)
      {
        user->SetPlayerCreatureId(world.Environment.GetPlayerCreatureId());
        player_creature =
            world.GetCreature(world.Environment.GetPlayerCreatureId());
      }
      if (!player_creature)
      {
        std::string species_id = "human";
        if (const auto &creature_definitions =
                world.GetCreatureDefinitionStorage())
        {
          const std::string controlled =
              creature_definitions->GetControlledDefaultSpeciesId();
          if (!controlled.empty())
          {
            species_id = controlled;
          }
        }
        const glm::vec3 eye_offset = world.ResolveControlledDefaultEyeOffset();
        const glm::vec3 body_origin = BodyOriginFromEye(position, eye_offset);
        const CreatureId pid = world.SpawnCreature(species_id, body_origin);
        if (pid != 0)
        {
          user->SetPlayerCreatureId(pid);
          world.Environment.SetPlayerCreatureId(pid);
          if (world.Users.size() == 1)
          {
            world.Environment.SetControlledCreatureId(pid);
          }
          if (UPlayer *player = dynamic_cast<UPlayer *>(world.GetCreature(pid)))
          {
            player->BindUser(user);
          }
          player_creature = world.GetCreature(pid);
        }
      }
      if (player_creature)
      {
        const glm::vec3 eye_offset = player_creature->GetEyeOffset();
        player_creature->SetBodyOrigin(
            BodyOriginFromEye(user->GetPosition(), eye_offset));
      }

      float yaw = -90.0f;
      float pitch = 0.0f;
      if (user_data.contains("yaw"))
      {
        yaw = user_data["yaw"].get<float>();
      }
      if (user_data.contains("pitch"))
      {
        pitch = user_data["pitch"].get<float>();
      }
      user->SetCameraOrientation(yaw, pitch);

      const size_t hotbar_count = 2;
      if (player_creature)
      {
        UCreatureInventory &inv = player_creature->GetInventory();
        const bool had_hotbars =
            user_data.contains("hotbars") && user_data["hotbars"].is_array();
        inv.DeserializeFromJson(user_data, hotbar_count);
        if (inv.GetStorage().empty())
        {
          if (ModePolicy::ShouldInitCreativeDefaults(world.GetGameMode()))
          {
            inv.InitCreativeDefaults();
          }
        }
        if (!had_hotbars || inv.IsPrimaryHotbarEmpty())
        {
          inv.EnsureDefaultHotbar();
        }
        if (const CreatureDefinition *def =
                world.GetCreatureDefinition(player_creature->GetTypeId()))
        {
          player_creature->ApplyStatsFromDefinition(*def);
        }
        if (!CreatureStatsJson::Read(user_data, player_creature->GetVitals(),
                                     player_creature->GetAttributes()))
        {
          // Keep definition defaults.
        }
        else
        {
          player_creature->GetAttributes().ClampAll();
          player_creature->GetVitals().ClampCurrents();
        }
        player_creature->SetOrientation(ModelYawFromCameraYaw(yaw), pitch);
        if (!user->GetSelectedSkinId().empty())
        {
          player_creature->SetSkinId(user->GetSelectedSkinId());
          if (const CreatureDefinition *def =
                  world.GetCreatureDefinition(player_creature->GetTypeId()))
          {
            player_creature->SetVisual(CreateCreatureVisual(*def));
          }
        }
      }

      if (auto camera = world.GetUserCamera(user_name))
      {
        camera->SetPosition(position);
        camera->SetOrientation(yaw, pitch);
      }
    }
  }
  catch (const json::exception &e)
  {
    std::cerr << "JSON parsing error in LoadUsers: " << e.what() << std::endl;
  }
}

void UWorldPersistence::SaveUsers(UWorld &world, const std::string &file_name)
{
  json objects;

  for (auto i = world.Users.begin(); i != world.Users.end(); ++i)
  {
    const auto &user_name = i->first;
    auto user = i->second;

    glm::vec3 position = user->GetPosition();
    float yaw = user->GetCameraYaw();
    float pitch = user->GetCameraPitch();
    if (user_name == world.CurrentUserName)
    {
      if (auto camera = world.GetUserCamera(user_name))
      {
        position = camera->GetPosition();
        yaw = camera->GetYaw();
        pitch = camera->GetPitch();
        user->SetPosition(position);
        user->SetCameraOrientation(yaw, pitch);
      }
    }

    json user_json;
    user_json["position"] = json::array({position.x, position.y, position.z});
    user_json["yaw"] = yaw;
    user_json["pitch"] = pitch;
    user_json["player_creature_id"] = user->GetPlayerCreatureId();
    if (!user->GetSelectedSkinId().empty())
    {
      user_json["selected_skin_id"] = user->GetSelectedSkinId();
    }
    else if (!user->GetSelectedAppearanceTypeId().empty())
    {
      user_json["selected_appearance_type"] =
          user->GetSelectedAppearanceTypeId();
    }

    if (UCreature *player_creature =
            world.GetCreature(user->GetPlayerCreatureId()))
    {
      player_creature->GetInventory().SerializeToJson(user_json);
      CreatureStatsJson::Write(user_json, player_creature->GetVitals(),
                               player_creature->GetAttributes());
    }

    objects[user_name] = user_json;
  }

  std::ofstream file(file_name);
  if (file.is_open())
  {
    file << objects.dump(4);
    file.close();
  }
}

void UWorldPersistence::LoadWorldData(UWorld &world,
                                      const std::string &file_name)
{
  std::string val;
  std::ifstream file(file_name);
  if (file.is_open())
  {
    std::stringstream buffer;
    buffer << file.rdbuf();
    val = buffer.str();
    file.close();
  }
  else
  {
    std::cerr << "Failed to open world data file: " << file_name << std::endl;
    return;
  }

  try
  {
    json d = json::parse(val);
    std::string world_name_value = d.value("world_name", "");
    json spawn_point_value = d.value("spawn_point", json::array());

    if (world_name_value.empty() || spawn_point_value.empty())
    {
      return;
    }

    if (!spawn_point_value.is_array())
    {
      return;
    }

    if (spawn_point_value.size() != 3)
    {
      return;
    }

    glm::vec3 spawn_point(spawn_point_value[0].get<float>(),
                          spawn_point_value[1].get<float>(),
                          spawn_point_value[2].get<float>());

    world.WorldName = world_name_value;
    world.SpawnPoint = spawn_point;

    if (d.contains("terrain") && d["terrain"].is_string())
    {
      world.TerrainType = d["terrain"].get<std::string>();
    }
    if (d.contains("world_seed"))
    {
      world.WorldSeed = d["world_seed"].get<uint32_t>();
    }
    if (d.contains("procedural") && d["procedural"].is_object())
    {
      world.ProceduralTemplate = ParseProceduralSettings(d);
      world.TerrainType =
          ProceduralGeneratorToString(world.ProceduralTemplate.Generator);
      world.WorldSeed = world.ProceduralTemplate.Seed;
    }
    else
    {
      world.ProceduralTemplate.Seed = world.WorldSeed;
      world.ProceduralTemplate.Generator =
          ProceduralGeneratorFromString(world.TerrainType);
      ResolveProceduralDefaults(world.ProceduralTemplate);
      ApplyGeneratorTierDefaults(world.ProceduralTemplate);
    }
    world.ResourcePacksEnabled.clear();
    world.ResourcePacksPrimary.clear();
    world.ResourcePacksSecondary.clear();
    world.WorldgenOwnerPackId.clear();
    if (d.contains("resource_packs") && d["resource_packs"].is_object())
    {
      const auto &rp = d["resource_packs"];
      auto parse_ids =
          [](const nlohmann::json &arr, std::vector<std::string> &out)
      {
        if (!arr.is_array())
        {
          return;
        }
        out.reserve(arr.size());
        for (const auto &id : arr)
        {
          if (id.is_string())
          {
            out.push_back(id.get<std::string>());
          }
        }
      };
      if (rp.contains("primary") && rp["primary"].is_array())
      {
        parse_ids(rp["primary"], world.ResourcePacksPrimary);
      }
      if (rp.contains("secondary") && rp["secondary"].is_array())
      {
        parse_ids(rp["secondary"], world.ResourcePacksSecondary);
      }
      if (world.ResourcePacksPrimary.empty() && rp.contains("enabled") &&
          rp["enabled"].is_array())
      {
        parse_ids(rp["enabled"], world.ResourcePacksPrimary);
      }
      if (rp.contains("worldgen_owner") && rp["worldgen_owner"].is_string())
      {
        world.WorldgenOwnerPackId = rp["worldgen_owner"].get<std::string>();
      }
      world.ResourcePacksEnabled = world.ResourcePacksPrimary;
      world.ResourcePacksEnabled.insert(world.ResourcePacksEnabled.end(),
                                        world.ResourcePacksSecondary.begin(),
                                        world.ResourcePacksSecondary.end());
    }
    if (d.contains("catalog_fingerprint") &&
        d["catalog_fingerprint"].is_string())
    {
      world.CatalogFingerprint = d["catalog_fingerprint"].get<std::string>();
    }
    else
    {
      world.CatalogFingerprint.clear();
    }
    if (d.contains("worldgen_sets") && d["worldgen_sets"].is_object())
    {
      std::string wg_error;
      if (!ParseWorldGenSets(d["worldgen_sets"], world.WorldGenSetsData,
                             wg_error))
      {
        std::cerr << "LoadWorldData: worldgen_sets error: " << wg_error
                  << std::endl;
      }
      else
      {
        std::string val_error;
        if (!ValidateWorldGenSets(world.WorldGenSetsData, val_error))
        {
          std::cerr << "LoadWorldData: worldgen_sets validation: " << val_error
                    << std::endl;
        }
        world.RebuildResolvedObjectFeatures();
      }
    }
    else
    {
      std::cerr << "LoadWorldData: missing required worldgen_sets" << std::endl;
    }

    if (d.contains("view") && d["view"].is_object())
    {
      world.SetViewSettings(WorldViewSettings::FromJson(d["view"]));
    }
    else
    {
      world.SetViewSettings(WorldViewSettings{});
    }

    if (d.contains("game_mode") && d["game_mode"].is_string())
    {
      world.SetGameMode(
          WorldGameModeFromString(d["game_mode"].get<std::string>()));
    }
    else
    {
      world.SetGameMode(WorldGameMode::Creative);
    }

    if (d.contains("difficulty") && d["difficulty"].is_string())
    {
      world.SetDifficulty(
          WorldDifficultyFromString(d["difficulty"].get<std::string>()));
    }
    else
    {
      world.SetDifficulty(WorldDifficulty::Normal);
    }

    if (d.contains("environment") && d["environment"].is_object())
    {
      const json &env = d["environment"];
      EnvironmentConfig config = EnvironmentConfig::FromJson(env);
      world.ApplyEnvironmentConfig(config, false);
      world.SetTimeFrozen(env.value("time_frozen", false));
      if (env.contains("weather") && env["weather"].is_string())
      {
        UWorld::WeatherType weather = UWorld::WeatherType::Clear;
        if (UWorld::WeatherTypeFromString(env["weather"].get<std::string>(),
                                          weather))
        {
          world.SetWeatherInternal(weather, config.WeatherAuto.TransitionSeconds,
                                   config.WeatherRuntime.ManualOverride);
        }
      }
      if (env.contains("weather_target") && env["weather_target"].is_string())
      {
        UWorld::WeatherType target = UWorld::WeatherType::Clear;
        if (UWorld::WeatherTypeFromString(
                env["weather_target"].get<std::string>(), target))
        {
          world.SetWeatherInternal(target, config.WeatherAuto.TransitionSeconds,
                                   config.WeatherRuntime.ManualOverride);
        }
      }
      if (env.contains("lighting") && env["lighting"].is_object())
      {
        const json &lighting = env["lighting"];
        world.SetLightingDebugEnabled(lighting.value("debug", false));
        world.SetWeatherOverlayEnabled(lighting.value("weather_overlay", true));
        world.SetWeatherParticlesEnabled(
            lighting.value("weather_particles", true));
      }
      if (env.contains("star_visibility"))
      {
        world.EnvironmentStateData.StarVisibility =
            std::clamp(env.value("star_visibility", 0.0f), 0.0f, 1.0f);
      }
      if (env.contains("cloud_coverage"))
      {
        world.EnvironmentStateData.CloudCoverage =
            std::clamp(env.value("cloud_coverage", 0.2f), 0.0f, 1.0f);
      }
      if (env.contains("star_visibility_override"))
      {
        world.EnvironmentStateData.StarVisibilityOverride = std::clamp(
            env.value("star_visibility_override", -1.0f), -1.0f, 1.0f);
      }
      if (env.contains("cloud_coverage_override"))
      {
        world.EnvironmentStateData.CloudCoverageOverride = std::clamp(
            env.value("cloud_coverage_override", -1.0f), -1.0f, 1.0f);
      }
      world.EnsureDefaultCelestialBodies();
    }
  }
  catch (const json::exception &e)
  {
    std::cerr << "JSON parsing error in LoadWorldData: " << e.what()
              << std::endl;
  }
}

void UWorldPersistence::SaveWorldData(UWorld &world,
                                      const std::string &file_name)
{
  json world_data;

  world_data["world_name"] = world.WorldName;
  world_data["terrain"] = world.TerrainType;
  world_data["world_seed"] = world.WorldSeed;
  WriteProceduralSettings(world_data, world.ProceduralTemplate);

  json arr =
      json::array({world.SpawnPoint.x, world.SpawnPoint.y, world.SpawnPoint.z});
  world_data["spawn_point"] = arr;

  if (!world.ResourcePacksPrimary.empty() ||
      !world.ResourcePacksSecondary.empty())
  {
    auto &rp = world_data["resource_packs"];
    if (!world.ResourcePacksPrimary.empty())
    {
      rp["primary"] = world.ResourcePacksPrimary;
    }
    if (!world.ResourcePacksSecondary.empty())
    {
      rp["secondary"] = world.ResourcePacksSecondary;
    }
    if (!world.WorldgenOwnerPackId.empty())
    {
      rp["worldgen_owner"] = world.WorldgenOwnerPackId;
    }
  }
  else if (!world.ResourcePacksEnabled.empty())
  {
    world_data["resource_packs"]["primary"] = world.ResourcePacksEnabled;
  }

  if (!world.CatalogFingerprint.empty())
  {
    world_data["catalog_fingerprint"] = world.CatalogFingerprint;
  }
  WriteWorldGenSets(world_data, world.WorldGenSetsData);
  if (auto camera = world.GetCurrentUserCamera())
  {
    world.SetViewSettings(camera->CaptureWorldViewSettings());
  }
  world.SyncDefaultCelestialBodiesToConfig();
  EnvironmentConfig config = world.GetEnvironmentConfig();
  config.TimeOfDay = world.GetEnvironmentState().TimeOfDayNormalized;
  config.DayLengthMinutes = world.GetEnvironmentState().DayLengthMinutes;
  json env = config.ToJson();
  env["time_frozen"] = world.GetEnvironmentState().TimeFrozen;
  env["weather"] =
      UWorld::WeatherTypeToString(world.GetEnvironmentState().Weather);
  env["weather_target"] =
      UWorld::WeatherTypeToString(world.GetEnvironmentState().TargetWeather);
  json weather_auto;
  config.WriteWeatherAutoToJson(weather_auto);
  env["weather_auto"] = weather_auto;
  env["lighting_version"] = 1;
  env["lighting"]["debug"] = world.GetLightingSettings().DebugEnabled;
  env["lighting"]["weather_overlay"] =
      world.GetLightingSettings().WeatherOverlayEnabled;
  env["lighting"]["weather_particles"] =
      world.GetLightingSettings().WeatherParticlesEnabled;
  env["star_visibility"] = world.GetEnvironmentState().StarVisibility;
  env["cloud_coverage"] = world.GetEnvironmentState().CloudCoverage;
  env["star_visibility_override"] =
      world.GetEnvironmentState().StarVisibilityOverride;
  env["cloud_coverage_override"] =
      world.GetEnvironmentState().CloudCoverageOverride;
  world_data["environment"] = env;
  world_data["view"] = world.GetViewSettings().ToJson();
  world_data["game_mode"] = WorldGameModeToString(world.GetGameMode());
  world_data["difficulty"] = WorldDifficultyToString(world.GetDifficulty());

  std::ofstream file(file_name);
  if (file.is_open())
  {
    file << world_data.dump(4);
    file.close();
  }
}

} // namespace cutum
