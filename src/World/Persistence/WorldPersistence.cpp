#include "World/Persistence/WorldPersistence.h"
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
#include "World/Core/RuntimeTuning.h"
#include "World/Core/World.h"
#include "World/Mesh/WorldMeshService.h"
#include "World/Chunks/Chunk.h"
#include "World/Chunks/TerrainColumnUtil.h"
#include "World/Lighting/ChunkLighting.h"
#include "World/Streaming/ColumnEmergeState.h"
#include "World/Streaming/ColumnFlowExecutor.h"
#include "World/Streaming/ColumnFlowScheduler.h"
#include "World/Math/GridMath.h"
#include "World/Math/BlockTypes.h"
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
#include <vector>

using json = nlohmann::json;

namespace cutum
{

namespace
{

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
  if (!LightCompleteDirty || WorldFolderPath.empty())
  {
    return;
  }
  try
  {
    std::filesystem::create_directories(WorldFolderPath);
    json data;
    data["format_version"] = 1;
    json complete = json::array();
    for (const glm::ivec2 &col : LightCompleteColumns)
    {
      complete.push_back(json::array({col.x, col.y}));
    }
    data["complete"] = std::move(complete);
    const std::string path = WorldFolderPath + "/column_light.json";
    std::ofstream file(path, std::ios::trunc);
    if (file.is_open())
    {
      file << data.dump();
      LightCompleteDirty = false;
    }
  }
  catch (const json::exception &)
  {
  }
}

void UWorldPersistence::EnqueueTerrainColumnRelight(int world_x, int world_z,
                                                    const bool priority,
                                                    int min_y, int max_y)
{
  const glm::ivec2 key(world_x, world_z);
  // Block-space key → column xz for light_complete invalidation.
  const glm::ivec2 ground_xz(FloorDiv(world_x, CHUNK_SIZE),
                             FloorDiv(world_z, CHUNK_SIZE));
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
      priority || ShouldForcePinColumnPriority(is_pin, /*miss_horiz=*/0);
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
    PendingTerrainColumnRelights.erase(victim_it);
    PendingTerrainColumnRelightKeys.erase(victim);
    PendingTerrainColumnRelightYBands.erase(victim);
    ++RelightFifoOverflowDroppedN;
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
  it->second.y_band.x = std::min(it->second.y_band.x, min_y);
  it->second.y_band.y = std::max(it->second.y_band.y, max_y);
  it->second.priority = it->second.priority || priority;
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
  const URuntimeTuning &tune = URuntimeTuning::Get();
  const int soft_cap = tune.RelightFifoSoftCap;
  const float frac = tune.RelightFifoAdmitFrac;
  int admitted = 0;
  std::vector<glm::ivec2> to_erase;
  to_erase.reserve(DeferredFarRelightColumns.size());
  for (const auto &kv : DeferredFarRelightColumns)
  {
    const glm::ivec2 ground_xz = kv.first;
    const int horiz = std::max(std::abs(ground_xz.x - focus_ground.x),
                               std::abs(ground_xz.y - focus_ground.z));
    if (horiz > pin_horiz)
    {
      continue;
    }
    const int fifo_n = GetPendingTerrainColumnRelightCount();
    if (ShouldDeferFarRelightEnqueueOnFifoPressure(horiz, pin_horiz, fifo_n,
                                                   soft_cap, frac))
    {
      break;
    }
    const glm::ivec2 band = kv.second.y_band;
    EnqueueTerrainColumnRelight(ground_xz.x * CHUNK_SIZE,
                                ground_xz.y * CHUNK_SIZE, kv.second.priority,
                                band.x, band.y);
    world.TryNotePendingLightBeforeMesh(glm::ivec3(ground_xz.x, 0, ground_xz.y),
                                     band.x, band.y);
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

void UWorldPersistence::PromoteTerrainColumnRelight(glm::ivec2 key)
{
  if (RelightFifoPinValid &&
      key != glm::ivec2(RelightFifoPinCx, RelightFifoPinCz))
  {
    ++RelightFifoProtectBlockN;
    return;
  }
  for (const glm::ivec2 &queued : PendingTerrainColumnRelightsPriority)
  {
    if (queued == key)
    {
      return;
    }
  }
  auto it = std::find(PendingTerrainColumnRelights.begin(),
                      PendingTerrainColumnRelights.end(), key);
  if (it != PendingTerrainColumnRelights.end())
  {
    PendingTerrainColumnRelights.erase(it);
    PendingTerrainColumnRelightsPriority.push_back(key);
    return;
  }
  // Keys-without-deque ghost: Drain used to re-Enqueue in-flight columns and
  // leave Keys set with no FIFO entry — pending_light then stuck forever.
  if (PendingTerrainColumnRelightKeys.count(key) != 0)
  {
    PendingTerrainColumnRelightsPriority.push_back(key);
  }
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
  if (world.BlocksAsyncRelightDrain())
  {
    return;
  }
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
  if (!moving && vb_no_ticket_n > 0)
  {
    static int stop_vb_stuck_frames = 0;
    static int last_stop_vb_no_ticket = -1;
    if (last_stop_vb_no_ticket >= 0 && vb_no_ticket_n >= last_stop_vb_no_ticket)
    {
      ++stop_vb_stuck_frames;
    }
    else
    {
      stop_vb_stuck_frames = 0;
    }
    last_stop_vb_no_ticket = vb_no_ticket_n;
    if (stop_vb_stuck_frames > 1200)
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
        telem.VisibleBlackStalledN);
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
      bg_cap = SoftDeferCaptureFloorWhenDepthFull(soft_defer_or_miss, 0,
                                                  completed_n, fifo_starve);
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
      return false;
    }
    const double elapsed_ms =
        std::chrono::duration<double, std::milli>(
            std::chrono::high_resolution_clock::now() - drain_loop_t0)
            .count();
    if (elapsed_ms >= capture_drain_budget_ms)
    {
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
      if (!enter_fov_lit && !soft_defer_hole && !miss_rim_pin &&
          !idle_pending_progress)
      {
        return false;
      }
      if (!async_bg &&
          frame_ms_so_far >= static_cast<double>(tune.CaptureSyncSkipWallMs) &&
          !idle_pending_progress)
      {
        return false;
      }
    }
    if (async_bg && world.GetAsyncRelightInFlightCount() >= max_inflight)
    {
      return false;
    }
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
      return false;
    }
    int relight_min = 0;
    int relight_max = max_y;
    const auto band_it = PendingTerrainColumnRelightYBands.find(col);
    if (band_it != PendingTerrainColumnRelightYBands.end())
    {
      relight_min = std::max(0, band_it->second.x);
      relight_max = std::min(max_y, band_it->second.y);
      PendingTerrainColumnRelightYBands.erase(band_it);
      if (relight_max < relight_min)
      {
        relight_min = 0;
        relight_max = max_y;
      }
    }
    // Era36/37 B1: clamp Capture Y-band to visible surface — drop underground.
    const glm::ivec2 ground_xz(FloorDiv(col.x, CHUNK_SIZE),
                               FloorDiv(col.y, CHUNK_SIZE));
    const int col_top_y = ColumnTopBlockY(world, ground_xz, max_y);
    const auto col_band = RelightSurfaceBandForColumn(
        focus_block.y, col_top_y, CHUNK_SIZE, max_y, relight_min, relight_max);
    relight_min = col_band.first;
    relight_max = col_band.second;
    if (relight_max < relight_min)
    {
      auto &telem = world.GetPhysicsTelemetryMutable();
      if (ColumnSurfaceBandNeedsRelight(world, ground_xz, surface_band_min,
                                        surface_band_max))
      {
        relight_min = surface_band_min;
        relight_max = surface_band_max;
        ++telem.RelightSkippedUndergroundN;
        world.TryNotePendingLightBeforeMesh(glm::ivec3(ground_xz.x, 0, ground_xz.y),
                                         relight_min, relight_max);
      }
      else
      {
        world.ClearPendingLightBeforeMesh(ground_xz);
        ++telem.RelightFalseClearN;
        PendingTerrainColumnRelightKeys.erase(col);
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
    // P2: miss nh≤2 prefers one surface finalize Capture (no partial Y-band).
    // Rim nh=3–4 keeps split. Era41b: enter FOV lit always finalizes.
    const int vb_focus_n = world.GetPhysicsTelemetry().VisibleBlackFocusN;
    const int finalize_ring =
        moving ? kVisualStageNearFovHoriz : kVisualStageLitDrawableHoriz;
    const bool miss_finalize_band =
        enter_fov_lit ||
        (visual_holes &&
         ShouldPreferMissFinalizeBand(horiz_dist, finalize_ring)) ||
        ShouldFinalizeRelightUnderPlPressure(pending_light_focus_n, horiz_dist,
                                             focus_radius) ||
        ShouldFinalizeRelightUnderVbPressure(vb_no_ticket_n, horiz_dist) ||
        ShouldFinalizeRelightUnderVbSteadyPressure(
            vb_focus_n, pending_light_focus_n, horiz_dist);
    if (async_bg && band_cy > 0 && !miss_finalize_band)
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
        if (plateau_suppress && cap_telem.RelightApplyNPrev == 0 &&
            world.IsAsyncRelightColumnInFlight(ground_xz) &&
            !consume_finalize_carve)
        {
          finalize_gate = false;
          ++telem.RelightFinalizeDedupN;
        }
        else if (fit != RelightLastFinalizeEpoch_.end() && epoch >= fit->second &&
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
              glm::ivec2(remainder_min, relight_max);
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
        return skipped_inflight < std::max(8, max_bg_columns * 4);
      }
    }
    PendingTerrainColumnRelightKeys.erase(col);
    const auto capture_t0 = std::chrono::high_resolution_clock::now();
    if (async_bg)
    {
      world.EnqueueAsyncTerrainColumnRelight(col.x, col.y, relight_min,
                                             relight_max, true, true,
                                             finalize_gate);
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
          telem.VisibleBlackStalledN);
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
  int dropped = 0;
  // Prefer drop from far (non-priority) deque first; then farthest priority.
  auto drop_farthest_from =
      [&](std::deque<glm::ivec2> &q) -> bool
  {
    auto best = q.end();
    int best_dist = -1;
    for (auto it = q.begin(); it != q.end(); ++it)
    {
      const int cx = FloorDiv(it->x, CHUNK_SIZE);
      const int cz = FloorDiv(it->y, CHUNK_SIZE);
      const int dist =
          std::max(std::abs(cx - focus_ground.x), std::abs(cz - focus_ground.z));
      // Era40 / P1: never Trim/drop LitDrawable-ring or pinned miss columns.
      if (ShouldProtectRelightFifoTrimVictim(
              cx, cz, RelightFifoPinValid, RelightFifoPinCx, RelightFifoPinCz,
              true, focus_ground.x, focus_ground.z, protect) ||
          dist <= RelightMissPinMaxHoriz())
      {
        continue;
      }
      if (dist > best_dist)
      {
        best_dist = dist;
        best = it;
      }
    }
    if (best == q.end())
    {
      return false;
    }
    const glm::ivec2 victim = *best;
    q.erase(best);
    PendingTerrainColumnRelightKeys.erase(victim);
    PendingTerrainColumnRelightYBands.erase(victim);
    ++dropped;
    return true;
  };
  while (total_fifo() > soft_cap)
  {
    if (!PendingTerrainColumnRelights.empty() &&
        drop_farthest_from(PendingTerrainColumnRelights))
    {
      continue;
    }
    if (!PendingTerrainColumnRelightsPriority.empty() &&
        drop_farthest_from(PendingTerrainColumnRelightsPriority))
    {
      continue;
    }
    ++RelightFifoProtectBlockN;
    break;
  }
  return dropped;
}

void UWorldPersistence::ClearPendingRelights()
{
  PendingPlayerRelights.clear();
  PendingTerrainColumnRelights.clear();
  PendingTerrainColumnRelightsPriority.clear();
  PendingTerrainColumnRelightKeys.clear();
  PendingTerrainColumnRelightYBands.clear();
}

int UWorldPersistence::GetPendingPlayerRelightCount() const
{
  return static_cast<int>(PendingPlayerRelights.size());
}

void UWorldPersistence::FinalizeAsyncTerrainColumnLoad(
    UWorld &world, glm::ivec3 ground_coord,
    PendingAsyncColumnLoadState state)
{
  if (ground_coord.y != 0)
  {
    ground_coord.y = 0;
  }
  const int max_height = world.ProceduralTemplate.MaxHeight;
  MaterializeRequiredTerrainColumnSlices(world.BlockWorld, ground_coord,
                                         max_height, state.highest_cy_on_disk);

  const bool has_disk = state.highest_cy_on_disk >= 0;
  const bool complete = IsTerrainChunkComplete(
      world.BlockWorld, ground_coord, max_height, state.highest_cy_on_disk);
  const bool should_retry =
      has_disk &&
      (state.had_disk_read_failure || state.had_invalid_token || !complete);

  if (should_retry && state.retry_generation < kMaxAsyncColumnLoadRetries)
  {
    ClearTerrainColumnChunks(world.BlockWorld, ground_coord, max_height);
    PendingAsyncColumnLoadState retry_state;
    const int load_to_cy = state.highest_cy_on_disk;
    retry_state.remaining_results = load_to_cy + 1;
    retry_state.highest_cy_on_disk = state.highest_cy_on_disk;
    retry_state.retry_generation = state.retry_generation + 1;
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
                                *ChunkStorage, WorldFolderPath, token);
    }
    return;
  }

  if (!complete)
  {
    if (has_disk)
    {
      ClearTerrainColumnChunks(world.BlockWorld, ground_coord, max_height);
      RemoveTerrainColumnFromDisk(ground_coord, max_height);
    }
    return;
  }

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
        world.TryNotePendingLightBeforeMesh(ground_coord, dmin, dmax);
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
                                              dirty_max);
        }
      }
      // Focus: first-mesh Dirty immediately (preview). Far waits MarkRelit
      // under Yellow/Red via commit path; disk-load always Dirty near.
      if (near_focus)
      {
        world.MarkTerrainChunkMeshDirtySeamed(ground_coord, dirty_min, dirty_max,
                                              false);
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
      world.MarkTerrainChunkMeshDirtySeamed(ground_coord, dirty_min, dirty_max,
                                            /*include_horizontal_neighbors=*/
                                            false);
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
      world.MarkTerrainChunkMeshDirtySeamed(ground_coord, dirty_min, dirty_max,
                                            near_focus);
    }
  }
  world.Streaming->GetStreamer()->NotifyChunkCommitted(ground_coord);
}

void UWorldPersistence::TickAsyncChunkIo(UWorld &world)
{
  if (!ChunkStorage)
  {
    return;
  }

  if (AsyncChunkIo && world.ProceduralTemplate.AsyncChunkIo)
  {
    const double frame_ms = world.GetLastMovementFrameMs();
    std::size_t max_slice_applies = 10;
    if (frame_ms > 24.0)
    {
      max_slice_applies = 4;
    }
    else if (frame_ms > 16.0)
    {
      max_slice_applies = 6;
    }
    for (AsyncChunkLoadResult &load :
         AsyncChunkIo->DrainLoadsUpTo(max_slice_applies))
    {
      const glm::ivec3 ground(load.coord.x, 0, load.coord.z);
      auto pending_it = PendingAsyncColumnLoadSlices.find(ground);
      if (pending_it == PendingAsyncColumnLoadSlices.end())
      {
        continue;
      }

      PendingAsyncColumnLoadState &state = pending_it->second;
      const ChunkDiskFormat disk_format =
          ChunkStorage->DetectFormatOnDisk(WorldFolderPath, load.coord);
      const uint64_t current_sequence =
          world.Streaming
              ? world.Streaming->GetChunkGenTokens().Current(ground).sequence
              : load.token.sequence;
      const bool token_valid = load.token.IsValidFor(ground, current_sequence);

      if (load.success && token_valid && world.BlockRegistry)
      {
        const UChunkBuffer buffer = ChunkStorage->DeserializeChunk(
            load.payload, load.coord, load.format, *world.BlockRegistry);
        if (!buffer.IsEmpty())
        {
          buffer.ApplyTo(world.BlockWorld);
          if (buffer.HasChunkLightData())
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

      --state.remaining_results;
      if (state.remaining_results > 0)
      {
        continue;
      }

      const PendingAsyncColumnLoadState finished = state;
      PendingAsyncColumnLoadSlices.erase(pending_it);
      FinalizeAsyncTerrainColumnLoad(world, ground, finished);
    }

    for (AsyncChunkSaveRequest &save : AsyncChunkIo->DrainSaves())
    {
      if (ChunkStorage->GetSettings().writeFormat == ChunkWriteFormat::Binary &&
          ChunkStorage->GetSettings().deleteLegacyJsonOnBinarySave)
      {
        const std::string legacy_json = ChunkStorage->ChunkFilePath(
            WorldFolderPath, save.coord, ChunkDiskFormat::Json);
        std::error_code ec;
        std::filesystem::remove(legacy_json, ec);
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
    }
  }
  SaveColumnLightFlagsIfDirty();
}

bool UWorldPersistence::IsAsyncChunkIoQuiescent() const
{
  if (!PendingAsyncColumnLoadSlices.empty() ||
      !PendingAsyncColumnSaveSlices.empty())
  {
    return false;
  }
  if (!AsyncChunkIo)
  {
    return true;
  }
  return AsyncChunkIo->CompletedLoadsEmpty() &&
         AsyncChunkIo->CompletedSavesEmpty();
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
  if (!AsyncChunkIo)
  {
    return;
  }
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
  SaveColumnLightFlagsIfDirty();
}

void UWorldPersistence::AbortAsyncChunkIo()
{
  (void)AbortAsyncChunkIoFor(std::chrono::milliseconds(200));
}

bool UWorldPersistence::AbortAsyncChunkIoFor(
    const std::chrono::milliseconds timeout)
{
  PendingAsyncColumnLoadSlices.clear();
  PendingAsyncColumnSaveSlices.clear();
  if (!AsyncChunkIo)
  {
    return true;
  }
  (void)AsyncChunkIo->DrainLoads();
  (void)AsyncChunkIo->DrainSaves();
  AsyncChunkIo->CancelPending();
  if (timeout.count() <= 0)
  {
    return true;
  }
  const bool idle = AsyncChunkIo->WaitIdleFor(timeout);
  (void)AsyncChunkIo->DrainLoads();
  (void)AsyncChunkIo->DrainSaves();
  return idle;
}

void UWorldPersistence::RequestAsyncTerrainColumnLoad(UWorld &world,
                                                      glm::ivec3 ground_coord)
{
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
  state.highest_cy_on_disk =
      ChunkStorage->GetHighestChunkSliceOnDisk(WorldFolderPath, ground_coord);
  if (state.highest_cy_on_disk < 0)
  {
    return;
  }
  state.remaining_results = state.highest_cy_on_disk + 1;
  PendingAsyncColumnLoadSlices[ground_coord] = state;

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
                              *ChunkStorage, WorldFolderPath, token);
  }
}

void UWorldPersistence::RequestAsyncTerrainColumnSave(UWorld &world,
                                                      glm::ivec3 ground_coord)
{
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
  if (!IsTerrainChunkComplete(world.BlockWorld, ground_coord, max_height))
  {
    RemoveTerrainColumnFromDisk(ground_coord, max_height);
    return;
  }
  const int max_cy = (max_height + CHUNK_SIZE - 1) / CHUNK_SIZE;
  const int highest_on_disk =
      ChunkStorage->GetHighestChunkSliceOnDisk(WorldFolderPath, ground_coord);
  const int highest_non_air =
      GetHighestNonAirChunkSlice(world.BlockWorld, ground_coord, max_height);
  int highest_to_save = std::max(highest_on_disk, highest_non_air);
  if (highest_to_save < 0)
  {
    return;
  }
  highest_to_save = std::min(highest_to_save, max_cy);

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
  ChunkStorage->MarkColumnSavePending(ground_coord);
  PendingAsyncColumnSaveSlices[ground_coord] = save_count;
  for (int cy = 0; cy <= highest_to_save; ++cy)
  {
    const glm::ivec3 slice(ground_coord.x, cy, ground_coord.z);
    AsyncChunkIo->RequestSave(
        slice, *ChunkStorage, WorldFolderPath, world.BlockWorld,
        *world.BlockRegistry,
        world.Streaming->GetChunkGenTokens().Current(ground_coord));
  }
  for (int cy = highest_to_save + 1; cy <= max_cy; ++cy)
  {
    ChunkStorage->RemoveChunkSliceFromDisk(
        WorldFolderPath, glm::ivec3(ground_coord.x, cy, ground_coord.z));
  }
}

void UWorldPersistence::CancelAsyncTerrainColumnLoad(glm::ivec3 ground_coord)
{
  if (ground_coord.y != 0)
  {
    ground_coord.y = 0;
  }
  PendingAsyncColumnLoadSlices.erase(ground_coord);
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
