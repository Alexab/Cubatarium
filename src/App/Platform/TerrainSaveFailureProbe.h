#pragma once
#include "Blocks/BlockDefinitionStorage.h"
#include "Blocks/BlockRegistry.h"
#include "Core/Progress/IUProgressSink.h"
#include "World/Core/World.h"
#include "World/IO/BinaryChunkSerializer.h"
#include "World/IO/ChunkStorageService.h"
#include "World/IO/TerrainSliceLoad.h"
#include "World/Persistence/WorldPersistence.h"
#include <thread>
#include <filesystem>
#include <fstream>
#include <map>
#include <nlohmann/json.hpp>

namespace cutum
{
// Opt-in native API probe. All fixtures are new subdirectories of a previously
// nonexistent output directory; no live world, config or source save is edited.
inline void RunTerrainSaveFailureProbe()
{
  const char *output = std::getenv("CUBA_FLIGHT_QA_TERRAIN_SAVE");
  if (!output || !*output)
    return;
  namespace fs = std::filesystem;
  using Json = nlohmann::json;
  const auto root = fs::absolute(output).lexically_normal();
  if (fs::exists(root))
    throw std::runtime_error("Terrain save probe refuses existing output");
  fs::create_directories(root);
  Json report = {{"schema", "terrain-save-failure-native.v1"},
                 {"scope", "Actual isolated UWorld cooperative save APIs; no application menu/error rendering or autosave scheduler acceptance"},
                 {"status", "FAIL"}, {"cases", Json::array()}};
  const auto require = [](bool value, const char *message)
  {
    if (!value) throw std::runtime_error(message);
  };
  const auto capture = [](const fs::path &folder)
  {
    std::map<std::string, std::vector<uint8_t>> files;
    for (const auto &entry : fs::recursive_directory_iterator(folder))
      if (entry.is_regular_file())
      {
        std::ifstream file(entry.path(), std::ios::binary);
        files[entry.path().lexically_relative(folder).generic_string()] =
            std::vector<uint8_t>(std::istreambuf_iterator<char>(file), {});
      }
    return files;
  };
  struct Progress final : IUProgressSink
  {
    Json phases = Json::array();
    void Begin(WorldOperationKind) override {}
    void End(bool, const std::string &) override {}
    void Report(const std::string &phase, float, const std::string &, int, int) override
    {
      if (phases.empty() || phases.back() != phase) phases.push_back(phase);
    }
  };
  try
  {
    for (bool io_failure : {false, true})
    {
      Json fence_evidence = Json::object();
      const auto folder = root / (io_failure ? "write-failure" : "incomplete");
      fs::create_directories(folder);
      UWorld world(nullptr, nullptr);
      world.ProceduralTemplate.MinHeight = -64;
      world.ProceduralTemplate.MaxHeight = -17;
      auto definitions = std::make_shared<UBlockDefinitionStorage>();
      BlockDefinition stone;
      stone.Physics = BlockPhysicsProfile::Solid();
      definitions->ReplaceAll({{8, stone}}, {{"stone", 8}});
      world.BlockRegistry = std::make_unique<UBlockRegistry>(nullptr, definitions);
      world.SetWorldFolderPath(folder.string());
      const glm::ivec3 column(0), floor(0, -4, 0);
      const auto deleted = floor * CHUNK_SIZE + glm::ivec3(1, 2, 3);
      const auto added = deleted + glm::ivec3(1, 0, 0);
      UBlockWorld original;
      UBinaryChunkSerializer codec;
      for (int cy = -4; cy <= -2; ++cy)
      {
        const glm::ivec3 coord(0, cy, 0);
        UChunk frame(coord);
        if (cy != -3) frame.SetBlockLocal({1, 2, 3}, 8);
        const auto decoded = codec.Deserialize(
            codec.Serialize(coord, frame, *world.BlockRegistry).bytes,
            coord, *world.BlockRegistry);
        require(ApplyTerrainSliceLoad(original, coord, &decoded, nullptr,
                                     {column, 1}, 1, false), "original decode");
        if (io_failure || cy == -4)
          require(ApplyTerrainSliceLoad(world.BlockWorld, coord, &decoded, nullptr,
                                       {column, 1}, 1, false), "resident decode");
      }
      auto &storage = world.GetChunkStorage();
      require(storage.SaveTerrainColumn(column, original, folder.string(),
                                       *world.BlockRegistry, -17, -64), "fixture save");
      world.BlockWorld.SetBlock(deleted, BLOCK_AIR);
      world.BlockWorld.SetBlock(added, 8);
      world.ModifiedChunks.insert(floor);
      const auto dirty = world.ModifiedChunks;
      const auto incarnation = world.BlockWorld.GetChunkManager().GetChunk(floor)->GetIncarnation();
      std::ofstream(folder / "users.json") << "{\"fixture\":true}\n";
      std::ofstream(folder / "world_data.json") << "{\"fixture\":true}\n";
      const fs::path light = UChunkStorageService::ColumnLightFilePath(folder.string(), column);
      const fs::path blocker = light.string() + ".tmp";
      if (io_failure)
      {
        require(fs::create_directory(blocker), "directory blocks temp-file open");
        std::ofstream(blocker / "fixture-blocker") << "keep this file";
      }
      // Settle fixture index/signature work before the no-write comparison.
      // The save fence is allowed to finish earlier IO; that must not be
      // confused with new writes issued by an incomplete save attempt.
      const auto prepared_by = std::chrono::steady_clock::now() + std::chrono::seconds(10);
      while (!world.Persistence->TickDrainAsyncChunkIo(world, 8))
      {
        require(std::chrono::steady_clock::now() < prepared_by, "fixture IO preparation timeout");
        std::this_thread::yield();
      }
      const auto before = capture(folder);
      Progress failed;
      world.BeginCooperativeSave(folder.string(), false);
      size_t ticks = 0;
      const auto failed_by = std::chrono::steady_clock::now() + std::chrono::seconds(12);
      while (!world.TickCooperativeSave(failed, 1))
      {
        ++ticks;
        require(std::chrono::steady_clock::now() < failed_by, "save failure must become terminal");
        std::this_thread::yield();
      }
      const auto error = world.GetCooperativeSaveError();
      require(!error.empty() && !world.HasActiveCooperativeOperation(), "failure result");
      require(error.find(io_failure ? "Terrain save failed" : "Terrain is incomplete") != std::string::npos,
              "correct incomplete or filesystem failure branch");
      require(before == capture(folder), "failed save changed files");
      require(world.ModifiedChunks == dirty, "failed save cleared dirty state");
      require(world.BlockWorld.GetBlock(deleted) == BLOCK_AIR &&
              world.BlockWorld.GetBlock(added) == 8 &&
              world.BlockWorld.GetChunkManager().GetChunk(floor)->GetIncarnation() == incarnation,
              "failed save lost RAM edit or owner");
      for (const auto &phase : failed.phases)
        require(phase != "done" && phase != "save_metadata", "failure claimed completion");
      world.SaveSessionSnapshotMetadataOnly(folder.string());
      require(world.ModifiedChunks == dirty,
              "metadata-only refresh must not acknowledge terrain edits");
      if (io_failure)
      {
        require(fs::remove(blocker / "fixture-blocker"), "remove only fixture blocker file");
        require(fs::remove(blocker), "remove only empty fixture blocker directory");
      }
      require(storage.LoadTerrainColumn(column, world.BlockWorld, folder.string(),
                                       *world.BlockRegistry, -17, -64) > 0, "recover disk base");
      Progress recovered;
      world.BeginCooperativeSave(folder.string(), false);
      ticks = 0;
      const auto recovered_by = std::chrono::steady_clock::now() + std::chrono::seconds(12);
      while (!world.TickCooperativeSave(recovered, 1))
      {
        ++ticks;
        require(std::chrono::steady_clock::now() < recovered_by, "recovered save must finish");
        std::this_thread::yield();
      }
      require(world.GetCooperativeSaveError().empty() && world.ModifiedChunks.empty(),
              "retry did not reset error or finish saving");
      UBlockWorld cold;
      require(storage.LoadTerrainColumn(column, cold, folder.string(),
                                       *world.BlockRegistry, -17, -64) > 0 &&
              cold.GetBlock(deleted) == BLOCK_AIR && cold.GetBlock(added) == 8,
              "successful retry lost persisted edit");
      // The unload path must wait for real ACKs and reject an ACK for a
      // snapshot which changed while the worker owned its immutable bytes.
      world.ProceduralTemplate.AsyncChunkIo = true;
      world.Persistence->EnsureChunkIoInitialized();
      const auto drain = [&]()
      {
        const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(10);
        while (storage.IsColumnSavePending(column))
        {
          require(std::chrono::steady_clock::now() < deadline, "async save ACK timeout");
          world.Persistence->TickAsyncChunkIo(world, 32, 6.0);
          std::this_thread::yield();
        }
      };
      const auto during = deleted + glm::ivec3(2, 0, 0);
      world.ModifiedChunks.insert(floor);
      require(!world.Persistence->TrySaveTerrainColumnForUnload(world, column) &&
                  storage.IsColumnSavePending(column), "enqueue cannot admit retirement");
      world.BlockWorld.SetBlock(during, 8);
      drain();
      require(
          !world.Persistence->TrySaveTerrainColumnForUnload(world, column) &&
              storage.IsColumnSavePending(column) &&
              world.ModifiedChunks.count(floor),
          "new local edit must invalidate old ACK without clearing dirty "
          "state");
      drain();
      require(world.Persistence->TrySaveTerrainColumnForUnload(world, column) &&
                  world.BlockWorld.GetChunkManager().HasChunk(floor) &&
                  !world.ModifiedChunks.count(floor),
              "only the current completed save admits retirement");
      if (io_failure)
      {
        require(fs::create_directory(blocker), "async temp blocker is owned");
        std::ofstream(blocker / "fixture-blocker") << "keep this file";
        world.BlockWorld.SetBlock(during, BLOCK_AIR);
        world.ModifiedChunks.insert(floor);
        require(!world.Persistence->TrySaveTerrainColumnForUnload(world, column) &&
                    storage.IsColumnSavePending(column), "failed async save starts with RAM retained");
        drain();
        require(
            !world.Persistence->TrySaveTerrainColumnForUnload(world, column) &&
                !storage.IsColumnSavePending(column) &&
                world.BlockWorld.GetBlock(during) == BLOCK_AIR &&
                world.ModifiedChunks.count(floor) &&
                world.BlockWorld.GetChunkManager().HasChunk(floor),
            "failed completed batch cannot authorize retirement");
        require(fs::remove(blocker / "fixture-blocker") && fs::remove(blocker),
                "remove only the owned async blocker");
        const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(10);
        while (!world.Persistence->TrySaveTerrainColumnForUnload(world, column))
        {
          require(std::chrono::steady_clock::now() < deadline, "async recovery timeout");
          world.Persistence->TickAsyncChunkIo(world, 32, 6.0);
          std::this_thread::yield();
        }
      }
      UBlockWorld async_cold;
      require(storage.LoadTerrainColumn(column, async_cold, folder.string(),
                                       *world.BlockRegistry, -17, -64) > 0 &&
                  async_cold.GetBlock(during) == (io_failure ? BLOCK_AIR : 8),
              "actual async recovery persisted the newest edit");
      if (io_failure)
      {
        // A late write to the old folder must update its physical index but
        // cannot acknowledge a save or grant retirement in the new folder.
        world.ProceduralTemplate.MaxHeight = 15;
        for (int cy = -1; cy <= 0; ++cy)
        {
          const glm::ivec3 extra(0, cy, 0);
          UChunk frame(extra);
          if (cy == 0) frame.SetBlockLocal({1, 2, 3}, 8);
          const auto decoded = codec.Deserialize(
              codec.Serialize(extra, frame, *world.BlockRegistry).bytes,
              extra, *world.BlockRegistry);
          require(ApplyTerrainSliceLoad(world.BlockWorld, extra, &decoded, nullptr,
                                       {column, 1}, 1, false), "expanded save fixture");
        }
        require(storage.GetHighestChunkSliceOnDisk(folder.string(), column) == -2,
                "old folder index starts below the new queued ceiling");
        require(!world.Persistence->TrySaveTerrainColumnForUnload(world, column) &&
                    storage.IsColumnSavePending(column), "old-folder save is pending");
        const auto switched = root / "switched-folder";
        world.SetWorldFolderPath(switched.string());
        const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(10);
        while (!world.Persistence->TickDrainAsyncChunkIo(world, 32))
        {
          require(std::chrono::steady_clock::now() < deadline, "old-folder ACK drain timeout");
          std::this_thread::yield();
        }
        require(storage.GetHighestChunkSliceOnDisk(folder.string(), column) == 0,
                "late successful old-folder write updates its own index");
        require(!world.Persistence->TrySaveTerrainColumnForUnload(world, column) &&
                    storage.IsColumnSavePending(column),
                "old-folder ACK cannot grant new-folder retirement");
        drain();
        require(world.Persistence->TrySaveTerrainColumnForUnload(world, column),
                "new folder requires its own complete save");
        // Hold an older immutable terrain write while requesting a newer
        // cooperative save to the same folder. No synchronous phase may pass
        // this active write. RAII releases the worker on every assertion exit.
        auto gate = std::make_shared<AsyncSaveWriteProbeGate>();
        struct ReleaseGate
        {
          std::shared_ptr<AsyncSaveWriteProbeGate> value;
          ~ReleaseGate() { value->released.store(true, std::memory_order_release); }
        } release{gate};
        world.Persistence->AsyncChunkIo->SaveWriteProbeGate = gate;
        world.BlockWorld.SetBlock(during, 8);
        require(!world.Persistence->TrySaveTerrainColumnForUnload(world, column),
                "old snapshot is queued before the newer edit");
        const auto entered_by = std::chrono::steady_clock::now() + std::chrono::seconds(3);
        while (!gate->entered.load(std::memory_order_acquire))
        {
          require(std::chrono::steady_clock::now() < entered_by, "writer did not enter probe gate");
          std::this_thread::yield();
        }
        world.BlockWorld.SetBlock(during, BLOCK_AIR);
        world.ModifiedChunks.insert(floor);
        bool snapshot_refused = false;
        try { world.SaveSessionSnapshot(switched.string(), true); }
        catch (const std::runtime_error &error)
        {
          snapshot_refused = std::string(error.what()).find("Pending terrain I/O") != std::string::npos;
        }
        require(snapshot_refused && world.ModifiedChunks.count(floor) == 1,
                "full snapshot cannot bypass a held older writer");
        Progress fenced;
        world.BeginCooperativeSave(switched.string(), false);
        const bool terminal = world.TickCooperativeSave(fenced, 1);
        require(!terminal && !fenced.phases.empty() &&
                    fenced.phases.back() == "save_waiting_for_io" &&
                    world.ModifiedChunks.count(floor) == 1,
                "cooperative save must wait for active writes");
        gate->released.store(true, std::memory_order_release);
        world.Persistence->AsyncChunkIo->SaveWriteProbeGate.reset();
        const auto saved_by = std::chrono::steady_clock::now() + std::chrono::seconds(10);
        while (!world.TickCooperativeSave(fenced, 1))
        {
          require(std::chrono::steady_clock::now() < saved_by, "fenced save did not finish");
          std::this_thread::yield();
        }
        require(world.GetCooperativeSaveError().empty() && world.ModifiedChunks.empty() &&
                    world.Persistence->IsAsyncChunkIoQuiescent(),
                "new cooperative save must finish after the old batch");
        UBlockWorld final_cold;
        require(storage.LoadTerrainColumn(column, final_cold, switched.string(),
                                         *world.BlockRegistry, 15, -64) > 0 &&
                    final_cold.GetBlock(during) == BLOCK_AIR,
                "old writer cannot overwrite the newer cooperative edit");
        auto slow_gate = std::make_shared<AsyncSaveWriteProbeGate>();
        slow_gate->limit = std::chrono::seconds(20);
        ReleaseGate release_slow{slow_gate};
        world.Persistence->AsyncChunkIo->SaveWriteProbeGate = slow_gate;
        world.BlockWorld.SetBlock(during, 8);
        require(!world.Persistence->TrySaveTerrainColumnForUnload(world, column),
                "timeout probe queues its own older snapshot");
        const auto slow_entered_by = std::chrono::steady_clock::now() + std::chrono::seconds(3);
        while (!slow_gate->entered.load(std::memory_order_acquire))
        {
          require(std::chrono::steady_clock::now() < slow_entered_by, "slow gate not entered");
          std::this_thread::yield();
        }
        world.BlockWorld.SetBlock(during, BLOCK_AIR);
        world.ModifiedChunks.insert(floor);
        Progress timeout_progress;
        world.BeginCooperativeSave(switched.string(), false);
        const auto timed_at = std::chrono::steady_clock::now();
        while (!world.TickCooperativeSave(timeout_progress, 1))
        {
          require(std::chrono::steady_clock::now() - timed_at < std::chrono::seconds(13),
                  "save I/O wait must have a bounded error result");
          std::this_thread::sleep_for(std::chrono::milliseconds(1));
        }
        const double waited_seconds = std::chrono::duration<double>(
            std::chrono::steady_clock::now() - timed_at).count();
        require(waited_seconds >= 9.99 && waited_seconds < 13.0,
                "native timeout must respect the actual ten-second wait bound");
        fence_evidence["timeout_seconds"] = waited_seconds;
        fence_evidence["timeout_phases"] = timeout_progress.phases;
        fence_evidence["fenced_save_phases"] = fenced.phases;
        require(!world.GetCooperativeSaveError().empty() &&
                    world.GetCooperativeSaveError().find("World files are still busy") != std::string::npos &&
                    world.ModifiedChunks.count(floor) == 1 &&
                    !world.Persistence->IsAsyncChunkIoQuiescent(),
                "I/O timeout cannot clear edits or pretend the writer stopped");
        for (const auto &phase : timeout_progress.phases)
          require(phase != "done" && phase != "save_metadata" && phase != "scan",
                  "I/O timeout cannot enter a synchronous save phase");
        slow_gate->released.store(true, std::memory_order_release);
        world.Persistence->AsyncChunkIo->SaveWriteProbeGate.reset();
        const auto drain_by = std::chrono::steady_clock::now() + std::chrono::seconds(10);
        while (!world.Persistence->TickDrainAsyncChunkIo(world, 8))
        {
          require(std::chrono::steady_clock::now() < drain_by, "timeout recovery drain");
          std::this_thread::yield();
        }
        Progress retried;
        world.BeginCooperativeSave(switched.string(), false);
        const auto retry_by = std::chrono::steady_clock::now() + std::chrono::seconds(10);
        while (!world.TickCooperativeSave(retried, 1))
        {
          require(std::chrono::steady_clock::now() < retry_by, "timeout recovery save");
          std::this_thread::yield();
        }
        UBlockWorld retry_cold;
        require(world.GetCooperativeSaveError().empty() && world.ModifiedChunks.empty() &&
                    world.Persistence->IsAsyncChunkIoQuiescent() &&
                    storage.LoadTerrainColumn(column, retry_cold, switched.string(),
                                              *world.BlockRegistry, 15, -64) > 0 &&
                    retry_cold.GetBlock(during) == BLOCK_AIR,
                "retry after I/O timeout must persist the newer edit");
        Progress late;
        world.BeginCooperativeSave(switched.string(), false);
        const auto chunks_by = std::chrono::steady_clock::now() + std::chrono::seconds(10);
        while (std::find(late.phases.begin(), late.phases.end(), "chunks") == late.phases.end())
        {
          require(std::chrono::steady_clock::now() < chunks_by &&
                      !world.TickCooperativeSave(late, 1), "late-edit save reaches terrain phase");
          std::this_thread::yield();
        }
        world.BlockWorld.SetBlock(during, 8);
        world.ModifiedChunks.insert(floor);
        const glm::ivec3 late_column(1, 0, 0), late_floor(1, -4, 0);
        for (int cy = -4; cy <= 0; ++cy)
        {
          const glm::ivec3 extra(1, cy, 0);
          UChunk frame(extra);
          if (cy == -4) frame.SetBlockLocal({3, 2, 3}, 8);
          const auto decoded = codec.Deserialize(
              codec.Serialize(extra, frame, *world.BlockRegistry).bytes,
              extra, *world.BlockRegistry);
          require(ApplyTerrainSliceLoad(world.BlockWorld, extra, &decoded, nullptr,
                                       {late_column, 1}, 1, false), "late new-column fixture");
        }
        const auto late_by = std::chrono::steady_clock::now() + std::chrono::seconds(10);
        while (!world.TickCooperativeSave(late, 1))
        {
          require(std::chrono::steady_clock::now() < late_by, "late-edit completion bound");
          std::this_thread::yield();
        }
        require(world.ModifiedChunks.count(floor) && world.ModifiedChunks.count(late_floor),
                "save cannot acknowledge post-write edits or unlisted new columns");
        require(!world.GetCooperativeSaveError().empty(),
                "exit save must refuse to discard newer unsaved data");
        fence_evidence["late_exit_phases"] = late.phases;
        Progress latest;
        world.BeginCooperativeSave(switched.string(), false);
        const auto latest_by = std::chrono::steady_clock::now() + std::chrono::seconds(10);
        while (!world.TickCooperativeSave(latest, 1))
        {
          require(std::chrono::steady_clock::now() < latest_by, "late-edit retry bound");
          std::this_thread::yield();
        }
        UBlockWorld late_cold;
        require(world.GetCooperativeSaveError().empty() && world.ModifiedChunks.empty() &&
                    storage.LoadTerrainColumn(column, late_cold, switched.string(),
                                              *world.BlockRegistry, 15, -64) > 0 &&
                    storage.LoadTerrainColumn(late_column, late_cold, switched.string(),
                                              *world.BlockRegistry, 15, -64) > 0 &&
                    late_cold.GetBlock(during) == 8 &&
                    late_cold.GetBlock({19, -62, 3}) == 8,
                "retry persists both newer edit and unlisted column");
        // A resumable save may finish its cohort, but newer changes must be
        // retained for the following save rather than silently acknowledged.
        Progress background;
        world.BeginCooperativeSave(switched.string(), true);
        const auto background_by =
            std::chrono::steady_clock::now() + std::chrono::seconds(10);
        while (std::find(background.phases.begin(), background.phases.end(),
                         "save_metadata") == background.phases.end())
        {
          require(std::chrono::steady_clock::now() < background_by &&
                      !world.TickCooperativeSave(background, 1),
                  "resumable save reaches metadata");
          std::this_thread::yield();
        }
        world.BlockWorld.SetBlock(during, BLOCK_AIR);
        world.ModifiedChunks.insert(floor);
        const glm::ivec3 background_column(2, 0, 0), background_floor(2, -4, 0);
        for (int cy = -4; cy <= 0; ++cy)
        {
          const glm::ivec3 extra(2, cy, 0);
          UChunk frame(extra);
          if (cy == -4)
            frame.SetBlockLocal({3, 2, 3}, 8);
          const auto decoded = codec.Deserialize(
              codec.Serialize(extra, frame, *world.BlockRegistry).bytes, extra,
              *world.BlockRegistry);
          require(ApplyTerrainSliceLoad(world.BlockWorld, extra, &decoded,
                                        nullptr, {background_column, 1}, 1,
                                        false),
                  "background new-column fixture");
        }
        while (!world.TickCooperativeSave(background, 1))
        {
          require(std::chrono::steady_clock::now() < background_by,
                  "resumable save completion bound");
          std::this_thread::yield();
        }
        UBlockWorld background_cold;
        require(
            world.GetCooperativeSaveError().empty() &&
                world.ModifiedChunks.count(floor) &&
                world.ModifiedChunks.count(background_floor) &&
                storage.LoadTerrainColumn(column, background_cold,
                                          switched.string(),
                                          *world.BlockRegistry, 15, -64) > 0 &&
                background_cold.GetBlock(during) == 8 &&
                !fs::exists(storage.ChunkFilePath(switched.string(),
                                                  background_floor,
                                                  ChunkDiskFormat::Binary)),
            "resumable save retains newer edits outside its persisted cohort");
        Progress background_retry;
        world.BeginCooperativeSave(switched.string(), true);
        const auto background_retry_by =
            std::chrono::steady_clock::now() + std::chrono::seconds(10);
        while (!world.TickCooperativeSave(background_retry, 1))
        {
          require(std::chrono::steady_clock::now() < background_retry_by,
                  "resumable retry bound");
          std::this_thread::yield();
        }
        UBlockWorld background_retry_cold;
        require(world.GetCooperativeSaveError().empty() &&
                    world.ModifiedChunks.empty() &&
                    storage.LoadTerrainColumn(
                        column, background_retry_cold, switched.string(),
                        *world.BlockRegistry, 15, -64) > 0 &&
                    storage.LoadTerrainColumn(
                        background_column, background_retry_cold,
                        switched.string(), *world.BlockRegistry, 15, -64) > 0 &&
                    background_retry_cold.GetBlock(during) == BLOCK_AIR &&
                    background_retry_cold.GetBlock({35, -62, 3}) == 8,
                "resumable retry persists deletion and new column");
        fence_evidence["resumable_late_edit_phases"] = background.phases;
      }
      report["cases"].push_back({{"kind", io_failure ? "write_failure" : "incomplete"},
          {"status", "PASS"}, {"failure", error}, {"failure_phases", failed.phases},
          {"recovery_phases", recovered.phases}, {"files_preserved", before.size()},
          {"dirty_preserved", dirty.size()}, {"cold_edit_preserved", true}, {"unload_waited_for_ack", true},
          {"newer_edit_invalidated_ack", true}, {"async_failure_retained", io_failure},
          {"async_cold_edit_preserved", true}, {"folder_switch_checked", io_failure}, {"cooperative_write_fence_checked", io_failure},
          {"metadata_only_preserved_dirty", true}, {"snapshot_fence_checked", io_failure},
          {"io_timeout_and_retry_checked", io_failure}, {"write_fence_evidence", fence_evidence}, {"late_edit_and_column_checked", io_failure}});
    }
    report["status"] = "PASS";
  }
  catch (const std::exception &error) { report["error"] = error.what(); }
  std::ofstream(root / "report.json") << report.dump(2) << '\n';
}
} // namespace cutum
