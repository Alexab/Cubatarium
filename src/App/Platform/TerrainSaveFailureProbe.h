#pragma once
#include "App/Core.h"
#include "App/WorldOperationRunner.h"
#include "Blocks/BlockDefinitionStorage.h"
#include "Blocks/BlockRegistry.h"
#include "Core/Progress/IUProgressSink.h"
#include "Core/Memory/StlSourceStorage.h"
#include "World/Core/World.h"
#include "World/Core/RuntimeTuning.h"
#include "World/Core/FluidColumnSurfaceQuery.h"
#include "World/IO/BinaryChunkSerializer.h"
#include "World/IO/ChunkStorageService.h"
#include "World/IO/TerrainSliceLoad.h"
#include "World/Persistence/WorldPersistence.h"
#include "World/Physics/WorldChunkDirtyService.h"
#include "World/Physics/WorldBlockPhysicsService.h"
#include "World/Physics/ChunkPhysicsSeed.h"
#include "World/Lighting/IULightingPipeline.h"
#include "World/Lighting/LightCacheCertificate.h"
#include "World/Lighting/AsyncRelightBuilder.h"
#include "World/Lighting/RelightInputValidation.h"
#include "World/Lighting/RelightResultInstall.h"
#include "World/Math/GridMath.h"
#include "World/Mesh/WorldMeshService.h"
#include "World/Streaming/WorldStreaming.h"
#include "World/Streaming/StreamerAmortizePolicy.h"
#include "World/Chunks/ChunkLoadScheduler.h"
#include "World/Chunks/TerrainColumnUtil.h"
#include "World/Streaming/ColumnFlowExecutor.h"
#include "World/Streaming/ColumnRecordCoordinator.h"
#include "World/Streaming/ChunkEmergeCoordinator.h"
#include "Render/Mesh/GpuFluidColumnScan.h"
#include "Render/Lighting/SectionRenderLightBindings.h"
#include "Render/Camera/Frustum.h"
#include "Render/Backend/RenderBackendFactory.h"
#include "Render/Mesh/GpuGreedyMesher.h"
#include "Render/Mesh/GpuMeshPipeline.h"
#include "Core/FrameDeadline.h"
#include "Core/Jobs/JobThreadBudget.h"
#include "WorldGen/Core/WorldGenContext.h"
#include "WorldGen/Core/CatalogStorageCensus.h"
#include "WorldGen/Core/CatalogRuntimeDomain.h"
#include "WorldGen/Core/SourceInputStorageCensus.h"
#include "WorldGen/Core/CatalogFilesystemSourceEnvelope.h"
#include "WorldGen/Core/CatalogCandidateSourceEnvelope.h"
#include "WorldGen/Core/RuntimeCoreInlineLayout.h"
#include "Core/Memory/GlogResourceSource.h"
#include "Core/Memory/AllocatorOwnerSource.h"
#include "Test/AllocatorOwnerGraphFixture.h"
#include "Test/AllocatorWorkerThreadFixture.h"
#include "Test/AllocatorReturnedControlsFixture.h"
#include "Test/AsyncIoStorageFixture.h"
#include "App/Platform/StreamingSchedulerReinitProbe.h"
#include "Test/Native/TextureAtlasConstructionProbe.h"
#include "WorldGen/Core/CatalogJsonSource.h"
#include <filesystem>
#include <atomic>
#include <fstream>
#include <future>
#include <map>
#include <nlohmann/json.hpp>
#include <thread>

namespace cutum
{
template <typename Phase>
#if defined(_MSC_VER)
__declspec(noinline)
#elif defined(__GNUC__)
__attribute__((noinline))
#endif
inline void RunTerrainProbePhase(Phase phase)
{
  phase();
}

#if defined(_MSC_VER)
__declspec(noinline)
#elif defined(__GNUC__)
__attribute__((noinline))
#endif
inline bool WaitForProbeMeshCapture(UChunkMeshCache &cache,UBlockWorld &world,
                                    UBlockRegistry &registry,glm::ivec3 target,
                                    uint64_t revision)
{
  cache.PrefetchMeshCapture(world,target,&registry);
  const auto deadline=std::chrono::steady_clock::now()+std::chrono::seconds(5);
  do {
    cache.PumpCaptureWorkerCommits(&world,&registry);
    if(cache.GetCaptureStore().TryGet(world,target,revision).has_value()) return true;
    std::this_thread::yield();
  } while(std::chrono::steady_clock::now()<deadline);
  return false;
}
// Opt-in native API probe. All fixtures are new subdirectories of a previously
// nonexistent output directory; no live world, config or source save is edited.
inline void RunTerrainSaveFailureProbe(UCore &core)
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
  Json report = {
      {"schema", "terrain-save-failure-native.v1"},
      {"scope", "Actual isolated UWorld cooperative saves, Core full-save "
                "facade and WorldOperationRunner; no application menu/error "
                "rendering or autosave scheduler acceptance"},
      {"status", "FAIL"},
      {"cases", Json::array()}};
  const auto require = [&](bool value, const char *message)
  {
    if (!value) {
      report["error"]=message;
      std::ofstream(root/"report.json") << report.dump(2) << '\n';
      throw std::runtime_error(message);
    }
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
    report["allocator_owner_graph"]=RunAllocatorOwnerGraphProbe();
    report["allocator_worker_threads"]=RunAllocatorWorkerThreadProbe();
    report["allocator_returned_controls"]=RunAllocatorReturnedControlsProbe();
    report.update(RunStreamingSchedulerReinitProbe(root / "generation-replacement"));
    report["async_io_storage"] = RunAsyncIoStorageProbe(root / "async-io-storage");
    RunTerrainProbePhase([&] {
      using namespace std::chrono_literals;
      auto owner = std::make_unique<UWorld>(nullptr, nullptr);
      auto &world = *owner;
      auto definitions = std::make_shared<UBlockDefinitionStorage>();
      BlockDefinition stone;
      stone.Name = "stone";
      stone.Physics = BlockPhysicsProfile::Solid();
      definitions->ReplaceAll({{8, stone}}, {{"stone", 8}});
      world.BlockRegistry = std::make_unique<UBlockRegistry>(nullptr, definitions);
      world.BlockWorld.SetFluidDefinitions(definitions.get());
      ProceduralSettings settings;
      settings.MinHeight = -64;
      settings.MaxHeight = 319;
      settings.AsyncChunkIo = true;
      settings.AsyncRelight = false;
      settings.AsyncChunkGeneration = true;
      world.SetSpawnPoint({8, -63, 8});
      world.SetProceduralSettings(settings, false);
      world.Streaming->InitStreamerCallbacks(world);
      auto &persistence = *world.Persistence;
      auto &storage = persistence.GetChunkStorage();
      auto &streamer = *world.Streaming->GetStreamer();
      const auto folder = root / "read-admission";
      fs::create_directories(folder / "chunks");
      UBinaryChunkSerializer codec;
      const auto write_slice = [&](glm::ivec3 coord) {
        UChunk source(coord);
        if (coord.y >= 19)
          source.SetBlockLocal({1, 1, 1}, 8);
        const auto frame = codec.Serialize(coord, source, *world.BlockRegistry);
        require(storage.WriteBytesAtomically(storage.ChunkFilePath(folder.string(), coord, frame.format),
                                             frame.bytes), "write real terrain slice for load limit");
      };
      constexpr auto columns = UWorldPersistence::kMaxPendingTerrainColumnLoads;
      constexpr auto reads = UAsyncChunkIO::kMaxOutstandingLoads;
      const glm::ivec3 target(0);
      const glm::ivec3 outside_profile(static_cast<int>(columns + 1), 0, 0);
      for (size_t i = 0; i <= columns; ++i)
        for (int cy = -4; cy <= 19; ++cy)
          write_slice({static_cast<int>(i), cy, 0});
      write_slice({outside_profile.x, 9000, outside_profile.z});
      world.SetWorldFolderPath(folder.string());
      auto &io = *persistence.AsyncChunkIo;
      for (size_t i = 1; i <= columns; ++i)
        require(persistence.RequestAsyncTerrainColumnLoad(world, {static_cast<int>(i), 0, 0}, i) ==
                    TerrainColumnRead::Pending, "admitted column keeps its remaining slices");
      const auto &retained = persistence.PendingAsyncColumnLoadSlices;
      const auto original = retained.at({1, 0, 0}).cancellation;
      require(retained.size() == columns && io.GetOutstandingLoadCount() == reads &&
                  persistence.RequestAsyncTerrainColumnLoad(world, {1, 7, 0}, 5000) == TerrainColumnRead::Pending &&
                  retained.at({1, 0, 0}).cancellation == original && retained.at({1, 0, 0}).demand_id == 1,
              "both production limits saturate and coalescing preserves the admitted owner");
      bool accepted_at_capacity = true;
      require(persistence.RequestAsyncTerrainColumnLoad(world, target, 6000) == TerrainColumnRead::Deferred &&
                  !streamer.EnsureChunkLoaded(target, true, &accepted_at_capacity) && !accepted_at_capacity &&
                  !world.Streaming->GetChunkScheduler()->IsPending(target) && !retained.count(target),
              "streamer leaves saturated saved terrain with its demand, without procedural fallback");

      const std::vector<glm::ivec3> demand{target};
      const auto deadline = std::chrono::steady_clock::now() + 15s;
      bool target_admitted = false;
      size_t peak_reads = 0;
      size_t peak_columns = 0;
      while (!target_admitted || !retained.empty() || io.GetOutstandingLoadCount())
      {
        persistence.TickAsyncChunkIo(world, 48, 20.0);
        if (!target_admitted)
        {
          streamer.EnsureDataColumns(demand, 1);
          if (const auto it = retained.find(target); it != retained.end())
          {
            require(it->second.highest_cy_on_disk == 19 && it->second.remaining_results == 24,
                    "retry keeps the complete active signed profile");
            target_admitted = true;
          }
        }
        peak_reads = std::max(peak_reads, io.GetOutstandingLoadCount());
        peak_columns = std::max(peak_columns, retained.size());
        require(peak_reads <= reads && peak_columns <= columns && std::chrono::steady_clock::now() < deadline,
                "retained streaming demand drains within both limits");
        std::this_thread::yield();
      }
      require(world.BlockWorld.GetBlock({1, 19 * CHUNK_SIZE + 1, 1}) == 8 &&
                  IsTerrainChunkComplete(world.BlockWorld, target, 319, 19, -64) &&
                  !world.Streaming->GetChunkScheduler()->IsPending(target),
              "normal streaming replay installs the original saved terrain");
      require(persistence.RequestAsyncTerrainColumnLoad(world, outside_profile) == TerrainColumnRead::Pending &&
                  retained.at(outside_profile).remaining_results == 24 && io.GetOutstandingLoadCount() <= 24,
              "out-of-profile filename cannot expand the bounded active read range");
      persistence.CancelAsyncTerrainColumnLoad(world, outside_profile);
      require(io.WaitIdleFor(5s), "cancelled out-of-profile column releases its active jobs");
      io.DiscardCancelledLoads();
      require(io.GetOutstandingLoadCount() == 0 && retained.empty() &&
                  storage.GetHighestChunkSliceOnDisk(folder.string(), outside_profile) == 9000,
              "cancellation retires read credits and leaves the out-of-profile file in place");
      report["bounded_disk_load_admission"] = {{"status", "PASS"}, {"slice_limit", reads},
          {"column_limit", columns}, {"deferred_streamer_recovery", target_admitted},
          {"out_of_profile_bounded", true}, {"final_outstanding", io.GetOutstandingLoadCount()},
          {"final_columns", retained.size()}};

      constexpr auto writes = UAsyncChunkIO::kMaxOutstandingWrites;
      constexpr size_t target_writes = 25;
      const glm::ivec3 missing(300, 19, 300);
      for (size_t i = 0; i < writes - target_writes + 1; ++i)
        require(io.RequestSave(missing, storage, folder.string(), world.BlockWorld,
                               *world.BlockRegistry, {missing, 1}, i + 1),
                "produce a real retained save result for a missing snapshot");
      auto held_writes = io.DrainSaves();
      const glm::ivec3 edited(2, 19 * CHUNK_SIZE + 1, 2), edited_slice(0, 19, 0);
      world.BlockWorld.SetBlock(edited, 8);
      world.ModifiedChunks.insert(edited_slice);
      require(held_writes.size() == writes - target_writes + 1 &&
                  io.GetOutstandingWriteCount() == held_writes.size() &&
                  !persistence.TrySaveTerrainColumnForUnload(world, target) &&
                  !storage.IsColumnSavePending(target) &&
                  !persistence.PendingAsyncColumnSaveSlices.count(target) &&
                  !persistence.UnloadSaveReceipts.count(target) &&
                  world.ModifiedChunks.count(edited_slice) && world.BlockWorld.GetBlock(edited) == 8,
              "insufficient capacity leaves the whole column and edit dirty");
      held_writes.pop_back();
      auto gate = std::make_shared<AsyncSaveWriteProbeGate>();
      io.SaveWriteProbeGate = gate;
      const bool unloaded = persistence.TrySaveTerrainColumnForUnload(world, target);
      const auto write_deadline = std::chrono::steady_clock::now() + 3s;
      while (!gate->entered.load(std::memory_order_acquire) && std::chrono::steady_clock::now() < write_deadline)
        std::this_thread::yield();
      const bool bounded_while_active = gate->entered.load(std::memory_order_acquire) &&
          io.GetPendingJobCount() > 0 && io.GetActiveJobCount() > 0 &&
          io.GetOutstandingWriteCount() == writes;
      gate->released.store(true, std::memory_order_release);
      io.SaveWriteProbeGate.reset();
      require(!unloaded && storage.IsColumnSavePending(target) && bounded_while_active &&
                  world.ModifiedChunks.count(edited_slice),
              "one released slot admits the complete batch through queued and active writes");
      require(io.WaitIdleFor(8s) && io.GetSaveResultQueueDepth() == target_writes &&
                  io.GetOutstandingWriteCount() == writes, "every admitted terrain write publishes an ACK");
      persistence.TickAsyncChunkIo(world);
      require(!storage.IsColumnSavePending(target) && world.ModifiedChunks.count(edited_slice) &&
                  persistence.TrySaveTerrainColumnForUnload(world, target) &&
                  !world.ModifiedChunks.count(edited_slice),
              "all matching ACKs permit the original unload path to acknowledge the edit");
      held_writes.clear();
      UBlockWorld cold;
      cold.SetVerticalProfile(settings.GetVerticalProfile());
      require(io.GetOutstandingWriteCount() == 0 && persistence.PendingAsyncColumnSaveSlices.empty() &&
                  persistence.UnloadSaveReceipts.empty() &&
                  storage.LoadTerrainColumn(target, cold, folder.string(), *world.BlockRegistry, 319, -64) > 0 &&
                  cold.GetBlock(edited) == 8,
              "cold replay preserves the edited terrain and all write reservations retire");

      // The temporary owner travels with a real returned result from a
      // separate fixture folder. WorldPersistence must keep its own revision.
      persistence.ProcessColumnLightFlagSaveResults();
      require(io.WaitForColumnLightFlagsSaveIdleFor(5s), "prior world flag write reaches its ACK");
      persistence.ProcessColumnLightFlagSaveResults();
      const auto foreign_flags = (root / "write-admission-foreign-flags").string();
      require(io.RequestSaveColumnLightFlags(foreign_flags, 9001, {{5, 6}}, "foreign") &&
                  io.WaitForColumnLightFlagsSaveIdleFor(5s), "foreign flags produce a retained ACK");
      auto flags = io.DrainColumnLightFlagsSaves();
      require(flags.size() == 1 && flags[0].success && persistence.EnsureLightCacheSignatureCurrent(),
              "separate flag reservation survives completed delivery");
      const glm::ivec2 flagged(12, 19);
      persistence.LightCompleteColumns.insert(flagged);
      persistence.LightCompleteDirty = true;
      ++persistence.LightCompleteRevision;
      persistence.SaveColumnLightFlagsIfDirty();
      require(!persistence.LightCompleteSaveInFlight && persistence.LightCompleteDirty &&
                  io.GetOutstandingFlagSaveCount() == 1,
              "flag saturation leaves the newest world revision dirty before copying it");
      flags.clear();
      persistence.SaveColumnLightFlagsIfDirty();
      require(persistence.LightCompleteSaveInFlight && io.WaitForColumnLightFlagsSaveIdleFor(5s),
              "the next flag save accepts the retained revision");
      persistence.ProcessColumnLightFlagSaveResults();
      require(!persistence.LightCompleteDirty && !persistence.LightCompleteSaveInFlight &&
                  io.GetOutstandingFlagSaveCount() == 0, "current flag revision ACK retires its reservation");
      persistence.LoadColumnLightFlags();
      require(persistence.LightCompleteLoaded && persistence.LightCompleteColumns.count(flagged) &&
                  persistence.LightCompleteSignature == persistence.CurrentLightCacheSignature(),
              "cold flag replay restores the deferred entry with the current signature");
      require(!persistence.IsColumnLightComplete(flagged),
              "a persisted flag alone does not certify live light coverage");
      const auto flags_path = folder / "column_light.json";
      std::string committed_flags;
      {
        std::ifstream file(flags_path, std::ios::binary);
        committed_flags.assign(std::istreambuf_iterator<char>(file), std::istreambuf_iterator<char>());
      }
      const auto install_flags = [&](std::string_view text) {
        std::ofstream file(flags_path, std::ios::binary | std::ios::trunc);
        file.write(text.data(), static_cast<std::streamsize>(text.size()));
        file.close();
        require(file.good(), "install physical flag reader control");
      };
      std::vector<glm::ivec2> large_flags{flagged};
      for (int n = 0; n < 1024; ++n)
        large_flags.emplace_back(-1 - n, 23);
      const auto grown_flags = EncodeColumnLightCache(std::move(large_flags), persistence.LightCompleteSignature).dump();
      install_flags(grown_flags);
      persistence.LoadColumnLightFlags();
      const auto grown_buckets = persistence.LightCompleteColumns.bucket_count();
      StlSourceStorage grown_storage;
      grown_storage.Hash(persistence.LightCompleteColumns);
      require(persistence.LightCompleteColumns.size() == 1025 && persistence.LightCompleteColumns.count(flagged) &&
                  !persistence.IsColumnLightComplete(flagged) && grown_buckets >= 1025,
              "physical flags publish their set without granting live coverage");
      install_flags(grown_flags + '\0' + "trailing");
      persistence.LoadColumnLightFlags();
      const auto reset_buckets = persistence.LightCompleteColumns.bucket_count();
      require(persistence.LightCompleteColumns.empty() && persistence.LightCompleteSignature.empty() &&
                  reset_buckets < grown_buckets,
              "a refused physical flag result retires the old set and its bucket high water");
      install_flags(committed_flags);
      persistence.LoadColumnLightFlags();
      require(persistence.LightCompleteColumns.count(flagged) && !persistence.IsColumnLightComplete(flagged),
              "the original physical flags reload after refusal");
      report["flag_read_publication"] = {{"status", "PASS"}, {"published", 1025},
          {"grown_buckets", grown_buckets}, {"reset_buckets", reset_buckets},
          {"grown_requested_heap", grown_storage.bytes.requested}, {"heap_supported", grown_storage.bytes.supported},
          {"invalid_tail_refused", true}, {"retry", true}};
      report["bounded_disk_write_admission"] = {{"status", "PASS"}, {"write_limit", writes},
          {"required_writes", target_writes}, {"rejected_with_free_slots", target_writes - 1},
          {"bounded_while_active", bounded_while_active}, {"cold_edit", true}, {"deferred_flag_revision", true},
          {"final_writes", io.GetOutstandingWriteCount()}, {"final_flags", io.GetOutstandingFlagSaveCount()}};

      constexpr auto owner_limit = UWorldPersistence::kMaxTerrainReadOwners;
      auto &cooldowns = persistence.FailedColumnRetryAfter;
      const auto fixture_deadline = std::chrono::steady_clock::now() + 60s;
      for (size_t i = 1; i < owner_limit; ++i)
        cooldowns.emplace(glm::ivec3(static_cast<int>(i), 0, 3100), fixture_deadline);
      require(persistence.RequestAsyncTerrainColumnLoad(world, outside_profile) == TerrainColumnRead::Pending &&
                  persistence.RequestAsyncTerrainColumnLoad(world, target) == TerrainColumnRead::Deferred &&
                  persistence.RequestAsyncTerrainColumnLoad(world, missing) == TerrainColumnRead::Absent &&
                  retained.size() + cooldowns.size() == owner_limit,
              "an active failed-read candidate reserves its timer without blocking a disk miss");
      const auto retry_deadline = std::chrono::steady_clock::now() + 10s;
      while (!retained.empty())
      {
        persistence.TickAsyncChunkIo(world, 48, 20.0);
        require(retained.size() + cooldowns.size() <= owner_limit &&
                    std::chrono::steady_clock::now() < retry_deadline,
                "real retry plans and cooldowns share one owner budget");
        std::this_thread::yield();
      }
      auto &manager = world.BlockWorld.GetChunkManager();
      require(cooldowns.size() == owner_limit && cooldowns.count(outside_profile) &&
                  manager.HasUnresolvedTerrainColumnRead(outside_profile) &&
                  persistence.RequestAsyncTerrainColumnLoad(world, outside_profile) == TerrainColumnRead::Deferred &&
                  persistence.RequestAsyncTerrainColumnLoad(world, target) == TerrainColumnRead::Deferred,
              "retry exhaustion retains the exact failed column under full owner admission");
      for (auto &[column, expiry] : cooldowns)
        expiry = std::chrono::steady_clock::now();
      persistence.TickAsyncChunkIo(world);
      require(cooldowns.empty() && manager.HasUnresolvedTerrainColumnRead(outside_profile) &&
                  persistence.RequestAsyncTerrainColumnLoad(world, target) == TerrainColumnRead::Pending,
              "expiry retires timers and restores admission without clearing protection of unread terrain");
      persistence.CancelAsyncTerrainColumnLoad(world, target);
      require(io.WaitIdleFor(5s), "all load attempts drain before the next fixture");
      io.DiscardCancelledLoads();

      auto warmup_gate = std::make_shared<AsyncSaveWriteProbeGate>();
      const auto pool_workers = io.BackgroundIoPool ? 1 : io.LoadPool.GetWorkerCount();
      auto entered = std::make_shared<std::atomic<size_t>>(0);
      for (size_t i = 0; i < pool_workers; ++i)
        io.EnqueueBackgroundIo([warmup_gate, entered] {
          ++*entered;
          const auto until = std::chrono::steady_clock::now() + 6s;
          while (!warmup_gate->released.load() && std::chrono::steady_clock::now() < until)
            std::this_thread::sleep_for(1ms);
        });
      const auto park_deadline = std::chrono::steady_clock::now() + 3s;
      while (entered->load() < pool_workers && std::chrono::steady_clock::now() < park_deadline)
        std::this_thread::yield();
      require(entered->load() == pool_workers, "park the warmup pool");
      const auto past = (root / "warmup-past").string();
      const auto skipped = (root / "warmup-skipped").string();
      const auto current = (root / "warmup-current").string();
      world.SetWorldFolderPath(past);
      require(io.GetOutstandingIndexWarmupCount() == 1 && !persistence.DiskIndexWarmupPending,
              "the first world owns the available warmup slot");
      world.SetWorldFolderPath(skipped);
      world.SetWorldFolderPath(current);
      const bool coalesced = persistence.DiskIndexWarmupPending && io.GetOutstandingIndexWarmupCount() == 1;
      warmup_gate->released.store(true);
      require(coalesced && io.WaitIdleFor(5s) && storage.InspectDiskIndexStorage().folders == 2,
              "rapid world changes leave only the last deferred warmup");
      persistence.TickAsyncChunkIo(world);
      require(!persistence.DiskIndexWarmupPending && io.WaitIdleFor(5s) &&
                  io.GetOutstandingIndexWarmupCount() == 0 && storage.InspectDiskIndexStorage().folders == 3,
              "the current world warms after the old job retires");
      report["bounded_read_retry_and_warmup"] = {{"status", "PASS"}, {"owner_limit", owner_limit},
          {"terminal_failure_retained", true}, {"expired_timers_retired", true},
          {"unread_terrain_protected", true}, {"rapid_world_changes_coalesced", true},
          {"cached_folders", storage.InspectDiskIndexStorage().folders},
          {"final_warmups", io.GetOutstandingIndexWarmupCount()}};
    });
    RunTerrainProbePhase([&] {
      struct RestoreFramePolicy
      {
        int unload_mode = URuntimeTuning::Get().UnloadAmortizeMode;
        UFrameDeadline deadline = UFrameDeadline::Get();
        ~RestoreFramePolicy()
        {
          URuntimeTuning::Get().UnloadAmortizeMode = unload_mode;
          UFrameDeadline::Get() = deadline;
        }
      } restore;
      URuntimeTuning::Get().UnloadAmortizeMode = kUnloadAmortizeU0;
      auto owner = std::make_unique<UWorld>(nullptr, nullptr);
      auto &world = *owner;
      auto definitions = std::make_shared<UBlockDefinitionStorage>();
      BlockDefinition stone;
      stone.Name = "stone";
      stone.Physics = BlockPhysicsProfile::Solid();
      definitions->ReplaceAll({{8, stone}}, {{"stone", 8}});
      world.BlockRegistry = std::make_unique<UBlockRegistry>(nullptr, definitions);
      ProceduralSettings settings;
      settings.MinHeight = -48;
      settings.MaxHeight = -33;
      settings.SeaLevel = -40;
      settings.AsyncChunkGeneration = false;
      settings.AsyncChunkIo = false;
      settings.AsyncRelight = false;
      world.SetSpawnPoint({8, -45, 8});
      world.SetProceduralSettings(settings, false);
      world.Streaming->InitStreamerCallbacks(world);
      auto &streamer = *world.Streaming->GetStreamer();
      streamer.SetKeepPrefetchMargin(0);
      streamer.SetRenderDistance(1);
      streamer.SetRequiredDataDomain({0, 0, 0}, 0);
      streamer.SetMaxLoadOpsPerFrame(2);
      streamer.SetMaxUnloadOpsPerFrame(1);
      const auto limit = streamer.GetTerrainColumnAdmissionLimit();
      require(limit == 50, "physical residency fixture uses the configured keep and consumer footprints");
      const auto folder = root / "resident-save-admission";
      fs::create_directories(folder / "chunks");
      world.SetWorldFolderPath(folder.string());
      auto &storage = world.Persistence->GetChunkStorage();
      const glm::ivec3 first(-100, -3, 3), second(-99, -3, 3), next(0, -3, 0);
      const glm::ivec3 next_ground(next.x, 0, next.z);
      auto &manager = world.BlockWorld.GetChunkManager();
      for (int i = 0; i < static_cast<int>(limit); ++i)
      {
        const auto coord = first + glm::ivec3(i, 0, 0);
        for (int x = 0; x < CHUNK_SIZE; ++x)
          for (int z = 0; z < CHUNK_SIZE; ++z)
            world.BlockWorld.SetBlock(coord * CHUNK_SIZE + glm::ivec3(x, 0, z), 8);
        world.SetColumnEmergeState({coord.x, 0, coord.z}, ColumnEmergeState::Lighting);
      }
      world.ModifiedChunks.insert(first);
      world.ModifiedChunks.insert(second);
      const auto held_record = glm::ivec2(first.x, first.z);
      const auto record_count = world.GetColumnRecords().Size();
      UChunk incoming(next);
      incoming.SetBlockLocal({1, 0, 1}, 8);
      require(storage.SaveChunk(next, incoming, folder.string(), *world.BlockRegistry),
              "prepare a genuine disk load for the blocked successor");
      const auto blocked_file = fs::path(storage.ChunkFilePath(folder.string(), first, ChunkDiskFormat::Binary) + ".tmp");
      fs::create_directory(blocked_file);
      const auto obstacle = blocked_file / "fixture.txt";
      std::ofstream(obstacle).put('0');
      const glm::ivec3 feet(8, -48, 8);
      const glm::vec3 eye(8, -45, 8);
      UFrameDeadline::Get().BeginFrame(0);
      streamer.UnloadPass(feet, eye, PlayerCapsule{});
      streamer.EnsureDataColumns({next_ground}, 1);
      require(streamer.GetLastFrameStats().unloadVetoesThisFrame == 1 &&
                  manager.GetResidentTerrainColumnCount() == limit && !manager.HasChunk(next) &&
                  world.GetColumnRecords().Size() == record_count && world.GetColumnRecords().Find(held_record) &&
                  world.ModifiedChunks.count(first) && world.BlockWorld.GetBlock(first * CHUNK_SIZE) == 8,
              "physical save failure keeps the exact edit and record and defers the cold column");
      streamer.UnloadPass(feet, eye, PlayerCapsule{});
      require(!manager.HasChunk(second) && manager.HasChunk(first) && !world.ModifiedChunks.count(second) &&
                  fs::exists(storage.ChunkFilePath(folder.string(), second, ChunkDiskFormat::Binary)),
              "one failing column cannot hide another column's successful physical save and unload");
      streamer.EnsureDataColumns({next_ground}, 1);
      const auto resumed_count = manager.GetResidentTerrainColumnCount();
      require(resumed_count == limit && world.BlockWorld.GetBlock(next * CHUNK_SIZE + glm::ivec3(1, 0, 1)) == 8,
              "released residency admits exactly the deferred disk column");
      require(fs::remove(obstacle) && fs::remove(blocked_file), "clear the test-owned write obstruction");
      streamer.SetMaxLoadOpsPerFrame(4);
      for (size_t frame = 0; frame <= limit && manager.HasChunk(first); ++frame)
        streamer.UnloadPass(feet, eye, PlayerCapsule{});
      UBlockWorld cold;
      cold.SetVerticalProfile(settings.GetVerticalProfile());
      require(!manager.HasChunk(first) && !world.ModifiedChunks.count(first) && !world.GetColumnRecords().Find(held_record) &&
                  storage.LoadTerrainColumn({first.x, 0, first.z}, cold, folder.string(), *world.BlockRegistry,
                                            settings.MaxHeight, settings.MinHeight) > 0 &&
                  cold.GetBlock(first * CHUNK_SIZE) == 8,
              "normal unload retries, retires the held record and preserves the edit on cold reload");
      report["resident_save_admission"] = {{"status", "PASS"}, {"resident_limit", limit},
          {"resumed_residents", resumed_count}, {"veto_retained_records", record_count},
          {"physical_veto", true}, {"later_column_saved", true}, {"cold_resumption", true},
          {"old_record_retired", true}, {"recovered_edit_on_disk", true}};
    });
    RunTerrainProbePhase([&] {
      auto owner = std::make_unique<UWorld>(nullptr, nullptr);
      auto &world = *owner;
      auto definitions = std::make_shared<UBlockDefinitionStorage>();
      BlockDefinition stone;
      stone.Name = "stone";
      stone.Physics = BlockPhysicsProfile::Solid();
      definitions->ReplaceAll({{8, stone}}, {{"stone", 8}});
      world.BlockRegistry = std::make_unique<UBlockRegistry>(nullptr, definitions);
      ProceduralSettings settings;
      settings.MinHeight = -48;
      settings.MaxHeight = -17;
      settings.SeaLevel = -32;
      settings.AsyncChunkIo = true;
      settings.AsyncRelight = false;
      world.SetProceduralSettings(settings, false);
      world.SetSpawnPoint({8, -45, 8});
      world.Streaming->InitStreamerCallbacks(world);
      auto &streamer = *world.Streaming->GetStreamer();
      streamer.SetKeepPrefetchMargin(0);
      streamer.SetRenderDistance(1);
      streamer.SetRequiredDataDomain({0, 0, 0}, 0);
      const auto limit = streamer.GetTerrainColumnAdmissionLimit();
      require(limit == 50, "async result fixture uses the configured terrain limit");

      const auto folder = root / "ready-disk-admission";
      fs::create_directories(folder / "chunks");
      world.SetWorldFolderPath(folder.string());
      auto &persistence = *world.Persistence;
      auto &storage = persistence.GetChunkStorage();
      auto &io = *persistence.AsyncChunkIo;
      auto &chunks = world.BlockWorld.GetChunkManager();
      const glm::ivec3 admitted(0, 0, 0), deferred(1, 0, 0), resident(3, 0, 0), control(4, 0, 0);
      const auto cell = [](glm::ivec3 ground, int cy) {
        return glm::ivec3(ground.x, cy, ground.z) * CHUNK_SIZE + glm::ivec3(1, 1, 1);
      };
      const auto enqueue = [&](glm::ivec3 ground, uint64_t demand, bool install_base = false) {
        for (int cy = -3; cy <= -2; ++cy)
        {
          const glm::ivec3 slice(ground.x, cy, ground.z);
          UChunk chunk(slice);
          chunk.SetBlockLocal({1, 1, 1}, 8);
          require(storage.SaveChunk(slice, chunk, folder.string(), *world.BlockRegistry),
                  "ready disk admission writes a real signed slice");
        }
        if (install_base)
        {
          const glm::ivec3 base(ground.x, -3, ground.z);
          require(storage.LoadChunk(base, world.BlockWorld, folder.string(), *world.BlockRegistry) == 1 &&
                      chunks.HasDecodedTerrainSlice(base),
                  "resident peer already owns its decoded lower slice");
        }
        require(persistence.RequestAsyncTerrainColumnLoad(world, ground, demand) == TerrainColumnRead::Pending,
                "ready disk admission accepts the real saved column");
      };
      for (int i = 0; i < static_cast<int>(limit) - 2; ++i)
        world.BlockWorld.SetBlock(cell({-60 - i, 0, 0}, -3), 8);
      enqueue(admitted, 125001);
      enqueue(deferred, 125002);
      enqueue(resident, 125003, true);
      const auto pending_owner = persistence.PendingAsyncColumnLoadSlices.at(deferred).cancellation;
      require(io.WaitIdleFor(std::chrono::seconds(5)) && io.GetLoadResultQueueDepth() == 6,
              "both signed slices of all three disk requests are ready");
      const auto first = persistence.TickAsyncChunkIo(world, 10, 200.0, limit);
      require(first.applied_slices_n == 1 && chunks.HasResidentTerrainColumn(admitted) &&
                  chunks.GetResidentTerrainColumnCount() == limit && !chunks.HasResidentTerrainColumn(deferred),
              "last available slot is used once even with several ready columns");
      persistence.TickAsyncChunkIo(world, 10, 200.0, limit);
      require(world.BlockWorld.GetBlock(cell(admitted, -3)) == 8 &&
                  world.BlockWorld.GetBlock(cell(admitted, -2)) == 8 &&
                  world.BlockWorld.GetBlock(cell(resident, -2)) == 8 &&
                  !persistence.IsTerrainColumnDiskLoadPending(resident) &&
                  !persistence.IsTerrainColumnDiskLoadPending(admitted),
              "admitted and already resident columns finish while another read is held");
      const auto held = persistence.TickAsyncChunkIo(world, 10, 200.0, limit);
      require(held.selected_loads_n == 0 && io.GetLoadResultQueueDepth() == 2 &&
                  io.GetOutstandingLoadCount() == 2 &&
                  persistence.PendingAsyncColumnLoadSlices.at(deferred).cancellation == pending_owner &&
                  !chunks.HasResidentTerrainColumn(deferred),
              "ready but blocked disk slices keep their exact owner and credits");
      chunks.RemoveChunk({-60, -3, 0});
      persistence.TickAsyncChunkIo(world, 10, 200.0, limit);
      persistence.TickAsyncChunkIo(world, 10, 200.0, limit);
      require(chunks.GetResidentTerrainColumnCount() == limit && io.GetOutstandingLoadCount() == 0 &&
                  !persistence.IsTerrainColumnDiskLoadPending(deferred) &&
                  world.BlockWorld.GetBlock(cell(deferred, -3)) == 8 &&
                  world.BlockWorld.GetBlock(cell(deferred, -2)) == 8,
              "the held demand completes both signed slices after one slot retires");

      enqueue(control, 125004);
      require(io.WaitIdleFor(std::chrono::seconds(5)) && io.GetLoadResultQueueDepth() == 2,
              "unrestricted drain control holds a real completed column");
      persistence.TickAsyncChunkIo(world, 10, 200.0);
      require(chunks.GetResidentTerrainColumnCount() == limit + 1 &&
                  world.BlockWorld.GetBlock(cell(control, -2)) == 8,
              "unrestricted drain demonstrates the old ready-result overshoot");
      report["ready_disk_residency_admission"] = {{"status", "PASS"}, {"resident_limit", limit},
          {"held_load_credits", 2}, {"resident_slices_completed", true},
          {"same_demand_resumed", true}, {"unrestricted_count", limit + 1}};
    });
    report["texture_atlas_construction"]=RunTextureAtlasConstructionProbe(root/"texture-atlas");
    const auto json_sources=GetCatalogJsonSourceSnapshot();
    const auto feature_candidate=BoundCatalogCandidateSource(CatalogCandidateKind::Features,{});
    const auto refs_candidate=BoundCatalogCandidateSource(CatalogCandidateKind::Refs,{});
    report["catalog_candidate_fixed_terms"]={{"schema","catalog-candidate-fixed-terms.v1"},
        {"supported",feature_candidate.supported && refs_candidate.supported},
        {"feature_and_diagnostic_shared_blocks",feature_candidate.shared_blocks_bytes},
        {"refs_shared_block",refs_candidate.shared_blocks_bytes},
        {"scope","Compiled coallocation terms included in per-input candidate bound;not native construction peak or process cap. Input DOM/prior pubs/platform terms separate."}};
    GlogResourceCensus glog_resources{};bool glog_resources_ok=false;
#if defined(CUBATARIUM_GLOG_RESOURCE_SOURCE)
    glog_resources_ok=CubatariumGlogResourceSource(glog_resources.data(),glog_resources.size());
#endif
    report["glog_resource_source"]={{"schema","glog-resource-native.v1"},{"query_ok",glog_resources_ok},{"fields",glog_resources},
        {"scope","Quiescent selected retained C++ resources only;custom/stdio/CRT/VLOG/symbolizer/signal/utility/process terms separate."}};
    const auto allocator_owners=GetAllocatorOwnerLayout();
    report["allocator_owner_source"]={{"schema","allocator-owner-native.v1"},{"supported",allocator_owners.supported},
        {"requests",allocator_owners.requests},{"reserve_callback_requests",allocator_owners.reserve_callback_bytes},
        {"release_callback_requests",allocator_owners.release_callback_bytes},
        {"scope","Compiled shared object/control profiles only;complete identity-deduplicated owner domains and E10/E11 overlap required separately."}};
    const auto runtime_inline=GetRuntimeCoreInlineLayout();const auto pipeline_inline=GetPipelineRuntimeInlineLayout();
    report["runtime_inline_source"]={{"schema","runtime-inline-native.v1"},{"widths",runtime_inline.widths},
        {"present",runtime_inline.present},{"supported",runtime_inline.supported},{"pipeline_whole",pipeline_inline.whole},
        {"pipeline_children",pipeline_inline.children},{"population_globals",pipeline_inline.global},{"population_clocks",pipeline_inline.clocks},
        {"scope","Actual native compiled20 named core fixed/TLS rows only;no dynamic/loader/renderer/process bound or inferred thread count."}};
    const auto filesystem_layout=GetCatalogFilesystemLayout();
    report["filesystem_source_layout"]={{"schema","filesystem-source-layout.v1"},{"supported",filesystem_layout.supported},
        {"simple_shared_block",filesystem_layout.simple_shared_block},{"recursive_shared_block",filesystem_layout.recursive_shared_block},
        {"handle_cell",filesystem_layout.handle_cell},{"iterator_inline",filesystem_layout.iterator_inline},
        {"recursive_iterator_inline",filesystem_layout.recursive_iterator_inline},{"entry_inline",filesystem_layout.entry_inline},
        {"creator_inline",filesystem_layout.creator_inline},{"find_data_inline",filesystem_layout.find_data_inline},
        {"filesystem_error_object",filesystem_layout.filesystem_error_object},{"system_error_object",filesystem_layout.system_error_object},
        {"error_code_inline",filesystem_layout.error_code_inline},
        {"scope","Compiled MSVC Release layout only;native does not sample all scan path/depth high-water domains or OS/CRT heap."}};
    report["catalog_json_sources"]={{"schema","catalog-json-source-model.v1"},
        {"active_parsers",json_sources.active},{"peak_active_parsers",json_sources.peak_active},
        {"completed",json_sources.completed},{"failed",json_sources.failed},{"largest_input_bytes",json_sources.largest_input},
        {"largest_requested_heap_upper",json_sources.largest_requested_heap_upper},{"attempts_by_kind",json_sources.attempts},
        {"largest_model_shape",{{"bytes",json_sources.largest_bound_shape.bytes},{"values",json_sources.largest_bound_shape.values},
          {"objects",json_sources.largest_bound_shape.objects},{"arrays",json_sources.largest_bound_shape.arrays},
          {"keys",json_sources.largest_bound_shape.keys},{"strings",json_sources.largest_bound_shape.strings},
          {"string_bytes",json_sources.largest_bound_shape.string_bytes},{"max_depth",json_sources.largest_bound_shape.max_depth},
          {"kind",static_cast<size_t>(json_sources.largest_bound_shape.kind)},{"success",json_sources.largest_bound_shape.success}}},
        {"shape_inline_bytes",sizeof(CatalogJsonSourceShape)},{"process_counters_inline_bytes",sizeof(CatalogJsonSourceSnapshot)},
        {"document_inline_bytes",sizeof(CatalogJsonDocument)},{"sax_inline_bytes",sizeof(detail::CatalogJsonSax)},
        {"scope","Per-source MSVC container/parser/destructor request model;not measured heap or simultaneous process peak;no compiled candidate/publication/CRT/GL cap"}};
    const auto stbi_process=GetStbiProcessAllocationSnapshot();
    report["stbi_process_allocation"]={{"schema","stbi-allocation-requests.v1"},
        {"live_payload_bytes",stbi_process.live_payload_bytes},{"live_request_bytes",stbi_process.live_request_bytes},
        {"live_allocations",stbi_process.live_allocations},{"reserved_request_bytes",stbi_process.reserved_request_bytes},
        {"peak_admitted_request_bytes",stbi_process.peak_admitted_request_bytes},
        {"header_bytes_per_allocation",StbiAllocationHeaderBytes()},{"ledger_inline_bytes",sizeof(UStbiAllocationLedger)},
        {"scope","All linked STBI hooks only; admitted peak includes pending malloc reservations and prefix,not total heap/RSS/GPU/streams"}};
    const auto catalog_content=CaptureWorldGenContentSnapshot();
    const auto catalog_blocks=core.BlockDefinitionsInstance ? core.BlockDefinitionsInstance->GetCatalogSnapshot() : nullptr;
    CatalogResourceRoots catalog_resources;
    catalog_resources.merge=core.BlockMergeRegistryInstance;catalog_resources.placeholder=core.PlaceholderCacheInstance;
    catalog_resources.texture_bases=core.TextureBaseStorageInstance;catalog_resources.texture_cubes=core.TextureCubeStorageInstance;
    catalog_resources.block_storage=core.BlockDefinitionsInstance;
    catalog_resources.registry=core.WorldInstance ? core.WorldInstance->BlockRegistry.get() : nullptr;
    catalog_resources.active_pack_directory=true;catalog_resources.calling_thread_reads=true;
    const auto storage=MeasureCatalogStorage(&catalog_content,1,&catalog_blocks,1,core.ObjectLibraryInstance,catalog_resources);
    const std::array<WorldGenContentSnapshot,2> catalog_duplicates={catalog_content,catalog_content};
    const auto duplicate_storage=MeasureCatalogStorage(catalog_duplicates.data(),catalog_duplicates.size(),&catalog_blocks,1,core.ObjectLibraryInstance,catalog_resources);
    require(!storage.overflow && duplicate_storage.known_storage_bytes==storage.known_storage_bytes &&
        duplicate_storage.shared_owners==storage.shared_owners && duplicate_storage.msvc_unordered_extra_bytes==storage.msvc_unordered_extra_bytes &&
        duplicate_storage.msvc_tree_extra_bytes==storage.msvc_tree_extra_bytes && duplicate_storage.msvc_list_extra_bytes==storage.msvc_list_extra_bytes,
        "catalog storage census duplicates shared payload or container metadata");
    report["catalog_storage"]={{"schema","catalog-storage-census.v2"},
        {"scope","Supplied worldgen/definition/prefab/merge/texture/placeholder roots, active directory and calling-thread reads. Known storage excludes separate container metadata, shared control/upstream, GPU allocations and unsupplied other-thread/retired roots. Not total heap/RSS or a cap."},
        {"known_storage_bytes",storage.known_storage_bytes},{"object_bytes",storage.object_bytes},
        {"vector_bytes",storage.vector_bytes},{"map_value_bytes",storage.map_value_bytes},
        {"text_bytes",storage.text_bytes},{"diagnostic_bytes",storage.diagnostic_bytes},
        {"list_value_bytes",storage.list_value_bytes},{"tree_tables",storage.tree_tables},{"tree_nodes",storage.tree_nodes},
        {"lists",storage.lists},{"list_nodes",storage.list_nodes},{"msvc_tree_extra_bytes",storage.msvc_tree_extra_bytes},
        {"msvc_list_extra_bytes",storage.msvc_list_extra_bytes},{"calling_thread_inline_bytes",storage.calling_thread_inline_bytes},
        {"vectors",storage.vectors},{"tables",storage.tables},{"nodes",storage.nodes},{"bucket_slots",storage.bucket_slots},
        {"vector_allocations",storage.vector_allocations},{"unordered_model_supported",storage.unordered_model_supported},
        {"msvc_unordered_extra_bytes",storage.msvc_unordered_extra_bytes},
        {"shared_owners",storage.shared_owners},{"snapshot_handle_bytes",storage.input_snapshot_handle_bytes},
        {"packs",storage.Objects(CatalogObjectKind::Pack)},{"biome_registries",storage.Objects(CatalogObjectKind::Biomes)},
        {"feature_configs",storage.Objects(CatalogObjectKind::Features)},{"refs",storage.Objects(CatalogObjectKind::Refs)},
        {"images",storage.Objects(CatalogObjectKind::Image)},{"diagnostic_catalogs",storage.Objects(CatalogObjectKind::Diagnostics)},
        {"block_catalogs",storage.Objects(CatalogObjectKind::Blocks)},{"prefab_indexes",storage.Objects(CatalogObjectKind::PrefabIndex)},
        {"merge_registries",storage.Objects(CatalogObjectKind::Merge)},{"placeholder_caches",storage.Objects(CatalogObjectKind::Placeholder)},
        {"texture_base_stores",storage.Objects(CatalogObjectKind::TextureBases)},{"texture_cube_stores",storage.Objects(CatalogObjectKind::TextureCubes)},
        {"block_stores",storage.Objects(CatalogObjectKind::BlockStorage)},
        {"block_registries",storage.Objects(CatalogObjectKind::Registry)},{"registry_tags",storage.Objects(CatalogObjectKind::RegistryIdentity)},
        {"prefabs",storage.Objects(CatalogObjectKind::Prefab)},{"duplicate_snapshot_deduplicated",true}};
    if(core.WorldInstance) {
      const WorldGenSets *settings[]={&core.WorldInstance->GetWorldGenSets()};
      const auto inputs=MeasureSourceInputStorage(settings,1);
      const WorldGenSets *repeated[]={settings[0],settings[0]};
      const auto duplicate_inputs=MeasureSourceInputStorage(repeated,2);
      require(!inputs.overflow && inputs.settings_roots==1 && duplicate_inputs.settings_roots==1 &&
          inputs.RequestedHeapComponents()==duplicate_inputs.RequestedHeapComponents(),
          "source input census duplicates world settings");
      WorldGenContentSnapshot world_features;
      // Empty-owner alias is a non-owning synchronous view. Never create an
      // independent shared control block for a member of the live UWorld.
      world_features.Features=std::shared_ptr<const ObjectFeatureConfig>(std::shared_ptr<const void>{},
          &core.WorldInstance->GetResolvedObjectFeatures());
      const std::array<WorldGenContentSnapshot,2> with_world={catalog_content,world_features};
      const auto joined=MeasureCatalogStorage(with_world.data(),with_world.size(),&catalog_blocks,1,core.ObjectLibraryInstance,catalog_resources);
      require(!joined.overflow && joined.known_storage_bytes>=storage.known_storage_bytes &&
          joined.shared_owners>=storage.shared_owners,"world resolved feature census invalid");
      report["worldgen_input_storage"]={{"schema","worldgen-input-storage-native.v1"},
          {"settings_roots",inputs.settings_roots},{"settings_inline_bytes",inputs.settings_inline_bytes},
          {"model_supported",inputs.storage.unordered_model_supported},
          {"settings_heap_components",inputs.RequestedHeapComponents()},
          {"settings_known_bytes",inputs.storage.known_storage_bytes},
          {"settings_unordered_metadata",inputs.storage.msvc_unordered_extra_bytes},
          {"settings_large_alignment_extra",inputs.msvc_large_allocation_extra_bytes},
          {"joined_catalog_and_resolved_known_bytes",joined.known_storage_bytes},
          {"resolved_feature_known_delta",joined.known_storage_bytes-storage.known_storage_bytes},
          {"resolved_inline_shell_bytes",sizeof(ObjectFeatureConfig)},
          {"joined_shared_owners",joined.shared_owners},{"joined_feature_configs",joined.Objects(CatalogObjectKind::Features)},
          {"duplicate_settings_deduplicated",true},{"json_roots_observed",0},
          {"scope","Current supplied world settings and resolved feature member only. Settings include actual capacities/MSVC request alignment;inline world member shells separate. Joined catalog census remains known components,not exact heap/RSS/all-version/process cap. No live pre-parsed DOM in this native snapshot;isolated unit covers DOM. No collector owners retained."}};
    }
    const char *variant=std::getenv("CUBA_FLIGHT_QA_TERRAIN_SAVE_VARIANT");
    if(variant && std::string(variant)=="flat_publication")
    {

      report["variant"]="flat_publication";
      const char *face_phase=std::getenv("CUBA_FLIGHT_QA_FACE_REPAIR_ONLY");
      const bool face_only=face_phase && std::string(face_phase)=="1";
      const char *flow_phase=std::getenv("CUBA_FLIGHT_QA_FLOW_ONLY");
      const bool flow_only=flow_phase && std::string(flow_phase)=="1";
      report["flow_only"]=flow_only;
      report["face_repair_only"]=face_only;
      report["scope"]="Actual CPU/Flat UWorld section publication/input readiness; no Full solver or framebuffer acceptance";
      const auto &caps=GetActiveRenderBackendCaps();
      require(caps.ForceCpuBackends &&
                  URenderBackendFactory::Select(caps).Mesher==MesherBackendKind::CpuGreedy,
              "Flat probe requires an actually CPU-selected application session");
      UWorld world(nullptr,nullptr);
      world.SetSpawnPoint({8.0f,-40.0f,8.0f});
      world.ProceduralTemplate.MinHeight=-48;
      world.ProceduralTemplate.MaxHeight=-33;
      world.ProceduralTemplate.SeaLevel=-40;
      world.BlockWorld.SetVerticalProfile({-48,-33,-40});
      auto definitions=std::make_shared<UBlockDefinitionStorage>();
      BlockDefinition stone; stone.Name="stone"; stone.Physics=BlockPhysicsProfile::Solid();
      BlockDefinition water;
      water.Name="water"; water.Physics=BlockPhysicsProfile::FromPreset("water");
      water.Render.Style=BlockRenderStyle::Fluid; water.Render.Transparent=true;
      definitions->ReplaceAll({{8,stone},{9,water}},{{"stone",8},{"water",9}});
      world.BlockRegistry=std::make_unique<UBlockRegistry>(nullptr,definitions);
      world.BlockWorld.SetFluidDefinitions(definitions.get());
      world.BlockWorld.SetKnownOpenSkyAbsentQuery([](glm::ivec3){return true;});
      RenderSettings settings;
      settings.Lighting=LightingMode::Flat; settings.LightingModeExplicit=true;
      settings.AsyncMeshing=false; settings.GpuPackedMeshing=false; settings.FrustumCulling=false;
      world.SetRenderSettings(settings);
      if(const char *queue_roots=std::getenv("CUBA_CATALOG_QUEUE_ROOTS");queue_roots && std::string(queue_roots)=="1") RunTerrainProbePhase([&] {
        // This independent world uses copied definitions and a new save folder.
        // No current application content or live save is replaced.
        auto defs=std::make_shared<UBlockDefinitionStorage>();
        const auto publish=[&](char marker){
          const auto source=definitions->GetCatalogSnapshot();auto ids=source->ById;auto names=source->NameToId;
          ids.at(8).DisplayName=std::string(4096,marker);defs->ReplaceAll(std::move(ids),std::move(names));
          return std::weak_ptr<const BlockDefinitionCatalog>(defs->GetCatalogSnapshot());
        };
        const auto release_reads=[] {UBlockDefinitionStorage::ReleaseThreadReads();UBlockRegistry::ReleaseThreadName();};
        struct Domain:CatalogRuntimeDomain {void Io(const UAsyncChunkIO *io){if(io)io->InspectJobPools(*this);}};
        auto old_a=publish('a');auto owner=std::make_unique<UWorld>(nullptr,nullptr);auto &test=*owner;
        test.ProceduralTemplate.MinHeight=0;test.ProceduralTemplate.MaxHeight=15;test.ProceduralTemplate.SeaLevel=8;
        test.ProceduralTemplate.AsyncChunkIo=true;test.ProceduralTemplate.AsyncRelight=false;
        test.BlockWorld.SetVerticalProfile({0,15,8});test.BlockRegistry=std::make_unique<UBlockRegistry>(nullptr,defs);
        test.BlockWorld.SetFluidDefinitions(defs.get());test.SetRenderSettings(settings);
        const auto folder=root/"catalog-save-owner-roots";fs::create_directories(folder);test.SetWorldFolderPath(folder.string());
        const glm::ivec3 column(0),edit(4,4,4);UBinaryChunkSerializer serializer;
        auto frame=std::make_unique<UChunk>(column);frame->SetBlockLocal(edit,8);
        const auto decoded=serializer.Deserialize(serializer.Serialize(column,*frame,*test.BlockRegistry).bytes,column,*test.BlockRegistry);
        require(ApplyTerrainSliceLoad(test.BlockWorld,column,&decoded,nullptr,{column,1},1,false),"catalog save uses complete real decoded terrain");
        test.BlockWorld.SetBlock({5,4,4},8);test.ModifiedChunks.insert(column);
        auto &p=*test.Persistence;p.EnsureChunkIoInitialized();p.CurrentLightCacheSignature();
        auto gate=std::make_shared<AsyncSaveWriteProbeGate>();
        struct Release {std::shared_ptr<AsyncSaveWriteProbeGate> gate;~Release(){gate->released.store(true);}} release{gate};
        p.AsyncChunkIo->SaveWriteProbeGate=gate;
        require(!p.TrySaveTerrainColumnForUnload(test,column) && p.UnloadSaveReceipts.size()==1,"real unload receipt owns captured catalog");
        const auto entered_by=std::chrono::steady_clock::now()+std::chrono::seconds(3);
        while(!gate->entered.load() && std::chrono::steady_clock::now()<entered_by)std::this_thread::yield();
        require(gate->entered.load(),"catalog save writer entered");
        auto old_b=publish('b');p.CurrentLightCacheSignature();release_reads();
        Domain receipt;p.InspectCatalogDomain(receipt);receipt.BlockCatalog(defs->GetCatalogSnapshot());
        require(!old_a.expired() && receipt.Catalogs().Objects(CatalogObjectKind::Blocks)==2,"actual pending receipt joins old and current catalogs");
        gate->released.store(true);p.AsyncChunkIo->SaveWriteProbeGate.reset();
        const auto wait_ack=[&]{
          const auto deadline=std::chrono::steady_clock::now()+std::chrono::seconds(10);
          while(p.GetChunkStorage().IsColumnSavePending(column)){
            p.TickAsyncChunkIo(test,32,6.0);require(std::chrono::steady_clock::now()<deadline,"catalog save ACK deadline");std::this_thread::yield();
          }
        };
        wait_ack();require(!p.TrySaveTerrainColumnForUnload(test,column),"catalog identity change requires current write before ACK");release_reads();
        require(old_a.expired(),"replaced unload receipt retires old catalog payload");
        wait_ack();require(p.TrySaveTerrainColumnForUnload(test,column) && test.ModifiedChunks.empty(),"current receipt ACK retires dirty obligation");
        UBlockWorld cold;cold.SetVerticalProfile({0,15,8});
        require(p.GetChunkStorage().LoadTerrainColumn(column,cold,folder.string(),*test.BlockRegistry,15,0)>0 && cold.GetBlock(edit)==8 && cold.GetBlock({5,4,4})==8,"current actual save cold data matches");
        auto session=std::make_unique<UWorldCooperativeSession>();session->ScanSaveChunkCoords(test);
        auto old_c=publish('c');p.CurrentLightCacheSignature();release_reads();
        Domain cooperative;session->InspectCatalogDomain(cooperative);p.InspectCatalogDomain(cooperative);cooperative.BlockCatalog(defs->GetCatalogSnapshot());
        require(!old_b.expired() && cooperative.Catalogs().Objects(CatalogObjectKind::Blocks)==2,"actual cooperative save stage retains its captured catalog");
        session.reset();require(old_b.expired(),"cooperative session destruction retires save catalog");
        publish('d');release_reads();Domain signature;p.InspectCatalogDomain(signature);signature.BlockCatalog(defs->GetCatalogSnapshot());
        require(!old_c.expired() && signature.Catalogs().Objects(CatalogObjectKind::Blocks)==2,"signature memo retains previous catalog until refresh");
        p.CurrentLightCacheSignature();release_reads();require(old_c.expired(),"signature refresh retires previous catalog despite weak inspectors");
        report["catalog_save_owner_roots"]={{"status","PASS"},{"receipt_union_catalogs",2},{"old_receipt_payloads_after_replace",0},{"current_ack",true},{"cold_latest",true},{"cooperative_save_union_catalogs",2},{"after_session_release",0},{"signature_union_catalogs",2},{"after_signature_refresh",0},{"scope","Actual isolated async save receipt,ACK,cold load,cooperative ScanSaveChunkCoords and signature memo;not full cooperative save,UI relocation or process heap acceptance"}};
      });
      if(!face_only) RunTerrainProbePhase([&] {
        const auto idle_deadline=std::chrono::steady_clock::now()+std::chrono::seconds(5);
        while(UPipelineAdmission::Get().WorkSlotsInUse()!=0 &&
            std::chrono::steady_clock::now()<idle_deadline) std::this_thread::yield();
        require(UPipelineAdmission::Get().WorkSlotsInUse()==0,
            "generation lifecycle starts with idle shared compute");
      {
        // Controlled payload production isolates scheduler ownership; the real
        // pool, ready queue, generation registry and buffer ApplyTo are used.
        struct OwnershipPopulator final : IUChunkPopulator {
          ChunkPopulateResult Populate(const ChunkPopulateRequest &request) override {
            ChunkPopulateResult result;
            result.coord=request.chunkCoord; result.token=request.token;
            result.discarded=request.shouldCancel && request.shouldCancel();
            if(!result.discarded) result.buffer.SetBlock(
                {8+static_cast<int>(request.token.sequence-1)*2,-40,8},8);
            return result;
          }
        } populator;
        UChunkGenerationRegistry generation;
        UChunkLoadScheduler scheduler(populator,generation);
        UBlockWorld terrain;
        terrain.SetFluidDefinitions(definitions.get());
        terrain.SetVerticalProfile({-48,-33,-40});
        const glm::ivec3 column(0);
        generation.Bump(column);
        ProceduralSettings generation_settings;
        generation_settings.MinHeight=-48; generation_settings.MaxHeight=-33;
        const auto wait_ready=[&]() {
          const auto deadline=std::chrono::steady_clock::now()+std::chrono::seconds(5);
          while(scheduler.GetCompletedReadyCount()==0 && std::chrono::steady_clock::now()<deadline)
            std::this_thread::yield();
          return scheduler.GetCompletedReadyCount()==1;
        };
        unsigned arrivals=0;
        scheduler.SetMarkDirtyFn([&](glm::ivec3,int,int,bool){++arrivals;});
        scheduler.RequestLoad(column,1,generation_settings);
        require(scheduler.StartQueuedGenerationWorkers(1)==1 &&
            wait_ready() &&
            scheduler.GetCompletedReadyCount()==1,"actual generation worker owns a completed payload");
        scheduler.Tick(terrain,0,0);
        require(scheduler.GetState(column)==ChunkLoadState::Ready && arrivals==0,
            "zero commit budget retains actual ready generation result");
        const auto successor_sequence=generation.Bump(column);
        terrain.SetBlock({8,-40,8},9);
        scheduler.Tick(terrain,1,0);
        const bool stale_rejected=terrain.GetBlock({8,-40,8})==9 && arrivals==0 &&
            !scheduler.IsPending(column) && !scheduler.IsCommitted(column) &&
            generation.Current(column).sequence==successor_sequence;
        report["generation_stale_ready"]={{"stale_rejected",stale_rejected},
            {"block",terrain.GetBlock({8,-40,8})},{"arrivals",arrivals}};
        require(stale_rejected,"expired registry token must reject ready buffer without overwriting new terrain or token");
        scheduler.RequestLoad(column,0,generation_settings);
        require(scheduler.StartQueuedGenerationWorkers(1)==1 &&
            wait_ready(),
            "current generation request starts without extra cancellation");
        scheduler.Tick(terrain,1,0);
        const bool current=scheduler.IsCommitted(column) && arrivals==1 &&
            terrain.GetBlock({10,-40,8})==8 && terrain.GetBlock({8,-40,8})==9 &&
            generation.Current(column).sequence==successor_sequence;
        report["generation_stale_ready"]["current_arrival"]=current;
        require(current,"actual current generation buffer applies and notifies once after stale retirement");
      }
      {
        for(int operation=0;operation<4;++operation) for(int phase=0;phase<3;++phase) {
          struct GatePopulator final : IUChunkPopulator {
            std::promise<void> entered,release;
            std::future<void> entered_future=entered.get_future();
            std::shared_future<void> resume=release.get_future().share();
            bool released=false;
            void Release() { if(!released) { released=true; release.set_value(); } }
            ChunkPopulateResult Populate(const ChunkPopulateRequest &request) override {
              ChunkPopulateResult result; result.coord=request.chunkCoord; result.token=request.token;
              result.buffer.SetBlock({request.chunkCoord.x*CHUNK_SIZE+
                  (request.settings.Seed==17?8:10),-40,8},8);
              if(request.settings.Seed==17) {
                entered.set_value();
                (void)resume.wait_for(std::chrono::seconds(5));
              }
              return result; // A late already-computed result must still be rejected.
            }
          } populator;
          UChunkGenerationRegistry generation;
          UChunkLoadScheduler scheduler(populator,generation);
          UBlockWorld terrain; terrain.SetFluidDefinitions(definitions.get());
          terrain.SetVerticalProfile({-48,-33,-40}); terrain.SetBlock({8,-40,8},9);
          const glm::ivec3 column(0); generation.Bump(column);
          ProceduralSettings old_settings; old_settings.MinHeight=-48; old_settings.MaxHeight=-33;
          old_settings.Seed=17;
          unsigned arrivals=0;
          scheduler.SetMarkDirtyFn([&](glm::ivec3,int,int,bool){++arrivals;});
          const auto wait_ready=[&]() {
            const auto deadline=std::chrono::steady_clock::now()+std::chrono::seconds(5);
            while(scheduler.GetCompletedReadyCount()==0 && std::chrono::steady_clock::now()<deadline)
              std::this_thread::yield();
            return scheduler.GetCompletedReadyCount()==1;
          };
          scheduler.RequestLoad(column,0,old_settings);
          if(phase>0) {
            require(scheduler.StartQueuedGenerationWorkers(1)==1 &&
                populator.entered_future.wait_for(std::chrono::seconds(5))==std::future_status::ready,
                "generation cancellation holds actual running producer after compute");
            if(phase==2) {
              populator.Release(); require(wait_ready(),"generation cancellation has actual completed result");
              scheduler.Tick(terrain,0,0);
            }
          }
          const glm::ivec3 retained(1,0,0);
          uint64_t retained_token=0;
          if(operation==3) {
            generation.Bump(retained);
            auto retained_settings=old_settings; retained_settings.Seed=43;
            scheduler.RequestLoad(retained,2,retained_settings);
            retained_token=generation.Current(retained).sequence;
            // Center Y is deliberately unrelated; generation owns XZ columns.
            require(scheduler.CancelPendingOutsideRadius({2,400,0},1)==1 &&
                scheduler.GetState(retained)==ChunkLoadState::Requested &&
                generation.Current(retained).sequence==retained_token,
                "outside radius abandons only old owner and preserves boundary demand");
          }
          if(operation==0) scheduler.Cancel(column);
          if(operation==1) scheduler.Invalidate(column);
          if(operation==2) scheduler.CancelAllPending(std::chrono::milliseconds(0));
          require(!scheduler.IsPending(column),"cancel retires old scheduler owner");
          auto current_settings=old_settings; current_settings.Seed=29;
          scheduler.RequestLoad(column,1,current_settings);
          const auto current_token=generation.Current(column).sequence;
          if(phase==1) { populator.Release(); require(wait_ready(),"late old generation publishes only to ready queue"); }
          scheduler.Tick(terrain,1,0);
          const bool preserved=arrivals==0 && terrain.GetBlock({8,-40,8})==9 &&
              scheduler.GetState(column)==ChunkLoadState::Requested &&
              generation.Current(column).sequence==current_token;
          const char *result_key=operation==3?"generation_outside_lifetime":"generation_cancel_lifetime";
          report[result_key].push_back({{"operation",operation},{"phase",phase},
              {"new_owner_preserved",preserved},{"old_arrivals",arrivals}});
          require(preserved,"cancelled generation cannot install or retire newer queued owner");
          require(scheduler.StartQueuedGenerationWorkers(1)==1 && wait_ready(),
              "new generation owner computes after old retirement");
          scheduler.Tick(terrain,1,0);
          const bool current=scheduler.IsCommitted(column) && arrivals==1 &&
              terrain.GetBlock({10,-40,8})==8 && terrain.GetBlock({8,-40,8})==9 &&
              generation.Current(column).sequence==current_token;
          report[result_key].back()["current_arrival"]=current;
          require(current,"cancelled generation successor installs exactly once");
          if(operation==3) {
            require(scheduler.GetState(retained)==ChunkLoadState::Requested &&
                generation.Current(retained).sequence==retained_token &&
                scheduler.StartQueuedGenerationWorkers(1)==1 && wait_ready(),
                "retained boundary request computes without repeated ingress");
            scheduler.Tick(terrain,1,0);
            const bool retained_arrival=scheduler.IsCommitted(retained) && arrivals==2 &&
                terrain.GetBlock({CHUNK_SIZE+10,-40,8})==8 &&
                generation.Current(retained).sequence==retained_token;
            report[result_key].back()["retained_arrival"]=retained_arrival;
            require(retained_arrival,"outside cancellation preserves actual boundary arrival");
          }
        }
      }
      {
        for(int phase=0;phase<3;++phase) for(int direction=0;direction<2;++direction) {
          struct AdmissionPopulator final : IUChunkPopulator {
            bool hold=false;
            std::atomic_int calls{0};
            std::promise<void> entered,release;
            std::future<void> entered_future=entered.get_future();
            std::shared_future<void> resume=release.get_future().share();
            ChunkPopulateResult Populate(const ChunkPopulateRequest &request) override {
              ++calls;
              ChunkPopulateResult result; result.coord=request.chunkCoord; result.token=request.token;
              result.buffer.SetBlock({request.chunkCoord.x*CHUNK_SIZE+
                  (request.settings.Seed==17?8:request.settings.Seed==29?10:12),-40,8},8);
              if(hold && request.chunkCoord.x==0) {
                entered.set_value(); (void)resume.wait_for(std::chrono::seconds(5));
              }
              return result;
            }
          } populator;
          populator.hold=phase==1;
          UChunkGenerationRegistry generation;
          UChunkLoadScheduler scheduler(populator,generation);
          UBlockWorld terrain; terrain.SetFluidDefinitions(definitions.get());
          terrain.SetVerticalProfile({-48,-33,-40});
          const glm::ivec3 target(0),peer(1,0,0),invalid(0,1,0);
          generation.Bump(target); generation.Bump(peer);
          const auto target_token=generation.Current(target).sequence;
          std::vector<glm::ivec3> arrivals;
          scheduler.SetMarkDirtyFn([&](glm::ivec3 c,int,int,bool){arrivals.push_back(c);});
          ProceduralSettings settings; settings.MinHeight=-48; settings.MaxHeight=-33; settings.Seed=17;
          auto duplicate=settings; duplicate.Seed=99;
          auto peer_settings=settings; peer_settings.Seed=29;
          const auto wait_ready=[&](int count) {
            const auto deadline=std::chrono::steady_clock::now()+std::chrono::seconds(5);
            while(scheduler.GetCompletedReadyCount()!=count && std::chrono::steady_clock::now()<deadline)
              std::this_thread::yield();
            return scheduler.GetCompletedReadyCount()==count;
          };
          scheduler.RequestLoad(invalid,-100,settings);
          require(scheduler.GetState(invalid)==ChunkLoadState::Absent &&
              scheduler.StartQueuedGenerationWorkers(1)==0 && populator.calls==0,
              "non-column request rejected before generation admission");
          const int initial=direction==0?10:-10,final=direction==0?-10:10;
          scheduler.RequestLoad(target,initial,settings);
          scheduler.RequestLoad(target,initial,duplicate);
          if(phase>0) {
            require(scheduler.StartQueuedGenerationWorkers(1)==1,"admission target starts once");
            if(phase==1) require(populator.entered_future.wait_for(std::chrono::seconds(5))==
                std::future_status::ready,"admission holds actual generating target");
            else { require(wait_ready(1),"admission target completed"); scheduler.Tick(terrain,0,0); }
          }
          scheduler.RequestLoad(target,final,duplicate);
          scheduler.RequestLoad(target,final,duplicate);
          scheduler.RequestLoad(peer,0,peer_settings);
          if(phase==1) { populator.release.set_value(); require(wait_ready(1),"admission release target"); }
          require(scheduler.StartQueuedGenerationWorkers(1)==1 && wait_ready(phase==0?1:2),
              "admission preserves one executable request per column");
          scheduler.Tick(terrain,1,0);
          const glm::ivec3 first=direction==0?target:peer,second=direction==0?peer:target;
          require(arrivals.size()==1 && arrivals[0]==first,"live priority orders first actual commit");
          if(phase==0) require(scheduler.StartQueuedGenerationWorkers(1)==1 && wait_ready(1),
              "queued priority refresh preserves remaining request");
          scheduler.Tick(terrain,1,0);
          scheduler.RequestLoad(target,-100,duplicate);
          const bool accepted=arrivals.size()==2 && arrivals[1]==second &&
              scheduler.IsCommitted(target) && scheduler.IsCommitted(peer) &&
              terrain.GetBlock({8,-40,8})==8 && terrain.GetBlock({12,-40,8})==0 &&
              terrain.GetBlock({CHUNK_SIZE+10,-40,8})==8 && populator.calls==2 &&
              generation.Current(target).sequence==target_token &&
              scheduler.StartQueuedGenerationWorkers(1)==0;
          report["generation_admission"].push_back({{"phase",phase},{"direction",direction},
              {"invalid_rejected",true},{"priority_order",true},{"immutable_request",accepted},
              {"single_arrival_per_owner",accepted}});
          require(accepted,"duplicates preserve original payload and committed owner without extra job");
        }
        for(int revoked=0;revoked<2;++revoked) {
          struct DiscardPopulator final : IUChunkPopulator {
            ChunkPopulateResult Populate(const ChunkPopulateRequest &request) override {
              ChunkPopulateResult result; result.coord=request.chunkCoord; result.token=request.token;
              result.discarded=request.settings.Seed==17;
              if(!result.discarded) result.buffer.SetBlock({10,-40,8},8);
              return result;
            }
          } populator;
          UChunkGenerationRegistry generation; UChunkLoadScheduler scheduler(populator,generation);
          UBlockWorld terrain; terrain.SetFluidDefinitions(definitions.get());
          terrain.SetVerticalProfile({-48,-33,-40}); terrain.SetBlock({8,-40,8},9);
          const glm::ivec3 target(0); generation.Bump(target);
          unsigned arrivals=0; scheduler.SetMarkDirtyFn([&](glm::ivec3,int,int,bool){++arrivals;});
          ProceduralSettings settings; settings.MinHeight=-48; settings.MaxHeight=-33; settings.Seed=17;
          const auto wait_ready=[&]() {
            const auto deadline=std::chrono::steady_clock::now()+std::chrono::seconds(5);
            while(scheduler.GetCompletedReadyCount()==0 && std::chrono::steady_clock::now()<deadline)
              std::this_thread::yield();
            return scheduler.GetCompletedReadyCount()==1;
          };
          scheduler.RequestLoad(target,0,settings);
          require(scheduler.StartQueuedGenerationWorkers(1)==1 && wait_ready(),"actual discarded completion ready");
          if(revoked) generation.Bump(target);
          const auto current_token=generation.Current(target).sequence;
          scheduler.Tick(terrain,1,0);
          require(scheduler.GetState(target)==ChunkLoadState::Absent && arrivals==0 &&
              terrain.GetBlock({8,-40,8})==9,"discarded result explicitly retires without terrain arrival");
          settings.Seed=29; scheduler.RequestLoad(target,0,settings);
          const auto successor_token=generation.Current(target).sequence;
          require(revoked ? successor_token==current_token : successor_token>current_token,
              "discarded token retirement preserves newer owner or creates fresh identity");
          require(scheduler.StartQueuedGenerationWorkers(1)==1 && wait_ready(),"discarded owner permits current reissue");
          scheduler.Tick(terrain,1,0);
          const bool current=scheduler.IsCommitted(target) && arrivals==1 &&
              terrain.GetBlock({10,-40,8})==8 && terrain.GetBlock({8,-40,8})==9 &&
              generation.Current(target).sequence==successor_token;
          report["generation_discarded"].push_back({{"registry_revoked",bool(revoked)},
              {"retired_without_arrival",true},{"current_arrival",current}});
          require(current,"current generation completes after discarded terminal outcome");
        }
      }
      });
      if(!face_only && !flow_only) {
      require(world.GetLightingPipeline().GetMode()==LightingMode::Flat && !world.RequiresLightingLitGate(),
              "Flat probe uses actual factory Flat pipeline");
      const glm::ivec3 coord(0,-3,0);
      for(int x=0;x<16;++x) for(int z=0;z<16;++z) world.BlockWorld.SetBlock({x,-48,z},8);
      world.BlockWorld.SetBlock({8,-40,8},8);
      world.GetLightingPipeline().FillAllLoadedChunks(world.BlockWorld);
      auto &mesh=world.GetMeshService(); auto &cache=mesh.GetCache();
      {
        struct DiskScanRestore {
          bool previous=PreferGpuFluidColumnScan();
          UFrameDeadline deadline=UFrameDeadline::Get();
          ~DiskScanRestore() { SetPreferGpuFluidColumnScan(previous); UFrameDeadline::Get()=deadline; }
        } restore;
        UFrameDeadline::Get().BeginFrame(5000);
        UWorld disk(nullptr,nullptr);
        disk.SetSpawnPoint({8,-40,8});
        disk.ProceduralTemplate.MinHeight=-48; disk.ProceduralTemplate.MaxHeight=-17;
        disk.ProceduralTemplate.SeaLevel=-40;
        disk.BlockWorld.SetVerticalProfile({-48,-17,-40});
        disk.BlockRegistry=std::make_unique<UBlockRegistry>(nullptr,definitions);
        disk.BlockWorld.SetFluidDefinitions(definitions.get());
        disk.SetRenderSettings(settings);
        const auto folder=root/"fluid-partial-disk";
        fs::create_directories(folder);
        disk.SetWorldFolderPath(folder.string());
        disk.Streaming->EnsureStreamer(disk.BlockWorld,*disk.BlockRegistry,42,disk.ProceduralTemplate);
        UBlockWorld source;
        source.SetVerticalProfile({-48,-17,-40});
        source.SetFluidDefinitions(definitions.get());
        for(int x=0;x<16;++x) for(int z=0;z<16;++z) source.SetBlock({x,-48,z},8);
        source.SetBlock({8,-40,8},9);
        source.SetFluidState({8,-40,8},FluidCellState::Source().WithKind(FluidKind::Water));
        source.SetBlock({8,-24,8},8);
        require(disk.GetChunkStorage().SaveTerrainColumn({0,0,0},source,folder.string(),
                    *disk.BlockRegistry,-17,-48),"partial fluid fixture saves two real disk slices");
        auto &disk_cache=disk.GetMeshService().GetCache();
        SetPreferGpuFluidColumnScan(false);
        const auto *dry=disk_cache.GetFluidSurfaceSlice(disk.BlockWorld,*disk.BlockRegistry,{0,0,0},-40);
        require(dry && !dry->HasSurface(8,8) &&
                    !disk_cache.GetFluidSurfaceDirtyGroundChunks().count({0,0,0}),
                "partial disk fixture has settled warm dry map");
        disk.Persistence->RequestAsyncTerrainColumnLoad(disk,{0,0,0},9601);
        const auto deadline=std::chrono::steady_clock::now()+std::chrono::seconds(10);
        std::size_t applied=0;
        while(applied==0) {
          applied=disk.Persistence->TickAsyncChunkIo(disk,1,1000).applied_slices_n;
          require(std::chrono::steady_clock::now()<deadline,"partial disk first-slice deadline");
          std::this_thread::yield();
        }
        const bool pending=disk.Persistence->IsTerrainColumnDiskLoadPending({0,0,0});
        const bool invalidated=disk_cache.GetFluidSurfaceDirtyGroundChunks().count({0,0,0})!=0;
        const bool premature_ready=disk.IsColumnRenderReady({0,0,0});
        while(!disk.Persistence->TickDrainAsyncChunkIo(disk,1)) {
          require(std::chrono::steady_clock::now()<deadline,"partial disk completion deadline");
          std::this_thread::yield();
        }
        SetPreferGpuFluidColumnScan(true);
        const auto *current=disk_cache.GetFluidSurfaceSlice(disk.BlockWorld,*disk.BlockRegistry,{0,0,0},-40);
        const auto direct=FindFluidColumnSurfaceAt(disk.BlockWorld,*disk.BlockRegistry,8,8,-40);
        const bool final_current=current && direct.valid && current->HasSurface(8,8) &&
            current->SurfaceBlockY[8][8]==-40 && current->SurfaceBlockY[8][8]==direct.surfaceBlockY &&
            current->BottomBlockY[8][8]==direct.bottomBlockY && current->FluidId[8][8]==9;
        report["fluid_partial_disk_arrival"]={{"first_applied",applied},
            {"still_pending",pending},{"invalidated_before_finalize",invalidated},
            {"premature_render_ready",premature_ready},{"final_surface_current",final_current}};
        require(applied==1 && pending && invalidated && !premature_ready && final_current,
                "disk slice arrival invalidates warm fluid map before column finalize");
      }
      {
        struct ScanRestore {
          bool previous=PreferGpuFluidColumnScan();
          UFrameDeadline deadline=UFrameDeadline::Get();
          ~ScanRestore() { SetPreferGpuFluidColumnScan(previous); UFrameDeadline::Get()=deadline; }
        } restore;
        UFrameDeadline::Get().BeginFrame(5000);
        SetPreferGpuFluidColumnScan(false);
        bool all_invalidated=true;
        for(int path=0;path<3;++path) {
          const glm::ivec3 ground(100+path*4,0,100);
          const glm::ivec3 cell(ground.x*CHUNK_SIZE+8,-40,ground.z*CHUNK_SIZE+8);
          world.BlockWorld.SetBlock({cell.x,-48,cell.z},8);
          const auto *dry=cache.GetFluidSurfaceSlice(world.BlockWorld,*world.BlockRegistry,ground,-40);
          require(dry && !dry->HasSurface(8,8) &&
                      !cache.GetFluidSurfaceDirtyGroundChunks().count(ground),
                  "arrival probe starts with settled dry map");
          world.BlockWorld.SetBlock(cell,9);
          world.BlockWorld.SetFluidState(cell,FluidCellState::Source().WithKind(FluidKind::Water));
          if(path==0) {
            WorldGenContext context(world.BlockWorld,*world.BlockRegistry,world.ProceduralTemplate,nullptr);
            context.OnColumnMeshDirty=[&](int x,int z,int min_y,int max_y) {
              world.MarkColumnMeshDirty(x,z,min_y,max_y);
            };
            context.ResetColumnDirty(ground.x*CHUNK_SIZE,ground.z*CHUNK_SIZE);
            context.AccumulateDirtyColumn(-40,-40);
            context.FlushColumnDirty();
          } else if(path==1) {
            mesh.MarkTerrainChunkMeshDirtySeamed(ground,-48,-33,false);
          } else {
            mesh.MarkTerrainChunkMeshDirtySeamedPriority(ground,-48,-33,false);
          }
          const bool invalidated=cache.GetFluidSurfaceDirtyGroundChunks().count(ground)!=0;
          // The desktop PreferGpu API actually runs the production CPU packed
          // scan. Exercise it, rather than the hitch-gated empty fallback.
          SetPreferGpuFluidColumnScan(true);
          const auto direct=FindFluidColumnSurfaceAt(world.BlockWorld,*world.BlockRegistry,
                                                     cell.x,cell.z,-40);
          const auto *current=cache.GetFluidSurfaceSlice(world.BlockWorld,*world.BlockRegistry,ground,-40);
          const bool surface_current=current && direct.valid && current->HasSurface(8,8) &&
              current->SurfaceBlockY[8][8]==direct.surfaceBlockY &&
              current->BottomBlockY[8][8]==direct.bottomBlockY &&
              current->FluidId[8][8]==direct.fluidId && direct.surfaceBlockY==-40 &&
              direct.fluidId==9 && !cache.GetFluidSurfaceDirtyGroundChunks().count(ground);
          SetPreferGpuFluidColumnScan(false);
          report["terrain_arrival_fluid_maps"].push_back({{"path",path},
              {"cy",-3},{"invalidated_before_publication",invalidated},
              {"actual_surface_current",surface_current},
              {"scan_backend","desktop_cpu_packed_scan"},
              {"publications",cache.GetPublishCount({ground.x,-3,ground.z})}});
          all_invalidated &= invalidated && surface_current;
        }
        require(all_invalidated,"terrain arrival invalidates and recomputes warm fluid maps before publication");
      }
      const auto physical=[&] {return world.BlockWorld.HasCompleteLightSamplingCalculation(
          coord,world.BlockRegistry->GetDefinitionsCatalogSnapshot());};
      const auto checkpoint=[&](const char *phase) {
        report["flat_publication_steps"].push_back({{"phase",phase},{"physical",physical()},
            {"complete",world.IsColumnRenderReady({0,0,0})},
            {"section_current",world.HasCurrentChunkSlicePublication(coord)},
            {"inputs_current",cache.IsPublishedMeshCurrentForInputs(coord,world.BlockWorld)},
            {"publications",cache.GetPublishCount(coord)},
            {"drawable",mesh.HasDrawableGreedyMesh(coord)},
            {"presentation_debt",world.SampleRequiredPresentationCoverage().publication_unready_sections}});
      };
      checkpoint("unpublished");
      require(!physical() && !world.IsColumnRenderReady({0,0,0}),"Flat still needs geometry publication");
      {
        struct DemandDeadlineRestore {
          UFrameDeadline previous=UFrameDeadline::Get();
          ~DemandDeadlineRestore() { UFrameDeadline::Get()=previous; }
        } restore;
        UFrameDeadline::Get().BeginFrame(5000);
        UColumnFlowExecutor executor;
        executor.Enqueue({0,0},ColumnWorkKind::FirstMesh,100);
        const int refused=executor.DrainBudget(world,1,{0,0,0},1,0);
        executor.SyncColumnJobStageFromWorld(world,{0,0});
        const auto &record=world.GetColumnRecords().GetOrCreate({0,0});
        const bool debt_retained=!world.IsColumnRenderReady({0,0,0}) &&
            !mesh.IsChunkMeshDirty(coord) && !mesh.HasInflightMeshBuild(coord) &&
            record.inflight_job==0 && record.pending.token==0 &&
            UColumnRecordCoordinator::RecordWantsFirstMeshEnqueue(record);
        // Advance only the documented three-frame enqueue cooldown; no
        // sleeping or direct access to the scheduler on the retry path.
        for(int frame=0;frame<3;++frame) executor.DrainBudget(world,0,{0,0,0},1,1);
        executor.Enqueue({0,0},ColumnWorkKind::FirstMesh,100);
        const bool successor_queued=executor.Scheduler().Contains({0,0},ColumnWorkKind::FirstMesh);
        const int admitted=executor.DrainBudget(world,1,{0,0,0},1,1);
        executor.SyncColumnJobStageFromWorld(world,{0,0});
        report["flow_unpublished_demand"]={{"refused_dispatch",refused},
            {"focus_x",world.GetPreferredLoadFocusBlock().x},
            {"focus_z",world.GetPreferredLoadFocusBlock().z},
            {"debt_retained",debt_retained},{"successor_queued",successor_queued},
            {"admitted_dispatch",admitted},{"dirty",mesh.IsChunkMeshDirty(coord)},
            {"pending_stage",static_cast<int>(executor.GetColumnJobStage({0,0}))}};
        require(refused==1 && debt_retained && successor_queued && admitted==1 &&
                    mesh.IsChunkMeshDirty(coord) &&
                    executor.GetColumnJobStage({0,0})==ColumnJobStage::Meshing,
                "unpublished demand survives refusal and returns to actual dirty owner");
      }
      mesh.RebuildChunkImmediate(world.BlockWorld,*world.BlockRegistry,coord);
      checkpoint("published");
      require(!physical() && mesh.HasDrawableGreedyMesh(coord) && world.IsColumnRenderReady({0,0,0}),
              "Flat actual publication completes without a physical light certificate");
      const auto previous=cache.GetPublishCount(coord);
      // S4.02: a flow ticket requests admission; a zero admission budget
      // cannot manufacture a running mesh owner in an already settled column.
      {
        struct AdmissionDeadlineRestore {
          UFrameDeadline previous=UFrameDeadline::Get();
          ~AdmissionDeadlineRestore() { UFrameDeadline::Get()=previous; }
        } restore;
        UFrameDeadline::Get().BeginFrame(5000);
        UColumnFlowExecutor executor;
        executor.Scheduler().Enqueue({0,0},ColumnWorkKind::FirstMesh,100);
        const int dispatched=executor.DrainBudget(world,1,{0,0,0},1,0);
        executor.SyncColumnJobStageFromWorld(world,{0,0});
        const auto &record=world.GetColumnRecords().GetOrCreate({0,0});
        report["flow_admission_refusal"]={{"dispatched",dispatched},
            {"inflight_job",record.inflight_job},{"pending_token",record.pending.token},
            {"dirty",mesh.IsChunkMeshDirty(coord)},
            {"actual_inflight",mesh.HasInflightMeshBuild(coord)},
            {"render_ready",world.IsColumnRenderReady({0,0,0})},
            {"stage",static_cast<int>(executor.GetColumnJobStage({0,0}))}};
        require(dispatched==1 && record.inflight_job==0 && record.pending.token==0 &&
                    executor.GetColumnJobStage({0,0})==ColumnJobStage::RenderReady,
                "refused FirstMesh admission leaves no fictitious running owner");
        executor.Enqueue({0,0},ColumnWorkKind::RemeshSeam,100);
        const int seam=executor.DrainBudget(world,1,{0,0,0},1,1);
        executor.SyncColumnJobStageFromWorld(world,{0,0});
        report["flow_seam_noop"]={{"dispatched",seam},
            {"inflight_job",record.inflight_job},{"pending_token",record.pending.token},
            {"dirty",mesh.IsChunkMeshDirty(coord)},
            {"stage",static_cast<int>(executor.GetColumnJobStage({0,0}))}};
        require(seam==1 && record.inflight_job==0 && record.pending.token==0 &&
                    !mesh.IsChunkMeshDirty(coord) &&
                    executor.GetColumnJobStage({0,0})==ColumnJobStage::RenderReady,
                "seam no-op leaves no fictitious running owner");
      }
      world.BlockWorld.SetBlock({3,-40,3},8);
      checkpoint("stale_geometry");
      require(!physical() && !world.IsColumnRenderReady({0,0,0}),"Flat geometry mutation invalidates completion");
      mesh.RebuildChunkImmediate(world.BlockWorld,*world.BlockRegistry,coord);
      checkpoint("replacement");
      require(!physical() && cache.GetPublishCount(coord)>previous && world.IsColumnRenderReady({0,0,0}) &&
                  world.SampleRequiredPresentationCoverage().publication_unready_sections==0,
              "Flat geometry replacement restores completion without relight");
      {
        struct OverflowDeadlineRestore {
          UFrameDeadline previous=UFrameDeadline::Get();
          ~OverflowDeadlineRestore() { UFrameDeadline::Get()=previous; }
        } restore;
        UFrameDeadline::Get().BeginFrame(5000);
        settings.AsyncMeshing=true;
        mesh.SetRenderSettings(settings);
        mesh.SetMeshRebuildFocus({0,0,0},4,1);
        mesh.SetMeshSnapshotBudgetMs(1000);
        mesh.SetMeshEmergeTotalBudgetMs(1000);
        MeshWorkAdmission admission;
        admission.enforce_schedule_lanes=true;
        admission.max_schedule=1;
        admission.first_mesh_schedule=0;
        admission.remesh_schedule=1;
        mesh.SetMeshWorkAdmission(admission);
        mesh.SetMaxOutsideFocusMeshPerFrame(0);
        cache.SetMeshCompletedCapacity(1);
        const auto progress=[&](const char *phase) {
          Json state={{"phase",phase},{"completed",cache.GetMeshCompletedSize()},
              {"overflow",cache.GetMeshCompletedDiscardedOverflow()},
              {"inflight",cache.GetAsyncBuilderInFlightCount()}};
          std::ofstream(root/"mesh-overflow-progress.json") << state.dump(2) << '\n';
        };
        progress("initialized");
        world.BlockWorld.SetBlock({24,-40,8},8);
        mesh.RebuildChunkImmediate(world.BlockWorld,*world.BlockRegistry,{1,-3,0});
        world.BlockWorld.SetBlock({4,-40,4},8);
        world.BlockWorld.SetBlock({25,-40,8},8);
        const auto schedule=[&](glm::ivec3 target) {
          progress("before_capture");
          cache.MarkDirtyPriority(target);
          const auto revision=cache.GetChunkMeshRevision(target);
          require(WaitForProbeMeshCapture(cache,world.BlockWorld,*world.BlockRegistry,target,revision),
                  "overflow fixture captures actual current inputs");
          progress("captured");
          const auto stats=cache.RebuildDirtyChunksWithStats(world.BlockWorld,*world.BlockRegistry,
                                                           0,1,false,0,1000,true);
          report["mesh_overflow_schedule_steps"].push_back({{"target_x",target.x},
              {"scheduled",stats.Scheduled},{"skip_other",cache.GetLastMeshDirtyScheduleSkipOtherN()},
              {"skip_pipeline",cache.GetLastMeshDirtyScheduleSkipPipelineN()},
              {"skip_snapshot",cache.GetLastMeshDirtyScheduleSkipSnapshotN()}});
          progress("scheduling_returned");
          const bool idle=cache.WaitForAsyncMeshIdleFor(std::chrono::seconds(5));
          const bool target_owned=cache.HasCpuMeshBuildInFlight(target);
          report["mesh_overflow_schedule_steps"].back()["target_owned"]=target_owned;
          require(stats.Scheduled==1 && idle && target_owned,
                  "overflow fixture completes one real worker job");
          progress("worker_complete");
        };
        const auto overflow_before=cache.GetMeshCompletedDiscardedOverflow();
        const auto publications_before=cache.GetPublishCount(coord);
        schedule(coord);
        schedule({1,-3,0});
        const auto dropped=cache.GetMeshCompletedDiscardedOverflow()-overflow_before;
        progress("before_overflow_drain");
        mesh.DrainAsyncMeshResults(world.BlockWorld,*world.BlockRegistry,0);
        progress("after_overflow_drain");
        const bool dirty=mesh.IsChunkMeshDirty(coord);
        const bool orphan=mesh.HasInflightMeshBuild(coord) && !cache.HasCpuMeshBuildInFlight(coord) &&
            !mesh.IsPendingGpuApply(coord) && !mesh.IsGpuExtractInFlight(coord);
        report["mesh_completed_overflow"]={{"dropped",dropped},{"dirty",dirty},
            {"orphan_inflight",orphan},{"completed",cache.GetMeshCompletedSize()}};
        require(dropped==1 && dirty && !orphan,
                "overflow drain returns dropped mesh debt without orphan inflight owner");
        mesh.DrainAsyncMeshResults(world.BlockWorld,*world.BlockRegistry,1);
        schedule(coord);
        mesh.DrainAsyncMeshResults(world.BlockWorld,*world.BlockRegistry,1);
        report["mesh_overflow_successor"]={{"publications_before",publications_before},
            {"publications_after",cache.GetPublishCount(coord)},
            {"inputs_current",cache.IsPublishedMeshCurrentForInputs(coord,world.BlockWorld)},
            {"section_current",world.HasCurrentChunkSlicePublication(coord)},
            {"dirty",mesh.IsChunkMeshDirty(coord)},
            {"cpu_inflight",cache.HasCpuMeshBuildInFlight(coord)},
            {"inflight",mesh.HasInflightMeshBuild(coord)},
            {"awaiting_field",cache.IsGeometryAwaitingLight(coord)},
            {"gpu_pending",mesh.IsPendingGpuApply(coord)},
            {"preview",cache.HasProvisionalLightPreview(coord)},
            {"stale_geom",cache.GetMeshApplyStaleGeomCount()},
            {"stale_light",cache.GetMeshApplyStaleLightCount()},
            {"stale_revision",cache.GetMeshApplyStaleRevCount()},
            {"drop_no_active",cache.GetMeshApplyDropNoActiveCount()}};
        require(cache.GetPublishCount(coord)>publications_before &&
                    world.HasCurrentChunkSlicePublication(coord),
                "overflow successor actually publishes current mesh");
        report["mesh_completed_overflow"]["actual_successor_published"]=true;
      }
      {
        struct CancelDeadlineRestore {
          UFrameDeadline previous=UFrameDeadline::Get();
          ~CancelDeadlineRestore() { UFrameDeadline::Get()=previous; }
        } restore;
        UFrameDeadline::Get().BeginFrame(5000);
        for(bool protected_case : {false,true}) {
        UWorld cancelled(nullptr,nullptr);
        cancelled.SetSpawnPoint({8,-40,8});
        cancelled.ProceduralTemplate.MinHeight=-48;
        cancelled.ProceduralTemplate.MaxHeight=-33;
        cancelled.ProceduralTemplate.SeaLevel=-40;
        cancelled.BlockWorld.SetVerticalProfile({-48,-33,-40});
        cancelled.BlockRegistry=std::make_unique<UBlockRegistry>(nullptr,definitions);
        cancelled.BlockWorld.SetFluidDefinitions(definitions.get());
        cancelled.BlockWorld.SetKnownOpenSkyAbsentQuery([](glm::ivec3){return true;});
        cancelled.SetRenderSettings(settings);
        for(int x=0;x<(protected_case?32:16);++x) for(int z=0;z<16;++z) cancelled.BlockWorld.SetBlock({x,-48,z},8);
        cancelled.BlockWorld.SetBlock({8,-40,8},8);
        if(protected_case) cancelled.BlockWorld.SetBlock({24,-40,8},8);
        cancelled.GetLightingPipeline().FillAllLoadedChunks(cancelled.BlockWorld);
        auto &cancel_mesh=cancelled.GetMeshService();
        auto &cancel_cache=cancel_mesh.GetCache();
        cancel_mesh.RebuildChunkImmediate(cancelled.BlockWorld,*cancelled.BlockRegistry,coord);
        require(cancelled.HasCurrentChunkSlicePublication(coord),"cancel fixture starts with actual current predecessor");
        if(protected_case) cancel_mesh.RebuildChunkImmediate(cancelled.BlockWorld,*cancelled.BlockRegistry,{1,-3,0});
        cancel_mesh.SetMeshRebuildFocus({0,0,0},1,1);
        cancel_mesh.SetMeshSnapshotBudgetMs(1000);
        cancel_mesh.SetMeshEmergeTotalBudgetMs(1000);
        MeshWorkAdmission admission;
        admission.enforce_schedule_lanes=true; admission.max_schedule=1;
        admission.first_mesh_schedule=1; admission.remesh_schedule=1;
        cancel_mesh.SetMeshWorkAdmission(admission);
        cancel_mesh.SetMaxOutsideFocusMeshPerFrame(0);
        const auto schedule_cancel=[&](bool new_request,glm::ivec3 target) {
          if(new_request) cancel_cache.MarkDirtyPriority(target);
          require(WaitForProbeMeshCapture(cancel_cache,cancelled.BlockWorld,*cancelled.BlockRegistry,
                      target,cancel_cache.GetChunkMeshRevision(target)),"cancel fixture actual capture");
          const auto stats=cancel_cache.RebuildDirtyChunksWithStats(cancelled.BlockWorld,
              *cancelled.BlockRegistry,0,1,false,0,1000,true);
          const bool idle=cancel_cache.WaitForAsyncMeshIdleFor(std::chrono::seconds(5));
          report["mesh_cancel_schedule_steps"].push_back({{"protected_case",protected_case},{"target_x",target.x},
              {"scheduled",stats.Scheduled},{"target_owned",cancel_cache.HasCpuMeshBuildInFlight(target)}});
          require(stats.Scheduled==1 && idle && cancel_cache.HasCpuMeshBuildInFlight(target),
                  "cancel fixture completes exact target worker");
        };
        if(!protected_case) {
        const auto before=cancel_cache.GetPublishCount(coord);
        cancelled.BlockWorld.SetBlock({5,-40,5},8);
        schedule_cancel(true,coord);
        const bool completed=cancel_cache.GetMeshCompletedSize()==1;
        const auto requested_revision=cancel_cache.GetInflightSourceRevision(coord);
        report["mesh_cancel_revision_witness"]={{"active",cancel_mesh.HasInflightMeshBuild(coord)},
            {"submitted",requested_revision},{"desired",cancel_mesh.GetChunkMeshRevision(coord)},
            {"published",cancel_cache.GetMeshPublishRevs(coord).geom_rev},
            {"inputs_current",cancel_cache.IsPublishedMeshCurrentForInputs(coord,cancelled.BlockWorld)}};
        cancel_cache.CancelAsyncInFlightKeepDirty();
        report["mesh_completed_cancel"]={{"completed_before_cancel",completed},
            {"dirty",cancel_mesh.IsChunkMeshDirty(coord)},
            {"retained_draw",cancel_mesh.HasDrawableGreedyMesh(coord)},
            {"inflight",cancel_mesh.HasInflightMeshBuild(coord)},
            {"current",cancelled.HasCurrentChunkSlicePublication(coord)}};
        require(completed && cancel_mesh.IsChunkMeshDirty(coord) &&
                    cancel_mesh.HasDrawableGreedyMesh(coord) && !cancel_mesh.HasInflightMeshBuild(coord) &&
                    !cancelled.HasCurrentChunkSlicePublication(coord),
                "cancel keeps executable current mesh debt while retaining predecessor draw");
        schedule_cancel(false,coord);
        cancel_mesh.DrainAsyncMeshResults(cancelled.BlockWorld,*cancelled.BlockRegistry,1);
        const bool successor=cancel_cache.GetPublishCount(coord)>before &&
            cancelled.HasCurrentChunkSlicePublication(coord);
        report["mesh_completed_cancel"]["actual_successor_published"]=successor;
        report["mesh_completed_cancel"]["same_requested_revision"]=
            cancel_mesh.GetChunkMeshRevision(coord)==requested_revision;
        require(successor,"cancelled mesh successor actually publishes current inputs");
        } else {
        const glm::ivec3 outside(1,-3,0);
        cancelled.BlockWorld.SetBlock({6,-40,6},8);
        cancelled.BlockWorld.SetBlock({25,-40,8},8);
        cancel_cache.SetMeshCompletedCapacity(4);
        const auto lease_before=cancel_cache.GetPublishCount(coord);
        const auto outside_before=cancel_cache.GetPublishCount(outside);
        schedule_cancel(true,coord);
        schedule_cancel(true,outside);
        require(cancel_cache.GetMeshCompletedSize()==2,"lease fixture has two exact completed jobs");
        cancel_cache.CancelAsyncInFlightKeepDirty({0,0,0},0);
        const bool protected_cpu=cancel_cache.HasCpuMeshBuildInFlight(coord);
        const bool protected_dirty=cancel_mesh.IsChunkMeshDirty(coord);
        const bool outside_dirty=cancel_mesh.IsChunkMeshDirty(outside);
        report["mesh_protected_cancel"]={{"protected_cpu",protected_cpu},
            {"protected_active",cancel_mesh.HasInflightMeshBuild(coord)},
            {"protected_dirty",protected_dirty},{"completed",cancel_cache.GetMeshCompletedSize()},
            {"outside_cpu",cancel_cache.HasCpuMeshBuildInFlight(outside)},
            {"outside_active",cancel_mesh.HasInflightMeshBuild(outside)},
            {"outside_dirty",outside_dirty}};
        require(protected_cpu && !protected_dirty && cancel_cache.GetMeshCompletedSize()==1 &&
                    !cancel_cache.HasCpuMeshBuildInFlight(outside) &&
                    !cancel_mesh.HasInflightMeshBuild(outside) && outside_dirty,
                "protected lease preserves real completed CPU job while cancelling outside debt");
        cancel_mesh.DrainAsyncMeshResults(cancelled.BlockWorld,*cancelled.BlockRegistry,1);
        const bool protected_published=cancel_cache.GetPublishCount(coord)>lease_before &&
            cancelled.HasCurrentChunkSlicePublication(coord);
        report["mesh_protected_cancel"]["protected_published_without_reschedule"]=protected_published;
        require(protected_published,"protected original job publishes without recapture or reschedule");
        schedule_cancel(false,outside);
        cancel_mesh.DrainAsyncMeshResults(cancelled.BlockWorld,*cancelled.BlockRegistry,1);
        const bool outside_published=cancel_cache.GetPublishCount(outside)>outside_before &&
            cancelled.HasCurrentChunkSlicePublication(outside);
        report["mesh_protected_cancel"]["outside_successor_published"]=outside_published;
        require(outside_published,"outside cancelled debt returns to actual current publication");
        }
        }
      }
      {
        struct ReplaceDeadlineRestore {
          UFrameDeadline previous=UFrameDeadline::Get();
          ~ReplaceDeadlineRestore() { UFrameDeadline::Get()=previous; }
        } restore;
        for (const bool gpu : {false,true}) {
          UFrameDeadline::Get().BeginFrame(5000);
          UGpuGreedyMesher gpu_backend;
          UWorld replaced(nullptr,nullptr);
          replaced.SetSpawnPoint({8,-40,8});
          replaced.BlockWorld.SetVerticalProfile({-48,-33,-40});
          replaced.BlockRegistry=std::make_unique<UBlockRegistry>(nullptr,definitions);
          replaced.BlockWorld.SetFluidDefinitions(definitions.get());
          replaced.BlockWorld.SetKnownOpenSkyAbsentQuery([](glm::ivec3){return true;});
          auto replacement_settings=settings;
          replacement_settings.AsyncMeshing=true;
          replacement_settings.GpuPackedMeshing=gpu;
          replaced.SetRenderSettings(replacement_settings);
          auto &service=replaced.GetMeshService(); auto &rc=service.GetCache();
          if(gpu) service.SetMesherBackend(&gpu_backend);
          service.SetMeshRebuildFocus({0,0,0},1,1);
          service.SetMeshSnapshotBudgetMs(1000); service.SetMeshEmergeTotalBudgetMs(1000);
          MeshWorkAdmission admission;
          admission.enforce_schedule_lanes=true; admission.max_schedule=1;
          admission.first_mesh_schedule=1; admission.remesh_schedule=1;
          service.SetMeshWorkAdmission(admission);
          const auto populate=[&](bool successor) {
            replaced.BlockWorld.SetFluidDefinitions(definitions.get());
            replaced.BlockWorld.SetKnownOpenSkyAbsentQuery([](glm::ivec3){return true;});
            for(int x=0;x<16;++x) for(int z=0;z<16;++z)
              replaced.BlockWorld.SetBlock({x,-48,z},8);
            replaced.BlockWorld.SetBlock(successor?glm::ivec3(10,-40,10):glm::ivec3(8,-40,8),8);
            replaced.GetLightingPipeline().FillAllLoadedChunks(replaced.BlockWorld);
          };
          const auto submit=[&]() {
            rc.MarkDirtyPriority(coord);
            const auto *chunk=replaced.BlockWorld.GetChunkManager().GetChunk(coord);
            auto &demand=rc.GetRenderDemandStore(); const auto epoch=rc.GetCaptureStore().WorldEpoch();
            demand.NoteDemand(coord,rc.GetChunkMeshRevision(coord),chunk->GetLightFieldRevision(),
                0,1.0,epoch,chunk->GetIncarnation());
            demand.NoteLightCalculationSettled(coord,epoch,chunk->GetIncarnation(),
                chunk->GetLightFieldRevision(),chunk->GetContentRevision());
            require(WaitForProbeMeshCapture(rc,replaced.BlockWorld,*replaced.BlockRegistry,
                coord,rc.GetChunkMeshRevision(coord)),"world replacement actual mesh capture");
            const auto tick=rc.RebuildDirtyChunksWithStats(replaced.BlockWorld,
                *replaced.BlockRegistry,0,1,false,0,1000,true);
            require(tick.Scheduled==1 && rc.WaitForAsyncMeshIdleFor(std::chrono::seconds(5)),
                "world replacement actual worker completes");
          };
          const auto baseline=UPipelineAdmission::Get().GpuPendingBytes();
          populate(false); submit();
          if(gpu) service.DrainAsyncMeshResults(replaced.BlockWorld,*replaced.BlockRegistry,1);
          const bool old_accepted=gpu?rc.GetPendingGpuAppliesCount()==1:
              rc.GetMeshCompletedSize()==1;
          require(old_accepted,"world replacement retains actual old accepted work");
          const auto old_epoch=rc.GetCaptureStore().WorldEpoch();
          auto &old_fifo=*replaced.Persistence;
          old_fifo.EnqueueBoundaryDependencyRelight(replaced,0,0,-48,-33);
          old_fifo.NoteVisibleFirstMeshRelight({0,0},-48,-33);
          old_fifo.DeferFarRelightColumn({3,0},-48,-33,true,"q08_world_retirement");
          old_fifo.EnqueuePlayerRelight({{8,-40,8}});
          require(old_fifo.IsTerrainColumnRelightQueued({0,0}) &&
              old_fifo.GetTerrainColumnRelightQueueInfo({48,0}).deferred_far &&
              old_fifo.GetPendingPlayerRelightCount()==1,"world retirement has actual queued/deferred/player work");
          require(replaced.TryAbandonTerrainForWorldReplace(), "retire idle relight world");
          const auto old_queue=old_fifo.GetTerrainColumnRelightQueueInfo({0,0});
          const bool relight_retired=old_fifo.GetPendingTerrainColumnRelightCount()==0 &&
              old_fifo.GetPendingPlayerRelightCount()==0 && !old_queue.boundary_dependency &&
              !old_queue.y_band_defined && !old_fifo.GetTerrainColumnRelightQueueInfo({48,0}).deferred_far;
          report["relight_world_retirement"].push_back({{"gpu",gpu},{"retired",relight_retired},
              {"queued",old_fifo.GetPendingTerrainColumnRelightCount()},
              {"player",old_fifo.GetPendingPlayerRelightCount()}});
          require(relight_retired,"actual world replacement retires persistence relight owners");
          const bool retired=rc.GetCaptureStore().WorldEpoch()!=old_epoch &&
              rc.GetPendingGpuAppliesCount()==0 && rc.GetMeshCompletedSize()==0 &&
              !rc.HasInflightMeshBuild(coord) && !rc.HasPendingDirty() &&
              replaced.BlockWorld.CountNonAir()==0 &&
              UPipelineAdmission::Get().GpuPendingBytes()==baseline;
          require(retired,"actual UWorld teardown retires old accepted mesh and byte owner");
          service.DrainAsyncMeshResults(replaced.BlockWorld,*replaced.BlockRegistry,8);
          require(!rc.HasPendingDirty() && !rc.HasPublishedSectionDraw(coord),
              "old world result cannot recreate debt or draw after teardown");
          populate(true); submit();
          service.DrainAsyncMeshResults(replaced.BlockWorld,*replaced.BlockRegistry,8);
          for(int n=0;n<16 && rc.GetPendingGpuAppliesCount();++n) {
            rc.DrainPendingGpuMeshes(replaced.BlockWorld,*replaced.BlockRegistry,8,1000);
            glFinish();
          }
          const bool current=replaced.HasCurrentChunkSlicePublication(coord) &&
              rc.IsPublishedMeshCurrentForInputs(coord,replaced.BlockWorld) &&
              (!gpu || (rc.GetGpuMeshPipeline() && rc.GetGpuMeshPipeline()->HasGpuMesh(coord))) &&
              !rc.IsChunkMeshDirty(coord) && rc.GetPendingGpuAppliesCount()==0 &&
              UPipelineAdmission::Get().GpuPendingBytes()==baseline;
          report["mesh_world_replacement"].push_back({{"gpu",gpu},{"old_accepted",old_accepted},
              {"retired",retired},{"current",current}});
          require(current,"new terrain after actual UWorld teardown publishes current mesh");
        }
      }
      {
        struct FifoDeadlineRestore {
          UFrameDeadline previous=UFrameDeadline::Get();
          ~FifoDeadlineRestore() { UFrameDeadline::Get()=previous; }
        } restore;
        UFrameDeadline::Get().BeginFrame(5000);
        UWorld fifo_world(nullptr,nullptr);
        fifo_world.SetSpawnPoint({8,-40,8});
        fifo_world.ProceduralTemplate.MinHeight=-48;
        fifo_world.ProceduralTemplate.MaxHeight=31;
        fifo_world.ProceduralTemplate.AsyncRelight=false;
        fifo_world.BlockWorld.SetVerticalProfile({-48,31,-40});
        fifo_world.BlockRegistry=std::make_unique<UBlockRegistry>(nullptr,definitions);
        fifo_world.BlockWorld.SetFluidDefinitions(definitions.get());
        fifo_world.BlockWorld.SetKnownOpenSkyAbsentQuery([](glm::ivec3){return true;});
        auto full_settings=settings;
        full_settings.Lighting=LightingMode::Full;
        full_settings.AsyncMeshing=false;
        fifo_world.SetRenderSettings(full_settings);
        fifo_world.BlockWorld.SetBlock({8,-40,8},8);
        fifo_world.BlockWorld.SetBlock({8,24,8},8);
        auto &fifo=*fifo_world.Persistence;
        fifo.EnqueueTerrainColumnRelight(0,0,false,-48,-33,"q08_column_negative",&fifo_world);
        fifo.EnqueueTerrainColumnRelight(0,0,true,16,31,"q08_column_positive",&fifo_world);
        fifo.PromoteTerrainColumnRelight({0,0});
        fifo.PromoteTerrainColumnRelight({0,0});
        const auto info=fifo.GetTerrainColumnRelightQueueInfo({0,0});
        const bool exact=info.keyed && info.priority && info.in_deque && info.y_band_defined &&
            info.min_world_y==-48 && info.max_world_y==31 &&
            fifo.GetPendingTerrainColumnRelightCount()==1;
        require(exact,"column FIFO coalescing/promotion preserves signed union exactly once");
        const auto catalog=fifo_world.BlockRegistry->GetDefinitionsCatalogSnapshot();
        const auto installed=[&]() {
          return fifo_world.BlockWorld.HasCompleteLightSamplingCalculation({0,-3,0},catalog) &&
              fifo_world.BlockWorld.HasCompleteLightSamplingCalculation({0,1,0},catalog);
        };
        for(int n=0;n<8 && !installed();++n) {
          UFrameDeadline::Get().BeginFrame(5000);
          fifo.DrainTerrainColumnRelights(fifo_world,1);
        }
        report["relight_fifo_column_band"]={{"exact_signed_union",exact},
            {"negative_and_positive_installed",installed()},
            {"pending",fifo.GetPendingTerrainColumnRelightCount()}};
        require(installed(),"actual FIFO consumer installs current light in both signed bands");
      }
      {
        struct PressureDeadlineRestore {
          UFrameDeadline previous=UFrameDeadline::Get();
          ~PressureDeadlineRestore() { UFrameDeadline::Get()=previous; }
        } restore;
        UFrameDeadline::Get().BeginFrame(5000);
        UWorld fifo_world(nullptr,nullptr);
        fifo_world.SetSpawnPoint({56,-40,8});
        fifo_world.ProceduralTemplate.MinHeight=-48;
        fifo_world.ProceduralTemplate.MaxHeight=31;
        fifo_world.ProceduralTemplate.AsyncRelight=false;
        fifo_world.BlockWorld.SetVerticalProfile({-48,31,-40});
        fifo_world.BlockRegistry=std::make_unique<UBlockRegistry>(nullptr,definitions);
        fifo_world.BlockWorld.SetFluidDefinitions(definitions.get());
        fifo_world.BlockWorld.SetKnownOpenSkyAbsentQuery([](glm::ivec3){return true;});
        auto full_settings=settings;
        full_settings.Lighting=LightingMode::Full;
        full_settings.AsyncMeshing=false;
        fifo_world.SetRenderSettings(full_settings);
        fifo_world.BlockWorld.SetBlock({56,-40,8},8);
        fifo_world.BlockWorld.SetBlock({56,24,8},8);
        auto &fifo=*fifo_world.Persistence;
        fifo.TrimFarRelightFifoFarthest({0,0,0},16);
        for(int n=0;n<16;++n)
          fifo.EnqueueTerrainColumnRelight((n+8)*CHUNK_SIZE,0,false,-48,-33,"q08_pressure_blocker");
        require(fifo.GetPendingTerrainColumnRelightCount()==16,"pressure fixture fills actual FIFO to16");
        fifo.SetColumnLightComplete({3,0},true,"q08_pressure_baseline");
        fifo.EnqueueTerrainColumnRelight(48,0,false,-48,-33,"q08_pressure_negative",&fifo_world);
        fifo.EnqueueTerrainColumnRelight(48,0,true,16,31,"q08_pressure_positive",&fifo_world);
        const auto denied=fifo.GetTerrainColumnRelightQueueInfo({48,0});
        const bool retained=!denied.keyed && denied.deferred_far && denied.y_band_defined &&
            denied.min_world_y==-48 && denied.max_world_y==31 &&
            fifo.GetPendingTerrainColumnRelightCount()==16 && !fifo.IsColumnLightComplete({3,0});
        require(retained,"pressure denial coalesces signed work into deferred owner and invalidates disk complete");
        for(int n=0;n<16;++n) fifo.CancelTerrainColumnRelight({(n+8)*CHUNK_SIZE,0});
        require(fifo.GetPendingTerrainColumnRelightCount()==0,"explicit blocker retirement removes FIFO pressure");
        const int admitted=fifo.AdmitDeferredFarRelightColumns(fifo_world,{3,0,0},4);
        const auto current=fifo.GetTerrainColumnRelightQueueInfo({48,0});
        const bool readmitted=admitted==1 && current.keyed && current.y_band_defined &&
            current.min_world_y==-48 && current.max_world_y==31 && !current.deferred_far;
        require(readmitted,"deferred band re-admits exactly without new producer request");
        const auto catalog=fifo_world.BlockRegistry->GetDefinitionsCatalogSnapshot();
        const auto installed=[&]() {
          return fifo_world.BlockWorld.HasCompleteLightSamplingCalculation({3,-3,0},catalog) &&
              fifo_world.BlockWorld.HasCompleteLightSamplingCalculation({3,1,0},catalog);
        };
        for(int n=0;n<8 && !installed();++n) {
          UFrameDeadline::Get().BeginFrame(5000);
          fifo.DrainTerrainColumnRelights(fifo_world,1);
        }
        report["relight_fifo_pressure_band"]={{"deferred_exact",retained},
            {"readmitted_without_ingress",readmitted},{"negative_and_positive_installed",installed()},
            {"pending",fifo.GetPendingTerrainColumnRelightCount()}};
        require(installed(),"deferred actual FIFO successor installs both signed light targets");
      }
      {
        struct PlayerDeadlineRestore {
          UFrameDeadline previous=UFrameDeadline::Get();
          ~PlayerDeadlineRestore() { UFrameDeadline::Get()=previous; }
        } restore;
        UFrameDeadline::Get().BeginFrame(5000);
        UWorld fifo_world(nullptr,nullptr);
        fifo_world.SetSpawnPoint({8,-40,8});
        fifo_world.ProceduralTemplate.MinHeight=-48;
        fifo_world.ProceduralTemplate.MaxHeight=31;
        fifo_world.ProceduralTemplate.AsyncRelight=false;
        fifo_world.BlockWorld.SetVerticalProfile({-48,31,-40});
        fifo_world.BlockRegistry=std::make_unique<UBlockRegistry>(nullptr,definitions);
        fifo_world.BlockWorld.SetFluidDefinitions(definitions.get());
        fifo_world.BlockWorld.SetKnownOpenSkyAbsentQuery([](glm::ivec3){return true;});
        auto full_settings=settings;
        full_settings.Lighting=LightingMode::Full;
        full_settings.AsyncMeshing=false;
        fifo_world.SetRenderSettings(full_settings);
        fifo_world.BlockWorld.SetBlock({8,-40,8},8);
        fifo_world.BlockWorld.SetBlock({8,24,8},8);
        auto &fifo=*fifo_world.Persistence;
        const std::vector<glm::ivec3> first{{8,-40,8},{8,24,8},{8,-40,8}};
        const std::vector<glm::ivec3> second{{9,-40,8},{9,24,8}};
        fifo.EnqueuePlayerRelight({});
        fifo.EnqueuePlayerRelight(first);
        fifo.EnqueuePlayerRelight(second);
        require(fifo.GetPendingPlayerRelightCount()==2,"player FIFO retains separate full requests, empty ignored");
        fifo_world.SetLightingRelightDeferred(true);
        fifo_world.DrainRelightQueuesBudget(1,0);
        const bool retained=fifo.GetPendingPlayerRelightCount()==2;
        report["relight_player_band"]["deferred_retained"]=retained;
        require(retained,"player lighting deferral must not consume unexecuted requests");
        fifo_world.SetLightingRelightDeferred(false);
        const auto catalog=fifo_world.BlockRegistry->GetDefinitionsCatalogSnapshot();
        const auto installed=[&]() {
          return fifo_world.BlockWorld.HasCompleteLightSamplingCalculation({0,-3,0},catalog) &&
              fifo_world.BlockWorld.HasCompleteLightSamplingCalculation({0,1,0},catalog);
        };
        fifo_world.DrainRelightQueuesBudget(1,0);
        const bool first_completed=fifo.GetPendingPlayerRelightCount()==1 && installed();
        require(first_completed,"one player request preserves negative/positive positions through actual install");
        fifo_world.DrainRelightQueuesBudget(1,0);
        const bool complete=fifo.GetPendingPlayerRelightCount()==0 && installed();
        report["relight_player_band"]={{"deferred_retained",retained},
            {"first_request_installed_both",first_completed},{"all_requests_completed",complete}};
        require(complete,"remaining player request completes without new ingress");
      }
      {
        struct BoundaryDeadlineRestore {
          UFrameDeadline previous=UFrameDeadline::Get();
          ~BoundaryDeadlineRestore() { UFrameDeadline::Get()=previous; }
        } restore;
        UFrameDeadline::Get().BeginFrame(5000);
        UWorld fifo_world(nullptr,nullptr);
        fifo_world.SetSpawnPoint({-8,-40,-8});
        fifo_world.ProceduralTemplate.MinHeight=-48;
        fifo_world.ProceduralTemplate.MaxHeight=31;
        fifo_world.ProceduralTemplate.AsyncRelight=false;
        fifo_world.BlockWorld.SetVerticalProfile({-48,31,-40});
        fifo_world.BlockRegistry=std::make_unique<UBlockRegistry>(nullptr,definitions);
        fifo_world.BlockWorld.SetFluidDefinitions(definitions.get());
        fifo_world.BlockWorld.SetKnownOpenSkyAbsentQuery([](glm::ivec3){return true;});
        auto full_settings=settings;
        full_settings.Lighting=LightingMode::Full;
        full_settings.AsyncMeshing=false;
        fifo_world.SetRenderSettings(full_settings);
        fifo_world.BlockWorld.SetBlock({-8,-40,-8},8);
        fifo_world.BlockWorld.SetBlock({-8,24,-8},8);
        auto &fifo=*fifo_world.Persistence;
        fifo.EnqueueBoundaryDependencyRelight(fifo_world,-1,-1,-48,-33);
        fifo.EnqueueBoundaryDependencyRelight(fifo_world,-8,-8,16,31);
        fifo.EnqueueBoundaryDependencyRelight(fifo_world,-16,-16,-48,-33);
        const auto info=fifo.GetTerrainColumnRelightQueueInfo({-16,-16});
        const bool exact=info.keyed && info.priority && info.in_deque &&
            info.boundary_dependency && info.y_band_defined &&
            info.min_world_y==-48 && info.max_world_y==31 &&
            fifo.GetPendingTerrainColumnRelightCount()==1;
        require(exact,"boundary producer normalizes negative column key and preserves exact signed band once");
        const auto catalog=fifo_world.BlockRegistry->GetDefinitionsCatalogSnapshot();
        const auto installed=[&]() {
          return fifo_world.BlockWorld.HasCompleteLightSamplingCalculation({-1,-3,-1},catalog) &&
              fifo_world.BlockWorld.HasCompleteLightSamplingCalculation({-1,1,-1},catalog);
        };
        for(int n=0;n<8 && !installed();++n) {
          UFrameDeadline::Get().BeginFrame(5000);
          fifo.DrainTerrainColumnRelights(fifo_world,1);
        }
        const auto terminal=fifo.GetTerrainColumnRelightQueueInfo({-16,-16});
        const bool retired=!terminal.keyed && !terminal.boundary_dependency &&
            fifo.GetPendingTerrainColumnRelightCount()==0;
        report["relight_boundary_band"]={{"exact_signed_union",exact},
            {"negative_and_positive_installed",installed()},{"terminal_marker_retired",retired}};
        require(installed() && retired,"boundary exact target installs current light and retires only completed marker");
      }
      {
        struct VisibleBandDeadlineRestore {
          UFrameDeadline previous=UFrameDeadline::Get();
          ~VisibleBandDeadlineRestore() { UFrameDeadline::Get()=previous; }
        } restore;
        UFrameDeadline::Get().BeginFrame(5000);
        for(const bool first_mesh : {false,true}) {
        UWorld fifo_world(nullptr,nullptr);
        fifo_world.SetSpawnPoint({8,24,8});
        fifo_world.ProceduralTemplate.MinHeight=-48;
        fifo_world.ProceduralTemplate.MaxHeight=31;
        fifo_world.ProceduralTemplate.AsyncRelight=true;
        fifo_world.BlockWorld.SetVerticalProfile({-48,31,-40});
        fifo_world.BlockRegistry=std::make_unique<UBlockRegistry>(nullptr,definitions);
        fifo_world.BlockWorld.SetFluidDefinitions(definitions.get());
        fifo_world.BlockWorld.SetKnownOpenSkyAbsentQuery([](glm::ivec3){return true;});
        auto full_settings=settings;
        full_settings.Lighting=LightingMode::Full;
        full_settings.AsyncMeshing=false;
        fifo_world.SetRenderSettings(full_settings);
        fifo_world.BlockWorld.SetBlock({8,-40,8},8);
        fifo_world.BlockWorld.SetBlock({8,24,8},8);
        auto &fifo=*fifo_world.Persistence;
        fifo_world.AsyncRelight=std::make_unique<UAsyncRelightBuilder>(1);
        const auto initial_job=fifo_world.NextAsyncRelightJobId;
        uint8_t first_outcome=0,second_outcome=0;
        if(first_mesh) {
          fifo_world.NoteVisibleFirstMeshRelightBand({0,0},-48,-33);
          fifo.EnqueueTerrainColumnRelight(0,0,false,-48,-33,"q08_first_mesh_negative",&fifo_world);
          fifo_world.NoteVisibleFirstMeshRelightBand({0,0},16,31);
          fifo.EnqueueTerrainColumnRelight(0,0,true,16,31,"q08_first_mesh_positive",&fifo_world);
        } else {
          require(fifo.EnqueueVisibleDrawGateRelight(0,0,-48,-33,{0,0,0},4,{{0,0}},
              &first_outcome,nullptr,"q08_draw_gate_negative"),"draw-gate first band admitted");
          require(fifo.EnqueueVisibleDrawGateRelight(0,0,16,31,{0,0,0},4,{{0,0}},
              &second_outcome,nullptr,"q08_draw_gate_positive"),"draw-gate second band coalesced");
        }
        const auto info=fifo.GetTerrainColumnRelightQueueInfo({0,0});
        const bool exact=info.keyed && info.priority && info.in_deque && info.y_band_defined &&
            info.min_world_y==-48 && info.max_world_y==31 &&
            fifo.GetPendingTerrainColumnRelightCount()==1;
        require(exact,"visible producer preserves both signed targets under one priority key");
        const auto catalog=fifo_world.BlockRegistry->GetDefinitionsCatalogSnapshot();
        const auto installed=[&]() {
          return fifo_world.BlockWorld.HasCompleteLightSamplingCalculation({0,-3,0},catalog) &&
              fifo_world.BlockWorld.HasCompleteLightSamplingCalculation({0,1,0},catalog);
        };
        int submitted=0;
        for(int n=0;n<8 && !installed();++n) {
          UFrameDeadline::Get().BeginFrame(5000);
          fifo.DrainTerrainColumnRelights(fifo_world,1);
          require(fifo_world.AsyncRelight->WaitIdleFor(std::chrono::seconds(5)),
              "visible signed-band actual relight worker finishes");
          fifo_world.DrainAsyncRelightResults(8,false,false);
          submitted=static_cast<int>(fifo_world.NextAsyncRelightJobId-initial_job);
        }
        report["relight_visible_bands"].push_back({{"first_mesh",first_mesh},
            {"exact_signed_union",exact},{"first_outcome",first_outcome},
            {"second_outcome",second_outcome},{"actual_jobs",submitted},
            {"negative_and_positive_installed",installed()}});
        require(submitted>0 && installed(),"visible exact bands survive async surface selection and actual install");
        }
      }
      {
        struct TrimDeadlineRestore {
          UFrameDeadline previous=UFrameDeadline::Get();
          ~TrimDeadlineRestore() { UFrameDeadline::Get()=previous; }
        } restore;
        UFrameDeadline::Get().BeginFrame(5000);
        for(const bool priority : {false,true}) {
        UWorld fifo_world(nullptr,nullptr);
        fifo_world.SetSpawnPoint({152,-40,8});
        fifo_world.ProceduralTemplate.MinHeight=-48;
        fifo_world.ProceduralTemplate.MaxHeight=31;
        fifo_world.ProceduralTemplate.AsyncRelight=false;
        fifo_world.BlockWorld.SetVerticalProfile({-48,31,-40});
        fifo_world.BlockRegistry=std::make_unique<UBlockRegistry>(nullptr,definitions);
        fifo_world.BlockWorld.SetFluidDefinitions(definitions.get());
        fifo_world.BlockWorld.SetKnownOpenSkyAbsentQuery([](glm::ivec3){return true;});
        auto full_settings=settings;
        full_settings.Lighting=LightingMode::Full;
        full_settings.AsyncMeshing=false;
        fifo_world.SetRenderSettings(full_settings);
        fifo_world.BlockWorld.SetBlock({152,-40,8},8);
        fifo_world.BlockWorld.SetBlock({152,24,8},8);
        auto &fifo=*fifo_world.Persistence;
        fifo.EnqueueTerrainColumnRelight(144,0,priority,-48,31,"q08_trim_target",&fifo_world);
        fifo.EnqueueTerrainColumnRelight(0,0,false,-48,-33,"q08_trim_protected");
        require(fifo.GetPendingTerrainColumnRelightCount()==2,"trim fixture owns target and protected survivor");
        const int trimmed=fifo.TrimFarRelightFifoFarthest({0,0,0},1,0);
        const auto deferred=fifo.GetTerrainColumnRelightQueueInfo({144,0});
        const bool retained=trimmed==1 && !deferred.keyed && deferred.deferred_far &&
            deferred.y_band_defined && deferred.min_world_y==-48 && deferred.max_world_y==31 &&
            fifo.IsTerrainColumnRelightQueued({0,0}) && fifo.GetPendingTerrainColumnRelightCount()==1;
        require(retained,"normal or priority trim transfers exact signed target while protected survivor stays");
        fifo.CancelTerrainColumnRelight({0,0});
        const int admitted=fifo.AdmitDeferredFarRelightColumns(fifo_world,{9,0,0},4);
        const auto readmitted=fifo.GetTerrainColumnRelightQueueInfo({144,0});
        const bool exact=admitted==1 && readmitted.keyed && !readmitted.deferred_far &&
            readmitted.priority==priority && readmitted.y_band_defined &&
            readmitted.min_world_y==-48 && readmitted.max_world_y==31;
        require(exact,"trim successor retains signed interval and priority without new target ingress");
        const auto catalog=fifo_world.BlockRegistry->GetDefinitionsCatalogSnapshot();
        const auto installed=[&]() {
          return fifo_world.BlockWorld.HasCompleteLightSamplingCalculation({9,-3,0},catalog) &&
              fifo_world.BlockWorld.HasCompleteLightSamplingCalculation({9,1,0},catalog);
        };
        for(int n=0;n<8 && !installed();++n) {
          UFrameDeadline::Get().BeginFrame(5000);
          fifo.DrainTerrainColumnRelights(fifo_world,1);
        }
        report["relight_fifo_trim"].push_back({{"priority",priority},{"deferred_exact",retained},
            {"readmitted_exact",exact},{"negative_and_positive_installed",installed()},
            {"pending",fifo.GetPendingTerrainColumnRelightCount()}});
        require(installed(),"trimmed actual FIFO target eventually installs both signed sections");
        }
      }
      {
        struct EvictionDeadlineRestore {
          UFrameDeadline previous=UFrameDeadline::Get();
          int previous_cap=URuntimeTuning::Get().RelightFifoSoftCap;
          ~EvictionDeadlineRestore() { UFrameDeadline::Get()=previous; URuntimeTuning::Get().RelightFifoSoftCap=previous_cap; }
        } restore;
        UFrameDeadline::Get().BeginFrame(5000);
        for(int mode=0;mode<3;++mode) {
        URuntimeTuning::Get().RelightFifoSoftCap=mode==0?1:96;
        UWorld fifo_world(nullptr,nullptr);
        fifo_world.SetSpawnPoint({216,-40,8});
        fifo_world.ProceduralTemplate.MinHeight=-48;
        fifo_world.ProceduralTemplate.MaxHeight=31;
        fifo_world.ProceduralTemplate.AsyncRelight=false;
        fifo_world.BlockWorld.SetVerticalProfile({-48,31,-40});
        fifo_world.BlockRegistry=std::make_unique<UBlockRegistry>(nullptr,definitions);
        fifo_world.BlockWorld.SetFluidDefinitions(definitions.get());
        fifo_world.BlockWorld.SetKnownOpenSkyAbsentQuery([](glm::ivec3){return true;});
        auto full_settings=settings;
        full_settings.Lighting=LightingMode::Full;
        full_settings.AsyncMeshing=false;
        fifo_world.SetRenderSettings(full_settings);
        fifo_world.BlockWorld.SetBlock({216,-40,8},8);
        fifo_world.BlockWorld.SetBlock({216,24,8},8);
        auto &fifo=*fifo_world.Persistence;
        uint8_t outcome=0;
        if(mode==0) {
          fifo.EnqueueTerrainColumnRelight(208,0,false,-48,31,"q08_softcap_victim",&fifo_world);
          fifo.EnqueueTerrainColumnRelight(224,0,false,-48,-33,"q08_softcap_trigger");
        } else {
          std::vector<glm::ivec2> protected_columns;
          const int blockers=mode==1?15:24;
          for(int n=0;n<blockers;++n) {
            const int cx=n+30;
            fifo.EnqueueTerrainColumnRelight(cx*CHUNK_SIZE,0,false,-48,-33,"q08_visible_blocker");
            protected_columns.push_back({cx,0});
          }
          if(mode==1)
            fifo.EnqueueTerrainColumnRelight(208,0,false,-48,31,"q08_visible_fifo_victim",&fifo_world);
          else for(int cx=6;cx<=13;++cx) {
            uint8_t side_outcome=0;
            require(fifo.EnqueueVisibleDrawGateRelight(cx*CHUNK_SIZE,0,-48,31,{0,0,0},20,
                protected_columns,&side_outcome,nullptr,"q08_side_lane_target") && side_outcome==9,
                "visible side lane accepts bounded exact target");
          }
          require(fifo.EnqueueVisibleDrawGateRelight(80,0,-48,-33,{0,0,0},20,protected_columns,
              &outcome,nullptr,"q08_nearer_visible_target") && outcome==(mode==1?3:14),
              "visible replacement selects expected FIFO or bounded side-lane victim");
        }
        const auto deferred=fifo.GetTerrainColumnRelightQueueInfo({208,0});
        const bool retained=!deferred.keyed && !deferred.deferred_visible && deferred.deferred_far &&
            deferred.y_band_defined && deferred.min_world_y==-48 && deferred.max_world_y==31;
        report["relight_fifo_eviction"].push_back({{"mode",mode},{"outcome",outcome},{"retained_exact",retained}});
        require(retained,"every accepted evicted target retains exact deferred band");
        fifo.CancelTerrainColumnRelight({224,0});
        for(int cx=5;cx<=12;++cx) fifo.CancelTerrainColumnRelight({cx*CHUNK_SIZE,0});
        for(int cx=30;cx<54;++cx) fifo.CancelTerrainColumnRelight({cx*CHUNK_SIZE,0});
        URuntimeTuning::Get().RelightFifoSoftCap=96;
        const int admitted=fifo.AdmitDeferredFarRelightColumns(fifo_world,{13,0,0},4);
        const auto restored=fifo.GetTerrainColumnRelightQueueInfo({208,0});
        const bool exact=admitted==1 && restored.keyed && !restored.deferred_far &&
            restored.y_band_defined && restored.min_world_y==-48 && restored.max_world_y==31;
        require(exact,"evicted target restores exact band without fresh target request");
        const auto catalog=fifo_world.BlockRegistry->GetDefinitionsCatalogSnapshot();
        const auto installed=[&]() {
          return fifo_world.BlockWorld.HasCompleteLightSamplingCalculation({13,-3,0},catalog) &&
              fifo_world.BlockWorld.HasCompleteLightSamplingCalculation({13,1,0},catalog);
        };
        for(int n=0;n<8 && !installed();++n) {
          UFrameDeadline::Get().BeginFrame(5000);
          fifo.DrainTerrainColumnRelights(fifo_world,1);
        }
        report["relight_fifo_eviction"].back()["readmitted_exact"]=exact;
        report["relight_fifo_eviction"].back()["negative_and_positive_installed"]=installed();
        report["relight_fifo_eviction"].back()["pending"]=fifo.GetPendingTerrainColumnRelightCount();
        require(installed(),"evicted actual FIFO target installs both signed sections");
        }
      }
      {
        struct QuiesceFifoDeadlineRestore {
          UFrameDeadline previous=UFrameDeadline::Get();
          ~QuiesceFifoDeadlineRestore() { UFrameDeadline::Get()=previous; }
        } restore;
        UFrameDeadline::Get().BeginFrame(5000);
        for(const bool deferred : {false,true}) {
        UWorld fifo_world(nullptr,nullptr);
        fifo_world.SetSpawnPoint({8,-40,8});
        fifo_world.ProceduralTemplate.MinHeight=-48;
        fifo_world.ProceduralTemplate.MaxHeight=31;
        fifo_world.ProceduralTemplate.AsyncRelight=false;
        fifo_world.BlockWorld.SetVerticalProfile({-48,31,-40});
        fifo_world.BlockRegistry=std::make_unique<UBlockRegistry>(nullptr,definitions);
        fifo_world.BlockWorld.SetFluidDefinitions(definitions.get());
        fifo_world.BlockWorld.SetKnownOpenSkyAbsentQuery([](glm::ivec3){return true;});
        auto full_settings=settings;
        full_settings.Lighting=LightingMode::Full;
        full_settings.AsyncMeshing=false;
        fifo_world.SetRenderSettings(full_settings);
        fifo_world.BlockWorld.SetBlock({8,-40,8},8);
        fifo_world.BlockWorld.SetBlock({8,24,8},8);
        auto &fifo=*fifo_world.Persistence;
        if(deferred) fifo.DeferFarRelightColumn({0,0},-48,31,true,"q08_quiesce");
        else fifo.EnqueueTerrainColumnRelight(0,0,true,-48,31,"q08_quiesce",&fifo_world);
        const auto before=fifo.GetTerrainColumnRelightQueueInfo({0,0});
        require(before.y_band_defined && before.min_world_y==-48 && before.max_world_y==31 &&
            (deferred?before.deferred_far:before.keyed),"quiesce fixture owns exact queued or deferred band");
        const auto next_job=fifo_world.NextAsyncRelightJobId;
        fifo_world.QuiesceBackgroundWork(std::chrono::milliseconds(100));
        const auto paused=fifo.GetTerrainColumnRelightQueueInfo({0,0});
        const bool retained=paused.y_band_defined && paused.min_world_y==-48 && paused.max_world_y==31 &&
            (deferred?paused.deferred_far:paused.keyed) && fifo_world.NextAsyncRelightJobId==next_job;
        require(retained,"temporary quiesce retains FIFO/deferred owner without dispatch");
        fifo_world.ResumeAfterSessionSave();
        if(deferred) require(fifo.AdmitDeferredFarRelightColumns(fifo_world,{0,0,0},4)==1,
            "resumed deferred target re-admits without new ingress");
        const auto catalog=fifo_world.BlockRegistry->GetDefinitionsCatalogSnapshot();
        const auto installed=[&]() {
          return fifo_world.BlockWorld.HasCompleteLightSamplingCalculation({0,-3,0},catalog) &&
              fifo_world.BlockWorld.HasCompleteLightSamplingCalculation({0,1,0},catalog);
        };
        for(int n=0;n<8 && !installed();++n) {
          UFrameDeadline::Get().BeginFrame(5000);
          fifo.DrainTerrainColumnRelights(fifo_world,1);
        }
        report["relight_fifo_quiesce"].push_back({{"deferred",deferred},{"retained",retained},
            {"negative_and_positive_installed",installed()},{"pending",fifo.GetPendingTerrainColumnRelightCount()}});
        require(installed(),"quiesced FIFO/deferred request completes actual current install after resume");
        }
      }
      {
        bool lifecycle_ok=true;
        for(const bool replace : {false,true}) {
          UWorld pending_world(nullptr,nullptr);
          pending_world.ProceduralTemplate.MinHeight=-48;
          pending_world.ProceduralTemplate.MaxHeight=-33;
          pending_world.BlockWorld.SetVerticalProfile({-48,-33,-40});
          pending_world.BlockRegistry=std::make_unique<UBlockRegistry>(nullptr,definitions);
          pending_world.BlockWorld.SetFluidDefinitions(definitions.get());
          pending_world.SetRenderSettings(settings);
          auto &streaming=*pending_world.Streaming;
          const glm::ivec3 seed(-1,-3,0),shore(-1,0,0);
          // Lifecycle adapter: install already-accepted pending owners directly.
          // Producer/dedupe/physical drain are separate Q11 obligations.
          streaming.DeferredPhysicsSeedQueue.push_back(seed);
          streaming.DeferredShoreSealQueue.push_back(shore);
          streaming.DeferredShoreSealQueued.insert(shore);
          streaming.DeferredIntraChunkSealNeeded.insert(shore);
          if(replace) require(pending_world.TryAbandonTerrainForWorldReplace(), "retire idle seed world");
          else pending_world.QuiesceBackgroundWork(std::chrono::milliseconds(100));
          const bool retained=streaming.DeferredPhysicsSeedQueue==std::deque<glm::ivec3>{seed} &&
              streaming.DeferredShoreSealQueue==std::deque<glm::ivec3>{shore} &&
              streaming.DeferredShoreSealQueued.size()==1 && streaming.DeferredShoreSealQueued.count(shore)==1 &&
              streaming.DeferredIntraChunkSealNeeded.size()==1 && streaming.DeferredIntraChunkSealNeeded.count(shore)==1;
          const bool retired=streaming.DeferredPhysicsSeedQueue.empty() && streaming.DeferredShoreSealQueue.empty() &&
              streaming.DeferredShoreSealQueued.empty() && streaming.DeferredIntraChunkSealNeeded.empty();
          if(!replace) pending_world.ResumeAfterSessionSave();
          const bool accepted=replace?retired:retained;
          report["seed_seal_lifetime"].push_back({{"world_replace",replace},{"retained",retained},
              {"retired",retired},{"accepted",accepted}});
          lifecycle_ok=lifecycle_ok && accepted;
        }
        require(lifecycle_ok,"temporary quiesce retains seed/seal owners; permanent world replacement retires them");
      }
      {
        UWorld arrival(nullptr,nullptr);
        arrival.SetSpawnPoint({8,-40,8});
        arrival.ProceduralTemplate.MinHeight=-48; arrival.ProceduralTemplate.MaxHeight=31;
        arrival.ProceduralTemplate.SeaLevel=-40; arrival.ProceduralTemplate.FillWater=true;
        arrival.BlockWorld.SetVerticalProfile({-48,31,-40});
        arrival.BlockRegistry=std::make_unique<UBlockRegistry>(nullptr,definitions);
        arrival.BlockWorld.SetFluidDefinitions(definitions.get());
        arrival.SetRenderSettings(settings);
        arrival.BlockWorld.SetBlock({8,-40,8},9); arrival.BlockWorld.SetBlock({8,24,8},9);
        arrival.BlockWorld.SetFluidState({8,-40,8},FluidCellState::Flowing(3).WithKind(FluidKind::Water));
        arrival.BlockWorld.SetFluidState({8,24,8},FluidCellState::Flowing(3).WithKind(FluidKind::Water));
        PhysicsFeatureFlags seed_flags;
        seed_flags.EnableFluids=true; seed_flags.LiquidShadowMode=false;
        arrival.SetPhysicsProfile(PhysicsProfile::Standard);
        arrival.SetPhysicsFeatureFlags(seed_flags);
        auto &streaming=*arrival.Streaming;
        streaming.InitChunkScheduler(arrival);
        require(streaming.ChunkScheduler && streaming.ChunkScheduler->MarkDirty,
                "actual production generation-arrival callback installed");
        // Simulate two accepted column arrivals; execute the registered actual
        // consumer, not a replacement lambda. Q09 proves scheduler invocation.
        streaming.ChunkScheduler->MarkDirty({0,0,0},-40,-40,false);
        streaming.ChunkScheduler->MarkDirty({0,0,0},24,24,true);
        const bool bands=streaming.DeferredPhysicsSeedQueue==
            std::deque<glm::ivec3>{{0,-3,0},{0,1,0}};
        const bool shore=streaming.DeferredShoreSealQueue.size()==1 &&
            streaming.DeferredShoreSealQueued.count({0,0,0})==1 &&
            streaming.DeferredIntraChunkSealNeeded.count({0,0,0})==1;
        arrival.BlockPhysicsService->ClearFluidQueue();
        while(!streaming.DeferredPhysicsSeedQueue.empty()) {
          const auto seed=streaming.DeferredPhysicsSeedQueue.front();
          streaming.DeferredPhysicsSeedQueue.pop_front();
          SeedPhysicsOnChunkCommitted(arrival,seed,ChunkPhysicsSeedBudgets{});
        }
        const auto depth=arrival.BlockPhysicsService->GetFluidQueueStats().Depth;
        report["seed_arrival_bands"]={{"signed_sections",bands},{"shore_coalesced",shore},
            {"fluid_frontier_depth",depth},{"actual_seed_completed",depth==2}};
        require(bands && shore && depth==2,
                "actual generation arrival seeds both signed sections and coalesces shore obligation");
      }
      {
        auto sync_world_owner=std::make_unique<UWorld>(nullptr,nullptr);
        auto &sync_world=*sync_world_owner;
        sync_world.SetSpawnPoint({8,-40,8});
        sync_world.ProceduralTemplate.MinHeight=-48; sync_world.ProceduralTemplate.MaxHeight=-33;
        sync_world.ProceduralTemplate.SeaLevel=-40; sync_world.ProceduralTemplate.FillWater=true;
        sync_world.BlockWorld.SetVerticalProfile({-48,-33,-40});
        sync_world.BlockRegistry=std::make_unique<UBlockRegistry>(nullptr,definitions);
        sync_world.BlockWorld.SetFluidDefinitions(definitions.get());
        sync_world.SetRenderSettings(settings);
        sync_world.BlockWorld.SetBlock({8,-48,8},8);
        auto &streaming=*sync_world.Streaming;
        streaming.EnsureStreamer(sync_world.BlockWorld,*sync_world.BlockRegistry,17,
                                 sync_world.ProceduralTemplate);
        streaming.InitStreamerCallbacks(sync_world);
        require(streaming.Streamer->OnRelightTerrainColumn && streaming.ChunkScheduler->MarkDirty,
                "actual sync and async production arrival callbacks installed");
        streaming.Streamer->OnRelightTerrainColumn({0,0,0});
        streaming.Streamer->OnRelightTerrainColumn({0,0,0});
        const bool sync_deduped=streaming.DeferredShoreSealQueue.size()==1 &&
            streaming.DeferredShoreSealQueued.count({0,0,0})==1;
        const auto sync_count=streaming.DeferredShoreSealQueue.size();
        streaming.ChunkScheduler->MarkDirty({0,0,0},-48,-33,true);
        const bool shared=streaming.DeferredShoreSealQueue.size()==1 &&
            streaming.DeferredShoreSealQueued.count({0,0,0})==1;
        report["seal_sync_ingress"]={{"sync_deduped",sync_deduped},{"sync_count",sync_count},
            {"shared_async_owner",shared},{"mixed_count",streaming.DeferredShoreSealQueue.size()}};
        require(sync_deduped && shared,"sync shore arrival shares one dedupe owner with async arrival");
      }
      {
        struct DrainEnvironmentRestore {
          UFrameDeadline deadline=UFrameDeadline::Get();
          bool scan=PreferGpuFluidColumnScan();
          ~DrainEnvironmentRestore() { UFrameDeadline::Get()=deadline; SetPreferGpuFluidColumnScan(scan); }
        } restore;
        for(const bool world_replace : {false,true}) {
        auto drain_owner=std::make_unique<UWorld>(nullptr,nullptr);
        auto &drain=*drain_owner;
        drain.SetSpawnPoint({8,-40,8});
        drain.ProceduralTemplate.MinHeight=-48; drain.ProceduralTemplate.MaxHeight=-33;
        drain.ProceduralTemplate.SeaLevel=-40; drain.ProceduralTemplate.FillWater=true;
        drain.ProceduralTemplate.AsyncRelight=false;
        drain.BlockWorld.SetVerticalProfile({-48,-33,-40});
        drain.BlockRegistry=std::make_unique<UBlockRegistry>(nullptr,definitions);
        drain.BlockWorld.SetFluidDefinitions(definitions.get());
        drain.BlockWorld.SetKnownOpenSkyAbsentQuery([](glm::ivec3){return true;});
        drain.SetRenderSettings(settings);
        PhysicsFeatureFlags flags; flags.EnableFluids=true; flags.LiquidShadowMode=false;
        drain.SetPhysicsProfile(PhysicsProfile::Standard); drain.SetPhysicsFeatureFlags(flags);
        drain.SetLastMovementFrameMs(1);
        uint64_t old_incarnation=0;
        if(world_replace) {
          drain.BlockWorld.SetBlock({8,-45,8},8);
          drain.BlockWorld.SetBlock({3*CHUNK_SIZE+8,-44,8},8);
          old_incarnation=drain.BlockWorld.GetChunkManager().GetChunk({0,-3,0})->GetIncarnation();
          auto &old_streaming=*drain.Streaming;
          old_streaming.DeferredPhysicsSeedQueue={{0,-3,0},{3,-3,0}};
          old_streaming.DeferredShoreSealQueue={{0,0,0},{3,0,0}};
          old_streaming.DeferredShoreSealQueued={{0,0,0},{3,0,0}};
          old_streaming.DeferredIntraChunkSealNeeded={{0,0,0},{3,0,0}};
          require(drain.TryAbandonTerrainForWorldReplace(), "retire idle seal world");
          require(drain.BlockWorld.CountNonAir()==0 &&
              old_streaming.DeferredPhysicsSeedQueue.empty() && old_streaming.DeferredShoreSealQueue.empty() &&
              old_streaming.DeferredShoreSealQueued.empty() && old_streaming.DeferredIntraChunkSealNeeded.empty(),
              "actual world replacement retires old terrain and all accepted seed/seal owners");
        }
        for(int x=0;x<CHUNK_SIZE;++x) for(int z=0;z<CHUNK_SIZE;++z)
          drain.BlockWorld.SetBlock({x,-48,z},8);
        drain.BlockWorld.SetBlock({8,-40,8},9);
        drain.BlockWorld.SetFluidState({8,-40,8},FluidCellState::Flowing(3).WithKind(FluidKind::Water));
        const bool fresh_incarnation=!world_replace ||
            drain.BlockWorld.GetChunkManager().GetChunk({0,-3,0})->GetIncarnation()!=old_incarnation;
        require(fresh_incarnation,"replacement terrain owns a fresh actual incarnation");
        auto &streaming=*drain.Streaming;
        auto &drain_mesh=*drain.MeshService;
        auto &drain_cache=drain_mesh.GetCache();
        UFrameDeadline::Get().BeginFrame(5000);
        drain_mesh.RebuildChunkImmediate(drain.BlockWorld,*drain.BlockRegistry,{0,-3,0});
        require(drain_mesh.HasDrawableGreedyMesh({0,-3,0}),"drain starts with actual healthy mesh");
        SetPreferGpuFluidColumnScan(true);
        const auto *before=drain_cache.GetFluidSurfaceSlice(drain.BlockWorld,*drain.BlockRegistry,{0,0,0},-40);
        require(before && before->HasSurface(8,8) && before->BottomBlockY[8][8]==-40,
                "drain starts with actual warm pre-seal fluid map");
        // Accepted pending-owner adapter; actual producer boundaries are covered
        // by the three ingress receipts. Do not invoke seed/seal by hand here.
        streaming.DeferredPhysicsSeedQueue.push_back({0,-3,0});
        streaming.DeferredShoreSealQueue.push_back({0,0,0});
        streaming.DeferredShoreSealQueued.insert({0,0,0});
        streaming.DeferredIntraChunkSealNeeded.insert({0,0,0});
        bool seed_seen=false,mutation=false,save=false,light=false,map_invalidated=false;
        int ticks=0;
        for(;ticks<16;++ticks) {
          UFrameDeadline::Get().BeginFrame(5000);
          drain.DrainRelightQueuesBudget(0,8);
          drain_mesh.RebuildChunkImmediate(drain.BlockWorld,*drain.BlockRegistry,{0,-3,0});
          streaming.TickAsyncChunkSystems(drain);
          seed_seen=seed_seen || drain.BlockPhysicsService->GetFluidQueueStats().Depth>0;
          mutation=mutation || drain.BlockWorld.GetBlock({8,-41,8})==9;
          save=save || drain.ModifiedChunks.count({0,-3,0})==1;
          light=light || drain.HasPendingLightBeforeMeshNear({0,0,0},0) ||
              drain.Persistence->GetPendingTerrainColumnRelightCount()>0;
          map_invalidated=map_invalidated || drain_cache.GetFluidSurfaceDirtyGroundChunks().count({0,0,0})==1;
          if(streaming.DeferredPhysicsSeedQueue.empty() && streaming.DeferredShoreSealQueue.empty() &&
              streaming.DeferredShoreSealQueued.empty() && streaming.DeferredIntraChunkSealNeeded.empty()) break;
        }
        const bool terminal=streaming.DeferredPhysicsSeedQueue.empty() && streaming.DeferredShoreSealQueue.empty() &&
            streaming.DeferredShoreSealQueued.empty() && streaming.DeferredIntraChunkSealNeeded.empty();
        const auto direct=FindFluidColumnSurfaceAt(drain.BlockWorld,*drain.BlockRegistry,8,8,-40);
        const auto *current=drain_cache.GetFluidSurfaceSlice(drain.BlockWorld,*drain.BlockRegistry,{0,0,0},-40);
        const bool current_map=current && direct.valid && current->BottomBlockY[8][8]==direct.bottomBlockY &&
            current->SurfaceBlockY[8][8]==direct.surfaceBlockY && direct.bottomBlockY<-40;
        const bool old_world_absent=!world_replace ||
            !drain.BlockWorld.GetChunkManager().HasChunk({3,-3,0});
        report[world_replace?"seed_seal_replace_drain":"seed_seal_streaming_drain"]=
            {{"ticks",ticks+1},{"terminal",terminal},{"world_replace",world_replace},
            {"fresh_incarnation",fresh_incarnation},{"old_world_absent",old_world_absent},
            {"seed_seen",seed_seen},{"actual_seal_mutation",mutation},{"save_owner",save},
            {"light_owner",light},{"map_invalidated",map_invalidated},{"current_map",current_map},
            {"seed_pending",streaming.DeferredPhysicsSeedQueue.size()},
            {"shore_pending",streaming.DeferredShoreSealQueue.size()},
            {"focus_dirty",drain.PhysicsTelemetryData.FocusDirtyChunks},
            {"focus_missing",drain.PhysicsTelemetryData.FocusMissingMesh}};
        require(terminal && seed_seen && mutation && save && light && map_invalidated && current_map && old_world_absent,
                "actual streaming tick drains accepted seed/seal and retains downstream mutation owners");
        }
      }
      {
        struct UnloadDeadlineRestore {
          UFrameDeadline deadline=UFrameDeadline::Get();
          ~UnloadDeadlineRestore() { UFrameDeadline::Get()=deadline; }
        } restore;
        bool all_retired=true;
        for(const bool veto : {true,false}) {
          auto unloaded_owner=std::make_unique<UWorld>(nullptr,nullptr);
          auto &unloaded=*unloaded_owner;
          unloaded.SetSpawnPoint({8,-40,8});
          unloaded.ProceduralTemplate.MinHeight=-48; unloaded.ProceduralTemplate.MaxHeight=-33;
          unloaded.ProceduralTemplate.SeaLevel=-40; unloaded.ProceduralTemplate.FillWater=true;
          unloaded.ProceduralTemplate.AsyncChunkIo=false;
          unloaded.BlockWorld.SetVerticalProfile({-48,-33,-40});
          unloaded.BlockRegistry=std::make_unique<UBlockRegistry>(nullptr,definitions);
          unloaded.BlockWorld.SetFluidDefinitions(definitions.get());
          unloaded.SetRenderSettings(settings);
          const auto folder=root/(veto?"seed-unload-veto":"seed-unload-success");
          fs::create_directories(folder); unloaded.SetWorldFolderPath(folder.string());
          const glm::ivec3 old(0,0,0),kept(64,0,0),old_seed(0,-3,0),kept_seed(64,-3,0);
          for(int x=0;x<CHUNK_SIZE;++x) for(int z=0;z<CHUNK_SIZE;++z) {
            unloaded.BlockWorld.SetBlock({x,-48,z},8);
            unloaded.BlockWorld.SetBlock({64*CHUNK_SIZE+x,-48,z},8);
          }
          require(IsTerrainChunkComplete(unloaded.BlockWorld,old,-33,kNoTerrainChunkSlice,-48),
                  "unload fixture owns complete terrain before veto or save");
          auto &streaming=*unloaded.Streaming;
          streaming.EnsureStreamer(unloaded.BlockWorld,*unloaded.BlockRegistry,17,unloaded.ProceduralTemplate);
          streaming.InitStreamerCallbacks(unloaded);
          streaming.Streamer->SetRenderDistance(3); streaming.Streamer->SetMaxLoadOpsPerFrame(4);
          streaming.Streamer->SetMaxUnloadOpsPerFrame(8);
          streaming.DeferredPhysicsSeedQueue={old_seed,kept_seed};
          streaming.DeferredShoreSealQueue={old,kept};
          streaming.DeferredShoreSealQueued={old,kept};
          streaming.DeferredIntraChunkSealNeeded={old,kept};
          if(veto) unloaded.BlockWorld.GetChunkManager().MarkTerrainColumnReadUnresolved(old);
          const auto incarnation=unloaded.BlockWorld.GetChunkManager().GetChunk(old_seed)->GetIncarnation();
          UFrameDeadline::Get().BeginFrame(5000);
          PlayerCapsule capsule;
          // Fixed explicit unload observer outside old keep ring, inside kept
          // column. This isolated API scene is not camera travel evidence.
          streaming.Streamer->UnloadPass({64*CHUNK_SIZE+8,-40,8},
              {float(64*CHUNK_SIZE+8),-40,8},capsule);
          const bool old_resident=unloaded.BlockWorld.GetChunkManager().HasChunk(old_seed);
          const bool kept_exact=unloaded.BlockWorld.GetChunkManager().HasChunk(kept_seed) &&
              streaming.DeferredShoreSealQueued.count(kept)==1 &&
              streaming.DeferredIntraChunkSealNeeded.count(kept)==1;
          bool accepted=false;
          if(veto) accepted=old_resident && kept_exact &&
              streaming.DeferredPhysicsSeedQueue==std::deque<glm::ivec3>{old_seed,kept_seed} &&
              streaming.DeferredShoreSealQueue==std::deque<glm::ivec3>{old,kept} &&
              streaming.DeferredShoreSealQueued.count(old)==1 && streaming.DeferredIntraChunkSealNeeded.count(old)==1;
          else accepted=!old_resident && kept_exact &&
              streaming.DeferredPhysicsSeedQueue==std::deque<glm::ivec3>{kept_seed} &&
              streaming.DeferredShoreSealQueue==std::deque<glm::ivec3>{kept} &&
              streaming.DeferredShoreSealQueued.count(old)==0 && streaming.DeferredIntraChunkSealNeeded.count(old)==0;
          report["seed_seal_unload"].push_back({{"veto",veto},{"old_resident",old_resident},
              {"kept_exact",kept_exact},{"accepted",accepted}});
          all_retired=all_retired && accepted;
          if(!veto && accepted) {
            unloaded.BlockWorld.SetBlock({8,-40,8},9);
            streaming.ChunkScheduler->MarkDirty(old,-40,-40,false);
            const bool fresh=unloaded.BlockWorld.GetChunkManager().GetChunk(old_seed)->GetIncarnation()!=incarnation &&
                streaming.DeferredPhysicsSeedQueue==std::deque<glm::ivec3>{kept_seed,old_seed} &&
                streaming.DeferredShoreSealQueue==std::deque<glm::ivec3>{kept,old} &&
                streaming.DeferredShoreSealQueued.count(old)==1 && streaming.DeferredIntraChunkSealNeeded.count(old)==1;
            report["seed_seal_unload"].back()["fresh_reissue"]=fresh;
            all_retired=all_retired && fresh;
          }
        }
        require(all_retired,"unload veto preserves owners; successful unload retires only old column and permits fresh reissue");
      }
      RunTerrainProbePhase([&] {
        auto load_owner=std::make_unique<UWorld>(nullptr,nullptr);
        auto &load_world=*load_owner;
        load_world.SetSpawnPoint({8,-63,8});
        load_world.ProceduralTemplate.MinHeight=-64; load_world.ProceduralTemplate.MaxHeight=319;
        load_world.ProceduralTemplate.SeaLevel=63; load_world.ProceduralTemplate.AsyncChunkIo=true;
        load_world.ProceduralTemplate.AsyncRelight=false;
        load_world.BlockWorld.SetVerticalProfile({-64,319,63});
        load_world.BlockRegistry=std::make_unique<UBlockRegistry>(nullptr,definitions);
        load_world.BlockWorld.SetFluidDefinitions(definitions.get()); load_world.SetRenderSettings(settings);
        const glm::ivec3 column(0),lower(0,-4,0),edit(1,-63,1);
        const auto folder=root/"load-retry-exhaustion"; fs::create_directories(folder);
        load_world.SetWorldFolderPath(folder.string());
        UBlockWorld saved; saved.SetVerticalProfile({-64,319,63});
        saved.SetFluidDefinitions(definitions.get());
        UBinaryChunkSerializer serializer;
        for(int cy=-4;cy<=19;++cy) {
          const glm::ivec3 slice(0,cy,0);
          auto frame=std::make_unique<UChunk>(slice);
          if(cy==-4) {
            for(int x=0;x<CHUNK_SIZE;++x) for(int z=0;z<CHUNK_SIZE;++z)
              frame->SetBlockLocal({x,0,z},8);
            frame->SetBlockLocal({1,1,1},8);
          }
          if(cy==19) frame->SetBlockLocal({8,8,8},8);
          const auto decoded=serializer.Deserialize(serializer.Serialize(slice,*frame,*load_world.BlockRegistry).bytes,
                                                    slice,*load_world.BlockRegistry);
          require(ApplyTerrainSliceLoad(saved,slice,&decoded,nullptr,{column,1},1,false),
                  "retry fixture has24 real decoded terrain frames including AIR");
        }
        auto &persistence=*load_world.Persistence;
        require(persistence.GetChunkStorage().SaveTerrainColumn(column,saved,folder.string(),
                    *load_world.BlockRegistry,319,-64),"retry fixture persisted by actual storage");
        auto registry_owner=std::move(load_world.BlockRegistry);
        persistence.RequestAsyncTerrainColumnLoad(load_world,column,96001);
        const bool dependency_rejected=persistence.PendingAsyncColumnLoadSlices.empty() &&
            load_world.BlockWorld.CountNonAir()==0;
        load_world.BlockRegistry=std::move(registry_owner);
        persistence.RequestAsyncTerrainColumnLoad(load_world,{4,7,0},96002);
        const bool miss_rejected=persistence.PendingAsyncColumnLoadSlices.empty() &&
            load_world.BlockWorld.CountNonAir()==0;
        persistence.GetChunkStorage().MarkColumnSavePending(column);
        persistence.RequestAsyncTerrainColumnLoad(load_world,column,96003);
        const bool save_rejected=persistence.PendingAsyncColumnLoadSlices.empty() &&
            persistence.GetChunkStorage().IsColumnSavePending(column);
        persistence.GetChunkStorage().ClearColumnSavePending(column);
        report["disk_load_admission"]={{"dependency_rejected",dependency_rejected},
            {"disk_miss_rejected",miss_rejected},{"pending_save_rejected",save_rejected}};
        require(dependency_rejected && miss_rejected && save_rejected,
                "load refusal leaves no ghost owner or terrain before dependency/save release");
        persistence.ReadFailureProbe=DiskReadFailureProbe(glm::ivec2(0,0));
        persistence.ReadFailureProbe.ObserveFocus({16,0,0});
        persistence.RequestAsyncTerrainColumnLoad(load_world,{0,7,0},96101);
        require(persistence.IsTerrainColumnDiskLoadPending(column),"retry request admitted");
        const auto initial_owner=persistence.PendingAsyncColumnLoadSlices.at(column).cancellation;
        const auto initial_remaining=persistence.PendingAsyncColumnLoadSlices.at(column).remaining_results;
        const auto initial_token=load_world.Streaming->GetChunkGenTokens().Current(column).sequence;
        persistence.RequestAsyncTerrainColumnLoad(load_world,{0,-99,0},96999);
        const auto &unchanged=persistence.PendingAsyncColumnLoadSlices.at(column);
        const bool duplicate_preserved=unchanged.cancellation==initial_owner && unchanged.demand_id==96101 &&
            unchanged.remaining_results==initial_remaining && persistence.PendingAsyncColumnLoadSlices.size()==1 &&
            load_world.Streaming->GetChunkGenTokens().Current(column).sequence==initial_token;
        report["disk_load_admission"]["normalized_duplicate_preserved"]=duplicate_preserved;
        require(duplicate_preserved,"duplicate normalized request preserves exact accepted owner and token");
        const auto deadline=std::chrono::steady_clock::now()+std::chrono::seconds(10);
        bool edited=false; uint64_t incarnation=0; int max_retry=0;
        std::unordered_set<uint64_t> demands;
        while(persistence.IsTerrainColumnDiskLoadPending(column)) {
          const auto &pending=persistence.PendingAsyncColumnLoadSlices.at(column);
          max_retry=std::max(max_retry,pending.retry_generation); demands.insert(pending.demand_id);
          persistence.TickAsyncChunkIo(load_world,1,0);
          if(!edited && load_world.BlockWorld.GetChunkManager().HasDecodedTerrainSlice(lower)) {
            load_world.BlockWorld.SetBlock(edit,BLOCK_AIR);
            load_world.ModifiedChunks.insert(lower);
            incarnation=load_world.BlockWorld.GetChunkManager().GetChunk(lower)->GetIncarnation(); edited=true;
            const auto owner=persistence.PendingAsyncColumnLoadSlices.at(column).cancellation;
            const auto demand=persistence.PendingAsyncColumnLoadSlices.at(column).demand_id;
            const auto remaining=persistence.PendingAsyncColumnLoadSlices.at(column).remaining_results;
            persistence.RequestAsyncTerrainColumnLoad(load_world,column,97000);
            const auto &after=persistence.PendingAsyncColumnLoadSlices.at(column);
            const bool partial_preserved=after.cancellation==owner && after.demand_id==demand &&
                after.remaining_results==remaining && load_world.BlockWorld.GetBlock(edit)==BLOCK_AIR;
            report["disk_load_admission"]["partial_duplicate_preserved"]=partial_preserved;
            require(partial_preserved,"partial-load duplicate cannot reset owner, slice count or local edit");
          }
          require(std::chrono::steady_clock::now()<deadline,"actual failed disk batch terminates in10s");
          std::this_thread::yield();
        }
        const auto retained=[&] {
          return edited && load_world.BlockWorld.GetBlock(edit)==BLOCK_AIR &&
              load_world.BlockWorld.GetChunkManager().GetChunk(lower)->GetIncarnation()==incarnation &&
              load_world.ModifiedChunks.count(lower)==1;
        };
        const bool exhausted=max_retry==4 && demands.size()==5 && retained() &&
            load_world.BlockWorld.GetChunkManager().HasUnresolvedTerrainColumnRead(column) &&
            persistence.FailedColumnRetryAfter.count(column)==1;
        persistence.RequestAsyncTerrainColumnLoad(load_world,column,96102);
        const bool cooldown=!persistence.IsTerrainColumnDiskLoadPending(column);
        require(exhausted && cooldown,"retry exhaustion preserves edit/unresolved owner and throttles reissue");
        // Explicit clock adapter: prove expired-gate recovery without sleeping.
        persistence.FailedColumnRetryAfter[column]=std::chrono::steady_clock::now();
        persistence.RequestAsyncTerrainColumnLoad(load_world,column,96103);
        require(persistence.IsTerrainColumnDiskLoadPending(column),"expired cooldown permits new current load");
        const auto recovered_by=std::chrono::steady_clock::now()+std::chrono::seconds(10);
        while(persistence.IsTerrainColumnDiskLoadPending(column)) {
          persistence.TickAsyncChunkIo(load_world,1,0);
          require(std::chrono::steady_clock::now()<recovered_by,"current disk reissue completes in10s");
          std::this_thread::yield();
        }
        int decoded_count=0;
        for(int cy=-4;cy<=19;++cy) decoded_count+=load_world.BlockWorld.GetChunkManager().HasDecodedTerrainSlice({0,cy,0});
        const bool recovered=decoded_count==24 && retained() &&
            !load_world.BlockWorld.GetChunkManager().HasUnresolvedTerrainColumnRead(column) &&
            persistence.FailedColumnRetryAfter.count(column)==0 &&
            load_world.BlockWorld.GetBlock({8,19*CHUNK_SIZE+8,8})==8;
        report["disk_load_retry_exhaustion"]={{"max_retry",max_retry},{"failed_demands",demands.size()},
            {"exhausted_retained",exhausted},{"cooldown_rejected",cooldown},
            {"decoded_sections",decoded_count},{"current_recovered",recovered}};
        report["disk_load_admission"]["current_completion_after_release"]=recovered;
        require(recovered,"actual current disk load fully recovers without replaying local deletion");
        for(int phase=0;phase<4;++phase) {
          auto cancel_owner=std::make_unique<UWorld>(nullptr,nullptr);
          auto &cancel_world=*cancel_owner;
          cancel_world.SetSpawnPoint({8,-63,8});
          cancel_world.ProceduralTemplate.MinHeight=-64; cancel_world.ProceduralTemplate.MaxHeight=319;
          cancel_world.ProceduralTemplate.SeaLevel=63; cancel_world.ProceduralTemplate.AsyncChunkIo=true;
          cancel_world.ProceduralTemplate.AsyncRelight=false;
          cancel_world.BlockWorld.SetVerticalProfile({-64,319,63});
          cancel_world.BlockRegistry=std::make_unique<UBlockRegistry>(nullptr,definitions);
          cancel_world.BlockWorld.SetFluidDefinitions(definitions.get()); cancel_world.SetRenderSettings(settings);
          cancel_world.SetWorldFolderPath(folder.string());
          const auto base=serializer.Deserialize(serializer.Serialize(lower,*saved.GetChunkManager().GetChunk(lower),
                    *cancel_world.BlockRegistry).bytes,lower,*cancel_world.BlockRegistry);
          require(ApplyTerrainSliceLoad(cancel_world.BlockWorld,lower,&base,nullptr,{column,0},0,false),
                  "cancel fixture starts with owned complete lower base");
          auto &pending=*cancel_world.Persistence; pending.EnsureChunkIoInitialized();
          auto &io=*pending.AsyncChunkIo;
          struct PoolGate {
            std::shared_ptr<std::atomic<int>> entered=std::make_shared<std::atomic<int>>(0);
            std::shared_ptr<std::atomic<bool>> timedOut=std::make_shared<std::atomic<bool>>(false);
            std::promise<void> release;
            std::shared_future<void> resume=release.get_future().share();
            bool released=false;
            void Release() { if(!released) { released=true; release.set_value(); } }
            ~PoolGate() { Release(); }
          };
          std::unique_ptr<PoolGate> pool_gate;
          const auto block_pool=[&] {
            pool_gate=std::make_unique<PoolGate>();
            const auto workers=io.GetLoadWorkerCount();
            for(size_t n=0;n<workers;++n) {
              auto entered=pool_gate->entered; auto resume=pool_gate->resume; auto timed_out=pool_gate->timedOut;
              io.LoadPool.Enqueue([entered,resume,timed_out] {
                ++*entered;
                if(resume.wait_for(std::chrono::seconds(5))!=std::future_status::ready) timed_out->store(true);
              });
            }
            const auto until=std::chrono::steady_clock::now()+std::chrono::seconds(5);
            while(size_t(pool_gate->entered->load())!=workers && std::chrono::steady_clock::now()<until)
              std::this_thread::yield();
            require(size_t(pool_gate->entered->load())==workers,"actual load pool workers held before queued requests");
          };
          const uint64_t old_demand=98000+phase*2,new_demand=old_demand+1;
          std::shared_ptr<AsyncLoadReadProbeGate> read_gate;
          if(phase==0) block_pool();
          if(phase==1) {
            read_gate=std::make_shared<AsyncLoadReadProbeGate>(); read_gate->demandId=old_demand;
            io.LoadReadProbeGate=read_gate;
          }
          pending.RequestAsyncTerrainColumnLoad(cancel_world,column,old_demand);
          const auto old_owner=pending.PendingAsyncColumnLoadSlices.at(column).cancellation;
          if(phase==0) require(io.GetLoadPendingJobCount()==24,"old disk work is actually pool queued");
          if(phase==1) {
            const auto until=std::chrono::steady_clock::now()+std::chrono::seconds(5);
            while(!read_gate->entered.load() && std::chrono::steady_clock::now()<until) std::this_thread::yield();
            require(read_gate->entered.load() && !read_gate->timedOut.load(),"actual old read/decode held running");
          }
          if(phase>=2) {
            require(io.WaitIdleFor(std::chrono::seconds(5)) && io.GetLoadResultQueueDepth()==24,
                    "actual old disk completion cohort ready");
            if(phase==3) {
              for(int n=0;n<24 && !cancel_world.BlockWorld.GetChunkManager().HasLoadedAirSlice({0,-3,0});++n)
                pending.TickAsyncChunkIo(cancel_world,1,0);
              require(cancel_world.BlockWorld.GetChunkManager().HasLoadedAirSlice({0,-3,0}) &&
                  pending.IsTerrainColumnDiskLoadPending(column),"actual old cohort partially installed AIR");
            }
          }
          auto late=phase>=2?io.DrainLoads():std::vector<AsyncChunkLoadResult>{};
          const auto late_count=late.size();
          pending.CancelAsyncTerrainColumnLoad(cancel_world,column);
          require(old_owner->load() && !pending.IsTerrainColumnDiskLoadPending(column),"cancel retires exact disk owner");
          cancel_world.BlockWorld.SetBlock(edit,BLOCK_AIR); cancel_world.ModifiedChunks.insert(lower);
          const auto edited_incarnation=cancel_world.BlockWorld.GetChunkManager().GetChunk(lower)->GetIncarnation();
          io.LoadReadProbeGate.reset();
          if(phase>=2) block_pool();
          pending.RequestAsyncTerrainColumnLoad(cancel_world,column,new_demand);
          const auto new_owner=pending.PendingAsyncColumnLoadSlices.at(column).cancellation;
          require(new_owner!=old_owner,"current disk request owns independent cancellation identity");
          if(phase>=2) {
            io.RequeueLoads(std::move(late)); // delayed actual old payloads, no fabricated buffer
            for(size_t n=0;n<late_count;++n) pending.TickAsyncChunkIo(cancel_world,1,0);
            const auto &current=pending.PendingAsyncColumnLoadSlices.at(column);
            require(current.cancellation==new_owner && current.demand_id==new_demand && current.remaining_results==24 &&
                cancel_world.BlockWorld.GetBlock(edit)==BLOCK_AIR,
                "late old disk payload cannot consume or erase newer pending owner/edit");
          }
          if(pool_gate) pool_gate->Release();
          if(read_gate) read_gate->released.store(true);
          require(io.WaitIdleFor(std::chrono::seconds(5)) && (!read_gate || !read_gate->timedOut.load()) &&
                  (!pool_gate || !pool_gate->timedOut->load()),
                  "actual cancelled/current disk workers finish after barriers release");
          const auto current_by=std::chrono::steady_clock::now()+std::chrono::seconds(10);
          while(pending.IsTerrainColumnDiskLoadPending(column)) {
            pending.TickAsyncChunkIo(cancel_world,1,0);
            require(std::chrono::steady_clock::now()<current_by,"current disk successor completes after cancellation");
            std::this_thread::yield();
          }
          int decoded_sections=0;
          for(int cy=-4;cy<=19;++cy) decoded_sections+=cancel_world.BlockWorld.GetChunkManager().HasDecodedTerrainSlice({0,cy,0});
          const bool successor=decoded_sections==24 && cancel_world.BlockWorld.GetBlock(edit)==BLOCK_AIR &&
              cancel_world.BlockWorld.GetChunkManager().GetChunk(lower)->GetIncarnation()==edited_incarnation &&
              cancel_world.ModifiedChunks.count(lower)==1 &&
              cancel_world.BlockWorld.GetBlock({8,19*CHUNK_SIZE+8,8})==8 &&
              !cancel_world.BlockWorld.GetChunkManager().HasUnresolvedTerrainColumnRead(column);
          report["disk_load_cancel_stale"].push_back({{"phase",phase},{"late_results",late_count},
              {"old_cancelled",old_owner->load()},{"new_owner_distinct",new_owner!=old_owner},
              {"decoded_sections",decoded_sections},{"current_successor",successor}});
          require(successor,"actual current load completes and retains local deletion after old cancellation");
        }
        // Real peer files let radius cancellation preserve an independently
        // admitted boundary demand; no fabricated pending map entry.
        const glm::ivec3 peer(1,0,0),owned_air(0,-3,0),prior_air(0,-2,0),materialized(1,-47,1);
        auto peer_saved=std::make_unique<UBlockWorld>(); peer_saved->SetVerticalProfile({-64,319,63});
        for(int cy=-4;cy<=19;++cy) {
          const glm::ivec3 slice(1,cy,0); auto frame=std::make_unique<UChunk>(slice);
          if(cy==-4) for(int x=0;x<CHUNK_SIZE;++x) for(int z=0;z<CHUNK_SIZE;++z) frame->SetBlockLocal({x,0,z},8);
          if(cy==19) frame->SetBlockLocal({8,8,8},8);
          const auto decoded=serializer.Deserialize(serializer.Serialize(slice,*frame,*load_world.BlockRegistry).bytes,
                                                    slice,*load_world.BlockRegistry);
          require(ApplyTerrainSliceLoad(*peer_saved,slice,&decoded,nullptr,{peer,0},0,false),"peer disk fixture decoded");
        }
        require(persistence.GetChunkStorage().SaveTerrainColumn(peer,*peer_saved,folder.string(),
                    *load_world.BlockRegistry,319,-64),"peer disk fixture saved");
        for(int operation=0;operation<3;++operation) for(const bool materialize : {false,true}) {
          auto reset_owner=std::make_unique<UWorld>(nullptr,nullptr); auto &reset=*reset_owner;
          reset.SetSpawnPoint({8,-47,8});
          reset.ProceduralTemplate.MinHeight=-64; reset.ProceduralTemplate.MaxHeight=319;
          reset.ProceduralTemplate.SeaLevel=63; reset.ProceduralTemplate.AsyncChunkIo=true;
          reset.ProceduralTemplate.AsyncRelight=false; reset.BlockWorld.SetVerticalProfile({-64,319,63});
          reset.BlockRegistry=std::make_unique<UBlockRegistry>(nullptr,definitions);
          reset.BlockWorld.SetFluidDefinitions(definitions.get()); reset.SetRenderSettings(settings);
          reset.SetWorldFolderPath(folder.string());
          auto prior_frame=std::make_unique<UChunk>(prior_air);
          const auto prior=serializer.Deserialize(serializer.Serialize(prior_air,*prior_frame,*reset.BlockRegistry).bytes,
                                                  prior_air,*reset.BlockRegistry);
          require(ApplyTerrainSliceLoad(reset.BlockWorld,prior_air,&prior,nullptr,{column,0},0,false),
                  "preexisting AIR owner installed independently of cancelled demand");
          const auto prior_incarnation=reset.BlockWorld.GetChunkManager().GetSectionLightField(prior_air)->GetIncarnation();
          auto &pending=*reset.Persistence;
          const uint64_t old_id=99000+operation*4+(materialize?2:0),new_id=old_id+1;
          pending.RequestAsyncTerrainColumnLoad(reset,column,old_id);
          pending.RequestAsyncTerrainColumnLoad(reset,peer,old_id+100);
          auto &io=*pending.AsyncChunkIo;
          const auto old_owner=pending.PendingAsyncColumnLoadSlices.at(column).cancellation;
          const auto peer_owner=pending.PendingAsyncColumnLoadSlices.at(peer).cancellation;
          require(io.WaitIdleFor(std::chrono::seconds(5)) && io.GetLoadResultQueueDepth()==48,
                  "two real disk cohorts completed before exact partial AIR install");
          auto all=io.DrainLoads(); std::vector<AsyncChunkLoadResult> late,peer_results,one;
          for(auto &result:all) {
            if(result.coord==owned_air) one.push_back(std::move(result));
            else if(result.coord.x==0) late.push_back(std::move(result));
            else peer_results.push_back(std::move(result));
          }
          require(one.size()==1 && late.size()==23 && peer_results.size()==24,"actual payload cohorts partitioned");
          io.RequeueLoads(std::move(one)); pending.TickAsyncChunkIo(reset,1,0);
          require(reset.BlockWorld.GetChunkManager().HasLoadedAirSlice(owned_air) &&
              !reset.BlockWorld.GetChunkManager().HasResidentGeometryColumn(column),"only demand-owned AIR partially installed");
          const auto old_air_incarnation=reset.BlockWorld.GetChunkManager().GetSectionLightField(owned_air)->GetIncarnation();
          if(materialize) { reset.BlockWorld.SetBlock(materialized,8); reset.ModifiedChunks.insert(owned_air); }
          const auto peer_token=reset.Streaming->GetChunkGenTokens().Current(peer).sequence;
          if(operation==0) require(pending.CancelAsyncTerrainColumnLoadsOutsideRadius(reset,{1,400,0},0)==1,
                                  "outside radius cancels only abandoned target");
          if(operation==1) (void)pending.AbortAsyncChunkIoFor(reset,std::chrono::milliseconds(0));
          if(operation==2) require(reset.TryAbandonTerrainForWorldReplace(), "retire idle generation before disk reset");
          const auto prior_field=reset.BlockWorld.GetChunkManager().GetSectionLightField(prior_air);
          const bool old_retired=operation==2?
              reset.BlockWorld.CountNonAir()==0 && !prior_field &&
                  !reset.BlockWorld.GetChunkManager().HasLoadedAirSlice(owned_air):
              prior_field && prior_field->GetIncarnation()==prior_incarnation &&
                  (materialize?reset.BlockWorld.GetBlock(materialized)==8:
                      !reset.BlockWorld.GetChunkManager().HasLoadedAirSlice(owned_air) &&
                      !reset.BlockWorld.GetChunkManager().GetSectionLightField(owned_air));
          const bool peer_preserved=operation!=0 ||
              pending.PendingAsyncColumnLoadSlices.at(peer).cancellation==peer_owner && !peer_owner->load() &&
              pending.PendingAsyncColumnLoadSlices.at(peer).remaining_results==24 &&
              reset.Streaming->GetChunkGenTokens().Current(peer).sequence==peer_token;
          require(old_owner->load() && !pending.IsTerrainColumnDiskLoadPending(column) && old_retired && peer_preserved,
                  "retirement removes only exact old AIR; preexisting/materialized/retained owners survive");
          auto current_gate=std::make_shared<AsyncLoadReadProbeGate>(); current_gate->demandId=new_id;
          io.LoadReadProbeGate=current_gate;
          pending.RequestAsyncTerrainColumnLoad(reset,column,new_id);
          const auto current_owner=pending.PendingAsyncColumnLoadSlices.at(column).cancellation;
          const auto entered_by=std::chrono::steady_clock::now()+std::chrono::seconds(5);
          while(!current_gate->entered.load() && std::chrono::steady_clock::now()<entered_by) std::this_thread::yield();
          require(current_gate->entered.load() && !current_gate->timedOut.load(),"current actual disk worker held after retirement");
          io.RequeueLoads(std::move(late));
          for(int n=0;n<23;++n) pending.TickAsyncChunkIo(reset,1,0);
          const auto &current=pending.PendingAsyncColumnLoadSlices.at(column);
          require(current.cancellation==current_owner && current.demand_id==new_id && current.remaining_results==24,
                  "late retired AIR/geometry results cannot consume new load owner");
          current_gate->released.store(true); io.LoadReadProbeGate.reset();
          require(io.WaitIdleFor(std::chrono::seconds(5)) && !current_gate->timedOut.load(),"current worker gate released without timeout");
          io.RequeueLoads(std::move(peer_results));
          const auto completed_by=std::chrono::steady_clock::now()+std::chrono::seconds(10);
          while(!pending.PendingAsyncColumnLoadSlices.empty() || io.GetLoadResultQueueDepth()>0) {
            pending.TickAsyncChunkIo(reset,1,0);
            require(std::chrono::steady_clock::now()<completed_by,"radius/abort/replacement current recovery bound");
            std::this_thread::yield();
          }
          int current_decoded=0,peer_decoded=0;
          for(int cy=-4;cy<=19;++cy) {
            current_decoded+=reset.BlockWorld.GetChunkManager().HasDecodedTerrainSlice({0,cy,0});
            peer_decoded+=reset.BlockWorld.GetChunkManager().HasDecodedTerrainSlice({1,cy,0});
          }
          const auto new_air=reset.BlockWorld.GetChunkManager().GetSectionLightField(owned_air);
          const bool successor=current_decoded==24 &&
              reset.BlockWorld.GetBlock({8,19*CHUNK_SIZE+8,8})==8 &&
              !reset.BlockWorld.GetChunkManager().HasUnresolvedTerrainColumnRead(column) &&
              (operation==0?peer_decoded==24:peer_decoded==0) &&
              (materialize && operation!=2?reset.BlockWorld.GetBlock(materialized)==8:
                  new_air && new_air->GetIncarnation()!=old_air_incarnation);
          report["disk_load_outside_reset"].push_back({{"operation",operation},{"materialized",materialize},
              {"exact_retirement",old_retired},{"peer_preserved",peer_preserved},
              {"current_decoded",current_decoded},{"peer_decoded",peer_decoded},{"current_successor",successor}});
          require(successor,"actual current load recovers after exact AIR retirement/radius/abort/world replacement");
        }
        // Save admission shares this actual complete, locally edited terrain.
        auto save_registry=std::move(load_world.BlockRegistry);
        const bool dependency_refused=!persistence.RequestAsyncTerrainColumnSave(load_world,column) &&
            persistence.PendingAsyncColumnSaveSlices.empty() && load_world.ModifiedChunks.count(lower)==1;
        load_world.BlockRegistry=std::move(save_registry);
        auto &save_storage=persistence.GetChunkStorage();
        save_storage.MarkColumnSavePending(column);
        const bool save_pending_refused=!persistence.RequestAsyncTerrainColumnSave(load_world,column) &&
            persistence.PendingAsyncColumnSaveSlices.empty() && save_storage.IsColumnSavePending(column) &&
            load_world.ModifiedChunks.count(lower)==1;
        save_storage.ClearColumnSavePending(column);
        auto incomplete=std::make_unique<UWorld>(nullptr,nullptr);
        incomplete->ProceduralTemplate.MinHeight=-64; incomplete->ProceduralTemplate.MaxHeight=319;
        incomplete->BlockWorld.SetVerticalProfile({-64,319,63});
        incomplete->BlockRegistry=std::make_unique<UBlockRegistry>(nullptr,definitions);
        incomplete->BlockWorld.SetBlock({1,-64,1},8); incomplete->ModifiedChunks.insert(lower);
        incomplete->SetWorldFolderPath((root/"save-admission-incomplete").string());
        incomplete->Persistence->EnsureChunkIoInitialized();
        const bool incomplete_refused=!incomplete->Persistence->RequestAsyncTerrainColumnSave(*incomplete,column) &&
            incomplete->Persistence->PendingAsyncColumnSaveSlices.empty() && incomplete->ModifiedChunks.count(lower)==1 &&
            !incomplete->Persistence->GetChunkStorage().IsColumnSavePending(column);
        const glm::ivec3 latest(3,-63,1);
        load_world.BlockWorld.SetBlock(latest,8); load_world.ModifiedChunks.insert(lower);
        load_world.BlockWorld.GetChunkManager().MarkTerrainColumnReadUnresolved(column);
        const bool unresolved_refused=!persistence.RequestAsyncTerrainColumnSave(load_world,column) &&
            persistence.PendingAsyncColumnSaveSlices.empty() && load_world.ModifiedChunks.count(lower)==1;
        persistence.RequestAsyncTerrainColumnLoad(load_world,column,100101);
        const auto resolved_by=std::chrono::steady_clock::now()+std::chrono::seconds(10);
        while(persistence.IsTerrainColumnDiskLoadPending(column)) {
          persistence.TickAsyncChunkIo(load_world,1,0);
          require(std::chrono::steady_clock::now()<resolved_by,"save admission read-fault recovery bound");
          std::this_thread::yield();
        }
        const bool resolved=!load_world.BlockWorld.GetChunkManager().HasUnresolvedTerrainColumnRead(column) &&
            load_world.BlockWorld.GetBlock(latest)==8 && load_world.BlockWorld.GetBlock(edit)==BLOCK_AIR;
        auto save_gate=std::make_shared<AsyncSaveWriteProbeGate>();
        struct ReleaseSaveAdmissionGate {
          std::shared_ptr<AsyncSaveWriteProbeGate> gate;
          ~ReleaseSaveAdmissionGate() { gate->released.store(true); }
        } save_release{save_gate};
        persistence.AsyncChunkIo->SaveWriteProbeGate=save_gate;
        const bool enqueue_not_acknowledged=!persistence.TrySaveTerrainColumnForUnload(load_world,column) &&
            save_storage.IsColumnSavePending(column) && load_world.ModifiedChunks.count(lower)==1;
        const auto save_entered_by=std::chrono::steady_clock::now()+std::chrono::seconds(3);
        while(!save_gate->entered.load() && std::chrono::steady_clock::now()<save_entered_by) std::this_thread::yield();
        require(save_gate->entered.load(),"actual save writer held before duplicate admission");
        const auto batch=persistence.PendingAsyncColumnSaveSlices.at(column).BatchId();
        const bool save_duplicate_preserved=!persistence.RequestAsyncTerrainColumnSave(load_world,column) &&
            persistence.PendingAsyncColumnSaveSlices.at(column).BatchId()==batch &&
            save_storage.IsColumnSavePending(column) && load_world.ModifiedChunks.count(lower)==1;
        save_gate->released.store(true); persistence.AsyncChunkIo->SaveWriteProbeGate.reset();
        const auto saved_by=std::chrono::steady_clock::now()+std::chrono::seconds(10);
        while(save_storage.IsColumnSavePending(column)) {
          persistence.TickAsyncChunkIo(load_world,32,6.0);
          require(std::chrono::steady_clock::now()<saved_by,"actual save admission ACK completes");
          std::this_thread::yield();
        }
        const bool acknowledged=persistence.TrySaveTerrainColumnForUnload(load_world,column) && load_world.ModifiedChunks.empty();
        auto cold=std::make_unique<UBlockWorld>(); cold->SetVerticalProfile({-64,319,63});
        const bool cold_latest=save_storage.LoadTerrainColumn(column,*cold,folder.string(),*load_world.BlockRegistry,319,-64)>0 &&
            cold->GetBlock(latest)==8 && cold->GetBlock(edit)==BLOCK_AIR;
        report["disk_save_admission"]={{"dependency_refused",dependency_refused},{"save_pending_refused",save_pending_refused},
            {"incomplete_refused",incomplete_refused},{"unresolved_refused",unresolved_refused},{"resolved_current",resolved},
            {"enqueue_not_acknowledged",enqueue_not_acknowledged},{"duplicate_preserved",save_duplicate_preserved},
            {"acknowledged",acknowledged},{"cold_latest",cold_latest}};
        require(dependency_refused && save_pending_refused && incomplete_refused && unresolved_refused && resolved &&
            enqueue_not_acknowledged && save_duplicate_preserved && acknowledged && cold_latest,
            "save admission preserves dirty owner through refusal/duplicate until real ACK and cold latest data");
      });
      }
      if(!flow_only) RunTerrainProbePhase([&] {
        struct FaceDeadlineRestore {
          UFrameDeadline deadline=UFrameDeadline::Get();
          ~FaceDeadlineRestore() { UFrameDeadline::Get()=deadline; }
        } restore;
        auto face_owner=std::make_unique<UWorld>(nullptr,nullptr); auto &face_world=*face_owner;
        face_world.SetSpawnPoint({8,-40,8});
        face_world.ProceduralTemplate.MinHeight=-48; face_world.ProceduralTemplate.MaxHeight=-33;
        face_world.BlockWorld.SetVerticalProfile({-48,-33,-40});
        face_world.BlockRegistry=std::make_unique<UBlockRegistry>(nullptr,definitions);
        face_world.BlockWorld.SetFluidDefinitions(definitions.get());
        face_world.BlockWorld.SetKnownOpenSkyAbsentQuery([](glm::ivec3){return true;});
        face_world.SetRenderSettings(settings);
        auto &face_mesh=*face_world.MeshService; auto &face_cache=face_mesh.GetCache();
        auto &demand=face_world.GetRenderDemandStore();
        auto &repair=face_world.Streaming->GetEmergeCoordinator();
        const glm::ivec3 target(0,-3,0),peer(1,-3,0); const uint8_t bit=2;
        face_world.BlockWorld.SetBlock({8,-40,8},8);
        UFrameDeadline::Get().BeginFrame(5000);
        face_mesh.RebuildChunkImmediate(face_world.BlockWorld,*face_world.BlockRegistry,target);
        require(face_mesh.HasDrawableGreedyMesh(target),"face repair starts with actual target mesh");
        repair.ResetFaceDebtEpoch(face_world);
        demand.NoteFaceDebt(target,bit,0);
        repair.EnqueueFaceDebtRepair(target); repair.EnqueueFaceDebtRepair(target);
        repair.DrainFaceDebtRepairs(face_world,0);
        const bool deduped=repair.PendingFaceDebtRepairs_.size()==1 && repair.PendingFaceDebtRepairSet_.size()==1;
        repair.DrainFaceDebtRepairs(face_world,1);
        const bool deferred=repair.PendingFaceDebtRepairs_.size()==1 && demand.Find(target) &&
            (demand.Find(target)->face_debt_mask&bit)!=0;
        face_world.BlockWorld.SetBlock({CHUNK_SIZE+8,-40,8},8);
        face_mesh.RebuildChunkImmediate(face_world.BlockWorld,*face_world.BlockRegistry,peer);
        const auto *peer_base=demand.Find(peer);
        require(peer_base && peer_base->published_geom_rev>0,"peer owns actual geometry before coverage request");
        demand.NoteDemand(peer,peer_base->published_geom_rev,peer_base->published_light_rev,1,0,
                          peer_base->world_epoch,peer_base->incarnation);
        face_mesh.RebuildChunkImmediate(face_world.BlockWorld,*face_world.BlockRegistry,peer);
        const auto *peer_record=demand.Find(peer);
        require(peer_record && peer_record->published_coverage_gen>0 && face_mesh.HasMeshSatisfyingColumnReady(peer),
                "face repair peer has actual current coverage publication");
        const auto drive=[&]() {
          const auto before=face_cache.GetPublishCount(target);
          repair.DrainFaceDebtRepairs(face_world,1);
          const bool transferred=face_cache.HasPendingMeshDependencyInvalidation(target);
          Json transition={{"before_publications",before},{"dependency_transferred",transferred},
              {"repair_pending",repair.PendingFaceDebtRepairs_.size()},
              {"target_mask",demand.Find(target)?demand.Find(target)->face_debt_mask:255},
              {"peer_coverage",demand.Find(peer)?demand.Find(peer)->published_coverage_gen:0}};
          std::ofstream(root/"face-repair-transitions.json") << transition.dump(2) << '\n';
          for(int frame=0;frame<8;++frame) {
            UFrameDeadline::Get().BeginFrame(5000);
            const auto stats=face_mesh.RebuildDirtyChunksWithStats(face_world.BlockWorld,*face_world.BlockRegistry,8,8);
            face_mesh.DrainAsyncMeshResults(face_world.BlockWorld,*face_world.BlockRegistry,8);
            transition["frames"].push_back({{"scheduled",stats.Scheduled},{"sync",stats.SyncRebuilt},
                {"completed",stats.Completed},{"publications",face_cache.GetPublishCount(target)},
                {"inputs_current",face_cache.IsPublishedMeshCurrentForInputs(target,face_world.BlockWorld)},
                {"dirty",face_mesh.IsChunkMeshDirty(target)},{"inflight",face_mesh.HasInflightMeshBuild(target)},
                {"dependency",face_cache.HasPendingMeshDependencyInvalidation(target)}});
            std::ofstream(root/"face-repair-transitions.json") << transition.dump(2) << '\n';
            if(face_cache.GetPublishCount(target)>before &&
                face_cache.IsPublishedMeshCurrentForInputs(target,face_world.BlockWorld)) break;
          }
          const bool current=face_cache.GetPublishCount(target)>before &&
              face_cache.IsPublishedMeshCurrentForInputs(target,face_world.BlockWorld) && demand.Find(target) &&
              demand.Find(target)->face_debt_mask==0 && !face_cache.HasPendingMeshDependencyInvalidation(target) &&
              repair.PendingFaceDebtRepairs_.empty() && repair.PendingFaceDebtRepairSet_.empty();
          transition["current_successor"]=current;
          report["face_repair_transitions"].push_back(std::move(transition));
          return transferred && current;
        };
        const bool first=drive();
        report["face_repair_queue"]={{"deduped",deduped},{"missing_peer_retained",deferred},{"current_successor",first}};
        require(deduped && deferred && first,"face repair retains missing peer and publishes actual current successor");
        const auto old_incarnation=face_world.BlockWorld.GetChunkManager().GetChunk(target)->GetIncarnation();
        demand.NoteFaceDebt(target,bit,peer_record->published_coverage_gen);
        repair.EnqueueFaceDebtRepair(target);
        face_mesh.RemoveColumn({0,0,0},-3,-3);
        face_world.BlockWorld.GetChunkManager().RemoveChunk(target);
        repair.DrainFaceDebtRepairs(face_world,1);
        const bool retired=repair.PendingFaceDebtRepairs_.empty() && repair.PendingFaceDebtRepairSet_.empty();
        face_world.BlockWorld.SetBlock({8,-40,8},8);
        face_mesh.RebuildChunkImmediate(face_world.BlockWorld,*face_world.BlockRegistry,target);
        const bool fresh=face_world.BlockWorld.GetChunkManager().GetChunk(target)->GetIncarnation()!=old_incarnation;
        face_mesh.RebuildChunkImmediate(face_world.BlockWorld,*face_world.BlockRegistry,peer);
        demand.NoteFaceDebt(target,bit,demand.Find(peer)->published_coverage_gen);
        repair.EnqueueFaceDebtRepair(target); repair.EnqueueFaceDebtRepair(target);
        const bool reissued=drive();
        report["face_repair_retirement"]={{"removed_owner_retired",retired},{"fresh_incarnation",fresh},
            {"current_reissue",reissued}};
        require(retired && fresh && reissued,"removed face repair owner retires and new incarnation completes");
        const auto replaced_incarnation=face_world.BlockWorld.GetChunkManager().GetChunk(target)->GetIncarnation();
        const auto replaced_epoch=face_cache.GetCaptureStore().WorldEpoch();
        const glm::ivec3 old_only{3,-3,0};
        face_world.BlockWorld.SetBlock({3*CHUNK_SIZE+8,-40,8},8);
        demand.NoteFaceDebt(target,bit,0); demand.NoteFaceDebt(old_only,bit,0);
        repair.EnqueueFaceDebtRepair(target); repair.EnqueueFaceDebtRepair(old_only);
        require(repair.PendingFaceDebtRepairs_.size()==2,"replacement starts with two actual queued repair owners");
        require(face_world.TryAbandonTerrainForWorldReplace(), "retire idle face-repair world");
        // Exact production epoch ingress used at the start of TickMeshEmerge;
        // isolate it from unrelated focus scheduling and admission policies.
        repair.ResetFaceDebtEpoch(face_world);
        const bool epoch_changed=face_cache.GetCaptureStore().WorldEpoch()!=replaced_epoch;
        const bool old_retired=repair.PendingFaceDebtRepairs_.empty() && repair.PendingFaceDebtRepairSet_.empty() &&
            !demand.Find(target) && !demand.Find(old_only) &&
            !face_world.BlockWorld.GetChunkManager().HasChunk(old_only);
        face_world.BlockWorld.SetBlock({8,-40,8},8);
        face_world.BlockWorld.SetBlock({CHUNK_SIZE+8,-40,8},8);
        const bool replacement_fresh=face_world.BlockWorld.GetChunkManager().GetChunk(target)->GetIncarnation()!=replaced_incarnation;
        face_mesh.RebuildChunkImmediate(face_world.BlockWorld,*face_world.BlockRegistry,target);
        face_mesh.RebuildChunkImmediate(face_world.BlockWorld,*face_world.BlockRegistry,peer);
        const auto *replacement_peer=demand.Find(peer);
        require(replacement_peer && replacement_peer->published_geom_rev>0,"replacement peer actually publishes");
        demand.NoteDemand(peer,replacement_peer->published_geom_rev,replacement_peer->published_light_rev,
            1,0,replacement_peer->world_epoch,replacement_peer->incarnation);
        face_mesh.RebuildChunkImmediate(face_world.BlockWorld,*face_world.BlockRegistry,peer);
        require(demand.Find(peer)->published_coverage_gen>0,"replacement peer has actual coverage");
        demand.NoteFaceDebt(target,bit,0); repair.EnqueueFaceDebtRepair(target);
        const bool replacement_current=drive();
        report["face_repair_world_replacement"]={{"epoch_changed",epoch_changed},{"old_owners_retired",old_retired},
            {"fresh_incarnation",replacement_fresh},{"current_successor",replacement_current}};
        require(epoch_changed && old_retired && replacement_fresh && replacement_current,
            "actual world replacement retires old face owners and current replacement publishes");
        repair.SoftDeferPolicy.world=&face_world;
        repair.InstallSoftDeferCallbacks(face_mesh);
        face_mesh.RemoveColumn({1,0,0},-3,-3);
        face_world.BlockWorld.GetChunkManager().RemoveChunk(peer);
        face_world.BlockWorld.SetKnownOpenSkyAbsentQuery([peer](glm::ivec3 c){return c!=peer;});
        face_mesh.RebuildChunkImmediate(face_world.BlockWorld,*face_world.BlockRegistry,target);
        const bool overlay_published=face_cache.HasActiveBoundaryOverlayFace(target,1) &&
            demand.Find(target) && (demand.Find(target)->overlay_face_debt_mask & bit)!=0;
        require(overlay_published,"actual missing peer capture publishes target boundary overlay");
        repair.EnqueueFaceDebtRepair(target);
        repair.DrainFaceDebtRepairs(face_world,1);
        const bool overlay_waits=repair.PendingFaceDebtRepairSet_.count(target)!=0 &&
            (demand.Find(target)->overlay_face_debt_mask & bit)!=0;
        face_world.BlockWorld.SetBlock({CHUNK_SIZE+8,-40,8},8);
        face_mesh.RebuildChunkImmediate(face_world.BlockWorld,*face_world.BlockRegistry,peer);
        const auto overlay_peer_incarnation=face_world.BlockWorld.GetChunkManager().GetChunk(peer)->GetIncarnation();
        const auto overlay_peer_coverage=demand.Find(peer)->published_coverage_gen;
        const auto overlay_target_light=face_world.BlockWorld.GetChunkManager().GetChunk(target)->GetLightFieldRevision();
        repair.DrainFaceDebtRepairs(face_world,1);
        const bool overlay_transferred=face_cache.HasPendingMeshDependencyInvalidation(target) &&
            (demand.Find(target)->overlay_repair_attempted_mask & bit)!=0;
        const bool same_state_refused=!demand.CanBeginBoundaryOverlayRepair(target,1,
            overlay_peer_incarnation,overlay_peer_coverage,overlay_target_light);
        repair.DrainFaceDebtRepairs(face_world,1);
        const auto overlay_before=face_cache.GetPublishCount(target);
        for(int pass=0;pass<8;++pass) {
          UFrameDeadline::Get().BeginFrame(5000);
          face_mesh.RebuildDirtyChunksWithStats(face_world.BlockWorld,*face_world.BlockRegistry,8,8);
          repair.DrainFaceDebtRepairs(face_world,1);
          if(!face_cache.HasActiveBoundaryOverlayFace(target,1) && repair.PendingFaceDebtRepairs_.empty()) break;
        }
        const bool overlay_current=face_cache.GetPublishCount(target)>overlay_before &&
            face_cache.IsPublishedMeshCurrentForInputs(target,face_world.BlockWorld) &&
            !face_cache.HasActiveBoundaryOverlay(target) && demand.Find(target)->face_debt_mask==0 &&
            repair.PendingFaceDebtRepairs_.empty() && repair.PendingFaceDebtRepairSet_.empty() &&
            !face_cache.HasPendingMeshDependencyInvalidation(target);
        report["face_repair_overlay"]={{"actual_overlay_published",overlay_published},{"missing_peer_retained",overlay_waits},
            {"dependency_transferred",overlay_transferred},{"same_state_refused",same_state_refused},
            {"current_successor",overlay_current}};
        require(overlay_published && overlay_waits && overlay_transferred && same_state_refused && overlay_current,
            "actual overlay repair waits, admits peer state once and publishes overlay-free current target");
      });
      if(flow_only) RunTerrainProbePhase([&] {
        struct FlowDeadlineRestore {
          UFrameDeadline previous=UFrameDeadline::Get();
          ~FlowDeadlineRestore() { UFrameDeadline::Get()=previous; }
        } restore;
        for(int mode=0;mode<3;++mode) {
          auto owner=std::make_unique<UWorld>(nullptr,nullptr); auto &flow=*owner;
          flow.SetSpawnPoint({8,-40,8});
          flow.ProceduralTemplate.MinHeight=-48; flow.ProceduralTemplate.MaxHeight=47;
          flow.ProceduralTemplate.SeaLevel=-40;
          flow.BlockWorld.SetVerticalProfile({-48,47,-40});
          flow.BlockRegistry=std::make_unique<UBlockRegistry>(nullptr,definitions);
          flow.BlockWorld.SetFluidDefinitions(definitions.get());
          flow.BlockWorld.SetKnownOpenSkyAbsentQuery([](glm::ivec3){return true;});
          auto flow_settings=settings; flow_settings.AsyncMeshing=false;
          flow.SetRenderSettings(flow_settings);
          flow.BlockWorld.SetBlock({8,-40,8},8); flow.BlockWorld.SetBlock({8,40,8},8);
          auto &mesh=*flow.MeshService; auto &cache=mesh.GetCache();
          UColumnFlowExecutor executor;
          for(int round=0;round<2;++round) {
            if(round) {
              mesh.RemoveColumn({0,0,0},-3,2);
              // Keep a neighbouring ticket/stage while retiring this exact
              // column's real dispatch cooldown and scheduling metadata.
              executor.Scheduler().Enqueue({1,0},ColumnWorkKind::FirstMesh,1);
              executor.SetColumnJobStage(flow,{1,0},ColumnJobStage::Meshing);
              const bool forgotten=executor.ForgetColumnWork({0,0});
              const bool isolated=executor.GetColumnJobStage({0,0})==ColumnJobStage::Absent &&
                  executor.Scheduler().ContainsColumn({1,0}) &&
                  executor.GetColumnJobStage({1,0})==ColumnJobStage::Meshing;
              const bool repeat_noop=!executor.ForgetColumnWork({0,0});
              require(forgotten && isolated && repeat_noop,"flow forget retires exact metadata and preserves neighbour");
              require(executor.ForgetColumnWork({1,0}),"flow peer metadata retires independently");
              auto reentry=ColumnWorkItem{}; reentry.column={0,0}; reentry.cy=-3;
              reentry.kind=ColumnWorkKind::FirstMesh; reentry.priority=90;
              executor.Enqueue(reentry);
              const bool cooldown_released=executor.Scheduler().ContainsColumn({0,0});
              report["flow_forget"].push_back({{"mode",mode},{"forgotten",forgotten},{"isolated",isolated},
                  {"repeat_noop",repeat_noop},{"cooldown_released",cooldown_released}});
              require(cooldown_released,"flow reentry does not inherit old dispatch cooldown");
              executor.Scheduler().RemoveColumn({0,0});
            }
            ColumnWorkItem old{}; old.column={0,0}; old.cy=-3; old.priority=90;
            old.kind=mode==0 ? ColumnWorkKind::FirstMesh :
                (mode==1 ? ColumnWorkKind::RemeshSeam : ColumnWorkKind::RelightThenMesh);
            auto next=old; next.cy=2; next.priority=20; next.kind=ColumnWorkKind::FirstMesh;
            // Accepted ticket adapter: production scheduler coalescing and real
            // executor admission are used; derived producer policy is separate.
            executor.Scheduler().Enqueue(old);
            if(mode==2) require(executor.Scheduler().ReplaceColumnTicket(next,old.kind),"flow dependent replacement accepted");
            else executor.Scheduler().Enqueue(next);
            ColumnWorkItem live{};
            const bool merged=executor.Scheduler().GetLiveTicket({0,0},live) &&
                live.cy==kColumnWorkWholeColumnCy && live.priority==90;
            UFrameDeadline::Get().BeginFrame(5000);
            const auto dispatched=executor.DrainBudget(flow,1,{0,-3,0},1,1);
            const bool both_dirty=mesh.IsChunkMeshDirty({0,-3,0}) && mesh.IsChunkMeshDirty({0,2,0});
            for(int pass=0;pass<8;++pass) {
              UFrameDeadline::Get().BeginFrame(5000);
              mesh.RebuildDirtyChunksWithStats(flow.BlockWorld,*flow.BlockRegistry,8,8);
              if(!mesh.IsChunkMeshDirty({0,-3,0}) && !mesh.IsChunkMeshDirty({0,2,0})) break;
            }
            const bool current=cache.GetPublishCount({0,-3,0})>0 && cache.GetPublishCount({0,2,0})>0 &&
                cache.IsPublishedMeshCurrentForInputs({0,-3,0},flow.BlockWorld) &&
                cache.IsPublishedMeshCurrentForInputs({0,2,0},flow.BlockWorld) &&
                mesh.HasDrawableGreedyMesh({0,-3,0}) && mesh.HasDrawableGreedyMesh({0,2,0});
            report["flow_signed_publication"].push_back({{"mode",mode},{"round",round},
                {"merged",merged},{"dispatched",dispatched},{"both_dirty",both_dirty},{"both_current",current}});
            require(merged && dispatched==1 && both_dirty && current,"merged flow ticket admits and publishes both signed slices");
            executor.SetRelightRetryCooldown(UColumnFlowExecutor::MakeCooldownKey({0,0},ColumnWorkKind::RelightThenMesh));
          }
          const auto cooldown_before=executor.DispatchCooldownCount();
          Json ages=Json::array();
          Json retry_ages=Json::array();
          for(int age=1;age<=3;++age) {
            executor.DrainBudget(flow,0,{0,-3,0},1,1);
            ages.push_back(executor.DispatchCooldownCount());
            retry_ages.push_back(executor.relight_retry_after_frame_.size());
          }
          const bool expires_exactly=cooldown_before==1 && ages[0]==1 && ages[1]==1 && ages[2]==0 &&
              executor.DispatchExpiryCount()==0;
          const bool retry_expired=retry_ages[0]==1 && retry_ages[1]==1 && retry_ages[2]==0 &&
              executor.relight_retry_after_frame_.empty() && executor.retry_expiry_.empty();
          const auto records_before_absent=flow.GetColumnRecords().Size();
          for(int n=0;n<2000;++n) executor.SetColumnJobStage(flow,{n,99},ColumnJobStage::Absent);
          const bool absent_not_stored=flow.GetColumnRecords().Size()==records_before_absent &&
              executor.GetColumnJobStage({0,0})!=ColumnJobStage::Absent;
          executor.SetPromoteRelightHold({0,0},true);
          executor.SetCaptureWitnessPin({0,0},true,7,true);
          executor.Clear();
          const bool cleared=flow.GetColumnRecords().GetFlowJobStage({0,0})==ColumnJobStage::Absent && !executor.promote_hold_valid_ &&
              !executor.capture_pin_valid_ && !executor.capture_pin_hold_ && executor.decide_world_==nullptr &&
              executor.DispatchExpiryCount()==0 && executor.retry_expiry_.empty();
          report["flow_metadata_lifetime"].push_back({{"mode",mode},{"retry_expired",retry_expired},
              {"retry_counts_by_age",retry_ages},
              {"absent_not_stored",absent_not_stored},{"clear_retires_metadata",cleared}});
          require(retry_expired && absent_not_stored && cleared,"flow retired metadata is not retained as history");
          report["flow_cooldown_expiry"].push_back({{"mode",mode},{"before",cooldown_before},
              {"counts_by_age",ages},{"receipts_after",executor.DispatchExpiryCount()},{"exact",expires_exactly}});
          require(expires_exactly,"old expiry receipt preserves renewed cooldown until its exact deadline");
          if(mode==2) {
            struct LateArrivalPopulator final : IUChunkPopulator {
              ChunkPopulateResult Populate(const ChunkPopulateRequest &request) override {
                ChunkPopulateResult result;
                result.coord=request.chunkCoord; result.token=request.token;
                result.buffer.SetBlock({CHUNK_SIZE+8,-40,8},8);
                return result;
              }
            } populator;
            UChunkGenerationRegistry generation;
            UChunkLoadScheduler scheduler(populator,generation);
            ProceduralSettings generation_settings;
            generation_settings.MinHeight=-48; generation_settings.MaxHeight=47;
            unsigned arrivals=0;
            scheduler.SetMarkDirtyFn([&](glm::ivec3,int,int,bool){++arrivals;});
            const bool settled_before=flow.HasCurrentChunkSlicePublication({0,-3,0}) &&
                cache.IsPublishedMeshCurrentForInputs({0,-3,0},flow.BlockWorld);
            scheduler.RequestLoad({1,0,0},100,generation_settings);
            require(scheduler.StartQueuedGenerationWorkers(1)==1,"late arrival starts actual generation worker");
            const auto deadline=std::chrono::steady_clock::now()+std::chrono::seconds(5);
            while(scheduler.GetCompletedReadyCount()==0 && std::chrono::steady_clock::now()<deadline)
              std::this_thread::yield();
            require(scheduler.GetCompletedReadyCount()==1,"late arrival owns actual completed payload");
            scheduler.Tick(flow.BlockWorld,0,0);
            const bool held=arrivals==0 && flow.HasCurrentChunkSlicePublication({0,-3,0});
            scheduler.Tick(flow.BlockWorld,1,0);
            const bool invalidated=arrivals==1 && !flow.HasCurrentChunkSlicePublication({0,-3,0}) &&
                !cache.IsPublishedMeshCurrentForInputs({0,-3,0},flow.BlockWorld);
            const auto before=cache.GetPublishCount({0,-3,0});
            for(int pass=0;pass<8;++pass) {
              UFrameDeadline::Get().BeginFrame(5000);
              mesh.RebuildDirtyChunksWithStats(flow.BlockWorld,*flow.BlockRegistry,8,8);
              if(flow.HasCurrentChunkSlicePublication({0,-3,0})) break;
            }
            const bool recovered=cache.GetPublishCount({0,-3,0})>before &&
                flow.HasCurrentChunkSlicePublication({0,-3,0}) &&
                cache.IsPublishedMeshCurrentForInputs({0,-3,0},flow.BlockWorld);
            report["late_generation_publication_transition"]={{"settled_before",settled_before},
                {"ready_payload_held",held},{"late_apply_invalidates",invalidated},{"real_successor",recovered}};
            require(settled_before && held && invalidated && recovered,
                "late actual generation commit invalidates a settled neighbour until real current republication");
          }
          if(mode==1) {
            mesh.RemoveColumn({0,0,0},-3,2);
            bool filled=true;
            for(size_t i=0;i<UColumnFlowScheduler::kMaxLiveTickets;++i)
              filled=executor.Scheduler().Enqueue({100+static_cast<int>(i),0},ColumnWorkKind::FirstMesh,1) && filled;
            ColumnWorkItem a{}; a.column={0,0}; a.cy=-3; a.priority=90;
            auto b=a; b.cy=2;
            // Owned callback before binding must remain in the world record.
            executor.EnqueueForWorld(flow,a); executor.EnqueueForWorld(flow,b);
            executor.DrainBudget(flow,0,{0,-3,0},1,1);
            const auto epoch=cache.GetCaptureStore().WorldEpoch();
            const auto *retained=flow.GetColumnRecords().FindFlowIntents({0,0},epoch);
            const bool refused_retained=filled && retained && retained->flow_intent[1].pending &&
                retained->flow_intent[1].cy==kColumnWorkWholeColumnCy &&
                !executor.Scheduler().ContainsColumn({0,0}) &&
                executor.Scheduler().LiveCount()==UColumnFlowScheduler::kMaxLiveTickets &&
                executor.Scheduler().CapacityRefusedCount()>0;
            executor.Scheduler().RemoveColumn({100,0});
            executor.DrainBudget(flow,0,{0,-3,0},1,1);
            const auto *readmitted_record=flow.GetColumnRecords().FindFlowIntents({0,0},epoch);
            const bool readmitted=executor.Scheduler().ContainsColumn({0,0}) && readmitted_record &&
                !readmitted_record->flow_intent[1].pending &&
                executor.Scheduler().LiveCount()==UColumnFlowScheduler::kMaxLiveTickets;
            // Explicitly retire synthetic capacity occupants, never execute
            // nonexistent far-column jobs. The target uses real world data.
            for(size_t i=0;i<UColumnFlowScheduler::kMaxLiveTickets;++i)
              executor.Scheduler().RemoveColumn({100+static_cast<int>(i),0});
            UFrameDeadline::Get().BeginFrame(5000);
            const auto dispatched=executor.DrainBudget(flow,1,{0,-3,0},1,1);
            const bool both_dirty=mesh.IsChunkMeshDirty({0,-3,0}) && mesh.IsChunkMeshDirty({0,2,0});
            mesh.RebuildDirtyChunksWithStats(flow.BlockWorld,*flow.BlockRegistry,8,8);
            const bool successor=dispatched==1 && both_dirty &&
                cache.GetPublishCount({0,-3,0})>0 && cache.GetPublishCount({0,2,0})>0 &&
                cache.IsPublishedMeshCurrentForInputs({0,-3,0},flow.BlockWorld) &&
                cache.IsPublishedMeshCurrentForInputs({0,2,0},flow.BlockWorld);
            report["flow_capacity"]={{"limit",UColumnFlowScheduler::kMaxLiveTickets},
                {"refused_intent_retained",refused_retained},{"automatic_readmission",readmitted},
                {"actual_current_successor",successor}};
            require(refused_retained && readmitted && successor,
                "saturated flow queue retains owned callback intent and readmits without fresh ingress");
          }
          if(mode==0) {
            auto &global=GetColumnFlowExecutor();
            struct RestoreGlobalFlow {
              UColumnFlowExecutor &target;
              std::unique_ptr<UColumnFlowExecutor> saved;
              ~RestoreGlobalFlow() { target=std::move(*saved); }
            } restore_global{global,std::make_unique<UColumnFlowExecutor>(std::move(global))};
            global=UColumnFlowExecutor{};
            global.BindDecideWorld(&flow);
            global.Scheduler().Enqueue({0,0},ColumnWorkKind::FirstMesh,90);
            global.Scheduler().Enqueue({3,0},ColumnWorkKind::FirstMesh,20);
            global.SetColumnJobStage(flow,{3,0},ColumnJobStage::Meshing);
            global.SetRelightRetryCooldown(UColumnFlowExecutor::MakeCooldownKey({3,0},ColumnWorkKind::RelightThenMesh));
            flow.SetColumnEmergeState({3,0,0},ColumnEmergeState::Lighting);
            ColumnWorkItem retired_intent{}; retired_intent.column={3,0};
            flow.GetColumnRecords().NoteFlowIntent(retired_intent,cache.GetCaptureStore().WorldEpoch());
            auto unrelated=std::make_unique<UWorld>(nullptr,nullptr);
            require(unrelated->TryAbandonTerrainForWorldReplace(), "retire idle unrelated world");
            const bool foreign_preserved=global.Scheduler().LiveCount()==2 && global.decide_world_==&flow;
            const auto old_inc=flow.BlockWorld.GetChunkManager().GetChunk({0,-3,0})->GetIncarnation();
            require(flow.TryAbandonTerrainForWorldReplace(), "retire idle ticket world");
            const bool retired=global.Scheduler().LiveCount()==0 && global.GetColumnJobStage({3,0})==ColumnJobStage::Absent &&
                global.relight_retry_after_frame_.empty() && global.retry_expiry_.empty() && global.decide_world_==nullptr;
            const bool world_records_retired=flow.GetColumnRecords().Size()==0 && flow.ColumnEmergeStates.empty() &&
                flow.ColumnLightingTelemetryN==0 && flow.ColumnMeshingTelemetryN==0 && flow.ColumnRenderReadyTelemetryN==0;
            flow.BlockWorld.SetBlock({8,-40,8},8); flow.BlockWorld.SetBlock({8,40,8},8);
            global.BindDecideWorld(&flow);
            ColumnWorkItem a{}; a.column={0,0}; a.cy=-3; a.priority=90;
            auto b=a; b.cy=2;
            global.Scheduler().Enqueue(a); global.Scheduler().Enqueue(b);
            UFrameDeadline::Get().BeginFrame(5000);
            const auto dispatched=global.DrainBudget(flow,1,{0,-3,0},1,1);
            mesh.RebuildDirtyChunksWithStats(flow.BlockWorld,*flow.BlockRegistry,8,8);
            const bool successor=dispatched==1 &&
                flow.BlockWorld.GetChunkManager().GetChunk({0,-3,0})->GetIncarnation()!=old_inc &&
                cache.GetPublishCount({0,-3,0})>0 && cache.GetPublishCount({0,2,0})>0 &&
                cache.IsPublishedMeshCurrentForInputs({0,-3,0},flow.BlockWorld) &&
                cache.IsPublishedMeshCurrentForInputs({0,2,0},flow.BlockWorld);
            global.BindDecideWorld(unrelated.get());
            const bool retargeted=global.Scheduler().LiveCount()==0 &&
                flow.GetColumnRecords().GetFlowJobStage({0,0})==ColumnJobStage::Absent &&
                global.DispatchCooldownCount()==0 && global.decide_world_==unrelated.get();
            global.Scheduler().Enqueue({7,0},ColumnWorkKind::FirstMesh,10);
            unrelated.reset();
            const bool destructor_retired=global.Scheduler().LiveCount()==0 && global.decide_world_==nullptr;
            report["flow_world_lifecycle"]={{"foreign_preserved",foreign_preserved},{"old_owner_retired",retired},
                {"world_records_retired",world_records_retired},
                {"current_successor",successor},{"retargeted",retargeted},{"destructor_retired",destructor_retired}};
            require(foreign_preserved && retired && world_records_retired && successor && retargeted && destructor_retired,
                "global flow retires only its world and replacement publishes actual current successor");
          }
          UColumnRecordStore empty_records;
          const auto baseline=empty_records.GetAllocationSnapshot();
          const auto live_records=flow.GetColumnRecords().Size();
          const auto before_memory=flow.GetColumnRecords().GetAllocationSnapshot();
          require(flow.TryAbandonTerrainForWorldReplace(), "retire idle record world");
          const auto after_memory=flow.GetColumnRecords().GetAllocationSnapshot();
          const bool released=flow.GetColumnRecords().Size()==0 &&
              after_memory.live_bytes==baseline.live_bytes &&
              after_memory.live_allocations==baseline.live_allocations;
          report["world_record_allocator"].push_back({{"mode",mode},{"records_before",live_records},
              {"bytes_before",before_memory.live_bytes},{"bytes_after",after_memory.live_bytes},
              {"empty_baseline_bytes",baseline.live_bytes},{"returned_to_baseline",released}});
          require(released,"actual world retirement releases record nodes to allocator baseline");
        }
      });
      if(flow_only) RunTerrainProbePhase([&] {
        struct PressurePopulator final : IUChunkPopulator {
          std::atomic<int> calls{0},seed{0};
          std::shared_future<void> settings_gate;
          std::promise<void> *settings_entered{nullptr};
          ChunkPopulateResult Populate(const ChunkPopulateRequest &request) override {
            ++calls; seed=static_cast<int>(request.settings.Seed);
            if(settings_entered) { settings_entered->set_value(); settings_gate.wait(); }
            ChunkPopulateResult result; result.coord=request.chunkCoord; result.token=request.token;
            result.buffer.SetBlock({request.chunkCoord.x*CHUNK_SIZE+8,-40,
                request.chunkCoord.z*CHUNK_SIZE+8},8);
            return result;
          }
        } populator;
        UChunkGenerationRegistry generation;
        UChunkLoadScheduler scheduler(populator,generation);
        const auto empty_metadata=scheduler.GetMetadataStorageSnapshot();
        const auto empty_completion=scheduler.Completed.GetStorageSnapshot();
        const auto empty_token_allocator=generation.GetAllocationSnapshot();
        UBlockWorld terrain; terrain.SetFluidDefinitions(definitions.get());
        terrain.SetVerticalProfile({-48,-33,-40});
        const glm::ivec3 target(0),peer(1,0,0);
        generation.Bump(target); generation.Bump(peer);
        ProceduralSettings request; request.MinHeight=-48; request.MaxHeight=-33; request.Seed=17;
        scheduler.RequestLoad(target,10,request);
        const auto original_seed=scheduler.QueuedRequests.at(target).settings->value.Seed;
        const auto original_time=scheduler.QueuedRequests.at(target).requestedAt;
        const auto original_token=scheduler.QueuedRequests.at(target).token.sequence;
        auto &credits=UPipelineAdmission::Get();
        const auto background_slots=credits.WorkSlotsInUse();
        const auto idle_deadline=std::chrono::steady_clock::now()+std::chrono::seconds(5);
        // The real loaded world may still own compute leases. Main-thread
        // probe execution prevents new application submissions during this
        // bounded wait; do not mistake those existing owners for our leak.
        while(credits.WorkSlotsInUse()!=0 && std::chrono::steady_clock::now()<idle_deadline)
          std::this_thread::yield();
        report["generation_pressure_details"]["background_work_slots_before_isolation"]=background_slots;
        require(credits.WorkSlotsInUse()==0,"generation pressure fixture starts with idle shared compute");
        struct RestoreSlots { ~RestoreSlots(){UPipelineAdmission::Get().SetWorkSlotCap(8);} } restore;
        credits.SetWorkSlotCap(0);
        const bool slot_refused=scheduler.StartQueuedGenerationWorkers(64)==0 &&
            scheduler.GetState(target)==ChunkLoadState::Requested && populator.calls==0 &&
            scheduler.GetPendingQueueCount()==1 && credits.WorkSlotsInUse()==0 &&
            scheduler.GetControlAllocationSnapshot().live_bytes==0;
        report["generation_pressure_details"]["slot"]={{"calls",populator.calls.load()},
            {"state",static_cast<int>(scheduler.GetState(target))},
            {"index",scheduler.GetPendingQueueCount()},{"live",scheduler.QueuedRequests.size()},
            {"global_work_slots",credits.WorkSlotsInUse()},
            {"own_result_slots",scheduler.GetRetainedGenerationResultCount()}};
        auto duplicate=request; duplicate.Seed=99;
        bool exact_index=true;
        for(int refresh=0;refresh<10000;++refresh) {
          scheduler.RequestLoad(target,refresh%7-3,duplicate);
          exact_index=exact_index && scheduler.GetPendingQueueCount()==1 &&
              scheduler.QueuedRequests.size()==1;
        }
        scheduler.RequestLoad(target,-10,duplicate);
        const auto &reranked=scheduler.QueuedRequests.at(target);
        const bool exact=reranked.settings->value.Seed==original_seed &&
            reranked.requestedAt==original_time &&
            reranked.token.sequence==original_token && reranked.priority==-10;
        credits.SetWorkSlotCap(8);
        std::promise<void> entered,release;
        auto entered_future=entered.get_future(); auto release_future=release.get_future().share();
        std::atomic<size_t> entered_count{0}; const auto workers=scheduler.Pool.GetWorkerCount();
        const auto worker_storage=scheduler.Pool.GetWorkerStorageSnapshot();
        std::vector<UJobThreadPool::StackStorageSnapshot> worker_stacks(workers);
        struct ReleasePool { std::promise<void> &p; UJobThreadPool &pool; bool done{false};
          void Release(){if(!done){p.set_value();done=true;}}
          ~ReleasePool(){Release();pool.WaitIdleFor(std::chrono::seconds(5));}
        } release_guard{release,scheduler.Pool};
        for(size_t n=0;n<workers;++n) scheduler.Pool.Enqueue([&,n] {
          worker_stacks[n]=UJobThreadPool::GetCurrentThreadStackStorage();
          if(++entered_count==workers) entered.set_value(); release_future.wait();
        });
        require(entered_future.wait_for(std::chrono::seconds(5))==std::future_status::ready,
            "generation pressure probe controls actual worker scheduling");
        bool stacks_observed=true; size_t stack_reservation=0,stack_commit_sample=0;
        for(const auto &stack:worker_stacks) {
          stacks_observed=stacks_observed && stack.observed && stack.reservation_bytes>0 &&
              stack.committed_bytes>0 && stack.committed_bytes<=stack.reservation_bytes;
          stack_reservation+=stack.reservation_bytes; stack_commit_sample+=stack.committed_bytes;
        }
        report["generation_worker_storage"]={{"workers",workers},{"thread_slots",worker_storage.thread_slots},
            {"thread_object_bytes",worker_storage.thread_bytes},{"all_stack_allocations_observed",stacks_observed},
            {"stack_reservation_bytes",stack_reservation},{"stack_committed_bytes_at_barrier",stack_commit_sample}};
        require(stacks_observed && workers>0 && workers<=kMaxWorkersPerPool &&
            worker_storage.workers==workers && worker_storage.thread_slots>=workers,
            "actual generation pool worker vector and every native stack reservation accounted");
        scheduler.Pool.SetMaxPendingJobCount(1); scheduler.Pool.Enqueue([]{});
        const bool pool_refused=scheduler.StartQueuedGenerationWorkers(64)==0 &&
            scheduler.GetState(target)==ChunkLoadState::Requested && populator.calls==0 &&
            scheduler.QueuedRequests.size()==1 && credits.WorkSlotsInUse()==0 &&
            scheduler.GetControlAllocationSnapshot().live_bytes==0;
        report["generation_pressure_details"]["pool"]={{"calls",populator.calls.load()},
            {"state",static_cast<int>(scheduler.GetState(target))},
            {"index",scheduler.GetPendingQueueCount()},{"live",scheduler.QueuedRequests.size()},
            {"global_work_slots",credits.WorkSlotsInUse()},
            {"own_result_slots",scheduler.GetRetainedGenerationResultCount()}};
        release_guard.Release();
        require(scheduler.Pool.WaitIdleFor(std::chrono::seconds(5)),"generation pressure barriers released");
        unsigned arrivals=0; scheduler.SetMarkDirtyFn([&](glm::ivec3,int,int,bool){++arrivals;});
        scheduler.Tick(terrain,1,1);
        require(scheduler.Pool.WaitIdleFor(std::chrono::seconds(5)),"normal generation tick admits retained request");
        scheduler.Tick(terrain,1,0);
        const bool successor=populator.calls==1 && populator.seed==17 && arrivals==1 &&
            terrain.GetBlock({8,-40,8})==8 && scheduler.GetState(target)==ChunkLoadState::Committed &&
            credits.WorkSlotsInUse()==0;
        report["generation_pressure_details"]["successor"]={{"calls",populator.calls.load()},
            {"seed",populator.seed.load()},{"arrivals",arrivals},
            {"state",static_cast<int>(scheduler.GetState(target))},
            {"block",terrain.GetBlock({8,-40,8})},{"global_work_slots",credits.WorkSlotsInUse()},
            {"own_result_slots",scheduler.GetRetainedGenerationResultCount()}};
        credits.SetWorkSlotCap(0); scheduler.RequestLoad(peer,0,request);
        scheduler.Tick(terrain,1,1); scheduler.Cancel(peer);
        credits.SetWorkSlotCap(8); scheduler.Tick(terrain,1,1);
        const bool cancelled=populator.calls==1 && scheduler.GetState(peer)==ChunkLoadState::Absent &&
            scheduler.GetPendingQueueCount()==0;
        report["generation_shared_admission"]={{"slot_refused_retained",slot_refused},
            {"pool_refused_retained",pool_refused},{"immutable_request_reranked",exact},
            {"exact_priority_index_after_10000_refreshes",exact_index},
            {"automatic_current_terrain_successor",successor},{"cancelled_refused_request",cancelled}};
        require(slot_refused && pool_refused && exact && exact_index && successor && cancelled,
            "generation shared admission retains exact request and normal tick installs successor");
        bool cohort=true;
        for(size_t n=0;n<UChunkLoadScheduler::kMaxRetainedGenerationResults;++n) {
          const glm::ivec3 c(10+static_cast<int>(n),0,0); generation.Bump(c);
          scheduler.RequestLoad(c,1,request);
          cohort=cohort && scheduler.StartQueuedGenerationWorkers(1)==1;
          require(scheduler.Pool.WaitIdleFor(std::chrono::seconds(5)),"bounded generation ready cohort completes");
        }
        const glm::ivec3 next(30,0,0); generation.Bump(next); scheduler.RequestLoad(next,2,request);
        const auto ready_payload_before=scheduler.GetCompletedReadyPayloadBytes();
        const auto full_completion=scheduler.Completed.GetStorageSnapshot();
        const auto full_controls=scheduler.GetControlAllocationSnapshot();
        const bool ready_full=cohort && scheduler.GetRetainedGenerationResultCount()==8 &&
            scheduler.GetCompletedReadyCount()==8 && scheduler.StartQueuedGenerationWorkers(64)==0 &&
            scheduler.GetState(next)==ChunkLoadState::Requested &&
            scheduler.GetAvailableGenerationWorkerSlots()==0 && credits.WorkSlotsInUse()==0;
        scheduler.Tick(terrain,0,64);
        const auto full_tick_batch=scheduler.GetTickBatchStorageSnapshot();
        const bool held=scheduler.GetRetainedGenerationResultCount()==8 && scheduler.GetCompletedReadyCount()==8;
        const bool payload_held=ready_payload_before>=8*CHUNK_VOLUME &&
            scheduler.GetCompletedReadyPayloadBytes()==ready_payload_before;
        scheduler.Cancel({10,0,0});
        scheduler.Tick(terrain,0,64);
        const bool stale_released=scheduler.GetRetainedGenerationResultCount()==7 &&
            scheduler.GetCompletedReadyCount()==7;
        const bool payload_stale_released=scheduler.GetCompletedReadyPayloadBytes()*8==ready_payload_before*7;
        scheduler.Tick(terrain,0,1);
        require(scheduler.Pool.WaitIdleFor(std::chrono::seconds(5)),"ready credit release admits queued successor");
        const bool readmitted=scheduler.GetRetainedGenerationResultCount()==8 &&
            scheduler.GetCompletedReadyCount()==8 && scheduler.IsPending(next);
        scheduler.Tick(terrain,32,0);
        const auto payload_after_install=scheduler.GetCompletedReadyPayloadBytes();
        const bool installed=scheduler.GetRetainedGenerationResultCount()==0 &&
            scheduler.GetCompletedReadyCount()==0 && terrain.GetBlock({30*CHUNK_SIZE+8,-40,8})==8 &&
            terrain.GetBlock({10*CHUNK_SIZE+8,-40,8})==BLOCK_AIR && credits.WorkSlotsInUse()==0 &&
            scheduler.GetCompletedReadyPayloadBytes()==0;
        scheduler.RequestLoad({50,0,0},1,request); scheduler.StartQueuedGenerationWorkers(1);
        require(scheduler.Pool.WaitIdleFor(std::chrono::seconds(5)),"ready retirement fixture completes");
        scheduler.CancelAllPending();
        const bool all_retired=scheduler.GetRetainedGenerationResultCount()==0;
        bool fixed_completion=empty_completion.slots==8 && full_completion.count==8 &&
            full_completion.bytes==empty_completion.bytes && scheduler.Completed.DiscardedOverflow()==0;
        bool released_owners=true,queue_plateau=true;
        size_t warm_queue_bytes=0;
        for(int cycle=0;cycle<64;++cycle) {
          for(int n=0;n<8;++n) {
            require(scheduler.RequestLoad({60+n,0,0},0,request),"storage cohort request admitted");
            require(scheduler.StartQueuedGenerationWorkers(1)==1 &&
                scheduler.Pool.WaitIdleFor(std::chrono::seconds(5)),"storage cohort worker finishes");
          }
          const auto full=scheduler.Completed.GetStorageSnapshot();
          fixed_completion=fixed_completion && full.count==8 && full.bytes==empty_completion.bytes &&
              scheduler.GetRetainedGenerationResultCount()==8;
          scheduler.CancelAllPending();
          const auto idle=scheduler.Completed.GetStorageSnapshot();
          const auto queue=scheduler.Pool.GetQueueAllocationSnapshot();
          if(cycle==31) warm_queue_bytes=queue.live_bytes;
          if(cycle>31) queue_plateau=queue_plateau && queue.live_bytes==warm_queue_bytes;
          released_owners=released_owners && idle.count==0 && idle.bytes==empty_completion.bytes &&
              scheduler.GetRetainedGenerationResultCount()==0 && scheduler.GetCompletedReadyPayloadBytes()==0 &&
              scheduler.BufferReservedBytes()==0 && scheduler.SettingsPayloadBytes()==0 &&
              scheduler.GetControlAllocationSnapshot().live_bytes==0;
        }
        const auto pool_storage=scheduler.Pool.GetQueueAllocationSnapshot();
        const auto tick_batch=scheduler.GetTickBatchStorageSnapshot();
        const auto current_workers=scheduler.Pool.GetWorkerStorageSnapshot();
        report["generation_tick_batch_storage"]={{"full_batch_count",full_tick_batch.count},
            {"full_batch_capacity_bytes",full_tick_batch.capacity_bytes},
            {"peak_capacity_bytes",tick_batch.peak_capacity_bytes},
            {"queue_plus_full_batch_bytes",empty_completion.bytes+full_tick_batch.capacity_bytes}};
        require(full_tick_batch.count==8 && full_tick_batch.capacity_bytes==8*sizeof(UChunkLoadScheduler::PendingResult) &&
            tick_batch.peak_capacity_bytes<=8*sizeof(UChunkLoadScheduler::PendingResult) &&
            current_workers.workers==worker_storage.workers && current_workers.thread_bytes==worker_storage.thread_bytes,
            "Tick transfer metadata and fixed worker storage stay bounded through repeated cohorts");
        const auto control_storage=scheduler.GetControlAllocationSnapshot();
        report["generation_queue_storage"]={{"cohorts",64},{"per_result_inline_bytes",sizeof(UChunkLoadScheduler::PendingResult)},
            {"completion_slots",empty_completion.slots},{"completion_capacity_bytes",empty_completion.bytes},
            {"completion_fixed",fixed_completion},{"retired_payload_owners",released_owners},
            {"pool_deque_idle_bytes",pool_storage.live_bytes},{"pool_deque_peak_bytes",pool_storage.peak_bytes},
            {"pool_deque_live_allocations",pool_storage.live_allocations},
            {"last32_idle_deque_plateau",queue_plateau}};
        report["generation_control_storage"]={{"ready8_bytes",full_controls.live_bytes},
            {"ready8_allocations",full_controls.live_allocations},{"peak_bytes",control_storage.peak_bytes},
            {"after_cancel_bytes",control_storage.live_bytes},{"after_cancel_allocations",control_storage.live_allocations},
            {"slot_refusal_releases",slot_refused},{"pool_refusal_releases",pool_refused}};
        require(full_controls.live_bytes>0 && full_controls.live_allocations==16 &&
            control_storage.live_bytes==0 && control_storage.live_allocations==0,
            "counted generation result and budget controls survive Ready then release exactly");
        require(fixed_completion && released_owners && queue_plateau &&
            scheduler.Completed.DiscardedOverflow()==0,
            "generation queue storage stays fixed and payload owners release over repeated cohorts");
        bool tokens_retired=generation.LiveTokenCount()==0;
        uint64_t previous_token=0;
        for(int n=0;n<128;++n) {
          const glm::ivec3 c(100+n,0,-100-n);
          scheduler.RequestLoad(c,1,request);
          const auto token=generation.Current(c).sequence;
          tokens_retired=tokens_retired && token>previous_token;
          previous_token=token;
          scheduler.Cancel(c);
          tokens_retired=tokens_retired && generation.LiveTokenCount()==0 &&
              scheduler.GetPendingQueueCount()==0;
        }
        report["generation_token_retirement"]={{"cancelled_request_cohorts",128},
            {"no_retained_token_history",tokens_retired},{"live_tokens",generation.LiveTokenCount()}};
        require(tokens_retired,"generation terminal owners release token history without sequence reuse");
        credits.SetWorkSlotCap(0);
        bool admitted_all=true;
        auto long_settings=request;
        long_settings.SeedText=std::string(4096,'s');
        long_settings.WorldGenPackId=std::string(64,'p');
        long_settings.WorldGenPresetId=std::string(96,'r');
        for(size_t n=0;n<UChunkLoadScheduler::kMaxActiveGenerationRequests;++n)
          admitted_all=scheduler.RequestLoad({1000+static_cast<int>(n),0,0},1000000000,long_settings) && admitted_all;
        const bool updated_at_full=scheduler.RequestLoad({1000,0,0},-10,duplicate) &&
            scheduler.QueuedRequests.at({1000,0,0}).settings->value.Seed==17 &&
            scheduler.QueuedRequests.at({1000,0,0}).priority==-10;
        const auto full_metadata=scheduler.GetMetadataStorageSnapshot();
        const auto full_token_allocator=generation.GetAllocationSnapshot();
        const glm::ivec3 desired(5000,0,0);
        const bool explicit_refusal=!scheduler.RequestLoad(desired,0,request) &&
            scheduler.GetState(desired)==ChunkLoadState::Absent && generation.Current(desired).sequence==0;
        const bool rejected_no_allocation=scheduler.GetMetadataStorageSnapshot().containers.live_bytes==
            full_metadata.containers.live_bytes && generation.GetAllocationSnapshot().live_bytes==
            full_token_allocator.live_bytes;
        UChunkStreamer producer(terrain,*world.BlockRegistry,1,0,-33,-48);
        producer.SetAsyncGeneration(true); producer.SetNearLoadRadius(1);
        producer.SetAsyncCallbacks([&](glm::ivec3 c,int priority,uint64_t demand) {
          return scheduler.RequestLoad(c,priority,request,{},false,demand);
        },[&](glm::ivec3 c){return scheduler.IsCommitted(c);});
        producer.SetColumnPendingCallback([&](glm::ivec3 c){return scheduler.IsPending(c);});
        const std::vector<glm::ivec3> required{desired};
        producer.EnsureDataColumns(required,1);
        const bool retained_requirement=admitted_all && explicit_refusal && updated_at_full &&
            scheduler.ActiveTokens.size()==1024 && scheduler.GetPendingQueueCount()==1024 &&
            generation.LiveTokenCount()==1024 && !scheduler.IsPending(desired) &&
            producer.GetLastFrameStats().asyncQueuedThisFrame==0;
        scheduler.Cancel({1000,0,0});
        producer.EnsureDataColumns(required,1);
        const bool producer_replayed=scheduler.IsPending(desired) && scheduler.ActiveTokens.size()==1024 &&
            producer.GetLastFrameStats().asyncQueuedThisFrame==1;
        credits.SetWorkSlotCap(8);
        scheduler.Tick(terrain,1,1);
        require(scheduler.Pool.WaitIdleFor(std::chrono::seconds(5)),"required-data replay executes actual generation");
        scheduler.Tick(terrain,1,0);
        const bool current_demand=scheduler.IsCommitted(desired) &&
            terrain.GetBlock({5000*CHUNK_SIZE+8,-40,8})==8;
        scheduler.CancelAllPending();
        const bool capacity_retired=scheduler.ActiveTokens.empty() && scheduler.QueuedRequests.empty() &&
            scheduler.GetPendingQueueCount()==0 && generation.LiveTokenCount()==0 &&
            scheduler.GetRetainedGenerationResultCount()==0;
        report["generation_request_capacity"]={{"limit",1024},{"same_key_update_at_full",updated_at_full},
            {"refused_requirement_retained",retained_requirement},{"normal_producer_replayed",producer_replayed},
            {"current_terrain_installed",current_demand},{"reset_releases_owners",capacity_retired}};
        require(retained_requirement && producer_replayed && current_demand && capacity_retired,
            "generation request capacity retains required-data producer demand without overflow storage");
        scheduler.SetMetadataCapacity(4);
        for(int n=0;n<4;++n) {
          require(scheduler.RequestLoad({6000+n,0,0},0,request),"metadata fixture admits within budget");
          scheduler.Tick(terrain,1,1);
          require(scheduler.Pool.WaitIdleFor(std::chrono::seconds(5)),"metadata fixture actual buffer ready");
          scheduler.Tick(terrain,1,0);
        }
        const glm::ivec3 fifth(6004,0,0),protected_column(6000,0,0);
        const bool metadata_full=scheduler.MetadataCount()==4 &&
            !scheduler.RequestLoad(fifth,0,request) && generation.Current(fifth).sequence==0;
        const std::vector<glm::ivec3> required_fifth{fifth};
        producer.EnsureDataColumns(required_fifth,1);
        const bool metadata_refused=!scheduler.IsPending(fifth);
        for(int n=1;n<4;++n) terrain.GetChunkManager().RemoveChunk({6000+n,-3,0});
        const auto has_data=[&](glm::ivec3 c) {
          return terrain.GetChunkManager().HasResidentTerrainColumn(c);
        };
        const auto retired_metadata=scheduler.RetireCommittedOutsideInterest({0,0,0},0,has_data,10000);
        const bool live_preserved=retired_metadata==3 && scheduler.MetadataCount()==1 &&
            scheduler.IsCommitted(protected_column) && terrain.GetBlock({6000*CHUNK_SIZE+8,-40,8})==8;
        producer.EnsureDataColumns(required_fifth,1);
        scheduler.Tick(terrain,1,1);
        require(scheduler.Pool.WaitIdleFor(std::chrono::seconds(5)),"metadata release admits required data");
        scheduler.Tick(terrain,1,0);
        const bool metadata_successor=scheduler.IsCommitted(fifth) &&
            terrain.GetBlock({6004*CHUNK_SIZE+8,-40,8})==8;
        scheduler.SetMetadataCapacity(1);
        const bool shrink_preserves=scheduler.MetadataCount()==2 &&
            !scheduler.RequestLoad({6005,0,0},0,request) &&
            scheduler.RetireCommittedOutsideInterest({0,0,0},0,has_data)==0;
        const auto newer_identity=generation.Bump(protected_column);
        terrain.GetChunkManager().RemoveChunk({6000,-3,0});
        terrain.GetChunkManager().RemoveChunk({6004,-3,0});
        scheduler.RetireCommittedOutsideInterest({0,0,0},0,has_data);
        const bool newer_preserved=scheduler.MetadataCount()==0 &&
            generation.Current(protected_column).sequence==newer_identity;
        generation.Retire(protected_column,newer_identity);
        scheduler.CancelAllPending();
        const auto reset_metadata=scheduler.GetMetadataStorageSnapshot();
        const auto reset_token_allocator=generation.GetAllocationSnapshot();
        const bool allocation_released=reset_metadata.containers.live_bytes==empty_metadata.containers.live_bytes &&
            reset_metadata.pending_string_capacity_bytes==0 &&
            reset_token_allocator.live_bytes==empty_token_allocator.live_bytes;
        report["generation_metadata_allocator"]={{"empty_container_bytes",empty_metadata.containers.live_bytes},
            {"full_container_bytes",full_metadata.containers.live_bytes},
            {"peak_container_bytes",reset_metadata.containers.peak_bytes},
            {"reset_container_bytes",reset_metadata.containers.live_bytes},
            {"full_pending_string_capacity_bytes",full_metadata.pending_string_capacity_bytes},
            {"empty_token_bytes",empty_token_allocator.live_bytes},{"full_token_bytes",full_token_allocator.live_bytes},
            {"reset_token_bytes",reset_token_allocator.live_bytes},{"rejected_no_allocation",rejected_no_allocation},
            {"reset_to_baseline",allocation_released}};
        require(rejected_no_allocation && allocation_released &&
            full_metadata.pending_string_capacity_bytes>=1024*(4096+64+96),
            "metadata allocator and separately charged string payload release to baseline");
        report["generation_metadata_capacity"]={{"test_capacity",4},{"full_refusal",metadata_full && metadata_refused},
            {"unowned_retired_live_data_preserved",live_preserved},{"required_successor",metadata_successor},
            {"shrink_does_not_evict",shrink_preserves},{"newer_identity_preserved",newer_preserved},
            {"final_live_tokens",generation.LiveTokenCount()}};
        require(metadata_full && metadata_refused && live_preserved && metadata_successor &&
            shrink_preserves && newer_preserved && generation.LiveTokenCount()==0,
            "generation metadata budget retains live owners and reclaims abandoned committed history");
        scheduler.SetMetadataCapacity(2048);
        require(scheduler.SettingsPayloadBytes()==0,"settings leases release after all preceding owners retire");
        auto byte_settings=long_settings; byte_settings.SeedText=std::string(8192,'b');
        const auto copies_before=scheduler.SettingsBudget->snapshots_created.load();
        size_t byte_admitted=0;
        glm::ivec3 byte_refused(0);
        for(size_t n=0;n<=UChunkLoadScheduler::kMaxActiveGenerationRequests;++n) {
          const glm::ivec3 c(7000+static_cast<int>(n),0,0);
          if(!scheduler.RequestLoad(c,1000000000,byte_settings)) {byte_refused=c;break;}
          ++byte_admitted;
        }
        const auto charged_before=scheduler.SettingsPayloadBytes();
        const auto copies_for_admission=scheduler.SettingsBudget->snapshots_created.load()-copies_before;
        const bool refusal_before_copy=copies_for_admission==byte_admitted;
        const auto normal_byte_peak=scheduler.SettingsPayloadPeak();
        const auto *first_snapshot=scheduler.QueuedRequests.at({7000,0,0}).settings.get();
        const bool byte_refusal=byte_admitted>0 && byte_admitted<1024 &&
            scheduler.SettingsPayloadLimit()==UChunkLoadScheduler::kSettingsPayloadBudget &&
            charged_before<=UChunkLoadScheduler::kSettingsPayloadBudget &&
            scheduler.GetState(byte_refused)==ChunkLoadState::Absent &&
            generation.Current(byte_refused).sequence==0;
        const bool byte_merge=scheduler.RequestLoad({7000,0,0},-10,duplicate) &&
            scheduler.QueuedRequests.at({7000,0,0}).settings.get()==first_snapshot &&
            scheduler.SettingsPayloadBytes()==charged_before;
        UChunkStreamer byte_producer(terrain,*world.BlockRegistry,1,0,-33,-48);
        byte_producer.SetAsyncGeneration(true); byte_producer.SetNearLoadRadius(1);
        byte_producer.SetAsyncCallbacks([&](glm::ivec3 c,int priority,uint64_t demand) {
          return scheduler.RequestLoad(c,priority,byte_settings,{},false,demand);
        },[&](glm::ivec3 c){return scheduler.IsCommitted(c);});
        byte_producer.SetColumnPendingCallback([&](glm::ivec3 c){return scheduler.IsPending(c);});
        const std::vector<glm::ivec3> byte_required{byte_refused};
        std::promise<void> settings_entered,settings_release;
        auto settings_started=settings_entered.get_future();
        populator.settings_gate=settings_release.get_future().share();
        populator.settings_entered=&settings_entered;
        struct ReleaseSettingsGate {std::promise<void> &p; UJobThreadPool &pool; bool done{false};
          void Release(){if(!done){p.set_value();done=true;}}
          ~ReleaseSettingsGate(){Release();pool.WaitIdleFor(std::chrono::seconds(5));}
        } settings_guard{settings_release,scheduler.Pool};
        require(scheduler.StartQueuedGenerationWorkers(1)==1 &&
            settings_started.wait_for(std::chrono::seconds(5))==std::future_status::ready,
            "settings byte lease fixture holds actual worker");
        scheduler.Cancel({7000,0,0});
        byte_producer.EnsureDataColumns(byte_required,1);
        const bool running_holds=scheduler.SettingsPayloadBytes()==charged_before &&
            !scheduler.IsPending(byte_refused);
        settings_guard.Release();
        require(scheduler.Pool.WaitIdleFor(std::chrono::seconds(5)),"settings worker lease releases after cancellation");
        populator.settings_entered=nullptr; populator.settings_gate={};
        byte_producer.EnsureDataColumns(byte_required,1);
        const bool byte_readmitted=scheduler.IsPending(byte_refused);
        scheduler.Tick(terrain,1,1);
        require(scheduler.Pool.WaitIdleFor(std::chrono::seconds(5)),"settings pressure successor computes");
        scheduler.Tick(terrain,1,0);
        const bool byte_installed=scheduler.IsCommitted(byte_refused) &&
            terrain.GetBlock({byte_refused.x*CHUNK_SIZE+8,-40,8})==8;
        scheduler.CancelAllPending();
        const bool byte_released=scheduler.SettingsPayloadBytes()==0;
        auto spare_settings=byte_settings;
        spare_settings.SeedText.reserve(UChunkLoadScheduler::kSettingsPayloadBudget*2);
        const auto external_text=[](const ProceduralSettings &s) {
          size_t bytes=0;
          for(const auto *text:{&s.SeedText,&s.WorldGenPackId,&s.WorldGenPresetId}) {
            const auto data=reinterpret_cast<uintptr_t>(text->data());
            const auto object=reinterpret_cast<uintptr_t>(text);
            if(data<object || data-object>=sizeof(*text)) bytes+=text->capacity()+1;
          }
          return bytes;
        };
        bool canonical_charge=true,caller_capacity_ignored=true;
        size_t boundary_cases=0;
        for(const size_t length:{0u,1u,15u,16u,17u,31u,32u,47u,48u,255u,256u,65536u}) {
          spare_settings.SeedText.assign(length,'s');
          spare_settings.WorldGenPackId.assign(length,'p');
          spare_settings.WorldGenPresetId.assign(length,'r');
          const ProceduralSettings canonical(spare_settings);
          const auto before=scheduler.SettingsBudget->snapshots_created.load();
          const bool accepted=scheduler.RequestLoad({8999,0,0},0,spare_settings);
          if(accepted) {
            const auto &snapshot=*scheduler.QueuedRequests.at({8999,0,0}).settings;
            const size_t expected=sizeof(UChunkLoadScheduler::OwnedSettings)+sizeof(ProceduralSettings)+
                2*external_text(canonical);
            canonical_charge=canonical_charge && snapshot.value.SeedText==canonical.SeedText &&
                snapshot.value.WorldGenPackId==canonical.WorldGenPackId &&
                snapshot.value.WorldGenPresetId==canonical.WorldGenPresetId &&
                external_text(snapshot.value)==external_text(canonical) &&
                snapshot.reservation.charge==expected && scheduler.SettingsPayloadBytes()==expected &&
                scheduler.SettingsBudget->snapshots_created.load()==before+1;
          }
          caller_capacity_ignored=caller_capacity_ignored && accepted &&
              scheduler.SettingsPayloadLimit()==UChunkLoadScheduler::kSettingsPayloadBudget &&
              spare_settings.SeedText.capacity()>=UChunkLoadScheduler::kSettingsPayloadBudget*2;
          scheduler.Cancel({8999,0,0});
          canonical_charge=canonical_charge && scheduler.SettingsPayloadBytes()==0;
          ++boundary_cases;
        }
        auto oversized=byte_settings;
        oversized.SeedText=std::string(UChunkLoadScheduler::kSettingsPayloadBudget/2+128,'h');
        const bool oversized_first=scheduler.RequestLoad({9000,0,0},0,oversized);
        const auto oversized_limit=scheduler.SettingsPayloadLimit();
        const bool single_floor=oversized_first && oversized_limit>UChunkLoadScheduler::kSettingsPayloadBudget &&
            scheduler.SettingsPayloadBytes()==oversized_limit &&
            !scheduler.RequestLoad({9001,0,0},0,oversized);
        scheduler.Cancel({9000,0,0});
        const bool oversized_reentry=scheduler.RequestLoad({9001,0,0},0,oversized);
        scheduler.CancelAllPending();
        report["generation_settings_bytes"]={{"base_budget",UChunkLoadScheduler::kSettingsPayloadBudget},
            {"accepted_before_byte_refusal",byte_admitted},{"normal_peak_charge",normal_byte_peak},
            {"snapshots_created",copies_for_admission},{"refusal_before_copy",refusal_before_copy},
            {"copy_boundary_cases",boundary_cases},{"canonical_capacity_charge",canonical_charge},
            {"caller_spare_capacity_ignored",caller_capacity_ignored},
            {"refusal_without_owner",byte_refusal},{"same_snapshot_merge",byte_merge},
            {"cancelled_worker_holds_charge",running_holds},{"normal_producer_readmission",byte_readmitted},
            {"actual_successor",byte_installed},{"normal_reset_releases",byte_released},
            {"one_oversized_request_floor",single_floor},{"oversized_reentry",oversized_reentry},
            {"oversized_limit",oversized_limit},{"final_charge",scheduler.SettingsPayloadBytes()}};
        require(canonical_charge && caller_capacity_ignored && refusal_before_copy && byte_refusal && byte_merge && running_holds && byte_readmitted && byte_installed &&
            byte_released && single_floor && oversized_reentry && scheduler.SettingsPayloadBytes()==0,
            "settings payload budget retains exact demand through pending worker and normal replay");
        report["generation_ready_credit"]={{"limit",8},{"full_refusal_retained",ready_full},
            {"ready_requeue_holds_credit",held},{"stale_release_exact",stale_released},
            {"automatic_readmission",readmitted},{"actual_install_releases_credit",installed},
            {"cancel_all_releases_credit",all_retired}};
        report["generation_buffer_payload"]={{"ready8_owned_bytes",ready_payload_before},
            {"budget_requeue_holds_bytes",payload_held},{"stale_drops_exact_buffer_bytes",payload_stale_released},
            {"after_install_ready_bytes",payload_after_install}};
        require(ready_full && held && stale_released && readmitted && installed && all_retired,
            "generation result quota spans compute ready install and exact retirement");
        require(payload_held && payload_stale_released,
            "actual generation buffer allocation ownership follows ready requeue and stale retirement");
      });
      if(flow_only) RunTerrainProbePhase([&] {
        struct BufferPopulator final : IUChunkPopulator {
          std::atomic<unsigned> calls{0};
          ChunkPopulateResult Populate(const ChunkPopulateRequest &request) override {
            ++calls;
            ChunkPopulateResult result; result.coord=request.chunkCoord; result.token=request.token;
            for(int n=0;n<1024;++n)
              result.buffer.SetBlock({request.chunkCoord.x*16+n%16,-48+n/256,
                  request.chunkCoord.z*16+(n/16)%16},request.settings.Seed==29 ? 9 : 8);
            return result;
          }
        } populator;
        UChunkGenerationRegistry generation;
        UChunkLoadScheduler scheduler(populator,generation);
        scheduler.BufferBudget=std::make_shared<UBufferAllocationBudget>(256*1024);
        UBlockWorld terrain; terrain.SetFluidDefinitions(definitions.get());
        terrain.SetVerticalProfile({-48,-33,-40});
        ProceduralSettings request; request.MinHeight=-48; request.MaxHeight=-33; request.Seed=17;
        unsigned arrivals=0;
        scheduler.SetMarkDirtyFn([&](glm::ivec3,int,int,bool){++arrivals;});
        auto compute=[&](glm::ivec3 coord,const ProceduralSettings &input) {
          require(scheduler.RequestLoad(coord,0,input),"buffer quota fixture accepts request");
          require(scheduler.StartQueuedGenerationWorkers(1)==1,"buffer quota fixture starts worker");
          require(scheduler.Pool.WaitIdleFor(std::chrono::seconds(5)),"buffer quota worker finishes");
        };
        for(int x=0;x<3;++x) compute({x,0,0},request);
        const auto ready_charge=scheduler.BufferReservedBytes();
        const glm::ivec3 target(3,0,0);
        require(scheduler.RequestLoad(target,0,request),"buffer retry request accepted");
        const auto token=scheduler.QueuedRequests.at(target).token.sequence;
        const auto time=scheduler.QueuedRequests.at(target).requestedAt;
        require(scheduler.StartQueuedGenerationWorkers(1)==1 &&
            scheduler.Pool.WaitIdleFor(std::chrono::seconds(5)),"buffer quota refusal completes normally");
        const bool refused=scheduler.BufferBudget->RefusalCount()>0 &&
            scheduler.BufferReservedBytes()==ready_charge;
        scheduler.Tick(terrain,0,0);
        const auto &retry=scheduler.QueuedRequests.at(target);
        const bool exact=retry.token.sequence==token && retry.requestedAt==time &&
            retry.settings->value.Seed==17;
        const bool no_spin=scheduler.StartQueuedGenerationWorkers(8)==0;
        scheduler.Tick(terrain,8,0);
        const bool drained=scheduler.BufferReservedBytes()==0 && arrivals==3;
        scheduler.Tick(terrain,1,1);
        require(scheduler.Pool.WaitIdleFor(std::chrono::seconds(5)),"buffer retry computes after drain");
        scheduler.Tick(terrain,1,0);
        const bool installed=scheduler.IsCommitted(target) && arrivals==4 && populator.calls==5 &&
            terrain.GetBlock({56,-48,0})==8 && scheduler.BufferReservedBytes()==0;
        report["generation_buffer_quota"]={{"base_bytes",256*1024},{"ready3_charge",ready_charge},
            {"refused_before_allocation",refused},{"exact_retry",exact},{"no_busy_retry",no_spin},
            {"drain_releases",drained},{"automatic_install",installed},
            {"peak_bytes",scheduler.BufferReservedPeak()},{"limit_bytes",scheduler.BufferBudgetLimit()}};
        require(refused && exact && no_spin && drained && installed &&
            scheduler.BufferReservedPeak()<=scheduler.BufferBudgetLimit() &&
            scheduler.BufferBudgetLimit()==256*1024,
            "actual buffer quota retains demand and automatically recovers after Ready drain");
        scheduler.Tick(terrain,0,0); // reset serial mode after the last owner retires
        for(int x=10;x<13;++x) compute({x,0,0},request);
        const auto refusals_before=scheduler.BufferBudget->RefusalCount();
        compute({13,0,0},request);
        const auto old_token=generation.Current({13,0,0}).sequence;
        require(scheduler.BufferBudget->RefusalCount()>refusals_before,
            "replacement fixture actually refuses old buffer allocation");
        scheduler.Cancel({13,0,0});
        auto replacement=request; replacement.Seed=29;
        require(scheduler.RequestLoad({13,0,0},-3,replacement),"new buffer owner admitted after cancel");
        const auto new_token=scheduler.QueuedRequests.at({13,0,0}).token.sequence;
        scheduler.Tick(terrain,8,0);
        const auto &new_request=scheduler.QueuedRequests.at({13,0,0});
        const bool new_owner=old_token!=new_token && new_request.token.sequence==new_token &&
            new_request.settings->value.Seed==29 && scheduler.BufferReservedBytes()==0;
        scheduler.Tick(terrain,1,1);
        require(scheduler.Pool.WaitIdleFor(std::chrono::seconds(5)),"replacement buffer computes");
        scheduler.Tick(terrain,1,0);
        const bool replacement_installed=scheduler.IsCommitted({13,0,0}) && arrivals==8 &&
            populator.calls==10 && terrain.GetBlock({216,-48,0})==9 && scheduler.BufferReservedBytes()==0;
        require(new_owner && replacement_installed,"late quota retry cannot resurrect cancelled old owner");

        scheduler.Tick(terrain,0,0);
        require(scheduler.GetRetainedGenerationResultCount()==0,"sole-job fixture has no retained owner");
        scheduler.BufferBudget=std::make_shared<UBufferAllocationBudget>(32*1024);
        compute({20,0,0},request);
        const auto floor=scheduler.BufferBudgetLimit();
        const bool oversized=floor>32*1024 && scheduler.BufferReservedBytes()==floor;
        require(scheduler.RequestLoad({21,0,0},0,replacement),"second floor request accepted");
        const bool floor_blocks=scheduler.StartQueuedGenerationWorkers(1)==0;
        scheduler.Tick(terrain,1,0);
        const bool floor_release=scheduler.BufferReservedBytes()==0;
        scheduler.Tick(terrain,1,1);
        require(scheduler.Pool.WaitIdleFor(std::chrono::seconds(5)),"sole-job floor successor computes");
        scheduler.Tick(terrain,1,0);
        const bool floor_successor=scheduler.IsCommitted({20,0,0}) && scheduler.IsCommitted({21,0,0}) &&
            terrain.GetBlock({328,-48,0})==8 && terrain.GetBlock({344,-48,0})==9 &&
            arrivals==10 && populator.calls==12 && scheduler.BufferReservedBytes()==0;
        report["generation_buffer_quota"]["cancel_preserves_new_owner"]=new_owner;
        report["generation_buffer_quota"]["replacement_installs_once"]=replacement_installed;
        report["generation_buffer_quota"]["sole_job_floor_bytes"]=floor;
        report["generation_buffer_quota"]["sole_job_floor"]=oversized;
        report["generation_buffer_quota"]["floor_blocks_competitor"]=floor_blocks;
        report["generation_buffer_quota"]["floor_owner_release"]=floor_release;
        report["generation_buffer_quota"]["floor_successor"]=floor_successor;
        require(oversized && floor_blocks && floor_release && floor_successor &&
            scheduler.BufferReservedPeak()<=scheduler.BufferBudgetLimit(),
            "one oversized buffer progresses without concurrent unbounded admission");
      });
      if(flow_only) RunTerrainProbePhase([&] {
        auto owner=std::make_unique<UWorld>(nullptr,nullptr); auto &test=*owner;
        test.SetSpawnPoint({8,-40,8});
        test.ProceduralTemplate.MinHeight=-48; test.ProceduralTemplate.MaxHeight=-33;
        test.BlockWorld.SetVerticalProfile({-48,-33,-40});
        test.BlockRegistry=std::make_unique<UBlockRegistry>(nullptr,definitions);
        test.BlockWorld.SetFluidDefinitions(definitions.get());
        test.BlockWorld.SetKnownOpenSkyAbsentQuery([](glm::ivec3){return true;});
        auto full=settings; full.Lighting=LightingMode::Full;
        test.SetRenderSettings(full);
        test.AsyncRelight=std::make_unique<UAsyncRelightBuilder>(1);
        test.NextAsyncRelightJobId=9700;
        RelightJobSpec spec;
        spec.job_id=9700; spec.block_positions={{4,-40,4}};
        spec.min_world_y=-48; spec.max_world_y=-33;
        spec.column_center_only=true; spec.include_known_air_outputs=true;
        spec.known_air_output_sections={{0,-3,0}};
        spec.light_sample_output_sections={{0,-3,0}};
        spec.finalize_pending_gate=false;
        auto &credits=UPipelineAdmission::Get();
        struct RestoreSlots { ~RestoreSlots(){UPipelineAdmission::Get().SetWorkSlotCap(8);} } restore;
        credits.SetWorkSlotCap(0);
        test.AsyncRelight->EnqueueJob(test.BlockWorld,spec,*test.BlockRegistry);
        const bool debt_visible=test.HasPendingAsyncRelightWork();
        test.DrainAsyncRelightResults(1,false,false);
        const bool deferred=test.DeferredRelightRetries.Size()==1 &&
            test.AsyncRelight->GetInFlightCount()==0 && test.AsyncRelight->GetQueuedJobCount()==0;
        credits.SetWorkSlotCap(8);
        test.DrainAsyncRelightResults(1,false,false);
        require(test.AsyncRelight->WaitIdleFor(std::chrono::seconds(5)),
            "world relight admission successor computes after shared pressure release");
        test.DrainAsyncRelightResults(4,false,false);
        const bool installed=test.BlockWorld.GetChunkManager().GetSectionLightField({0,-3,0})!=nullptr;
        report["relight_shared_admission"]={{"deferred_without_compute",deferred},
            {"refused_debt_visible",debt_visible},{"automatic_air_field_install",installed},
            {"remaining_retry",test.DeferredRelightRetries.Size()}};
        require(debt_visible && deferred && installed && test.DeferredRelightRetries.Empty(),
            "real world retains refused full relight and automatically installs AIR successor");
        require(test.TryAbandonTerrainForWorldReplace(), "retire idle relight retry world");
      });
      if(flow_only) RunTerrainProbePhase([&] {
        struct MetadataPopulator final : IUChunkPopulator {
          ChunkPopulateResult Populate(const ChunkPopulateRequest &request) override {
            ChunkPopulateResult result; result.coord=request.chunkCoord; result.token=request.token;
            result.discarded=true; return result;
          }
        } populator;
        auto owner=std::make_unique<UWorld>(nullptr,nullptr); auto &test=*owner;
        test.SetSpawnPoint({8,-40,8});
        test.ProceduralTemplate.MinHeight=-48; test.ProceduralTemplate.MaxHeight=-33;
        test.BlockWorld.SetVerticalProfile({-48,-33,-40});
        test.BlockRegistry=std::make_unique<UBlockRegistry>(nullptr,definitions);
        test.BlockWorld.SetFluidDefinitions(definitions.get());
        test.BlockWorld.SetKnownOpenSkyAbsentQuery([](glm::ivec3){return true;});
        test.SetRenderSettings(settings);
        UColumnFlowExecutor flow; flow.BindDecideWorld(&test);
        auto &stream=*test.Streaming; auto &p=*test.Persistence;
        auto &records=test.GetColumnRecords();
        const auto epoch=test.MeshService->GetCache().GetCaptureStore().WorldEpoch();
        for(int n=0;n<100;++n) {
          ColumnWorkItem item{}; item.column={1000+n,0}; item.cy=-3;
          if(n%2==0) records.NoteFlowIntent(item,epoch);
          else flow.Scheduler().Enqueue(item);
          flow.SetColumnJobStage(test,item.column,ColumnJobStage::Meshing);
        }
        for(const int x : {0,1100,1101,1200,1201,1300,1301,1302})
          flow.SetColumnJobStage(test,{x,0},ColumnJobStage::Meshing);
        test.BlockWorld.SetBlock({1100*CHUNK_SIZE+8,-40,8},8);
        const glm::ivec3 air(1101,-3,0);
        auto frame=std::make_unique<UChunk>(air);
        UBinaryChunkSerializer codec;
        const auto decoded=codec.Deserialize(codec.Serialize(air,*frame,*test.BlockRegistry).bytes,air,*test.BlockRegistry);
        require(ApplyTerrainSliceLoad(test.BlockWorld,air,&decoded,nullptr,{{1101,0,0},0},0,false),
            "metadata AIR guard owns actual decoded geometry-free terrain");
        require(!test.BlockWorld.GetChunkManager().HasResidentGeometryColumn({1101,0,0}) &&
            test.BlockWorld.GetChunkManager().HasResidentTerrainColumn({1101,0,0}),"AIR has terrain ownership without geometry");
        // Indexed IO/light guards; physical IO ownership is separately proven
        // by the Q10 receipts. Generation below is a real queued request.
        p.PendingAsyncColumnLoadSlices.try_emplace({1200,0,0});
        p.PendingAsyncColumnSaveSlices.try_emplace({1201,0,0});
        p.PendingTerrainColumnRelightKeys.insert({1300*CHUNK_SIZE,0});
        test.AsyncRelight=std::make_unique<UAsyncRelightBuilder>(1);
        RelightJobSpec owner_spec;
        owner_spec.job_id=1302;
        owner_spec.block_positions={{1302*CHUNK_SIZE+8,-40,8}};
        owner_spec.min_world_y=-48; owner_spec.max_world_y=-33;
        owner_spec.column_center_only=true;
        test.AsyncRelight->EnqueueJob(test.BlockWorld,owner_spec,*test.BlockRegistry);
        require(test.AsyncRelight->WaitIdleFor(std::chrono::seconds(5)) &&
            test.AsyncRelight->GetCompletedSize()==1,"metadata light guard has real undelivered result");
        test.AsyncRelightColumnsInFlight[{1302,0}]=owner_spec.job_id;
        stream.ChunkScheduler=std::make_unique<UChunkLoadScheduler>(populator,stream.ChunkGenTokens);
        stream.ChunkScheduler->RequestLoad({1301,0,0},1,test.ProceduralTemplate);
        report["column_record_owner_preconditions"]={
            {"generation_pending",stream.ChunkScheduler->IsPending({1301,0,0})},
            {"async_light_active",test.IsAsyncRelightColumnInFlight({1302,0})}};
        require(stream.ChunkScheduler->IsPending({1301,0,0}) &&
            test.IsAsyncRelightColumnInFlight({1302,0}),"metadata owner guards are executable before retirement");
        size_t retired=0,max_visit=0;
        for(int n=0;n<8;++n) {
          const auto result=stream.RetireUnownedColumnRecords(test,flow,{0,0,0},4,10000);
          retired+=result.retired; max_visit=std::max(max_visit,result.visited);
        }
        bool orphans_gone=true,protected_kept=true;
        for(int n=0;n<100;++n) orphans_gone=orphans_gone && !records.Find({1000+n,0});
        for(const int x : {0,1100,1101,1200,1201,1300,1301,1302}) {
          const bool kept=records.Find({x,0})!=nullptr;
          report["column_record_owner_results"].push_back({{"column_x",x},{"kept",kept}});
          protected_kept=protected_kept && kept;
        }
        p.PendingAsyncColumnLoadSlices.erase({1200,0,0});
        p.PendingAsyncColumnSaveSlices.erase({1201,0,0});
        p.PendingTerrainColumnRelightKeys.erase({1300*CHUNK_SIZE,0});
        test.AsyncRelightColumnsInFlight.erase({1302,0});
        require(test.AsyncRelight->DrainCompleted(1).size()==1,
            "metadata light owner releases its actual result");
        stream.ChunkScheduler->Cancel({1301,0,0});
        for(int n=0;n<4;++n) stream.RetireUnownedColumnRecords(test,flow,{0,0,0},4,32);
        bool released_gone=true;
        for(const int x : {1200,1201,1300,1301,1302}) released_gone=released_gone && !records.Find({x,0});
        auto &fresh=records.GetOrCreate({1000,0});
        bool fresh_intent=true; for(const auto &item:fresh.flow_intent) fresh_intent=fresh_intent && !item.pending;
        const bool fresh_stage=records.GetFlowJobStage({1000,0})==ColumnJobStage::Absent;
        test.BlockWorld.SetBlock({1000*CHUNK_SIZE+8,-40,8},8);
        test.MeshService->RebuildChunkImmediate(test.BlockWorld,*test.BlockRegistry,{1000,-3,0});
        const bool current=test.HasCurrentChunkSlicePublication({1000,-3,0});
        report["column_record_interest_retirement"]={{"retired",retired},{"max_visited",max_visit},
            {"orphans_gone",orphans_gone},{"protected_owners_kept",protected_kept},
            {"released_owners_retired",released_gone},{"fresh_intent",fresh_intent},
            {"fresh_stage",fresh_stage},{"current_publication",current}};
        require(max_visit<=32 && orphans_gone && protected_kept && released_gone &&
            flow.Scheduler().LiveCount()==0 && fresh_intent && fresh_stage && current,
            "bounded interest retirement preserves live owners and fresh current reentry");
        require(test.TryAbandonTerrainForWorldReplace(), "retire idle metadata world");
      });
      if(flow_only) report["cases"].push_back({{"kind","flow_signed_publication"},{"status","PASS"}});
      else if(face_only) report["cases"].push_back({{"kind","face_repair_lifecycle"},{"status","PASS"}});
      else report["cases"].push_back({{"kind","flat_section_publication"},{"status","PASS"},
          {"physical_certificate_absent",true},{"actual_publication_recovers",true},
          {"geometry_mutation_rejected",true},{"replacement_without_relight",true}});
      report["status"]="PASS";
      std::ofstream(root/"report.json") << report.dump(2) << '\n';
      return;


    }
    // S4.01: section physical truth versus surviving coarse column state.
    {
      UGpuGreedyMesher gpu_mesher;
      UWorld world(nullptr,nullptr);
      world.ProceduralTemplate.MinHeight=-48;
      world.ProceduralTemplate.MaxHeight=-33;
      world.ProceduralTemplate.SeaLevel=-40;
      world.BlockWorld.SetVerticalProfile({-48,-33,-40});
      (void)world.GetLightingPipeline();
      auto definitions=std::make_shared<UBlockDefinitionStorage>();
      BlockDefinition stone,torch;
      stone.Physics=BlockPhysicsProfile::Solid(); stone.Name="stone";
      torch.Render.Style=BlockRenderStyle::Cross; torch.Name="torch"; torch.Lighting.Emission=14;
      definitions->ReplaceAll({{8,stone},{50,torch}},{{"stone",8},{"torch",50}});
      world.BlockRegistry=std::make_unique<UBlockRegistry>(nullptr,definitions);
      world.BlockWorld.SetFluidDefinitions(definitions.get());
      world.BlockWorld.SetKnownOpenSkyAbsentQuery([](glm::ivec3){return true;});
      const glm::ivec3 coord(0,-3,0);
      const auto populate=[&] {
        for(int x=0;x<16;++x) for(int z=0;z<16;++z) world.BlockWorld.SetBlock({x,-48,z},8);
        world.BlockWorld.SetBlock({8,-40,8},50);
      };
      const auto settle=[&] {
        RelightJobSpec spec; spec.min_world_y=-48; spec.max_world_y=-33;
        auto snapshot=UChunkRelightSnapshot::CaptureSections(world.BlockWorld,spec,{coord});
        snapshot.SetInputCatalog(world.BlockRegistry->GetDefinitionsCatalogSnapshot());
        const auto result=snapshot.Compute(*world.BlockRegistry);
        require(result.inputs_complete && result.chunks.size()==1,"section owner fixture complete calculation");
        InstallComputedLight(world.BlockWorld,result.chunks[0],true,true);
        require(world.BlockWorld.RecordLightCalculation(result,
                    world.BlockRegistry->GetDefinitionsCatalogSnapshot())==1,
                "section owner fixture records physical certificate");
        world.NoteChunkSliceLightCalculationSettled(coord);
      };
      populate(); settle();
      world.SetColumnEmergeState({0,0,0},ColumnEmergeState::LitReady);
      require(world.HasCurrentColumnLightCoverage({0,0},-48,-33) &&
                  world.HasCurrentChunkSliceLightSettlement(coord) && world.IsColumnLitReady({0,0,0}),
              "section owner positive ready precondition");
      world.BlockWorld.SetBlock({2,-40,2},8);
      report["section_owner_checkpoint"]={
          {"physical",world.HasCurrentColumnLightCoverage({0,0},-48,-33)},
          {"section_settlement",world.HasCurrentChunkSliceLightSettlement(coord)},
          {"column_ready",world.IsColumnLitReady({0,0,0})},
          {"coarse_state",static_cast<int>(world.GetColumnEmergeState({0,0,0}))},
          {"pending",world.IsPendingLightBeforeMesh({0,0})}};
      require(!world.HasCurrentColumnLightCoverage({0,0},-48,-33) &&
                  !world.HasCurrentChunkSliceLightSettlement(coord) && !world.IsColumnLitReady({0,0,0}),
              "coarse column cannot certify geometry-invalidated section");
      UColumnFlowExecutor section_executor;
      section_executor.SyncColumnJobStageFromWorld(world,{0,0});
      report["section_owner_job_stage_checkpoint"]={
          {"stage",ColumnJobStageName(section_executor.GetColumnJobStage({0,0}))},
          {"pending_light",world.GetColumnRecords().GetOrCreate({0,0}).pending_light},
          {"physical",world.HasCurrentColumnLightCoverage({0,0},-48,-33)}};
      require(world.GetColumnRecords().GetOrCreate({0,0}).pending_light &&
                  section_executor.GetColumnJobStage({0,0})==ColumnJobStage::PendingLight,
              "executor retains uncertified section light debt without queue metadata");
      settle();
      require(world.IsColumnLitReady({0,0,0}),"section owner recompute restores aggregate");
      section_executor.SyncColumnJobStageFromWorld(world,{0,0});
      require(!world.GetColumnRecords().GetOrCreate({0,0}).pending_light &&
                  section_executor.GetColumnJobStage({0,0})!=ColumnJobStage::PendingLight,
              "executor clears physical light debt after current calculation");
      const auto previous_incarnation=world.BlockWorld.GetChunkManager().GetChunk(coord)->GetIncarnation();
      world.BlockWorld.GetChunkManager().RemoveChunk(coord);
      populate(); world.BlockWorld.SetBlock({2,-40,2},8);
      require(world.BlockWorld.GetChunkManager().GetChunk(coord)->GetIncarnation()!=previous_incarnation &&
                  !world.HasCurrentChunkSliceLightSettlement(coord) && !world.IsColumnLitReady({0,0,0}),
              "old section incarnation cannot confirm replacement aggregate");
      settle();
      const auto revision=world.BlockWorld.GetChunkManager().GetChunk(coord)->GetLightFieldRevision();
      world.NoteChunkSliceLightCalculationSettled(coord);
      world.NoteChunkSliceLightCalculationSettled(coord);
      require(world.IsColumnLitReady({0,0,0}) &&
                  world.BlockWorld.GetChunkManager().GetChunk(coord)->GetLightFieldRevision()==revision,
              "repeated settlement preserves current field identity");
      require(!world.IsColumnLitReady({7,0,7}),"unknown coarse Empty is not physical readiness");
      // An unfinished different slice must not reopen an already-current band.
      world.ProceduralTemplate.MaxHeight=-17;
      auto wider=world.BlockWorld.GetVerticalProfile(); wider.MaxY=-17;
      world.BlockWorld.SetVerticalProfile(wider);
      world.BlockWorld.SetBlock({4,-24,4},8);
      settle();
      require(world.HasCurrentColumnLightCoverage({0,0},-48,-33) &&
                  !world.IsColumnLitReady({0,0,0}),
              "section owner mixed-band precondition");
      require(!world.Persistence->TryEnqueueTerrainColumnRelight(world,0,0,false,-48,-33) &&
                  !world.IsTerrainColumnRelightQueued({0,0}),
              "current band is not requeued because a different slice is unfinished");
      world.SetColumnEmergeState({0,0,0},ColumnEmergeState::Meshing);
      const bool promoted=UChunkEmergeCoordinator::TryPromoteLitColumn(world,{0,0},section_executor);
      report["section_owner_publication_checkpoint"]={
          {"lower_current",world.HasCurrentColumnLightCoverage({0,0},-48,-33)},
          {"upper_current",world.HasCurrentColumnLightCoverage({0,0},-32,-17)},
          {"pending",world.IsPendingLightBeforeMesh({0,0})},
          {"promoted",promoted},
          {"coarse_state",static_cast<int>(world.GetColumnEmergeState({0,0,0}))},
          {"stage",ColumnJobStageName(section_executor.GetColumnJobStage({0,0}))}};
      require(!promoted && world.GetColumnEmergeState({0,0,0})!=ColumnEmergeState::RenderReady,
              "lit publication cannot promote column with uncertified sibling");
      world.SetColumnEmergeState({0,0,0},ColumnEmergeState::LitReady);
      require(!UChunkEmergeCoordinator::TryPromoteLitColumn(world,{0,0},section_executor),
              "coarse LitReady also cannot bypass uncertified sibling");
      RelightJobSpec all_bands;
      all_bands.min_world_y=-48; all_bands.max_world_y=-17;
      auto full_snapshot=UChunkRelightSnapshot::CaptureSections(
          world.BlockWorld,all_bands,{coord,{0,-2,0}});
      full_snapshot.SetInputCatalog(world.BlockRegistry->GetDefinitionsCatalogSnapshot());
      const auto full_result=full_snapshot.Compute(*world.BlockRegistry);
      require(full_result.inputs_complete && full_result.chunks.size()==2,
              "publication aggregate full-column calculation precondition");
      for(const auto &chunk:full_result.chunks)
        InstallComputedLight(world.BlockWorld,chunk,true,true);
      require(world.BlockWorld.RecordLightCalculation(full_result,
                  world.BlockRegistry->GetDefinitionsCatalogSnapshot())==2,
              "publication aggregate records both section certificates");
      world.NoteChunkSliceLightCalculationSettled(coord);
      world.NoteChunkSliceLightCalculationSettled({0,-2,0});
      require(world.IsColumnLitReady({0,0,0}),
              "full calculation restores the current column light aggregate");
      world.ClearColumnEmergeState({0,0});
      require(world.GetColumnEmergeState({0,0,0})==ColumnEmergeState::Empty,
              "completion fixture starts without surviving column metadata");
      report["section_owner_completion_checkpoint"]={
          {"coarse_state",static_cast<int>(world.GetColumnEmergeState({0,0,0}))},
          {"draw_permission",world.GetColumnRenderableState({0,0}).draw_ok},
          {"column_complete",world.IsColumnRenderReady({0,0,0})},
          {"light_current",world.IsColumnLitReady({0,0,0})},
          {"lower_publications",world.GetMeshService().GetCache().GetPublishCount(coord)},
          {"upper_publications",world.GetMeshService().GetCache().GetPublishCount({0,-2,0})}};
      require(!world.IsColumnRenderReady({0,0,0}),
              "draw permission cannot certify missing section publications");
      require(!UChunkEmergeCoordinator::TryPromoteLitColumn(world,{0,0},section_executor),
              "light-ready notification alone cannot certify missing geometry");
      auto &mesh=world.GetMeshService();
      auto &cache=mesh.GetCache();
      SectionRenderLightBindings bindings;
      const auto source_light=world.BlockWorld.CaptureRenderLightSnapshot(coord);
      require(source_light && bindings.BeginWorld(cache.GetCaptureStore().WorldEpoch(),
                  source_light->GeometryInputs().world_geometry_identity,16,
                  8u*SectionLightVolume::CellCount,8),
              "completion fixture initializes actual GPU light resource owner");
      RenderSettings settings;
      settings.AsyncMeshing=false; settings.GpuPackedMeshing=false;
      settings.FrustumCulling=false;
      mesh.SetRenderSettings(settings);
      cache.SetPrepareRenderLightFn([&](uint64_t epoch,
          const SectionGeometryRenderInputs &geometry,const SectionRenderLightSnapshot &light) {
        auto prepared=bindings.Prepare(epoch,geometry,light);
        report["section_owner_resource_prepares"].push_back({{"cy",geometry.coord.y},
            {"geometry_matches",light.MatchesGeometry(geometry)},
            {"field_revision",light.FieldRevision()},{"prepared",static_cast<bool>(prepared)}});
        return prepared;
      });
      int callbacks=0, promotions=0;
      mesh.SetOnLitDrawableCommittedFn([&](glm::ivec3) {
        ++callbacks;
        // Same event bookkeeping as the registered emerge callback, before
        // its production promotion owner checks the complete column.
        world.GetColumnRecords().GetOrCreate({0,0}).visual_obligation=
            VisualObligation::LitDrawable;
        if(UChunkEmergeCoordinator::TryPromoteLitColumn(world,{0,0},section_executor))
          ++promotions;
      });
      const auto checkpoint=[&](const char *phase) {
        Json sections=Json::array();
        for(const auto c:std::array<glm::ivec3,2>{coord,glm::ivec3(0,-2,0)}) {
          const auto published=cache.GetMeshPublishRevs(c);
          sections.push_back({{"cy",c.y},{"publications",cache.GetPublishCount(c)},
              {"drawable",mesh.HasDrawableGreedyMesh(c)},
              {"published_draw",cache.HasPublishedSectionDraw(c)},
              {"publication_current",world.HasCurrentChunkSlicePublication(c)},
              {"satisfying",mesh.HasMeshSatisfyingColumnReady(c)},
              {"inputs_current",cache.IsPublishedMeshCurrentForInputs(c,world.BlockWorld)},
              {"no_surface",world.HasCurrentNoSurfaceProof(c)},
              {"dirty",mesh.IsChunkMeshDirty(c)},{"inflight",mesh.HasInflightMeshBuild(c)},
              {"gpu_pending",mesh.IsPendingGpuApply(c)},{"extract",mesh.IsGpuExtractInFlight(c)},
              {"capture",cache.HasPendingCaptureWork(c)},
              {"mesh_revision",mesh.GetChunkMeshRevision(c)},
              {"published_geometry",published.geom_rev},{"published_light",published.light_rev}});
        }
        report["section_owner_completion_steps"].push_back({{"phase",phase},
            {"terrain_complete",world.IsTerrainColumnCompleteFast({0,0,0})},
            {"light_current",world.IsColumnLitReady({0,0,0})},
            {"complete",world.IsColumnRenderReady({0,0,0})},
            {"presentation_publication_debt",world.SampleRequiredPresentationCoverage().publication_unready_sections},
            {"draw_permission",world.GetColumnRenderableState({0,0}).draw_ok},
            {"callbacks",callbacks},{"promotions",promotions},{"sections",sections}});
      };
      mesh.RebuildChunkImmediate(world.BlockWorld,*world.BlockRegistry,coord);
      checkpoint("lower_only");
      require(cache.GetPublishCount(coord)>0 && mesh.HasDrawableGreedyMesh(coord) &&
                  cache.GetPublishedRenderLightBinding(coord) && callbacks==1 && promotions==0 &&
                  world.GetColumnRenderableState({0,0}).draw_ok && !world.IsColumnRenderReady({0,0,0}),
              "actual partial publication may draw but cannot complete column");
      section_executor.SyncColumnJobStageFromWorld(world,{0,0});
      require(section_executor.GetColumnJobStage({0,0})!=ColumnJobStage::RenderReady,
              "executor cannot credit partial draw permission as completion");
      mesh.RebuildChunkImmediate(world.BlockWorld,*world.BlockRegistry,{0,-2,0});
      checkpoint("both_published");
      require(cache.GetPublishCount({0,-2,0})>0 && mesh.HasDrawableGreedyMesh({0,-2,0}) &&
                  callbacks==2 && promotions==1 && world.IsColumnRenderReady({0,0,0}),
              "actual complete publications promote through real cache callback");
      section_executor.SyncColumnJobStageFromWorld(world,{0,0});
      require(section_executor.GetColumnJobStage({0,0})==ColumnJobStage::RenderReady,
              "actual current CPU publications project RenderReady without GPU identity");
      const auto predecessor=cache.GetPublishedRenderLightBinding(coord);
      const auto old_publication=cache.GetPublishCount(coord);
      world.BlockWorld.SetBlock({3,-40,3},8);
      checkpoint("retained_stale");
      require(cache.GetPublishCount(coord)==old_publication &&
                  cache.GetPublishedRenderLightBinding(coord)==predecessor && predecessor->IsLive() &&
                  world.GetColumnRenderableState({0,0}).draw_ok && !world.IsColumnRenderReady({0,0,0}),
              "retained drawable cannot certify current completion after geometry mutation");
      const auto settle_all=[&] {
        auto current=UChunkRelightSnapshot::CaptureSections(world.BlockWorld,all_bands,{coord,{0,-2,0}});
        current.SetInputCatalog(world.BlockRegistry->GetDefinitionsCatalogSnapshot());
        const auto result=current.Compute(*world.BlockRegistry);
        require(result.inputs_complete && result.chunks.size()==2,"completion successor calculation");
        for(const auto &chunk:result.chunks) InstallComputedLight(world.BlockWorld,chunk,true,true);
        require(world.BlockWorld.RecordLightCalculation(result,
                    world.BlockRegistry->GetDefinitionsCatalogSnapshot())==2,
                "completion successor physical certificates");
        world.NoteChunkSliceLightCalculationSettled(coord);
        world.NoteChunkSliceLightCalculationSettled({0,-2,0});
      };
      settle_all();
      checkpoint("light_only_successor");
      require(world.IsColumnLitReady({0,0,0}) && !world.IsColumnRenderReady({0,0,0}),
              "current light alone cannot certify old geometry publication");
      mesh.RebuildChunkImmediate(world.BlockWorld,*world.BlockRegistry,coord);
      mesh.RebuildChunkImmediate(world.BlockWorld,*world.BlockRegistry,{0,-2,0});
      checkpoint("current_successor");
      require(world.IsColumnRenderReady({0,0,0}) && cache.GetPublishCount(coord)>old_publication,
              "current replacement publications restore completion");
      world.BlockWorld.SetBlock({4,-24,4},BLOCK_AIR);
      settle_all();
      checkpoint("no_surface_with_old_drawable");
      require(world.HasCurrentNoSurfaceProof({0,-2,0}) &&
                  mesh.HasDrawableGreedyMesh({0,-2,0}) && !world.IsColumnRenderReady({0,0,0}),
              "no-surface proof cannot retire old drawable publication");
      mesh.RebuildChunkImmediate(world.BlockWorld,*world.BlockRegistry,coord);
      mesh.RebuildChunkImmediate(world.BlockWorld,*world.BlockRegistry,{0,-2,0});
      checkpoint("no_surface_retired");
      require(!mesh.HasDrawableGreedyMesh({0,-2,0}) && world.IsColumnRenderReady({0,0,0}) &&
                  !world.IsColumnRenderReady({7,0,7}),
              "actual no-surface retirement completes column while Unknown stays incomplete");
      const auto upper_cross_count=[&] {
        cache.UpdateVisibleInstances(Frustum{},glm::mat4(1),glm::vec3(8,-24,8));
        size_t count=0;
        for(const auto &batch:cache.GetCrossBatches())
          for(const auto &instance:batch.instances)
            if(UChunkManager::WorldToChunk(glm::ivec3(glm::floor(instance.center)))==glm::ivec3(0,-2,0))
              ++count;
        return count;
      };
      world.BlockWorld.SetBlock({4,-24,4},50);
      settle_all();
      mesh.RebuildChunkImmediate(world.BlockWorld,*world.BlockRegistry,coord);
      mesh.RebuildChunkImmediate(world.BlockWorld,*world.BlockRegistry,{0,-2,0});
      checkpoint("cross_published");
      require(upper_cross_count()==1 && cache.GetPublishedRenderLightBinding({0,-2,0}) &&
                  world.IsColumnRenderReady({0,0,0}),
              "cross retirement fixture owns one actual current published instance");
      world.BlockWorld.SetBlock({4,-24,4},BLOCK_AIR);
      settle_all();
      mesh.RebuildChunkImmediate(world.BlockWorld,*world.BlockRegistry,coord);
      const auto retained_cross=upper_cross_count();
      checkpoint("cross_retained_after_air");
      report["section_owner_cross_checkpoint"]={
          {"retained_cross",retained_cross},
          {"no_surface",world.HasCurrentNoSurfaceProof({0,-2,0})},
          {"greedy_drawable",mesh.HasDrawableGreedyMesh({0,-2,0})},
          {"presentation_publication_debt",world.SampleRequiredPresentationCoverage().publication_unready_sections},
          {"column_complete",world.IsColumnRenderReady({0,0,0})}};
      require(retained_cross==1 && world.HasCurrentNoSurfaceProof({0,-2,0}) &&
                  cache.HasPublishedSectionDraw({0,-2,0}) &&
                  world.SampleRequiredPresentationCoverage().publication_unready_sections==1 &&
                  !world.HasCurrentChunkSlicePublication({0,-2,0}) && !world.IsColumnRenderReady({0,0,0}),
              "no-surface completion cannot ignore retained cross instances");
      mesh.RebuildChunkImmediate(world.BlockWorld,*world.BlockRegistry,{0,-2,0});
      checkpoint("cross_retired");
      require(upper_cross_count()==0 && !cache.HasPublishedSectionDraw({0,-2,0}) &&
                  world.SampleRequiredPresentationCoverage().publication_unready_sections==0 &&
                  world.HasCurrentChunkSlicePublication({0,-2,0}) && world.IsColumnRenderReady({0,0,0}),
              "actual cross retirement restores completion");
      // H7: actual worker -> deferred GPU apply -> packed publication. The
      // registered production drain is withheld, never a synthetic queue flag.
      struct DeadlineRestore {
        UFrameDeadline previous=UFrameDeadline::Get();
        ~DeadlineRestore() { UFrameDeadline::Get()=previous; }
      } restore_deadline;
      UFrameDeadline::Get().BeginFrame(5000);
      const glm::ivec3 gpu_target(0,-2,0);
      world.BlockWorld.SetBlock({4,-24,4},8);
      settle_all();
      mesh.RebuildChunkImmediate(world.BlockWorld,*world.BlockRegistry,coord);
      settings.AsyncMeshing=true; settings.GpuPackedMeshing=true;
      mesh.SetRenderSettings(settings);
      mesh.SetMesherBackend(&gpu_mesher);
      mesh.SetMeshRebuildFocus({0,0,0},4,1);
      mesh.SetMeshSnapshotBudgetMs(1000);
      mesh.SetMeshEmergeTotalBudgetMs(1000);
      auto *gpu_pipeline=cache.GetGpuMeshPipeline();
      report["section_owner_gpu_context"]={{"version",GetActiveRenderBackendCaps().GlVersion},
          {"renderer",GetActiveRenderBackendCaps().GlRenderer},
          {"pipeline_ready",gpu_pipeline && gpu_pipeline->IsReady()}};
      require(gpu_pipeline && gpu_pipeline->IsReady(),"actual GPU mesh pipeline initializes on app context");
      cache.MarkDirtyPriority(gpu_target);
      const auto gpu_revision=cache.GetChunkMeshRevision(gpu_target);
      cache.PrefetchMeshCapture(world.BlockWorld,gpu_target,world.BlockRegistry.get());
      const auto capture_deadline=std::chrono::steady_clock::now()+std::chrono::seconds(5);
      bool captured=false;
      do {
        cache.PumpCaptureWorkerCommits(&world.BlockWorld,world.BlockRegistry.get());
        captured=cache.GetCaptureStore().TryGet(world.BlockWorld,gpu_target,gpu_revision).has_value();
        if(!captured) std::this_thread::yield();
      } while(!captured && std::chrono::steady_clock::now()<capture_deadline);
      checkpoint("gpu_capture_ready");
      require(captured,"actual GPU target capture completes within bounded wait");
      const auto scheduled=cache.RebuildDirtyChunksWithStats(
          world.BlockWorld,*world.BlockRegistry,0,1,false,0,1000,true);
      report["section_owner_gpu_schedule"]={{"scheduled",scheduled.Scheduled},
          {"skip_pipeline",cache.GetLastMeshDirtyScheduleSkipPipelineN()},
          {"skip_snapshot",cache.GetLastMeshDirtyScheduleSkipSnapshotN()},
          {"skip_other",cache.GetLastMeshDirtyScheduleSkipOtherN()}};
      require(scheduled.Scheduled==1 && cache.WaitForAsyncMeshIdleFor(std::chrono::seconds(5)),
              "one actual GPU-eligible mesh worker job completes");
      mesh.DrainAsyncMeshResults(world.BlockWorld,*world.BlockRegistry,1);
      section_executor.SyncColumnJobStageFromWorld(world,{0,0});
      checkpoint("gpu_queued");
      report["section_owner_gpu_pending"]={{"queued",cache.GetPendingGpuQueuedCount()},
          {"pending",cache.IsPendingGpuApply(gpu_target)},
          {"extract",cache.IsGpuExtractInFlight(gpu_target)},
          {"cpu_worker",cache.HasCpuMeshBuildInFlight(gpu_target)},
          {"column_complete",world.IsColumnRenderReady({0,0,0})},
          {"section_current",world.HasCurrentChunkSlicePublication(gpu_target)}};
      report["section_owner_gpu_pending"]["job_stage"]=
          ColumnJobStageName(section_executor.GetColumnJobStage({0,0}));
      require(cache.GetPendingGpuQueuedCount()==1 && cache.IsPendingGpuApply(gpu_target) &&
                  section_executor.GetColumnJobStage({0,0})==ColumnJobStage::GpuPending &&
                  !world.HasCurrentChunkSlicePublication(gpu_target) && !world.IsColumnRenderReady({0,0,0}) &&
                  !UChunkEmergeCoordinator::TryPromoteLitColumn(world,{0,0},section_executor),
              "actual queued GPU publication cannot certify completion");
      for(int pass=0;pass<16 && (cache.GetPendingGpuAppliesCount() ||
                               cache.IsGeometryAwaitingLight(gpu_target));++pass) {
        glFinish();
        bindings.CollectRetired();
        cache.DrainPendingGpuMeshes(world.BlockWorld,*world.BlockRegistry,1,1000);
        glFinish();
        report["section_owner_gpu_owner_steps"].push_back({{"pass",pass},
            {"gpu_pending",cache.GetPendingGpuAppliesCount()},
            {"awaiting_field",cache.IsGeometryAwaitingLight(gpu_target)},
            {"field_settled",world.HasCurrentChunkSliceLightSettlement(gpu_target)},
            {"kick",cache.GetLastGpuKickN()},{"finish",cache.GetLastGpuFinishN()}});
      }
      checkpoint("gpu_published");
      const auto *gpu_slot=gpu_pipeline->GetAllocator().GetSlot(gpu_target);
      report["section_owner_gpu_result"]={{"pending",cache.GetPendingGpuAppliesCount()},
          {"kick",cache.GetLastGpuKickN()},{"finish",cache.GetLastGpuFinishN()},
          {"quads",gpu_slot ? gpu_slot->QuadCount : 0},
          {"residency",cache.QueryLiveGpuResidencyToken(gpu_target)},
          {"column_complete",world.IsColumnRenderReady({0,0,0})}};
      require(cache.GetPendingGpuAppliesCount()==0 && cache.GetLastGpuKickN()>0 && cache.GetLastGpuFinishN()>0 &&
                  gpu_slot && gpu_slot->QuadCount>0 && gpu_pipeline->HasGpuMesh(gpu_target) &&
                  cache.QueryLiveGpuResidencyToken(gpu_target)!=0 && world.IsColumnRenderReady({0,0,0}),
              "actual packed GPU successor publishes current geometry and field");
      section_executor.SyncColumnJobStageFromWorld(world,{0,0});
      require(section_executor.GetColumnJobStage({0,0})==ColumnJobStage::RenderReady,
              "actual GPU publication projects current completion");
      const auto retained_gpu=cache.QueryLiveGpuResidencyToken(gpu_target);
      world.BlockWorld.SetBlock({5,-24,4},8);
      settle_all();
      mesh.RebuildChunkImmediate(world.BlockWorld,*world.BlockRegistry,coord);
      section_executor.SyncColumnJobStageFromWorld(world,{0,0});
      checkpoint("gpu_predecessor_not_current");
      report["section_owner_record_projection"]={
          {"retained_gpu",cache.QueryLiveGpuResidencyToken(gpu_target)},
          {"previous_gpu",retained_gpu},
          {"column_complete",world.IsColumnRenderReady({0,0,0})},
          {"job_stage",ColumnJobStageName(section_executor.GetColumnJobStage({0,0}))},
          {"record_gpu",world.GetColumnRecords().GetOrCreate({0,0}).published.gpu_handle}};
      require(retained_gpu!=0 && cache.QueryLiveGpuResidencyToken(gpu_target)==retained_gpu &&
                  world.GetColumnRecords().GetOrCreate({0,0}).published.gpu_handle==retained_gpu &&
                  !world.IsColumnRenderReady({0,0,0}) &&
                  section_executor.GetColumnJobStage({0,0})==ColumnJobStage::LitReady,
              "real retained GPU resource cannot confirm current replacement in record projection");
      mesh.SetMesherBackend(nullptr);
      mesh.RebuildChunkImmediate(world.BlockWorld,*world.BlockRegistry,gpu_target);
      section_executor.SyncColumnJobStageFromWorld(world,{0,0});
      checkpoint("cpu_successor_after_gpu");
      require(world.IsColumnRenderReady({0,0,0}) && !gpu_pipeline->HasGpuMesh(gpu_target) &&
                  world.GetColumnRecords().GetOrCreate({0,0}).published.gpu_handle==0 &&
                  section_executor.GetColumnJobStage({0,0})==ColumnJobStage::RenderReady,
              "actual CPU successor replaces GPU and remains ready without a GPU handle");
      mesh.SetOnLitDrawableCommittedFn({});
      cache.SetPrepareRenderLightFn({});
      report["cases"].push_back({{"kind","section_owner_column_aggregate"},{"status","PASS"},
          {"stale_geometry_rejected",true},{"replacement_rejected",true},
          {"recompute_recovers",true},{"repeat_idempotent",true},{"band_local_noop",true},
          {"executor_retains_physical_debt",true},{"executor_settle_clears_debt",true},
          {"publication_rejects_uncertified_sibling",true},
          {"publication_current_light_recovers",true},
          {"partial_draw_not_complete",true},{"actual_publication_recovers",true},
          {"retained_geometry_not_complete",true},{"no_surface_requires_retirement",true},
          {"retained_cross_not_complete",true},{"actual_cross_retirement_recovers",true},
          {"actual_gpu_pending_not_complete",true},{"actual_gpu_successor_recovers",true},
          {"record_retained_gpu_not_current",true},{"record_cpu_completion_without_gpu",true}});
    }
    // S3.06: actual factory-selected desktop owner on the app's GL context.
    {
      UWorld world(nullptr, nullptr);
      world.BlockWorld.SetVerticalProfile({-48,47,-17});
      auto definitions = std::make_shared<UBlockDefinitionStorage>();
      BlockDefinition stone, torch;
      stone.Name = "stone"; stone.Physics = BlockPhysicsProfile::Solid();
      torch.Name = "torch"; torch.Render.Style = BlockRenderStyle::Cross;
      torch.Lighting.Emission = 14;
      definitions->ReplaceAll({{8,stone},{50,torch}},{{"stone",8},{"torch",50}});
      world.BlockRegistry = std::make_unique<UBlockRegistry>(nullptr,definitions);
      world.BlockWorld.SetFluidDefinitions(definitions.get());
      world.BlockWorld.SetKnownOpenSkyAbsentQuery([](glm::ivec3){return true;});
      world.BlockWorld.SetBlock({-1,-40,-8},50);
      for(int x=-32;x<16;++x)
        for(int z=-32;z<16;++z) world.BlockWorld.SetBlock({x,32,z},8);
      auto &pipeline = world.GetLightingPipeline();
      RelightJobSpec spec;
      spec.min_world_y=-48; spec.max_world_y=47;
      auto snapshot=UChunkRelightSnapshot::CaptureSections(
          world.BlockWorld,spec,{{-1,-3,-1}});
      snapshot.SetInputCatalog(world.BlockRegistry->GetDefinitionsCatalogSnapshot());
      const auto expected=snapshot.Compute(*world.BlockRegistry);
      require(expected.inputs_complete && expected.chunks.size()==1,
              "desktop GPU owner probe has complete CPU reference");
      const auto before=GpuLightTransportDispatchCount();
      pipeline.RelightChunk(world.BlockWorld,*world.BlockRegistry,{-1,-3,-1},true,true);
      const auto dispatched=GpuLightTransportDispatchCount()-before;
      const auto field=world.BlockWorld.GetChunkManager().GetSectionLightField({-1,-3,-1});
      require(dispatched==15 && field && field->GetPaddedData() &&
                  field->GetPaddedData()->cells==expected.chunks[0].padded_light->cells &&
                  world.BlockWorld.HasCompleteLightSamplingCalculation(
                      {-1,-3,-1},world.BlockRegistry->GetDefinitionsCatalogSnapshot()),
              "app desktop owner must dispatch real GPU and publish exact certified field");
      require(SyncRelightTransport==nullptr,"app desktop owner restores transport scope");
      report["cases"].push_back({{"kind","desktop_gpu_relight_owner"},
          {"status","PASS"},{"actual_dispatches",dispatched},
          {"cpu_padded_bytes_match",true},{"current_certificate",true},
          {"scope_restored",true}});
    }
    // S3.04: real World lifecycle; no live player/world or disk data involved.
    {
      UWorld world(nullptr, nullptr);
      auto definitions = std::make_shared<UBlockDefinitionStorage>();
      world.BlockRegistry = std::make_unique<UBlockRegistry>(nullptr, definitions);
      world.BlockWorld.SetFluidDefinitions(definitions.get());
      auto profile = world.BlockWorld.GetVerticalProfile();
      profile.MinY = -48;
      profile.MaxY = -17;
      world.BlockWorld.SetVerticalProfile(profile);
      world.BlockWorld.SetKnownOpenSkyAbsentQuery([](glm::ivec3) { return true; });
      world.AsyncRelight = std::make_unique<UAsyncRelightBuilder>(1);
      world.NextAsyncRelightJobId = 9200;
      RelightJobSpec spec;
      spec.job_id = 9199;
      spec.block_positions = {{4, -40, 4}};
      spec.min_world_y = -48;
      spec.max_world_y = -33;
      spec.column_center_only = true;
      spec.include_known_air_outputs = true;
      spec.known_air_output_sections = {{1, -2, 0}};
      spec.light_sample_output_sections = {{0, -3, 0}};
      spec.finalize_pending_gate = false;
      spec.visible_draw_gate_repair = true;
      world.AsyncRelight->EnqueueJob(world.BlockWorld, spec, *world.BlockRegistry);
      require(world.AsyncRelight->WaitIdleFor(std::chrono::seconds(5)) &&
                  world.AsyncRelight->GetCompletedSize() == 1,
              "relight lifecycle completed predecessor precondition");
      world.QuiesceBackgroundWork(std::chrono::milliseconds(100));
      require(world.DeferredRelightRetries.Size() == 1 &&
                  world.DeferredRelightRetries.IsSuspended() &&
                  world.DrainAsyncRelightResults(4, false, false) == 0 &&
                  world.AsyncRelight->GetInFlightCount() == 0 &&
                  world.BlockWorld.GetChunkManager().GetSectionLightFieldCount() == 0,
              "quiesce retains exact debt without dispatch/publication");
      world.ResumeAfterSessionSave();
      bool exact = false;
      world.DeferredRelightRetries.Drain(1, [&](const RelightJobSpec &retry) {
        exact = retry.job_id == spec.job_id &&
                retry.block_positions == spec.block_positions &&
                retry.min_world_y == spec.min_world_y &&
                retry.max_world_y == spec.max_world_y &&
                retry.column_center_only == spec.column_center_only &&
                retry.include_known_air_outputs == spec.include_known_air_outputs &&
                retry.known_air_output_sections == spec.known_air_output_sections &&
                retry.light_sample_output_sections == spec.light_sample_output_sections &&
                retry.include_skylight == spec.include_skylight &&
                retry.include_block_light == spec.include_block_light &&
                retry.frontier_iterations == spec.frontier_iterations &&
                retry.finalize_pending_gate == spec.finalize_pending_gate &&
                retry.visible_draw_gate_repair == spec.visible_draw_gate_repair;
        return false; // Observe the actual retained request without consuming it.
      });
      require(exact, "World preserves every retry specification field");
      world.DrainAsyncRelightResults(4, false, false);
      require(world.AsyncRelight->WaitIdleFor(std::chrono::seconds(5)),
              "resumed relight successor worker bound");
      world.DrainAsyncRelightResults(4, false, false);
      require(world.DeferredRelightRetries.Empty() &&
                  world.BlockWorld.GetCurrentLightSamplingVolume({0, -3, 0}) &&
                  world.BlockWorld.GetCurrentLightSamplingVolume({1, -2, 0}),
              "resumed World installs certified AIR and out-of-band target");
      const auto sample = world.BlockWorld.SampleSceneLight({20, -24, 4});
      require(sample.packed && UnpackSky(*sample.packed) == 15,
              "out-of-band AIR receives physical open sky");
      spec.job_id = 9300;
      world.AsyncRelight->EnqueueJob(world.BlockWorld, spec, *world.BlockRegistry);
      require(world.AsyncRelight->WaitIdleFor(std::chrono::seconds(5)),
              "replacement predecessor worker bound");
      world.CancelAsyncRelightWork();
      require(world.DeferredRelightRetries.Size() == 1,
              "replacement fixture contains old-world debt");
      require(world.TryAbandonTerrainForWorldReplace(), "retire idle relight lifecycle world");
      require(world.DeferredRelightRetries.Empty() &&
                  world.BlockWorld.GetChunkManager().GetSectionLightFieldCount() == 0,
              "world replacement retires old debt and fields");
      report["cases"].push_back({{"kind", "relight_cancel_quiesce_resume"},
          {"status", "PASS"}, {"exact_spec_retained", true},
          {"no_dispatch_during_quiesce", true}, {"certified_air_successor", true},
          {"outside_band_target", true}, {"replacement_retires_debt", true}});
    }
    // Fixed S3.04 install matrix: mutate only after the worker has completed.
    for (const std::string mutation : {"read_set", "catalog", "profile",
                                       "configuration", "incarnation"})
    {
      UWorld world(nullptr, nullptr);
      auto definitions = std::make_shared<UBlockDefinitionStorage>();
      BlockDefinition stone;
      stone.Physics = BlockPhysicsProfile::Solid();
      definitions->ReplaceAll({{8, stone}}, {{"stone", 8}});
      world.BlockRegistry = std::make_unique<UBlockRegistry>(nullptr, definitions);
      world.BlockWorld.SetFluidDefinitions(definitions.get());
      auto profile = world.BlockWorld.GetVerticalProfile();
      profile.MinY = -48;
      profile.MaxY = -17;
      world.BlockWorld.SetVerticalProfile(profile);
      world.BlockWorld.SetKnownOpenSkyAbsentQuery([](glm::ivec3) { return true; });
      auto &manager = world.BlockWorld.GetChunkManager();
      manager.EnsureChunk({1, -3, 0});
      const auto old_incarnation = manager.GetChunk({1, -3, 0})->GetIncarnation();
      world.AsyncRelight = std::make_unique<UAsyncRelightBuilder>(1);
      world.NextAsyncRelightJobId = 9400;
      RelightJobSpec spec;
      spec.job_id = 9399;
      spec.block_positions = {{4, -40, 4}};
      spec.min_world_y = -48;
      spec.max_world_y = -33;
      spec.column_center_only = true;
      spec.include_known_air_outputs = true;
      spec.light_sample_output_sections = {{0, -3, 0}};
      spec.finalize_pending_gate = false;
      auto sync_snapshot = UChunkRelightSnapshot::Capture(world.BlockWorld, spec);
      sync_snapshot.SetInputCatalog(world.BlockRegistry->GetDefinitionsCatalogSnapshot());
      const auto sync_result = sync_snapshot.Compute(*world.BlockRegistry);
      require(ValidateRelightInputs(sync_result, world.BlockWorld,
                  world.BlockRegistry->GetDefinitionsCatalogSnapshot(), true) ==
                  RelightInputValidation::Current,
              "stale matrix requires current complete original inputs");
      world.AsyncRelight->EnqueueJob(world.BlockWorld, spec, *world.BlockRegistry);
      require(world.AsyncRelight->WaitIdleFor(std::chrono::seconds(5)) &&
                  world.AsyncRelight->GetCompletedSize() == 1,
              "stale matrix completed worker precondition");
      auto expected = RelightInputValidation::ReadSet;
      if (mutation == "read_set")
        world.BlockWorld.SetBlock({16, -40, 4}, 8);
      else if (mutation == "catalog")
      {
        stone.Lighting.Emission = 7;
        definitions->ReplaceAll({{8, stone}}, {{"stone", 8}});
        expected = RelightInputValidation::Catalog;
      }
      else if (mutation == "profile")
      {
        profile.MaxY = -1;
        world.BlockWorld.SetVerticalProfile(profile);
        expected = RelightInputValidation::Profile;
      }
      else if (mutation == "configuration")
      {
        world.BlockWorld.SetLightingConfigurationIdentity(99);
        expected = RelightInputValidation::Configuration;
      }
      else
      {
        manager.RemoveChunk({1, -3, 0});
        manager.EnsureChunk({1, -3, 0});
        require(manager.GetChunk({1, -3, 0})->GetIncarnation() != old_incarnation,
                "incarnation control must replace equal AIR geometry");
      }
      require(ValidateRelightInputs(sync_result, world.BlockWorld,
                  world.BlockRegistry->GetDefinitionsCatalogSnapshot(), true) == expected,
              "immutable snapshot must reject the exact changed input category");
      const int old_applied = world.DrainAsyncRelightResults(1, false, false);
      require(old_applied == 0 && world.NextAsyncRelightJobId == 9401 &&
                  !manager.GetSectionLightField({0, -3, 0}),
              "World rejects stale completion before publishing and enqueues successor");
      require(world.AsyncRelight->WaitIdleFor(std::chrono::seconds(5)),
              "stale successor worker bound");
      require(world.DrainAsyncRelightResults(1, false, false) == 1 &&
                  world.BlockWorld.GetCurrentLightSamplingVolume({0, -3, 0}) &&
                  !world.HasPendingAsyncRelightWork(),
              "World stale successor installs current certified AIR field");
      report["cases"].push_back({{"kind", "relight_stale_" + mutation},
          {"status", "PASS"}, {"validation_reason", static_cast<int>(expected)},
          {"stale_applied", old_applied}, {"successor_job", 9401},
          {"certified_air_successor", true}});
    }
    // S3.05: upper async persistence owner, with actual disk worker results.
    for (const std::string cache_case : {"compatible", "materials", "legacy"})
    {
      const auto folder = root / ("persisted-load-" + cache_case);
      fs::create_directories(folder);
      UWorld world(nullptr, nullptr);
      world.ProceduralTemplate.MinHeight = -48;
      world.ProceduralTemplate.MaxHeight = -17;
      world.ProceduralTemplate.SeaLevel = -33;
      auto profile = world.BlockWorld.GetVerticalProfile();
      profile.MinY = -48;
      profile.MaxY = -17;
      profile.SeaLevelY = -33;
      world.BlockWorld.SetVerticalProfile(profile);
      const auto lighting_identity =
          static_cast<uint64_t>(world.GetLightingPipeline().GetMode());
      require(world.RequiresLightingLitGate() &&
                  world.BlockWorld.GetLightingConfigurationIdentity() == lighting_identity,
              "upper persistence fixture binds the actual Full lighting domain");
      auto definitions = std::make_shared<UBlockDefinitionStorage>();
      BlockDefinition stone, torch, water;
      stone.Name = "stone";
      stone.Physics = BlockPhysicsProfile::Solid();
      torch.Name = "torch";
      torch.Render.Style = BlockRenderStyle::Cross;
      torch.Lighting.Emission = 14;
      water.Name = "water";
      water.Physics.IsLiquid = true;
      water.Physics.FluidKindPreset = FluidKind::Water;
      water.Render.Style = BlockRenderStyle::Fluid;
      const auto bind = [&] {
        definitions->ReplaceAll({{8, stone}, {50, torch}, {9, water}},
            {{"stone", 8}, {"torch", 50}, {"water", 9}});
      };
      bind();
      world.BlockRegistry = std::make_unique<UBlockRegistry>(nullptr, definitions);
      world.BlockWorld.SetFluidDefinitions(definitions.get());
      world.SetWorldFolderPath(folder.string());
      world.Streaming->EnsureStreamer(world.BlockWorld, *world.BlockRegistry,
                                      42, world.ProceduralTemplate);
      require(world.Streaming->HasStreamer() &&
                  std::max(world.ProceduralTemplate.MinHeight,
                      world.ProceduralTemplate.SeaLevel - CHUNK_SIZE * 2) <=
                      world.ProceduralTemplate.MaxHeight,
              "upper persistence requires streamer and valid fallback band");
      auto &storage = world.GetChunkStorage();
      UBlockWorld original;
      original.SetVerticalProfile(profile);
      original.SetLightingConfigurationIdentity(lighting_identity);
      original.SetFluidDefinitions(definitions.get());
      original.SetKnownOpenSkyAbsentQuery([](glm::ivec3) { return true; });
      // A procedural column without decoded AIR slices is complete only when
      // every XZ cell has terrain. Keep a real floor across both columns.
      for (int x = 0; x < 32; ++x)
        for (int z = 0; z < 16; ++z)
          original.SetBlock({x, -48, z}, 8);
      original.SetBlock({16, -24, 4}, 50);
      original.SetBlock({2, -47, 2}, 9);
      const auto fluid = FluidCellState::Flowing(3, true).WithKind(FluidKind::Water);
      original.SetFluidState({2, -47, 2}, fluid);
      RelightJobSpec spec;
      spec.min_world_y = -48;
      spec.max_world_y = -17;
      spec.include_known_air_outputs = true;
      const auto computed = UChunkRelightSnapshot::CaptureSections(
          original, spec, {{0, -2, 0}}).Compute(*world.BlockRegistry);
      require(IsTerrainChunkComplete(original, {0, 0, 0}, -17,
                  kNoTerrainChunkSlice, -48) &&
                  IsTerrainChunkComplete(original, {1, 0, 0}, -17,
                  kNoTerrainChunkSlice, -48),
              "upper persistence source columns must satisfy terrain completeness");
      require(computed.inputs_complete && computed.chunks.size() == 1,
              "upper persistence fixture requires complete actual calculation");
      original.GetChunkManager().ReplaceComputedSectionLightVolume(
          {0, -2, 0}, *computed.chunks[0].padded_light, true, true);
      require(original.RecordLightCalculation(computed,
                  world.BlockRegistry->GetDefinitionsCatalogSnapshot()) == 1 &&
                  storage.SaveTerrainColumn({0, 0, 0}, original, folder.string(),
                      *world.BlockRegistry, -17, -48) &&
                  storage.SaveTerrainColumn({1, 0, 0}, original, folder.string(),
                      *world.BlockRegistry, -17, -48),
              "upper persistence fixture saves terrain and certified AIR cache");
      if (cache_case == "legacy")
        fs::remove(UChunkStorageService::ColumnLightFilePath(folder.string(), {0, 0, 0}));
      if (cache_case == "materials")
      {
        torch.Lighting.Emission = 6;
        bind();
      }
      world.BlockWorld.SetKnownOpenSkyAbsentQuery([&](glm::ivec3 coord) {
        return storage.IsChunkSliceAbsentOnDisk(folder.string(), coord);
      });
      world.Persistence->RequestAsyncTerrainColumnLoad(world, {1, 0, 0}, 9501);
      world.Persistence->RequestAsyncTerrainColumnLoad(world, {0, 0, 0}, 9502);
      const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(10);
      while (!world.Persistence->TickDrainAsyncChunkIo(world, 4))
      {
        require(std::chrono::steady_clock::now() < deadline,
                "upper persistence load worker/drain deadline");
        std::this_thread::yield();
      }
      const auto catalog = world.BlockRegistry->GetDefinitionsCatalogSnapshot();
      require(world.BlockWorld.GetBlock({16, -24, 4}) == 50 &&
                  world.BlockWorld.GetBlock({0, -48, 0}) == 8 &&
                  world.BlockWorld.GetBlock({2, -47, 2}) == 9 &&
                  PackFluidCellState(world.BlockWorld.GetFluidState({2, -47, 2})) == PackFluidCellState(fluid),
              "upper persistence preserves terrain and packed fluid state");
      if (cache_case == "compatible")
        require(world.BlockWorld.HasCompleteLightSamplingCalculation({0, -2, 0}, catalog) &&
                    world.BlockWorld.GetChunkManager().GetSectionLightField({0, -2, 0})->GetPaddedData()->cells ==
                        computed.chunks[0].padded_light->cells,
                "upper persistence restores compatible AIR cache exactly");
      else
      {
        report["persisted_fallback_checkpoint"] = {
            {"case", cache_case},
            {"column_state", static_cast<int>(world.GetColumnEmergeState({0, 0, 0}))},
            {"coarse_lit_ready", world.IsColumnLitReady({0, 0, 0})},
            {"physical_coverage", world.HasCurrentColumnLightCoverage({0, 0}, -48, -17)},
            {"queued", world.IsTerrainColumnRelightQueued({0, 0})},
            {"noop_skipped", world.GetPhysicsTelemetry().RelightSkippedNoOpEnqueueN}};
        require(!world.BlockWorld.HasCompleteLightSamplingCalculation({0, -2, 0}, catalog) &&
                    world.IsTerrainColumnRelightQueued({0, 0}),
                "upper persistence incompatible/legacy cache retains ordinary relight obligation");
        world.AsyncRelight = std::make_unique<UAsyncRelightBuilder>(1);
        spec.block_positions = {{4, -24, 4}};
        spec.light_sample_output_sections = {{0, -2, 0}};
        spec.column_center_only = true;
        spec.finalize_pending_gate = false;
        world.AsyncRelight->EnqueueJob(world.BlockWorld, spec, *world.BlockRegistry);
        require(world.AsyncRelight->WaitIdleFor(std::chrono::seconds(5)) &&
                    world.DrainAsyncRelightResults(1, false, false) == 1 &&
                    world.BlockWorld.HasCompleteLightSamplingCalculation({0, -2, 0}, catalog),
                "upper persistence cache fallback installs physical certified successor");
      }
      const auto field = world.BlockWorld.GetChunkManager().GetSectionLightField({0, -2, 0});
      require(field && field->GetPaddedData() &&
                  UnpackBlock(field->GetPaddedData()->cells[SectionLightVolume::Index(15, 8, 4)]) ==
                      (cache_case == "materials" ? 5 : 13),
              "upper persistence field reflects actual current emission");
      report["cases"].push_back({{"kind", "persisted_world_load_" + cache_case},
          {"status", "PASS"}, {"real_async_load_owner", true},
          {"terrain_fluid_preserved", true}, {"certified_field", true}});
    }
    // Test receipt delivery, not the shore-sealing algorithm or GPU scan.
    // A dry cached slice is the correct initial value even when the normal
    // CPU hitch gate returns empty; no backend result is fabricated here.
    const bool original_gpu_scan = PreferGpuFluidColumnScan();
    bool seal_maps_invalidated = true;
    for (bool near_column : {false, true})
    {
      struct RestoreScanPreference
      {
        bool Previous{PreferGpuFluidColumnScan()};
        ~RestoreScanPreference() { SetPreferGpuFluidColumnScan(Previous); }
      } restore_scan_preference;
      SetPreferGpuFluidColumnScan(false);
      UWorld world(nullptr, nullptr);
      world.ProceduralTemplate.MinHeight = -48;
      world.ProceduralTemplate.MaxHeight = -17;
      auto profile = world.BlockWorld.GetVerticalProfile();
      profile.MinY = -48;
      profile.MaxY = -17;
      world.BlockWorld.SetVerticalProfile(profile);
      auto definitions = std::make_shared<UBlockDefinitionStorage>();
      BlockDefinition stone, water;
      stone.Physics = BlockPhysicsProfile::Solid();
      water.Physics.IsLiquid = true;
      water.Physics.FluidKindPreset = FluidKind::Water;
      definitions->ReplaceAll({{8, stone}, {9, water}}, {{"stone", 8}, {"water", 9}});
      world.BlockRegistry = std::make_unique<UBlockRegistry>(nullptr, definitions);
      world.BlockWorld.SetFluidDefinitions(definitions.get());
      auto &cache = world.MeshService->GetCache();
      const glm::ivec3 origin(100 * CHUNK_SIZE, 0, 100 * CHUNK_SIZE);
      const std::vector<glm::ivec3> cells{
          origin + glm::ivec3(15, -33, 15),
          origin + glm::ivec3(16, -32, 15),
          origin + glm::ivec3(15, -33, 16)};
      const std::vector<glm::ivec3> columns{{100, 0, 100}, {101, 0, 100},
                                             {100, 0, 101}, {101, 0, 101}};
      for (const auto ground : columns)
      {
        world.BlockWorld.SetBlock({ground.x * CHUNK_SIZE, -48, ground.z * CHUNK_SIZE}, 8);
        const auto *slice = cache.GetFluidSurfaceSlice(world.BlockWorld,
            *world.BlockRegistry, ground, -33);
        require(slice && !slice->HasSurface(15, 15), "seal fixture initial dry cached slice");
        require(cache.GetFluidSurfaceSlice(world.BlockWorld, *world.BlockRegistry,
                    ground, -33) == slice, "seal fixture warm cache hit");
        require(!cache.GetFluidSurfaceDirtyGroundChunks().count(ground),
                "seal fixture must start with settled cache");
      }
      world.ModifiedChunks.clear();
      std::vector<glm::ivec3> changed;
      for (const auto cell : cells)
      {
        world.BlockWorld.SetBlock(cell, 9);
        world.BlockWorld.SetFluidState(cell, FluidCellState::Source().WithKind(FluidKind::Water));
        changed.push_back(UChunkManager::WorldToChunk(cell));
      }
      changed.push_back(changed.front()); // real producer receipts may coalesce
      world.Streaming->PublishFluidSealMutations(world, changed, near_column);
      bool invalidated = true, saved = world.ModifiedChunks.size() == cells.size();
      bool no_mesh = true;
      for (const auto cell : cells)
      {
        const auto coord = UChunkManager::WorldToChunk(cell);
        invalidated &= cache.GetFluidSurfaceDirtyGroundChunks().count({coord.x, 0, coord.z}) == 1;
        saved &= world.ModifiedChunks.count(coord) == 1;
        no_mesh &= !cache.HasGreedyMesh(coord);
      }
      const bool control_untouched = !cache.GetFluidSurfaceDirtyGroundChunks().count(columns.back());
      const bool light_obligation = world.GetPendingLightBeforeMeshCount() >= cells.size();
      const bool passed = invalidated && saved && no_mesh && control_untouched && light_obligation;
      report["cases"].push_back({
          {"kind", near_column ? "seal_map_near" : "seal_map_far"},
          {"status", passed ? "PASS" : "FAIL"}, {"cached_dry_columns", columns.size()},
          {"dirty_map_before_remesh", invalidated}, {"actual_owners_saved", saved},
          {"control_untouched", control_untouched}, {"no_mesh_published", no_mesh},
          {"light_obligation_retained", light_obligation},
          {"scope", "Actual shore-seal receipt owner, cached dry map invalidation before visual work; not seal algorithm, surface recompute, or GPU/framebuffer parity"}});
      seal_maps_invalidated &= passed;
    }
    report["fluid_scan_preference_restored"] = PreferGpuFluidColumnScan() == original_gpu_scan;
    require(report["fluid_scan_preference_restored"].get<bool>(), "seal probe changed live scan preference");
    require(seal_maps_invalidated, "shore-seal receipt did not invalidate cached fluid map");
    // Physics owns persistence independently of the best-effort visual queue.
    // These are raw API fixtures, not persisted procedural-profile fixtures.
    bool fluid_dirty_retained = true;
    for (bool eviction : {false, true})
    {
      const auto folder = root / (eviction ? "fluid-visual-eviction" : "fluid-visual-rejection");
      fs::create_directories(folder);
      UWorld world(nullptr, nullptr);
      world.ProceduralTemplate.MinHeight = -48;
      world.ProceduralTemplate.MaxHeight = -33;
      auto profile = world.BlockWorld.GetVerticalProfile();
      profile.MinY = -48;
      profile.MaxY = -33;
      world.BlockWorld.SetVerticalProfile(profile);
      auto definitions = std::make_shared<UBlockDefinitionStorage>();
      BlockDefinition stone, water;
      stone.Physics = BlockPhysicsProfile::Solid();
      water.Physics.IsLiquid = true;
      water.Physics.FluidKindPreset = FluidKind::Water;
      definitions->ReplaceAll({{8, stone}, {9, water}}, {{"stone", 8}, {"water", 9}});
      world.BlockRegistry = std::make_unique<UBlockRegistry>(nullptr, definitions);
      world.BlockWorld.SetFluidDefinitions(definitions.get());
      auto service = std::make_unique<UWorldChunkDirtyService>();
      auto *dirty_service = service.get();
      PhysicsBudgets budgets;
      budgets.VisualRemeshPerTickMax = 0;
      budgets.VisualRemeshQueueSoftLimit = eviction ? 1 : 0;
      budgets.VisualRemeshQueueHardLimit = eviction ? 1 : 0;
      dirty_service->SetBudgets(budgets);
      world.ChunkDirtyService = std::move(service);
      const int columns = eviction ? 2 : 1;
      for (int cx = 0; cx < columns; ++cx)
        for (int x = 0; x < CHUNK_SIZE; ++x)
          for (int z = 0; z < CHUNK_SIZE; ++z)
            world.BlockWorld.SetBlock({cx * CHUNK_SIZE + x, -48, z}, 8);
      world.ModifiedChunks.clear();
      bool before_drain = true;
      for (int cx = 0; cx < columns; ++cx)
      {
        const glm::ivec3 cell(cx * CHUNK_SIZE + 8, -40, 8);
        world.BlockWorld.SetBlock(cell, 9);
        world.BlockWorld.SetFluidState(cell, FluidCellState::Flowing(3).WithKind(FluidKind::Water));
        world.MarkFluidChangeDirty(cell);
        before_drain &= world.ModifiedChunks.count({cx, -3, 0}) == 1;
      }
      dirty_service->DrainRebuildQueues(world);
      bool after_drain = true;
      bool cold_matches = true;
      for (int cx = 0; cx < columns; ++cx)
      {
        after_drain &= world.ModifiedChunks.count({cx, -3, 0}) == 1;
        require(world.GetChunkStorage().SaveTerrainColumn(
                    {cx, 0, 0}, world.BlockWorld, folder.string(),
                    *world.BlockRegistry, -33, -48), "fluid fixture cold save");
        UBlockWorld cold;
        cold.SetVerticalProfile(profile);
        cold.SetFluidDefinitions(definitions.get());
        require(world.GetChunkStorage().LoadTerrainColumn(
                    {cx, 0, 0}, cold, folder.string(), *world.BlockRegistry,
                    -33, -48) > 0, "fluid fixture cold load");
        const glm::ivec3 cell(cx * CHUNK_SIZE + 8, -40, 8);
        cold_matches &= cold.GetBlock(cell) == 9 &&
            PackFluidCellState(cold.GetFluidState(cell)) ==
                PackFluidCellState(world.BlockWorld.GetFluidState(cell));
      }
      const bool capacity_exercised = eviction
          ? world.PhysicsTelemetryData.PurgedUpdates > 0
          : world.PhysicsTelemetryData.DroppedUpdates > 0;
      const bool passed = before_drain && after_drain && cold_matches && capacity_exercised;
      report["cases"].push_back({
          {"kind", eviction ? "fluid_visual_eviction" : "fluid_visual_rejection"},
          {"status", passed ? "PASS" : "FAIL"},
          {"dirty_before_drain", before_drain}, {"dirty_after_drain", after_drain},
          {"capacity_exercised", capacity_exercised},
          {"visual_backlog", world.PhysicsTelemetryData.VisualRemeshBacklog},
          {"cold_block_and_fluid_state", cold_matches},
          {"scope", "Physics mutation ownership and direct cold storage; not autosave scheduling or save acknowledgement"}});
      fluid_dirty_retained &= passed;
    }
    require(fluid_dirty_retained, "physics fluid dirty-save lost before visual drain");
    bool fluid_light_delivered = true;
    const bool original_drying_policy = URuntimeTuning::Get().WaterFlowingDries;
    for (int light_case = 0; light_case < 3; ++light_case)
    {
      const bool removal = light_case == 1;
      const bool player_placement = light_case == 2;
      struct RestoreDryingPolicy
      {
        bool Previous{URuntimeTuning::Get().WaterFlowingDries};
        ~RestoreDryingPolicy() { URuntimeTuning::Get().WaterFlowingDries = Previous; }
      } restore_drying_policy;
      // Stable puddles are the default game policy. Exercise the existing
      // opt-in decay path only in this isolated removal fixture.
      if (removal) URuntimeTuning::Get().WaterFlowingDries = true;
      UWorld world(nullptr, nullptr);
      world.ProceduralTemplate.MinHeight = -48;
      world.ProceduralTemplate.MaxHeight = -17;
      world.ProceduralTemplate.FillWater = false;
      auto profile = world.BlockWorld.GetVerticalProfile();
      profile.MinY = -48;
      profile.MaxY = -17;
      world.BlockWorld.SetVerticalProfile(profile);
      auto definitions = std::make_shared<UBlockDefinitionStorage>();
      BlockDefinition stone, water;
      stone.Physics = BlockPhysicsProfile::Solid();
      water.Physics.IsLiquid = true;
      water.Physics.FluidKindPreset = FluidKind::Water;
      water.Physics.FluidSpreadPeriodTicks = 1;
      definitions->ReplaceAll({{8, stone}, {9, water}}, {{"stone", 8}, {"water", 9}});
      world.BlockRegistry = std::make_unique<UBlockRegistry>(nullptr, definitions);
      world.BlockWorld.SetFluidDefinitions(definitions.get());
      // Fully resident narrow shaft, closed sides: no unknown terrain or
      // alternate sky path. The falling input crosses Y=-32/-33.
      for (int x = 0; x < 32; ++x)
        for (int z = 0; z < 32; ++z)
          for (int y = -48; y <= -17; ++y)
            world.BlockWorld.SetBlock({x, y, z}, x == (removal ? 14 : 15) && z == 15 && y > -48 ? BLOCK_AIR : 8);
      const glm::ivec3 cell(15, -33, 15);
      if (removal)
      {
        world.BlockWorld.SetBlock(cell, 9);
        world.BlockWorld.SetFluidState(cell, FluidCellState::Flowing(3).WithKind(FluidKind::Water));
        // Side alcove with solid floor/roof: no downward spread or incoming
        // fluid. Sky enters horizontally from the adjacent open shaft.
        world.BlockWorld.SetBlock(cell + glm::ivec3(1, 0, 0), BLOCK_AIR);
      }
      else if (!player_placement)
        world.BlockWorld.SetBlock(cell + glm::ivec3(0, 1, 0), 9);
      auto &lighting = world.GetLightingPipeline();
      lighting.RelightAllLoadedChunks(world.BlockWorld, *world.BlockRegistry);
      const auto sky = [&]()
      {
        const auto sample = cell + (removal ? glm::ivec3(1, 0, 0) : glm::ivec3(0, -2, 0));
        return world.BlockWorld.GetChunkManager().GetChunk(
            UChunkManager::WorldToChunk(sample))->GetSkyLightLocal(
                UChunkManager::WorldToLocal(sample));
      };
      const int before = sky();
      PhysicsFeatureFlags flags;
      flags.EnableFluids = true;
      flags.LiquidShadowMode = false;
      world.SetPhysicsProfile(PhysicsProfile::Standard);
      world.SetPhysicsFeatureFlags(flags);
      PhysicsBudgets budgets;
      budgets.BlockEventsPerTickMax = 0;
      budgets.LiquidEventsPerTickMax = 1;
      budgets.VisualRemeshPerTickMax = 0;
      world.SetPhysicsBudgets(budgets);
      world.ModifiedChunks.clear();
      bool accepted = true;
      if (player_placement)
      {
        const auto place_position = BlockCenter(cell);
        require(WorldPosToBlock(place_position) == cell, "player fluid fixture coordinate conversion");
        require(world.BlockRegistry->GetIdByTypeName("water") == 9, "player fluid fixture name resolution");
        require(UFluidSpreadSystem::CanReceiveFluid(world.BlockWorld, *world.BlockRegistry, cell),
                "player fluid fixture must accept water");
        accepted = world.AddObject("water", place_position);
      }
      else
      {
        world.BlockPhysicsService->PublishFluid(cell);
        world.BlockPhysicsService->TickBlockPhysics(world);
      }
      const bool mutated = world.BlockWorld.GetBlock(cell) == (removal ? BLOCK_AIR : 9);
      const int delivered = sky();
      // Delivery oracle only: same solver explicitly invoked after recording
      // the tick's result. Independent solver correctness belongs to S3.
      lighting.RelightAllLoadedChunks(world.BlockWorld, *world.BlockRegistry);
      const int reference = sky();
      const bool changed_reference = removal ? reference > before : reference < before;
      const bool dirty_owner = world.ModifiedChunks.count(UChunkManager::WorldToChunk(cell)) == 1;
      const bool source_state = !player_placement ||
          (world.BlockWorld.GetFluidState(cell).IsSource() &&
           world.BlockWorld.GetFluidState(cell).GetKind() == FluidKind::Water);
      const bool passed = accepted && mutated && dirty_owner && source_state &&
          changed_reference && delivered == reference;
      report["cases"].push_back({
          {"kind", player_placement ? "fluid_light_player_placement" :
                   removal ? "fluid_light_removal_alcove" : "fluid_light_addition"},
          {"status", passed ? "PASS" : "FAIL"}, {"mutated", mutated},
          {"accepted", accepted}, {"dirty_owner", dirty_owner},
          {"source_state", source_state}, {"physics_tick_called", !player_placement},
          {"water_flowing_dries", URuntimeTuning::Get().WaterFlowingDries},
          {"sky_before", before}, {"sky_delivered", delivered},
          {"sky_explicit_recompute", reference},
          {"scope", player_placement
               ? "Actual player AddObject light delivery before any physics tick; not independent solver or framebuffer correctness"
               : "Actual nonemissive fluid tick delivery before maintenance; not independent solver or framebuffer correctness"}});
      fluid_light_delivered &= passed;
    }
    report["fluid_drying_policy_restored"] =
        URuntimeTuning::Get().WaterFlowingDries == original_drying_policy;
    require(report["fluid_drying_policy_restored"].get<bool>(), "fluid probe changed live drying policy");
    require(fluid_light_delivered, "nonemissive fluid mutation did not deliver skylight update");
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
      // Redirect only the main-thread facade call to this owned fixture.
      // Restore the live Core even if an assertion or save throws; never
      // construct another Core whose destructor clears global render caches.
      const auto with_fixture_core = [&](auto action)
      {
        const auto previous_world = core.WorldInstance;
        const auto previous_folder = core.ActiveWorldFolder;
        const auto previous_merge = core.BlockMergeRegistryInstance;
        const auto restore = [&]()
        {
          core.WorldInstance = previous_world;
          core.ActiveWorldFolder = previous_folder;
          core.BlockMergeRegistryInstance = previous_merge;
        };
        core.WorldInstance = std::shared_ptr<UWorld>(&world, [](UWorld *) {});
        core.ActiveWorldFolder = folder;
        core.BlockMergeRegistryInstance.reset();
        try
        {
          action();
        }
        catch (...)
        {
          restore();
          throw;
        }
        restore();
      };
      require(world.IsStreamingEnabled() &&
                  UWorld::HasPersistedTerrainOnDisk(folder.string()),
              "facade fixture selects streaming persisted save path");
      bool facade_failed = false;
      with_fixture_core(
          [&]()
          {
            try
            {
              core.SaveWorld("isolated-save-probe");
            }
            catch (const std::exception &error)
            {
              facade_failed = std::string(error.what())
                                  .find(io_failure ? "Terrain save failed"
                                                   : "Terrain is incomplete") !=
                              std::string::npos;
            }
          });
      require(facade_failed, "full Core SaveWorld must reject unsaved terrain "
                             "instead of metadata-only success");
      require(capture(folder) == before && world.ModifiedChunks == dirty,
              "failed full Core SaveWorld preserves files and dirty edits");
      Json runner_failures = Json::array();
      for (auto op : {WorldRunnerOp::SaveThenLoad,
                      WorldRunnerOp::SaveThenCreate, WorldRunnerOp::Shutdown})
      {
        Progress runner_progress;
        with_fixture_core(
            [&]()
            {
              UWorldOperationRunner runner(core, world);
              WorldRunnerRequest request;
              request.op = op;
              request.worldName = "must-not-replace-current-world";
              request.saveConfigAfter = false;
              request.shutdownSaveSession = true;
              request.shutdownCloseApplication = true;
              runner.Start(request);
              const auto runner_by =
                  std::chrono::steady_clock::now() + std::chrono::seconds(12);
              while (!runner.Tick(runner_progress, 1))
              {
                require(std::chrono::steady_clock::now() < runner_by,
                        "failed save runner bound");
                require(
                    std::find(runner_progress.phases.begin(),
                              runner_progress.phases.end(),
                              "teardown") == runner_progress.phases.end(),
                    "save failure must stop runner before replacing terrain");
                std::this_thread::yield();
              }
              require(!runner.Succeeded() && runner.HasSaveFailure() &&
                          !runner.ShouldCloseApplication() &&
                          !runner.ErrorMessage().empty() &&
                          core.GetActiveWorldFolder() == folder &&
                          capture(folder) == before &&
                          world.ModifiedChunks == dirty &&
                          world.BlockWorld.GetBlock(deleted) == BLOCK_AIR &&
                          world.BlockWorld.GetBlock(added) == 8 &&
                          world.BlockWorld.GetChunkManager()
                                  .GetChunk(floor)
                                  ->GetIncarnation() == incarnation,
                      "failed save runner preserves current world and denies "
                      "close");
            });
        runner_failures.push_back(
            {{"operation", op == WorldRunnerOp::Shutdown ? "shutdown"
                           : op == WorldRunnerOp::SaveThenLoad
                               ? "save_then_load"
                               : "save_then_create"},
             {"phases", runner_progress.phases},
             {"status", "PASS"}});
        world.ResumeAfterSessionSave();
      }
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
      const auto facade_cell = deleted + glm::ivec3(4, 0, 0);
      world.BlockWorld.SetBlock(facade_cell, 8);
      world.ModifiedChunks.insert(floor);
      with_fixture_core([&]() { core.SaveWorld("isolated-save-probe"); });
      UBlockWorld facade_cold;
      require(
          world.ModifiedChunks.empty() &&
              storage.LoadTerrainColumn(column, facade_cold, folder.string(),
                                        *world.BlockRegistry, -17, -64) > 0 &&
              facade_cold.GetBlock(facade_cell) == 8,
          "full Core SaveWorld persists edits in an existing streaming world");
      const auto runner_cell = deleted + glm::ivec3(5, 0, 0);
      world.BlockWorld.SetBlock(runner_cell, 8);
      world.ModifiedChunks.insert(floor);
      Progress runner_retry;
      with_fixture_core(
          [&]()
          {
            UWorldOperationRunner runner(core, world);
            WorldRunnerRequest request;
            request.op = WorldRunnerOp::Save;
            request.saveConfigAfter = false;
            runner.Start(request);
            const auto runner_by =
                std::chrono::steady_clock::now() + std::chrono::seconds(12);
            while (!runner.Tick(runner_retry, 1))
            {
              require(std::chrono::steady_clock::now() < runner_by,
                      "successful save runner bound");
              std::this_thread::yield();
            }
            require(runner.Succeeded() && !runner.HasSaveFailure() &&
                        !runner.ShouldCloseApplication(),
                    "runner retry reports successful save");
          });
      UBlockWorld runner_cold;
      require(
          world.ModifiedChunks.empty() &&
              storage.LoadTerrainColumn(column, runner_cold, folder.string(),
                                        *world.BlockRegistry, -17, -64) > 0 &&
              runner_cold.GetBlock(runner_cell) == 8,
          "runner retry persists resident edit");
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
      report["cases"].push_back(
          {{"kind", io_failure ? "write_failure" : "incomplete"},
           {"status", "PASS"},
           {"failure", error},
           {"failure_phases", failed.phases},
           {"recovery_phases", recovered.phases},
           {"files_preserved", before.size()},
           {"dirty_preserved", dirty.size()},
           {"cold_edit_preserved", true},
           {"unload_waited_for_ack", true},
           {"newer_edit_invalidated_ack", true},
           {"async_failure_retained", io_failure},
           {"async_cold_edit_preserved", true},
           {"folder_switch_checked", io_failure},
           {"cooperative_write_fence_checked", io_failure},
           {"full_core_save_failure_and_retry_checked", true},
           {"runner_failure_gates", runner_failures},
           {"runner_retry_checked", true},
           {"metadata_only_preserved_dirty", true},
           {"snapshot_fence_checked", io_failure},
           {"io_timeout_and_retry_checked", io_failure},
           {"write_fence_evidence", fence_evidence},
           {"late_edit_and_column_checked", io_failure}});
    }
    report["status"] = "PASS";
  }
  catch (const std::exception &error) { report["error"] = error.what(); }
  std::ofstream(root / "report.json") << report.dump(2) << '\n';
}
} // namespace cutum
