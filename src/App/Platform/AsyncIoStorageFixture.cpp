#include "Test/AsyncIoStorageFixture.h"
#include "Blocks/BlockDefinitionStorage.h"
#include "Blocks/BlockRegistry.h"
#include "Core/Memory/RootStorageSource.h"
#include "World/Core/BlockWorld.h"
#include "World/IO/AsyncChunkIO.h"
#include "World/IO/ChunkStorageService.h"
#include "World/Lighting/PersistedSectionLight.h"
#include <nlohmann/json.hpp>
#include <stdexcept>

namespace cutum
{
struct AsyncIoStorageProbe
{
  struct WorkerTrace
  {
    std::mutex mutex;
    std::vector<AsyncIoWorkerSample> records = std::vector<AsyncIoWorkerSample>(96);
    std::array<const AsyncIoWorkerSample *, 8> parked{};
    size_t used{}, parked_count{};
    std::atomic<bool> released{false}, timed_out{false};
    bool pause{false}, overflow{false};
    std::atomic<bool> *cancel_on_decode{};

    static void Sample(void *context, const AsyncIoWorkerSample &sample) noexcept
    {
      auto &trace = *static_cast<WorkerTrace *>(context);
      if (trace.cancel_on_decode && sample.stage == AsyncIoWorkerStage::Decoded)
        trace.cancel_on_decode->store(true);
      const bool park = trace.pause &&
          ((sample.kind == AsyncIoJobKind::Load && sample.stage == AsyncIoWorkerStage::Decoded) ||
           (sample.kind == AsyncIoJobKind::Flags && sample.stage == AsyncIoWorkerStage::Encoded));
      {
        std::lock_guard lock(trace.mutex);
        if (trace.used == trace.records.size() ||
            (park && trace.parked_count == trace.parked.size()))
        {
          trace.overflow = true;
          return;
        }
        auto &record = trace.records[trace.used++];
        record = sample;
        if (park)
          trace.parked[trace.parked_count++] = &record;
      }
      if (!park)
        return;
      const auto deadline = Clock::now() + std::chrono::seconds(8);
      while (!trace.released.load() && Clock::now() < deadline)
        std::this_thread::sleep_for(std::chrono::milliseconds(1));
      if (!trace.released.load())
        trace.timed_out.store(true);
    }

    const AsyncIoWorkerSample *Find(AsyncIoJobKind kind, AsyncIoWorkerStage stage,
                                    uint64_t key) const
    {
      for (size_t i = 0; i < used; ++i)
        if (records[i].kind == kind && records[i].stage == stage && records[i].key == key)
          return &records[i];
      return nullptr;
    }

    const IoCodecStorage *FindCodec(IoCodecKind kind, uint64_t key) const
    {
      for (size_t i = 0; i < used; ++i)
        if (records[i].codec.kind == kind && records[i].key == key)
          return &records[i].codec;
      return nullptr;
    }
  };

  using Clock = std::chrono::steady_clock;
  struct ReleaseWorkers
  {
    AllocatorSourcePhase &phase;
    WorkerTrace &trace;
    ~ReleaseWorkers()
    {
      phase.Close();
      trace.released.store(true);
    }
  };

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
            {"unknown_jobs", s.unknown_jobs}, {"warmup_keys", s.warmup_keys},
            {"warmup_requested", s.warmup_requested}};
  }

  static nlohmann::json CheckWorkerTrace(const WorkerTrace &trace)
  {
    using Kind = AsyncIoJobKind;
    using Stage = AsyncIoWorkerStage;
    std::array<size_t, static_cast<size_t>(Kind::Count)> entered{};
    for (size_t i = 0; i < trace.used; ++i)
    {
      const auto &sample = trace.records[i];
      Require(sample.sources.retained_supported, "actual worker storage profile supported at checkpoint");
      if (sample.stage == Stage::Entered)
        ++entered[static_cast<size_t>(sample.kind)];
    }
    Require(!trace.overflow && !trace.timed_out.load() &&
                entered == std::array<size_t, 6>{2, 1, 2, 1, 1, 2},
            "all six actual I/O producers run on their worker");
    auto *raw = trace.Find(Kind::Load, Stage::ReadBytes, 100);
    auto *decoded = trace.Find(Kind::Load, Stage::Decoded, 100);
    auto *read_publish = trace.Find(Kind::Load, Stage::BeforePublish, 100);
    Require(raw && decoded && read_publish && raw->input_bytes && !raw->decoded_bytes &&
                decoded->input_bytes == raw->input_bytes && decoded->decoded_bytes &&
                !read_publish->input_bytes && read_publish->decoded_bytes == decoded->decoded_bytes,
            "raw and decoded backing overlap before the raw input retires");
    auto *save = trace.Find(Kind::Save, Stage::Encoded, 200);
    auto *save_publish = trace.Find(Kind::Save, Stage::BeforePublish, 200);
    auto *flags_entered = trace.Find(Kind::Flags, Stage::Entered, 300);
    auto *flags_encoded = trace.Find(Kind::Flags, Stage::Encoded, 300);
    Require(save && save->encoded_bytes && save->path_bytes && save_publish &&
                save_publish->encoded_bytes == save->encoded_bytes && flags_entered &&
                flags_encoded && flags_encoded->encoded_bytes && flags_encoded->path_bytes &&
                flags_entered->sources.queued_backing >=
                    flags_encoded->sources.queued_backing + 600 * sizeof(glm::ivec2),
            "actual save and flags serialization sources and vector transfer");
    const auto *index = trace.Find(Kind::Warmup, Stage::Finished, 0);
    Require(index && index->disk_index.supported && index->disk_index.initialized == 1 &&
                index->disk_index.slices >= 1 &&
                trace.Find(Kind::Receipts, Stage::BeforePublish, 202) &&
                trace.Find(Kind::Cleanup, Stage::BeforePublish, 203) &&
                trace.Find(Kind::Cleanup, Stage::BeforePublish, 0),
            "index, receipts and both cleanup paths reach their worker terminal checkpoint");
    const auto *read_codec = trace.FindCodec(IoCodecKind::BinaryRead, 100);
    const auto *write_codec = trace.FindCodec(IoCodecKind::BinaryWrite, 200);
    const auto *receipt_encode = trace.FindCodec(IoCodecKind::ReceiptEncode, 201);
    const auto *receipt_write = trace.FindCodec(IoCodecKind::ReceiptWrite, 201);
    Require(read_codec && read_codec->input_bytes == raw->input_bytes &&
                read_codec->output_bytes + sizeof(UChunkBuffer) == decoded->decoded_bytes &&
                write_codec && write_codec->scratch_bytes &&
                write_codec->output_bytes == save->encoded_bytes &&
                flags_encoded->codec.kind == IoCodecKind::FlagsWrite &&
                flags_encoded->codec.scratch_bytes &&
                flags_encoded->codec.output_bytes == flags_encoded->encoded_bytes &&
                receipt_encode && receipt_encode->scratch_bytes &&
                receipt_write && receipt_write->output_bytes,
            "binary, receipt and flags codec storage joins the actual worker");
    return {{"entered", entered}, {"read_input", raw->input_bytes},
            {"decoded_backing", decoded->decoded_bytes}, {"binary_encoded", save->encoded_bytes},
            {"flags_encoded", flags_encoded->encoded_bytes}, {"samples", trace.used},
            {"binary_read_scratch", read_codec->scratch_bytes},
            {"binary_write_scratch", write_codec->scratch_bytes},
            {"flags_dom", flags_encoded->codec.scratch_bytes},
            {"receipt_write_dom", receipt_encode->scratch_bytes},
            {"receipt_write_output", receipt_write->output_bytes},
            {"disk_index_slices", index->disk_index.slices},
            {"disk_index_requested", index->disk_index.requested}};
  }

  static nlohmann::json CheckJoinedExecution(const std::string &folder, const glm::ivec3 &coord,
                                             UChunkStorageService &storage, UBlockRegistry &registry)
  {
    using Stage = AsyncIoWorkerStage;
    const auto participants = ComputeWorkerThreadCount(JobPoolKind::ChunkIo) + 1;
    AllocatorSourcePhase phase(NewRootStorageEpoch(), participants);
    WorkerTrace trace;
    trace.pause = true;
    const AsyncIoWorkerProbe probe{phase, &trace, WorkerTrace::Sample};
    UAsyncChunkIO io;
    ReleaseWorkers release{phase, trace};
    io.WorkerProbe = &probe;
    auto cancellation = std::make_shared<std::atomic<bool>>(false);
    std::weak_ptr<std::atomic<bool>> weak = cancellation;
    // One extra read remains pending while every load worker holds the same
    // cancellation control. This crosses the actual pool ownership boundary.
    for (size_t i = 0; i < participants; ++i)
      io.RequestLoad(coord, storage, registry, folder, {coord, 2}, cancellation, 600 + i);
    io.RequestSaveColumnLightFlags(folder, 700, {{0, 0}}, "join-execution");
    const auto deadline = Clock::now() + std::chrono::seconds(5);
    for (;;)
    {
      {
        std::lock_guard lock(trace.mutex);
        if (trace.parked_count == participants)
          break;
      }
      Require(Clock::now() < deadline, "I/O execution participants park before the deadline");
      std::this_thread::sleep_for(std::chrono::milliseconds(1));
    }
    std::array<AsyncIoStorageIdentity, 32> queue_shared{}, scratch{};
    const auto queues = io.InspectRetainedIo(queue_shared.data(), queue_shared.size(), &phase);
    const auto join = [&](const AllocatorSourcePhase &source_phase, const auto &list, size_t count) {
      return JoinAsyncIoWorkers(source_phase, queues, queue_shared.data(), queues.shared_blocks,
                                list.data(), count, scratch.data(), scratch.size());
    };
    const auto joined = join(phase, trace.parked, participants);
    Require(queues.retained_supported && queues.active == participants &&
                queues.queued[0] == 1 && queues.shared_blocks == 1 &&
                joined.supported && joined.participants == participants &&
                joined.shared_blocks == 1 && joined.shared_aliases == participants - 1 &&
                joined.shared_bytes == queues.shared_bytes && joined.local_backing,
            "joined active and pending allocations count shared cancellation once");
    Require(!join(phase, trace.parked, participants - 1).supported, "missing worker rejects");
    auto altered = trace.parked;
    altered[0] = altered[1];
    Require(!join(phase, altered, participants).supported, "duplicate worker rejects");
    auto conflict = *trace.parked[0];
    if (!conflict.sources.shared_blocks)
      conflict = *trace.parked[1];
    Require(conflict.sources.shared_blocks == 1, "sample supplies shared cancellation");
    auto conflict_list = trace.parked;
    for (size_t i = 0; i < participants; ++i)
      if (conflict_list[i]->job == conflict.job)
        conflict_list[i] = &conflict;
    ++conflict.shared[0].backing;
    ++conflict.sources.shared_bytes;
    Require(!join(phase, conflict_list, participants).supported, "shared size disagreement rejects");
    AllocatorSourcePhase foreign(NewRootStorageEpoch(), participants);
    Require(!join(foreign, trace.parked, participants).supported, "foreign phase epoch rejects");
    phase.Close();
    Require(!join(phase, trace.parked, participants).supported, "closed phase rejects");
    trace.released.store(true);
    cancellation.reset();
    Require(io.WaitIdleFor(std::chrono::seconds(6)) &&
                io.WaitForColumnLightFlagsSaveIdleFor(std::chrono::seconds(6)) &&
                !trace.timed_out.load(), "coherent I/O execution completes before deadline");
    const auto completed = Inspect(io);
    Require(completed.supported && completed.completed[0] == participants &&
                completed.completed[2] == 1 && completed.shared_bytes == joined.shared_bytes,
            "the same cancellation control survives queue publication");
    auto reads = io.DrainLoads();
    Require(!weak.expired() && Inspect(io).shared_bytes == 0, "returned owners leave the I/O census");
    reads.clear();
    Require(weak.expired(), "all joined source owners retire");
    return {{"participants", participants}, {"shared_bytes", joined.shared_bytes},
            {"shared_aliases", joined.shared_aliases}, {"callable_bytes", joined.callable_bytes},
            {"local_backing", joined.local_backing}, {"negative_controls", true}};
  }

  static nlohmann::json CheckJsonAndCancellation(const std::string &folder, UBlockWorld &world,
                                                UBlockRegistry &registry)
  {
    using Kind = AsyncIoJobKind;
    using Stage = AsyncIoWorkerStage;
    AllocatorSourcePhase phase(NewRootStorageEpoch(), 1);
    WorkerTrace trace;
    auto cancellation = std::make_shared<std::atomic<bool>>(false);
    std::weak_ptr<std::atomic<bool>> weak = cancellation;
    const AsyncIoWorkerProbe probe{phase, &trace, WorkerTrace::Sample};
    UChunkStorageService storage({ChunkWriteFormat::Json, false});
    UAsyncChunkIO io;
    ReleaseWorkers release{phase, trace};
    io.WorkerProbe = &probe;
    const glm::ivec3 receipt_coord{0, 0, 0};
    io.RequestLoad(receipt_coord, storage, registry, folder, {receipt_coord, 1},
                   {}, 799, true);
    Require(io.WaitIdleFor(std::chrono::seconds(6)), "receipt read finishes");
    const auto receipt_result = io.DrainLoads();
    const auto *receipt_codec = trace.FindCodec(IoCodecKind::ReceiptRead, 799);
    Require(receipt_result.size() == 1 && receipt_result[0].columnLight &&
                receipt_result[0].columnLight->size() == 1 && receipt_codec &&
                receipt_codec->scratch_bytes && receipt_codec->output_bytes &&
                receipt_codec->stack_buffer_bytes == 4096,
            "actual persisted receipt recovers after DOM and text observation");
    trace.cancel_on_decode = cancellation.get();
    const glm::ivec3 coord{2, 0, 0};
    world.SetBlock({2 * CHUNK_SIZE + 3, 2, 3}, 8);
    io.RequestSave(coord, storage, folder, world, registry, {coord, 1}, 800);
    Require(io.WaitIdleFor(std::chrono::seconds(6)), "JSON save finishes");
    const auto saves = io.DrainSaves();
    Require(saves.size() == 1 && saves[0].success && saves[0].format == ChunkDiskFormat::Json,
            "real JSON save publishes");
    io.RequestLoad(coord, storage, registry, folder, {coord, 1}, cancellation, 801);
    Require(io.WaitIdleFor(std::chrono::seconds(6)) && io.CompletedLoadsEmpty(),
            "active cancellation suppresses result publication");
    auto *save = trace.Find(Kind::Save, Stage::Encoded, 800);
    auto *decoded = trace.Find(Kind::Load, Stage::Decoded, 801);
    auto *cancelled = trace.Find(Kind::Load, Stage::Cancelled, 801);
    const auto *json_write = trace.FindCodec(IoCodecKind::JsonWrite, 800);
    const auto *json_read = trace.FindCodec(IoCodecKind::JsonRead, 801);
    Require(save && save->encoded_bytes && decoded && decoded->input_bytes &&
                decoded->decoded_bytes && cancelled && !cancelled->input_bytes &&
                cancelled->decoded_bytes == decoded->decoded_bytes &&
                json_write && json_write->scratch_bytes &&
                json_write->output_bytes == save->encoded_bytes &&
                json_read && json_read->scratch_bytes &&
                json_read->output_bytes + sizeof(UChunkBuffer) == decoded->decoded_bytes &&
                !trace.Find(Kind::Load, Stage::BeforePublish, 801),
            "active cancellation retires decoded source without publication");
    cancellation.reset();
    Require(weak.expired() && Inspect(io).shared_blocks == 0 && !trace.overflow,
            "cancelled source leaves no retained I/O owner");
    return {{"json_encoded", save->encoded_bytes}, {"json_input", decoded->input_bytes},
            {"decoded_backing", decoded->decoded_bytes}, {"cancelled_before_publish", true},
            {"json_write_scratch", json_write->scratch_bytes},
            {"json_read_scratch", json_read->scratch_bytes},
            {"receipt_read_scratch", receipt_codec->scratch_bytes},
            {"receipt_read_output", receipt_codec->output_bytes},
            {"receipt_read_stack_buffer", receipt_codec->stack_buffer_bytes}};
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

    AllocatorSourcePhase worker_phase(NewRootStorageEpoch(), 1);
    WorkerTrace worker_trace;
    const AsyncIoWorkerProbe worker_probe{worker_phase, &worker_trace, WorkerTrace::Sample};
    Gate gate;
    UAsyncChunkIO io;
    io.WorkerProbe = &worker_probe;
    // Release before pool destruction on every exit from the fixture.
    struct Release { Gate &gate; ~Release() { gate.released.store(true); } } release{gate};
    const auto parked = Park(io, gate, true);
    auto cancellation = std::make_shared<std::atomic<bool>>(false);
    std::weak_ptr<std::atomic<bool>> weak_cancellation = cancellation;
    auto receipts = std::make_shared<PersistedColumnLight>();
    auto receipt = std::make_shared<PersistedSectionLight>();
    receipt->coord = {0, 1, 0};
    receipt->profile = {0, 31, 8};
    receipt->signature = "io-worker-light-fixture";
    receipt->signature.reserve(4096);
    receipt->inputs.reserve(173);
    for (int x = -1; x <= 1; ++x)
      for (int y = 0; y <= 1; ++y)
        for (int z = -1; z <= 1; ++z)
          receipt->inputs.push_back({{x, y, z}, 1});
    receipt->checksum = persisted_light_detail::Checksum(*receipt);
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
    worker_phase.Close();
    const auto worker_stages = CheckWorkerTrace(worker_trace);
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
    Require(queued.warmup_keys == 1 && drained.warmup_keys == 1 &&
                queued.warmup_requested == drained.warmup_requested,
            "the warmed folder key remains owned after all I/O tasks retire");

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
            {"worker_stages", worker_stages},
            {"joined_execution", CheckJoinedExecution(folder, coord, storage, registry)},
            {"json_cancelled", CheckJsonAndCancellation(folder, world, registry)},
            {"active_worker_storage_complete", false}};
  }
};

nlohmann::json RunAsyncIoStorageProbe(const std::filesystem::path &root)
{
  return AsyncIoStorageProbe::Run(root);
}
} // namespace cutum
