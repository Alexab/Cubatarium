#include "Test/AsyncIoStorageFixture.h"
#include "Blocks/BlockDefinitionStorage.h"
#include "Blocks/BlockRegistry.h"
#include "World/Core/BlockWorld.h"
#include "World/IO/AsyncChunkIO.h"
#include "World/IO/ChunkStorageService.h"
#include <nlohmann/json.hpp>
#include <stdexcept>

namespace cutum
{
struct AsyncIoStorageProbe
{
  struct Gate
  {
    std::atomic<size_t> entered{0};
    std::atomic<bool> released{false};
    void Run()
    {
      ++entered;
      const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(10);
      while (!released.load() && std::chrono::steady_clock::now() < deadline)
        std::this_thread::sleep_for(std::chrono::milliseconds(1));
    }
  };

  static void Require(bool condition, const char *message)
  {
    if (!condition)
      throw std::runtime_error(message);
  }

  static size_t Park(UAsyncChunkIO &io, Gate &gate, bool flags)
  {
    size_t count = 0;
    const auto park = [&](UJobThreadPool &pool) {
      for (size_t i = 0; i < pool.GetWorkerCount(); ++i)
        pool.Enqueue([&gate] { gate.Run(); });
      count += pool.GetWorkerCount();
    };
    park(io.LoadPool);
    if (io.BackgroundIoPool)
      park(*io.BackgroundIoPool);
    if (flags)
      park(io.ColumnLightFlagsPool);
    const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(5);
    while (gate.entered.load() != count && std::chrono::steady_clock::now() < deadline)
      std::this_thread::sleep_for(std::chrono::milliseconds(1));
    Require(gate.entered.load() == count, "I/O fixture workers did not reach the gate");
    return count;
  }

  static AsyncIoStorage Inspect(const UAsyncChunkIO &io)
  {
    std::array<AsyncIoStorageIdentity, 32> scratch{};
    return io.InspectRetainedIo(scratch.data(), scratch.size());
  }

  static nlohmann::json Describe(const AsyncIoStorage &s)
  {
    return {{"supported", s.supported}, {"retained_supported", s.retained_supported},
            {"queued", s.queued}, {"completed", s.completed}, {"active", s.active},
            {"callable", s.callable_bytes}, {"queued_backing", s.queued_backing},
            {"completed_backing", s.completed_backing}, {"shared_bytes", s.shared_bytes},
            {"shared_blocks", s.shared_blocks}, {"shared_aliases", s.shared_aliases},
            {"unknown_jobs", s.unknown_jobs}};
  }

  static nlohmann::json Run(const std::filesystem::path &root)
  {
    namespace fs = std::filesystem;
    Require(fs::create_directories(root), "I/O storage probe requires fresh output");
    const auto folder = root.string();
    const glm::ivec3 coord{0, 0, 0};
    const glm::ivec3 save_coord{1, 0, 0};
    auto definitions = std::make_shared<UBlockDefinitionStorage>();
    BlockDefinition stone;
    stone.Physics = BlockPhysicsProfile::Solid();
    definitions->ReplaceAll({{8, stone}}, {{"stone", 8}});
    UBlockRegistry registry(nullptr, definitions);
    UBlockWorld world;
    world.SetBlock({1, 2, 3}, 8);
    world.SetBlock({CHUNK_SIZE + 1, 2, 3}, 8);
    UChunkStorageService storage;
    auto snapshot = world.GetChunkManager().MakeTerrainSaveSnapshot(coord);
    Require(snapshot && storage.SaveChunk(coord, *snapshot, folder, registry), "I/O fixture saves baseline slice");

    Gate gate;
    UAsyncChunkIO io;
    // Release before pool destruction on every exit from the fixture.
    struct Release { Gate &gate; ~Release() { gate.released.store(true); } } release{gate};
    const auto parked = Park(io, gate, true);
    auto cancellation = std::make_shared<std::atomic<bool>>(false);
    std::weak_ptr<std::atomic<bool>> weak_cancellation = cancellation;
    auto receipts = std::make_shared<PersistedColumnLight>();
    auto receipt = std::make_shared<PersistedSectionLight>();
    receipt->coord = {0, 1, 0};
    receipt->signature.reserve(4096);
    receipt->inputs.reserve(173);
    receipts->push_back(receipt);
    const auto receipt_refs = receipts.use_count();
    io.RequestLoad(coord, storage, registry, folder, {coord, 1}, cancellation, 100);
    io.RequestLoad(coord, storage, registry, folder, {coord, 1}, cancellation, 101);
    io.RequestSave(save_coord, storage, folder, world, registry, {save_coord, 1}, 200);
    io.RequestSaveColumnLight(coord, receipts, storage, folder, 201);
    io.RequestSaveColumnLight(coord, receipts, storage, folder, 202);
    std::vector<glm::ivec2> complete{{0, 0}};
    complete.reserve(600);
    io.RequestSaveColumnLightFlags(folder, 300, std::move(complete), "io-storage-test");
    io.RequestDiskIndexWarmup(storage, folder);
    io.RequestRemoveChunkSlices({4, 0, 4}, 0, 1, storage, folder, 203);
    io.RequestRemoveTerrainColumn({5, 0, 5}, 16, 0, storage, folder);
    const auto queued = Inspect(io);
    Require(queued.retained_supported && !queued.supported && queued.active == parked,
            "queued source is supported while active worker storage remains open");
    Require(queued.queued == std::array<size_t, 6>{2, 1, 2, 1, 1, 2} &&
                queued.callable_bytes && queued.queued_backing >= 600 * sizeof(glm::ivec2) &&
                queued.shared_blocks == 5 && queued.shared_aliases == 2,
            "all six actual queue source kinds and shared aliases");
    Require(receipts.use_count() == receipt_refs + 2 && cancellation.use_count() == 3,
            "diagnostic does not change source references");
    const auto short_scratch = io.InspectRetainedIo(nullptr, 0);
    Require(!short_scratch.retained_supported && short_scratch.scratch_exhausted,
            "missing scratch cannot pass a partial source census");
    receipts.reset();
    receipt.reset();
    gate.released.store(true);
    Require(io.WaitIdleFor(std::chrono::seconds(8)) &&
                io.WaitForColumnLightFlagsSaveIdleFor(std::chrono::seconds(8)),
            "I/O fixture runs and publishes before deadline");
    const auto completed = Inspect(io);
    Require(completed.supported && completed.completed == std::array<size_t, 3>{2, 5, 1} &&
                completed.completed_backing && completed.shared_blocks == 1 &&
                completed.shared_aliases == 1 && completed.callable_bytes == 0,
            "queue owners retire and both results share the same cancellation control");
    auto reads = io.DrainLoads();
    Require(reads.size() == 2 && reads[0].success && reads[1].success &&
                reads[0].decodedBuffer && reads[1].decodedBuffer,
            "actual terrain reads decode successfully");
    const auto flags = io.DrainColumnLightFlagsSaves();
    Require(flags.size() == 1 && flags[0].success && flags[0].revision == 300,
            "actual light flags publish to disk");
    const auto saves = io.DrainSaves();
    Require(saves.size() == 5 && std::all_of(saves.begin(), saves.end(),
                [](const auto &save) { return save.success; }), "all actual saves and cleanups succeed");
    cancellation.reset();
    Require(!weak_cancellation.expired() && Inspect(io).shared_blocks == 0,
            "drained result retains cancellation outside the I/O queue");
    reads.clear();
    Require(weak_cancellation.expired(), "last returned owner retires");
    const auto drained = Inspect(io);
    Require(drained.supported && drained.completed_backing == 0 && drained.shared_bytes == 0,
            "drained queues retain slots only");

    // The fallback route submits saves and warmup to the real load pool.
    Gate cancel_gate;
    UAsyncChunkIO fallback;
    fallback.BackgroundIoPool.reset();
    Release release_cancel{cancel_gate};
    Park(fallback, cancel_gate, false);
    auto refused = std::make_shared<std::atomic<bool>>(false);
    std::weak_ptr<std::atomic<bool>> weak_refused = refused;
    fallback.RequestLoad(coord, storage, registry, folder, {coord, 2}, refused, 400);
    fallback.RequestSave(coord, storage, folder, world, registry, {coord, 2}, 401);
    refused.reset();
    auto fallback_queued = Inspect(fallback);
    Require(fallback_queued.retained_supported && fallback_queued.pools[0].pending == 2 &&
                fallback_queued.pools[1].pending == 0 && !weak_refused.expired(),
            "fallback load pool retains the actual read and save");
    fallback.LoadPool.Enqueue([] {});
    Require(Inspect(fallback).unknown_jobs == 1 && !Inspect(fallback).retained_supported,
            "foreign queued callable is reported unsupported");
    fallback.CancelPending();
    Require(weak_refused.expired() && Inspect(fallback).pools[0].pending == 0,
            "cancellation destroys pending captures");
    cancel_gate.released.store(true);
    Require(fallback.WaitIdleFor(std::chrono::seconds(5)) && Inspect(fallback).supported &&
                fallback.CompletedLoadsEmpty() && fallback.CompletedSavesEmpty(),
            "cancelled jobs publish no results");

    const auto shutdown = root / "shutdown";
    {
      UAsyncChunkIO retiring;
      retiring.RequestSaveColumnLightFlags(shutdown.string(), 500, {{0, 0}}, "shutdown");
    }
    Require(fs::is_regular_file(shutdown / "column_light.json"),
            "normal I/O destruction drains its flags worker before freeing results");

    return {{"schema", "async-io-storage.v1"}, {"status", "PASS"},
            {"queued", Describe(queued)}, {"completed", Describe(completed)},
            {"drained", Describe(drained)}, {"fallback", Describe(fallback_queued)},
            {"unknown_scratch_controls", true}, {"returned_owner_retired", true},
            {"cancelled_pending_retired", true}, {"normal_shutdown_drained", true},
            {"active_worker_storage_complete", false}};
  }
};

nlohmann::json RunAsyncIoStorageProbe(const std::filesystem::path &root)
{
  return AsyncIoStorageProbe::Run(root);
}
} // namespace cutum
