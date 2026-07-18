#include "World/Persistence/WorldPersistence.h"
#include "Blocks/BlockRegistry.h"
#include "Creatures/Core/Creature.h"
#include "Creatures/Core/CreatureInventory.h"
#include "Creatures/Definition/CreatureDefinitionStorage.h"
#include "Creatures/Player/Player.h"
#include "Creatures/Player/PlayerCapsule.h"
#include "Creatures/Player/User.h"
#include "Creatures/Visual/CreaturePartMeshData.h"
#include "Creatures/Visual/CreatureVisualFactory.h"
#include "Render/Camera/Camera.h"
#include "World/Chunks/Chunk.h"
#include "World/Chunks/ChunkBuffer.h"
#include "World/Chunks/ChunkManager.h"
#include "World/Chunks/ChunkStreamer.h"
#include "World/Core/BlockWorld.h"
#include "World/Core/World.h"
#include "World/Chunks/Chunk.h"
#include "World/Chunks/TerrainColumnUtil.h"
#include "World/Lighting/ChunkLighting.h"
#include "World/Environment/EnvironmentConfig.h"
#include "World/Math/GridMath.h"
#include "World/Streaming/WorldStreaming.h"
#include "WorldGen/Core/ProceduralConfigIO.h"
#include "WorldGen/Core/ProceduralSettings.h"
#include "WorldGen/Core/WorldGenSets.h"
#include "WorldGen/Features/ObjectFeatureConfig.h"
#include <algorithm>
#include <chrono>
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

void UWorldPersistence::EnqueueTerrainColumnRelight(int world_x, int world_z,
                                                    const bool priority,
                                                    int min_y, int max_y)
{
  const glm::ivec2 key(world_x, world_z);
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
    // Already queued: if caller now wants priority, promote out of the far FIFO.
    if (priority)
    {
      PromoteTerrainColumnRelight(key);
    }
    return;
  }
  if (priority)
  {
    PendingTerrainColumnRelightsPriority.push_back(key);
  }
  else
  {
    PendingTerrainColumnRelights.push_back(key);
  }
}

void UWorldPersistence::PromoteTerrainColumnRelight(glm::ivec2 key)
{
  for (const glm::ivec2 &queued : PendingTerrainColumnRelightsPriority)
  {
    if (queued == key)
    {
      return;
    }
  }
  auto it = std::find(PendingTerrainColumnRelights.begin(),
                      PendingTerrainColumnRelights.end(), key);
  if (it == PendingTerrainColumnRelights.end())
  {
    return;
  }
  PendingTerrainColumnRelights.erase(it);
  PendingTerrainColumnRelightsPriority.push_back(key);
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
    return;
  }
  const int max_y = world.ProceduralTemplate.MaxHeight;
  const bool async_bg =
      world.ProceduralTemplate.AsyncRelight && !world.IsLightingRelightDeferred();
  const int max_inflight =
      async_bg ? std::clamp(world.ProceduralTemplate.RelightThreadCount, 1, 8) * 2
               : 0;
  int drained_bg = 0;
  auto drain_one = [&]()
  {
    if (drained_bg >= max_bg_columns)
    {
      return false;
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
    PendingTerrainColumnRelightKeys.erase(col);
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
    if (async_bg)
    {
      world.EnqueueAsyncTerrainColumnRelight(col.x, col.y, relight_min,
                                             relight_max);
    }
    else
    {
      world.RelightTerrainColumn(col.x, col.y, relight_min, relight_max, false);
    }
    ++drained_bg;
    return true;
  };
  while (drain_one())
  {
  }
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
    EnqueueTerrainColumnRelight(ground_coord.x * CHUNK_SIZE,
                                ground_coord.z * CHUNK_SIZE, near_focus);
    const ProceduralSettings &settings = world.GetProceduralSettings();
    if (!world.IsLightingRelightDeferred() && !state.had_disk_light)
    {
      // Mesh gate band = sea±CHUNK (same idea as streaming commit). Full
      // 0..MaxHeight made MarkRelit expand Dirty across the whole stack.
      const int sea = settings.SeaLevel;
      const int dirty_min = std::max(0, sea - CHUNK_SIZE);
      const int dirty_max =
          std::min(settings.MaxHeight, sea + CHUNK_SIZE * 2);
      world.NotePendingLightBeforeMesh(ground_coord, dirty_min, dirty_max);
    }
    else
    {
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
        const glm::vec3 eye_offset(0.0f, 1.62f, 0.0f);
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
          inv.InitCreativeDefaults();
        }
        if (!had_hotbars || inv.IsPrimaryHotbarEmpty())
        {
          inv.EnsureDefaultHotbar();
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

  std::ofstream file(file_name);
  if (file.is_open())
  {
    file << world_data.dump(4);
    file.close();
  }
}

} // namespace cutum
