#pragma once

#include "Test/OrdinaryNewObservation.h"
#include "Blocks/BlockDefinitionStorage.h"
#include "Blocks/BlockRegistry.h"
#include "Core/Memory/NameStorageSource.h"
#include "ResourcePacks/BlockMergeRegistry.h"
#include "World/Chunks/Chunk.h"
#include "World/Chunks/ChunkGenerationToken.h"
#include "World/Core/BlockWorld.h"
#include "World/IO/ChunkStorageService.h"
#include "World/Lighting/ChunkRelightSnapshot.h"
#include "World/Lighting/PersistedSectionLight.h"
#include <fstream>
#include <limits>
#include <nlohmann/json.hpp>
#include <sstream>
#include <stdexcept>

namespace cutum
{
namespace io_codec_fixture
{
inline void Require(bool condition, const char *message)
{
  if (!condition)
    throw std::runtime_error(message);
}

inline nlohmann::json CheckRuntimeIdAssignment()
{
  BlockDefinition definition;
  definition.Name = "fixture_runtime_" + std::string(128, 'r');
  constexpr auto expected_id = kRuntimeBlockIdMin;
  size_t attempts = 0;
  {
    UBlockMergeRegistry merge;
    merge.RegisterRuntimeBlock(definition, {});
    observation::Scope allocations;
    merge.Rebuild({}, nullptr, 16);
    Require(merge.LookupBlockName(definition.Name) == expected_id, "assign the first runtime ID");
    attempts = observation::State.attempts;
  }
  Require(attempts && !observation::State.live && !observation::State.blocks,
          "the successful rebuild's observed backing retires");

  for (size_t prefix = 0; prefix < attempts; ++prefix)
  {
    {
      UBlockMergeRegistry merge;
      merge.RegisterRuntimeBlock(definition, {});
      bool refused = false;
      {
        observation::Scope allocations(prefix);
        try { merge.Rebuild({}, nullptr, 16); }
        catch (const std::bad_alloc &) { refused = true; }
        Require(refused && observation::State.refusals == 1, "force one refusal within runtime ID assignment");
      }
      if (!merge.GetNameToId().empty() || !merge.GetCubeDescriptors().empty() || merge.HasBlockId(expected_id))
        throw std::runtime_error("runtime ID binding changed after refusal prefix " + std::to_string(prefix));
      merge.Rebuild({}, nullptr, 16);
      const auto *reverse = merge.GetTypeNameById(expected_id);
      Require(merge.LookupBlockName(definition.Name) == expected_id && reverse && *reverse == definition.Name &&
                  merge.GetNameToId().size() == 1 && merge.GetCubeDescriptors().size() == 1,
              "rebuild retry publishes matching name, ID and descriptor");
    }
    Require(!observation::State.live && !observation::State.blocks, "runtime refusal backing retires with its registry");
  }
  return {{"refusals", attempts}, {"stable_retry_id", expected_id}};
}

inline nlohmann::json CheckSyntheticRegistry()
{
  const std::string name = "stored_block_" + std::string(127, 'n');
  constexpr auto first_id = kRuntimeBlockIdMin;
  constexpr auto next_id = static_cast<BlockId>(first_id + 1);
  size_t attempts = 0;
  size_t peak = 0;
  size_t refusal_backing = 0;
  {
    UBlockMergeRegistry merge;
    Require(merge.ResolveName("existing_fixture") == first_id, "seed the first synthetic ID");
    observation::Scope allocations;
    Require(merge.ResolveName(name) == next_id, "publish the next synthetic ID");
    attempts = observation::State.attempts;
    peak = observation::State.peak;
  }
  Require(attempts && !observation::State.live && !observation::State.blocks,
          "the successful registry's observed backing retires");

  for (size_t prefix = 0; prefix < attempts; ++prefix)
  {
    {
      UBlockMergeRegistry merge;
      Require(merge.ResolveName("existing_fixture") == first_id, "seed the before-refusal catalog");
      bool refused = false;
      {
        observation::Scope allocations(prefix);
        try { (void)merge.ResolveName(name); }
        catch (const std::bad_alloc &) { refused = true; }
        Require(refused && observation::State.refusals == 1, "force one refusal in synthetic publication");
      }
      if (merge.HasBlock(name) || merge.HasBlockId(next_id) || merge.ResolveName("existing_fixture") != first_id ||
          merge.GetNameToId().size() != 1 || merge.GetCubeDescriptors().size() != 1)
        throw std::runtime_error("synthetic catalog changed after refusal prefix " + std::to_string(prefix));
      refusal_backing = std::max(refusal_backing, observation::State.live);
      Require(merge.ResolveName(name) == next_id && merge.GetCubeDescriptors().back().Name == name,
              "retry publishes one descriptor at the same ID");
      const auto *reverse = merge.GetTypeNameById(next_id);
      Require(reverse && *reverse == name && merge.GetNameToId().size() == 2 && merge.GetCubeDescriptors().size() == 2,
              "retry publishes each name, ID and descriptor exactly once");
    }
    Require(!observation::State.live && !observation::State.blocks,
            "refused publication and retries leave no observed backing after retirement");
  }

  constexpr auto last_id = (std::numeric_limits<BlockId>::max)();
  auto merge = std::make_shared<UBlockMergeRegistry>();
  UBlockRegistry blocks(nullptr);
  blocks.SetMergeRegistry(merge);
  const auto stored_name = [](uint32_t id) { return "fixture_saved_" + std::to_string(id); };
  for (uint32_t id = first_id; id <= last_id; ++id)
    Require(merge->ResolveName(stored_name(id)) == id, "allocate the exact remaining synthetic ID");
  constexpr size_t count = size_t{last_id} - first_id + 1;
  const glm::ivec3 coord{0};
  UJsonChunkSerializer codec;
  const auto decode = [&](const std::string &type) {
    std::istringstream source(R"({"voxels":[{"lx":0,"ly":0,"lz":0,"type":")" + type + R"("}]})");
    return codec.DeserializeStream(source, coord, blocks);
  };
  for (int i = 0; i < 32; ++i)
    Require(!decode("fixture_extra_" + std::to_string(i)).HasDecodedTerrainFrame(coord),
            "unknown legacy names cannot publish a chunk after ID exhaustion");
  for (const auto id : {first_id, static_cast<BlockId>(last_id - 1), last_id})
  {
    const auto type = stored_name(id);
    const auto decoded = decode(type);
    const auto *reverse = merge->GetTypeNameById(id);
    Require(decoded.HasDecodedTerrainFrame(coord) && decoded.GetBlock(coord) == id && reverse && *reverse == type,
            "exhaustion preserves existing chunk names and numeric IDs");
  }
  Require(merge->GetCubeDescriptors().size() == count && merge->GetNameToId().size() == count,
          "refused names do not grow the saturated catalog");
  return {{"first_id", first_id}, {"last_id", last_id}, {"entries", count},
          {"refused_names", 32}, {"allocation_refusals", attempts}, {"allocation_peak", peak},
          {"refusal_retained_backing", refusal_backing}};
}

struct CapturedRequest
{
  struct WindowPeak { size_t previous{}, baseline{}, peak{}, additional{}, calls{}; };
  IoCodecStorage storage;
  std::array<IoCodecStorage, static_cast<size_t>(IoCodecKind::Count)> points{};
  std::array<size_t, static_cast<size_t>(IoCodecKind::Count)> point_live{};
  std::array<WindowPeak, static_cast<size_t>(IoCodecWindow::Count)> windows{};
  size_t observed{}, calls{}, operation_peak{};
  static void Sample(void *context, const IoCodecStorage &storage) noexcept
  {
    auto &value = *static_cast<CapturedRequest *>(context);
    value.storage = storage;
    value.observed = observation::State.live;
    const auto index = static_cast<size_t>(storage.kind);
    value.points[index] = storage;
    value.point_live[index] = value.observed;
    ++value.calls;
  }

  static void Window(void *context, IoCodecWindow kind, bool entering) noexcept
  {
    auto &window = static_cast<CapturedRequest *>(context)->windows[static_cast<size_t>(kind)];
    if (entering)
    {
      window.previous = observation::State.peak;
      window.baseline = observation::State.live;
      observation::State.peak = window.baseline;
      return;
    }
    const auto peak = observation::State.peak;
    window.peak = (std::max)(window.peak, peak);
    window.additional = (std::max)(window.additional, peak - window.baseline);
    observation::State.peak = (std::max)(peak, window.previous);
    ++window.calls;
  }

  IoCodecProbe Probe() { return {this, Sample, Window}; }
};

inline nlohmann::json Describe(const CapturedRequest &capture)
{
  return {{"scratch", capture.storage.scratch_bytes}, {"output", capture.storage.output_bytes},
          {"ordinary_live", capture.observed}, {"operation_peak", capture.operation_peak}};
}

inline void Check(const CapturedRequest &capture)
{
  if (capture.calls != 1 || !capture.storage.supported ||
      capture.observed != capture.storage.scratch_bytes + capture.storage.output_bytes ||
      !capture.storage.output_bytes)
    throw std::runtime_error("codec checkpoint differs from independently observed ordinary requests: " +
        std::to_string(static_cast<int>(capture.storage.kind)) + " " + Describe(capture).dump());
}

inline nlohmann::json CheckTerrainSaveSnapshots()
{
  const glm::ivec3 coord{0, -2, 0}, air_coord{0, -3, 0};
  const glm::ivec3 local{1, 2, 3};
  auto definitions = std::make_shared<UBlockDefinitionStorage>();
  BlockDefinition stone;
  stone.Physics = BlockPhysicsProfile::Solid();
  definitions->ReplaceAll({{8, stone}}, {{"stone", 8}});
  UBlockRegistry registry(nullptr, definitions);
  UBlockWorld world;
  world.SetKnownOpenSkyAbsentQuery([](glm::ivec3) { return true; });
  world.SetBlock(coord * CHUNK_SIZE + local, 8);
  auto &manager = world.GetChunkManager();
  auto *live = manager.GetChunk(coord);
  live->SetFluidLocal(local, FluidCellState::Flowing(3, true).WithKind(FluidKind::Water));
  live->SetWeatherState(0.75f, 71.125);
  {
    RelightJobSpec spec;
    spec.min_world_y = world.GetVerticalProfile().MinY;
    spec.max_world_y = world.GetVerticalProfile().MaxY;
    const auto light = UChunkRelightSnapshot::CaptureSections(world, spec, {coord}).Compute(registry);
    Require(light.chunks.size() == 1 && light.chunks.front().padded_light,
            "a real calculation supplies padded light on the source chunk");
    live->ReplaceComputedLightVolume(*light.chunks.front().padded_light, true, true);
    Require(world.RecordLightCalculation(light, definitions->GetCatalogSnapshot()) == 1,
            "the source chunk retains a real light calculation proof");
  }
  std::weak_ptr<const SectionLightVolume> padded = live->GetSectionLightField().GetPaddedData();
  std::weak_ptr<const RelightComputeResult> proof = world.GetRecordedLightInputContext(coord, LightDomain::Sky);
  Require(!padded.expired() && !proof.expired(), "both proof and padded source owners are live before capture");

  UBinaryChunkSerializer binary;
  UJsonChunkSerializer json;
  const auto expected_binary = binary.Serialize(coord, *live, registry).bytes;
  const auto expected_json = json.Serialize(coord, *live, registry).bytes;
  UChunk air(air_coord);
  std::array<uint8_t, CHUNK_VOLUME> pattern;
  for (size_t i = 0; i < pattern.size(); ++i)
    pattern[i] = static_cast<uint8_t>(i * 13 + 0x52);
  air.ReplaceLightData(pattern);
  air.SetWeatherState(0.25f, 135.25);
  const auto expected_air = binary.Serialize(air_coord, air, registry).bytes;
  const auto decoded = binary.Deserialize(expected_air, air_coord, registry);
  const auto loaded = decoded.GetDecodedAirSlice(air_coord);
  Require(loaded && manager.InstallLoadedAirSlice(*loaded, {{air_coord.x, 0, air_coord.z}, 1}, 1, false) &&
              !manager.HasChunk(air_coord), "decoded AIR remains free of resident geometry");

  size_t refusals = 0;
  for (const auto target : {coord, air_coord})
    for (size_t prefix = 0; prefix < 3; ++prefix)
    {
      observation::Scope allocations(prefix);
      try { const std::shared_ptr<const UChunk> snapshot = manager.MakeTerrainSaveSnapshot(target); }
      catch (const std::bad_alloc &) { ++refusals; }
      Require(observation::State.refusals == 1 && !observation::State.live && !observation::State.blocks,
              "failed terrain snapshot retires all observed allocations");
    }

  std::shared_ptr<const UChunk> resident, known_air;
  size_t resident_bytes = 0, air_bytes = 0, attempts = 0;
  {
    observation::Scope allocations;
    resident = manager.MakeTerrainSaveSnapshot(coord);
    resident_bytes = observation::State.live;
    known_air = manager.MakeTerrainSaveSnapshot(air_coord);
    air_bytes = observation::State.live - resident_bytes;
    attempts = observation::State.attempts;
    Require(resident && known_air && known_air->IsAirOnly() && observation::State.blocks == 6 &&
                observation::State.peak == resident_bytes + air_bytes && resident_bytes == air_bytes,
            "resident and decoded AIR captures have identical ordinary ownership widths");
  }
  world.Clear();
  const bool held_padding = !padded.expired();
  Require(!held_padding && proof.expired() && binary.Serialize(coord, *resident, registry).bytes == expected_binary &&
              json.Serialize(coord, *resident, registry).bytes == expected_json &&
              binary.Serialize(air_coord, *known_air, registry).bytes == expected_air,
          "snapshots preserve all persisted bytes after the source world retires");
  resident.reset();
  known_air.reset();
  Require(refusals == 6 && attempts == 6 && padded.expired() && !observation::State.live && !observation::State.blocks,
          "both complete terrain snapshots and all refusal cases retire");
  return {{"resident_bytes", resident_bytes}, {"air_bytes", air_bytes}, {"attempts", attempts},
          {"refusals", refusals}, {"held_padding_after_world_clear", held_padding}, {"proof_retired", true},
          {"binary_and_json_byte_match", true}, {"air_byte_match", true}};
}

inline nlohmann::json CheckBinaryWriter()
{
  UBlockRegistry registry(nullptr);
  UBinaryChunkSerializer codec;
  const glm::ivec3 coord{-21, -2, 3};
  UChunk chunk(coord);
  chunk.SetWeatherState(0.5f, 135.25);
  const auto measure = [&] {
    std::vector<BlockId> palette{BLOCK_AIR};
    std::vector<std::pair<uint16_t, uint16_t>> runs;
    for (const auto id : chunk.GetData())
    {
      const auto found = std::find(palette.begin(), palette.end(), id);
      const auto index = static_cast<uint16_t>(found - palette.begin());
      if (found == palette.end())
        palette.push_back(id);
      if (runs.empty() || runs.back().second != index)
        runs.emplace_back(0, index);
      ++runs.back().first;
    }
    std::vector<uint8_t> expected{'C', 'C', 'H', 'K', 4};
    const auto append = [&](uint64_t number, size_t width) {
      for (size_t i = 0; i < width; ++i)
        expected.push_back(static_cast<uint8_t>(number >> (i * 8)));
    };
    for (int axis = 0; axis < 3; ++axis)
      append(static_cast<uint32_t>(coord[axis]), 4);
    append(palette.size(), 2);
    for (const auto id : palette)
      append(id, 2);
    append(runs.size(), 4);
    for (const auto &run : runs)
    {
      append(run.first, 2);
      append(run.second, 2);
    }
    for (const auto *plane : {&chunk.GetFluidData(), &chunk.GetLightData()})
    {
      append(plane->size(), 4);
      expected.insert(expected.end(), plane->begin(), plane->end());
    }
    append(32768, 2);
    append(135250, 8);

    CapturedRequest encode;
    const auto probe = encode.Probe();
    size_t capacity = 0, requests = 0;
    {
      observation::Scope allocations;
      {
        const auto output = codec.Serialize(coord, chunk, registry, &probe);
        Require(output.format == ChunkDiskFormat::Binary && output.bytes == expected,
                "CCHK bytes match the independent v4 reference");
        capacity = output.bytes.capacity();
        requests = observation::State.attempts;
      }
      encode.operation_peak = observation::State.peak;
      Require(!observation::State.live && !observation::State.blocks, "all CCHK writer backing retires");
    }
    Check(encode);
    const auto extent = expected.size();
    Require(extent <= UBinaryChunkSerializer::kMaxWrittenFrameBytes && capacity == extent && requests == 1 &&
                !encode.storage.scratch_bytes && encode.storage.stack_buffer_bytes == (3 * CHUNK_VOLUME + 2) * sizeof(uint16_t) &&
                encode.operation_peak == encode.storage.output_bytes &&
                encode.operation_peak == extent + NameStorageAlignmentSlack(extent),
            "CCHK has one exact encoded allocation and a fixed palette on the stack");
    return nlohmann::json{{"size", expected.size()}, {"capacity", capacity}, {"requests", requests},
                          {"palette", palette.size()}, {"runs", runs.size()}, {"peak", encode.operation_peak},
                          {"scratch", encode.storage.scratch_bytes}, {"stack_buffer", encode.storage.stack_buffer_bytes},
                          {"byte_match", true}};
  };

  auto air = measure();
  std::array<uint8_t, CHUNK_VOLUME> light;
  for (int i = 0; i < CHUNK_VOLUME; ++i)
  {
    const glm::ivec3 pos{i % CHUNK_SIZE, i / CHUNK_SIZE % CHUNK_SIZE, i / (CHUNK_SIZE * CHUNK_SIZE)};
    chunk.SetBlockLocal(pos, i / 9 % 3 == 0 ? BLOCK_AIR : i / 9 % 3 == 1 ? 1024 : UINT16_MAX);
    if (chunk.GetBlockLocal(pos) != BLOCK_AIR)
      chunk.SetFluidLocal(pos, UnpackFluidCellState(static_cast<uint8_t>(i)));
    light[i] = static_cast<uint8_t>(i * 13);
  }
  chunk.ReplaceLightData(light);
  auto mixed = measure();
  for (int i = 0; i < CHUNK_VOLUME; ++i)
  {
    const glm::ivec3 pos{i % CHUNK_SIZE, i / CHUNK_SIZE % CHUNK_SIZE, i / (CHUNK_SIZE * CHUNK_SIZE)};
    chunk.SetBlockLocal(pos, static_cast<BlockId>(UINT16_MAX - i));
  }
  bool refused = false;
  {
    observation::Scope allocations(0);
    try { (void)codec.Serialize(coord, chunk, registry); }
    catch (const std::bad_alloc &) { refused = true; }
    Require(refused && observation::State.refusals == 1 && !observation::State.live && !observation::State.blocks,
            "CCHK output refusal retires every writer allocation");
  }
  auto dense = measure();
  for (int i = 0; i < CHUNK_VOLUME; ++i)
  {
    const glm::ivec3 pos{i % CHUNK_SIZE, i / CHUNK_SIZE % CHUNK_SIZE, i / (CHUNK_SIZE * CHUNK_SIZE)};
    const auto cell = i < CHUNK_VOLUME / 2 ? i : CHUNK_VOLUME - 1 - i;
    chunk.SetBlockLocal(pos, static_cast<BlockId>(UINT16_MAX - 13 * cell));
  }
  auto collisions = measure();
  constexpr auto max_frame = UBinaryChunkSerializer::kMaxWrittenFrameBytes;
  Require(dense["size"] == max_frame, "full palette and run tables reach the writer size limit");
  return {{"air", std::move(air)}, {"mixed", std::move(mixed)}, {"dense", std::move(dense)},
          {"collisions", std::move(collisions)}, {"max_frame", max_frame},
          {"max_requested", max_frame + NameStorageAlignmentSlack(max_frame)},
          {"allocation_refused_and_retried", true}};
}

inline nlohmann::json CheckJsonWriter()
{
  UBlockRegistry registry(nullptr);
  UJsonChunkSerializer codec;
  const auto reference = [](const UChunk &chunk, glm::ivec3 coord) {
    nlohmann::json cells = nlohmann::json::array();
    for (size_t i = 0; i < chunk.GetData().size(); ++i)
    {
      const auto id = chunk.GetData()[i];
      if (id == BLOCK_AIR)
        continue;
      nlohmann::json cell{{"id", id}, {"lx", i % CHUNK_SIZE},
                          {"ly", i / CHUNK_SIZE % CHUNK_SIZE}, {"lz", i / (CHUNK_SIZE * CHUNK_SIZE)}};
      if (const auto fluid = chunk.GetFluidData()[i])
        cell["fluid"] = fluid;
      cells.push_back(std::move(cell));
    }
    return nlohmann::json{{"cx", coord.x}, {"cy", coord.y}, {"cz", coord.z}, {"format_version", 4},
        {"voxels", std::move(cells)}, {"weather_state", {{"last_update_sec", std::max(0.0, chunk.GetWeatherLastUpdateSec())},
            {"surface_wetness", std::clamp(chunk.GetSurfaceWetness(), 0.0f, 1.0f)}}}}.dump();
  };
  const auto measure = [&](const UChunk &chunk, glm::ivec3 coord) {
    const auto expected = reference(chunk, coord);
    CapturedRequest encode;
    const auto probe = encode.Probe();
    size_t size = 0, capacity = 0, requests = 0;
    {
      observation::Scope allocations;
      {
        const auto output = codec.Serialize(coord, chunk, registry, &probe);
        size = output.bytes.size();
        capacity = output.bytes.capacity();
        Require(output.format == ChunkDiskFormat::Json &&
                    std::equal(output.bytes.begin(), output.bytes.end(), expected.begin(), expected.end()),
                "JSON bytes match the independent v4 reference");
        requests = observation::State.attempts;
      }
      encode.operation_peak = observation::State.peak;
      Require(!observation::State.live && !observation::State.blocks, "all writer backing retires");
    }
    Check(encode);
    Require(size <= UJsonChunkSerializer::MaxFrameBytes() && capacity == size && requests == 1 &&
                !encode.storage.scratch_bytes && encode.storage.stack_buffer_bytes == 64 &&
                encode.operation_peak == encode.storage.output_bytes &&
                encode.operation_peak == size + NameStorageAlignmentSlack(size),
            "v4 JSON writer acquires only its exact output backing");
    return nlohmann::json{{"size", size}, {"capacity", capacity}, {"requests", requests},
                          {"peak", encode.operation_peak}, {"scratch", encode.storage.scratch_bytes},
                          {"stack_buffer", encode.storage.stack_buffer_bytes}, {"byte_match", true}};
  };

  const glm::ivec3 limits{(std::numeric_limits<int>::min)(), (std::numeric_limits<int>::max)(), -1};
  UChunk chunk({0, 0, 0});
  chunk.SetWeatherState(-0.0f, std::numeric_limits<double>::denorm_min());
  auto empty = measure(chunk, limits);
  chunk.SetBlockLocal({3, 1, 2}, 9);
  chunk.SetBlockLocal({15, 14, 13}, 10000);
  chunk.SetFluidLocal({15, 14, 13}, FluidCellState::Flowing(7, true).WithKind(FluidKind::Water));
  chunk.SetWeatherState(0.73f, 135.25);
  auto sparse = measure(chunk, {-14, -2, 20});
  for (int z = 0; z < CHUNK_SIZE; ++z)
    for (int y = 0; y < CHUNK_SIZE; ++y)
      for (int x = 0; x < CHUNK_SIZE; ++x)
      {
        const glm::ivec3 pos{x, y, z};
        chunk.SetBlockLocal(pos, (std::numeric_limits<BlockId>::max)());
        chunk.SetFluidLocal(pos, UnpackFluidCellState(UINT8_MAX));
      }
  chunk.SetWeatherState(0.125f, (std::numeric_limits<double>::max)());
  bool refused = false;
  {
    observation::Scope allocations(0);
    try { (void)codec.Serialize(limits, chunk, registry); }
    catch (const std::bad_alloc &) { refused = true; }
    Require(refused && observation::State.refusals == 1 && !observation::State.live && !observation::State.blocks,
            "output allocation refusal leaves no writer backing");
  }
  auto dense = measure(chunk, limits);
  const auto max_frame = UJsonChunkSerializer::MaxFrameBytes();
  return {{"empty", std::move(empty)}, {"sparse", std::move(sparse)}, {"dense", std::move(dense)},
          {"max_frame", max_frame}, {"max_requested", max_frame + NameStorageAlignmentSlack(max_frame)},
          {"allocation_refused_and_retried", true}};
}

inline nlohmann::json CheckCodecs()
{
  const glm::ivec3 coord{0};
  UBlockRegistry registry(nullptr);
  UChunk chunk(coord);
  for (int n = 0; n < 400; ++n)
    chunk.SetBlockLocal({n % CHUNK_SIZE, n / CHUNK_SIZE % CHUNK_SIZE,
                         n / (CHUNK_SIZE * CHUNK_SIZE)}, static_cast<BlockId>(n + 2));
  UBinaryChunkSerializer binary;
  UJsonChunkSerializer json;
  // Warm the standard C++ infrastructure outside observed lifetimes.
  for (const IUChunkSerializer *codec : std::array<const IUChunkSerializer *, 2>{&binary, &json})
  {
    const auto output = codec->Serialize(coord, chunk, registry);
    codec->Deserialize(output.bytes, coord, registry);
  }
  nlohmann::json report = nlohmann::json::array();
  for (const IUChunkSerializer *codec : std::array<const IUChunkSerializer *, 2>{&binary, &json})
  {
    CapturedRequest encode;
    const auto encode_probe = encode.Probe();
    {
      observation::Scope allocations;
      { const auto bytes = codec->Serialize(coord, chunk, registry, &encode_probe); }
      encode.operation_peak = observation::State.peak;
      Require(!observation::State.live && !observation::State.blocks, "encoded allocations retire");
    }
    Check(encode);

    const auto bytes = codec->Serialize(coord, chunk, registry);
    CapturedRequest decode;
    const auto decode_probe = decode.Probe();
    {
      observation::Scope allocations;
      {
        const auto decoded = codec->Deserialize(bytes.bytes, coord, registry, &decode_probe);
        Require(decoded.HasDecodedTerrainFrame(coord) && decoded.GetBlock({0, 0, 0}) == 2,
                "ordinary allocation fixture decodes the original game data");
      }
      decode.operation_peak = observation::State.peak;
      Require(!observation::State.live && !observation::State.blocks, "decoded allocations retire");
    }
    Check(decode);
    if (codec->GetFormat() == ChunkDiskFormat::Binary)
      Require(!decode.storage.scratch_bytes && decode.storage.stack_buffer_bytes == CHUNK_VOLUME,
              "binary decode borrows its palette and uses one local light plane");
    Require(encode.operation_peak >= encode.observed && decode.operation_peak >= decode.observed,
            "ordinary peak covers the retained checkpoint");
    auto phases = nlohmann::json::object();
    if (codec->GetFormat() == ChunkDiskFormat::Json)
    {
      const auto &parse = decode.windows[static_cast<size_t>(IoCodecWindow::JsonParse)];
      const auto &dump = encode.windows[static_cast<size_t>(IoCodecWindow::JsonDump)];
      Require(parse.calls == 1 && parse.additional > 0 && dump.calls == 1 &&
                  dump.additional == encode.storage.output_bytes && !encode.storage.scratch_bytes,
              "JSON parse retains its window and the writer observes exactly the output allocation");
      Require(!decode.storage.scratch_bytes && decode.storage.stack_buffer_bytes,
              "JSON read retires its syntax parser and keeps no value tree");
      const auto stream_peak = [&](const std::string &source, bool valid = true) {
        std::istringstream file(source);
        observation::Scope allocations;
        {
          const auto data = json.DeserializeStream(file, coord, registry);
          const bool decoded = data.HasDecodedTerrainFrame(coord);
          Require(decoded == valid && (!valid || data.GetBlock({0, 0, 0}) == 8),
                  "the stream publishes the authored cell only after validation");
        }
        Require(!observation::State.live && !observation::State.blocks,
                "the streamed JSON parser and result retire");
        return observation::State.peak;
      };
      constexpr std::string_view row = R"({"lx":0,"ly":0,"lz":0,"id":8})";
      const std::string prefix = R"({"voxels":[)" + std::string(row);
      const auto compact_peak = stream_peak(prefix + "]} ");
      std::string padded = prefix;
      for (int i = 0; i < 32768; ++i)
      {
        padded += ',';
        padded += row;
      }
      padded += "]}";
      padded.append(8 * 1024 * 1024, ' ');
      const auto padded_peak = stream_peak(padded);
      const auto long_token_peak = stream_peak(prefix + R"(],"extra":")" + std::string(131072, 'x') + "\"}");
      std::string literals = prefix + R"(],"extra":[)";
      const auto compact_literal_peak = stream_peak(literals + "null]}");
      const auto compact_error_peak = stream_peak(literals + "nulx]}", false);
      for (int i = 0; i < 262144; ++i)
        literals += "null,";
      const auto literal_error_peak = stream_peak(literals + "nulx]}", false);
      literals += "null]}";
      const auto literal_peak = stream_peak(literals);
      constexpr size_t depth = UJsonChunkSerializer::kMaxContainerDepth - 1;
      const auto nested = [&](size_t levels) {
        return prefix + R"(],"extra":)" + std::string(levels, '[') +
               "null" + std::string(levels, ']') + "}";
      };
      const auto deep_peak = stream_peak(nested(depth));
      const auto beyond_depth_peak = stream_peak(nested(depth + 1), false);
      Require(compact_peak == padded_peak && beyond_depth_peak <= deep_peak + 1024,
              "the parser bounds nesting and preserves flat repeated cells and whitespace");
      Require(deep_peak > compact_literal_peak, "nesting still needs parser state");
#ifdef CUBATARIUM_JSON_LEXER_OVERRIDES
      Require(compact_literal_peak == literal_peak && literal_error_peak <= compact_error_peak + 1024,
              "literal runs keep the allocation peak both on success and on malformed trailing input");
#endif
      bool has_default_diagnostic = false;
      try
      {
        (void)nlohmann::json::parse("[1,nulx]");
      }
      catch (const nlohmann::json::parse_error &error)
      {
        has_default_diagnostic = std::string_view(error.what()).find("last read: '1,nulx'") != std::string_view::npos;
      }
      Require(has_default_diagnostic, "ordinary JSON clients retain the raw token in diagnostics");
      phases = {{"parse_peak", parse.peak}, {"parse_additional", parse.additional},
                {"dump_peak", dump.peak}, {"dump_additional", dump.additional},
                {"compact_stream_peak", compact_peak}, {"padded_stream_peak", padded_peak},
                {"padded_stream_bytes", padded.size()}, {"long_token_peak", long_token_peak},
                {"compact_literal_peak", compact_literal_peak}, {"literal_stream_peak", literal_peak},
                {"literal_stream_bytes", literals.size()}, {"compact_error_peak", compact_error_peak},
                {"literal_error_peak", literal_error_peak}, {"default_raw_diagnostic", has_default_diagnostic},
                {"deep_peak", deep_peak}, {"nesting", depth}, {"beyond_depth_peak", beyond_depth_peak}};
#ifdef CUBATARIUM_JSON_LEXER_OVERRIDES
      const auto extra_text = prefix + R"(],"extra":")";
      const auto short_token_peak = stream_peak(extra_text + std::string(32, 'x') + "\"}");
      const auto long_key_peak = stream_peak(prefix + R"(],"voxels)" + std::string(131072, 'x') + R"(":false})");
      stream_peak(extra_text + std::string(131072, 'x') + R"(\uD800"})", false);
      stream_peak(prefix + "]}" + '\0' + "trailing", false);
      Require(long_token_peak == short_token_peak && long_key_peak == short_token_peak,
              "unknown strings and keys keep a bounded prefix without bypassing syntax checks");

      const auto extra_number = prefix + R"(],"extra":)";
      constexpr size_t number_limit = UJsonChunkSerializer::kMaxNumericTokenBytes;
      const auto boundary_number = std::string("0.") + std::string(number_limit - 2, '0');
      const auto number_peak = stream_peak(extra_number + boundary_number + "}");
      const auto over_number_peak = stream_peak(extra_number + boundary_number + "0}", false);
      const auto huge_number_peak = stream_peak(extra_number + "1e+" + std::string(131072, '0') + "}", false);
      Require(number_peak < 24 * 1024 && over_number_peak <= number_peak + 1024 &&
                  huge_number_peak <= number_peak + 1024,
              "the exact numeric limit is readable and excess bytes use bounded storage");

      constexpr size_t name_limit = UJsonChunkSerializer::kMaxLegacyNameBytes;
      const std::string name = "fixture::" + std::string(name_limit - 9, 'n');
      auto definitions = std::make_shared<UBlockDefinitionStorage>();
      definitions->ReplaceAll({}, {{name, 8}, {name + 'x', 8}});
      registry.SetDefinitions(definitions);
      const std::string legacy = R"({"voxels":[{"lx":0,"ly":0,"lz":0,"type":")";
      const auto name_peak = stream_peak(legacy + name + R"("}]})");
      const auto over_name_peak = stream_peak(legacy + name + R"(x"}]})", false);
      const auto numeric_precedence_peak = stream_peak(legacy + name + R"(x","id":8}]})");
      Require(name.size() == name_limit && name_peak < 256 * 1024 &&
                  over_name_peak <= name_peak + 1024 && numeric_precedence_peak <= name_peak + 1024,
              "the byte-limit legacy name resolves exactly and valid numeric IDs retain precedence");
      phases["read_limits"] = {{"short_token_peak", short_token_peak}, {"long_key_peak", long_key_peak},
                               {"numeric_bytes", number_limit}, {"numeric_peak", number_peak},
                               {"over_numeric_peak", over_number_peak}, {"huge_numeric_peak", huge_number_peak},
                               {"legacy_name_bytes", name_limit}, {"legacy_name_peak", name_peak},
                               {"over_legacy_name_peak", over_name_peak}, {"numeric_precedence_peak", numeric_precedence_peak}};
#endif
    }
    else
    {
      constexpr auto limit = UBinaryChunkSerializer::kMaxFrameBytes;
      const auto raw_request = limit + NameStorageAlignmentSlack(limit);
      std::istringstream accepted(std::string(limit, 'b'));
      std::istringstream rejected(std::string(limit + 1, 'b'));
      size_t file_input_peak = 0;
      {
        observation::Scope allocations;
        {
          const auto input = UChunkStorageService::ReadBinaryChunkBytes(accepted);
          Require(input.size() == limit && observation::State.live == raw_request,
                  "the reader allocates one exact input buffer");
        }
        file_input_peak = observation::State.peak;
        Require(!observation::State.live && file_input_peak == raw_request, "the exact file input returns all requests");
      }
      {
        observation::Scope allocations(0);
        Require(UChunkStorageService::ReadBinaryChunkBytes(rejected).empty() &&
                    !observation::State.attempts, "overlong input is refused before requesting memory");
      }
      phases = {{"file_input_peak", file_input_peak}, {"overlong_allocation_attempts", 0},
                {"inline_light_bytes", decode.storage.stack_buffer_bytes}};
    }
    report.push_back({{"format", static_cast<int>(codec->GetFormat())},
                      {"encode", Describe(encode)}, {"decode", Describe(decode)}, {"windows", phases}});
  }
  return report;
}

inline nlohmann::json CheckMalformedJson()
{
  UBlockRegistry registry(nullptr);
  UJsonChunkSerializer codec;
  const auto text = std::string{"{\"values\":["} + std::string(4096, ' ') + "0,";
  const std::vector<uint8_t> input(text.begin(), text.end());
  codec.Deserialize(input, {0, 0, 0}, registry);
  CapturedRequest capture;
  const auto probe = capture.Probe();
  {
    observation::Scope allocations;
    {
      const auto output = codec.Deserialize(input, {0, 0, 0}, registry, &probe);
      Require(!output.HasDecodedTerrainFrame({0, 0, 0}), "malformed JSON stays rejected");
    }
    capture.operation_peak = observation::State.peak;
    Require(!observation::State.live && !observation::State.blocks, "parse exception retires its requests");
  }
  const auto &parse = capture.windows[static_cast<size_t>(IoCodecWindow::JsonParse)];
  Require(parse.calls == 1 && parse.additional > 0 && !capture.calls, "parse error closes its observed window");
  for (const auto *broken : {R"({"voxels":[],"format_version":1     2})",
                             R"({"voxels":[],"extra":tr     ue})",
                             "{\"voxels\":[],\"extra\":\"raw\nnewline\"}"})
  {
    std::istringstream file(broken);
    Require(!codec.DeserializeStream(file, {0, 0, 0}, registry).HasDecodedTerrainFrame({0, 0, 0}),
            "whitespace normalization preserves invalid token boundaries and strings");
  }
  return {{"peak", capture.operation_peak}, {"parse_peak", parse.peak}, {"retired", true}};
}

inline nlohmann::json CheckReceiptWriter(const std::filesystem::path &root)
{
  UChunkStorageService storage;
  const glm::ivec3 column{1, 0, -2};
  const auto measure = [&](int sections, std::string_view signature, bool plain, const std::string &folder) {
    PersistedColumnLight receipts;
    auto expected_fields = nlohmann::json::array();
    for (int y = 0; y < sections; ++y)
    {
      auto row = std::make_shared<PersistedSectionLight>();
      row->coord = {column.x, y, column.z};
      row->profile = {0, sections * CHUNK_SIZE - 1, 7};
      row->configuration = UINT64_MAX;
      row->signature = signature;
      for (int x = column.x - 1; x <= column.x + 1; ++x)
        for (int iy = 0; iy < sections; ++iy)
          for (int z = column.z - 1; z <= column.z + 1; ++z)
            row->inputs.push_back({{x, iy, z}, UINT64_MAX - uint64_t{static_cast<unsigned>(iy)}});
      for (size_t i = 0; i < row->volume.cells.size(); ++i)
        row->volume.cells[i] = static_cast<uint8_t>(i + y);
      row->checksum = persisted_light_detail::HashBytes(persisted_light_detail::Payload(*row).dump());
      Require(persisted_light_detail::Checksum(*row) == row->checksum, "receipt checksum matches the canonical v1 oracle");
      expected_fields.push_back(EncodeSectionLight(*row));
      receipts.push_back(std::move(row));
    }
    const auto expected = nlohmann::json{{"column", {column.x, column.z}}, {"fields", std::move(expected_fields)},
                                         {"format_version", 1}}.dump();
    CapturedRequest capture;
    const auto probe = capture.Probe();
    Require(storage.WriteColumnLight(folder, column, receipts), "prepare an existing physical receipt file");
    {
      observation::Scope allocations;
      Require(storage.WriteColumnLight(folder, column, receipts, nullptr, &probe), "write observed column receipts");
      capture.operation_peak = observation::State.peak;
      Require(!observation::State.live && !observation::State.blocks, "column writer allocations retire");
    }
    std::ifstream file(storage.ColumnLightFilePath(folder, column), std::ios::binary);
    const std::string bytes{std::istreambuf_iterator<char>(file), std::istreambuf_iterator<char>()};
    file.close();
    Require(bytes == expected, "column receipt file preserves the exact canonical v1 bytes");
    const auto loaded = storage.ReadColumnLight(folder, column);
    Require(loaded && loaded->size() == receipts.size() &&
                loaded->back()->checksum == receipts.back()->checksum && loaded->back()->signature == receipts.back()->signature,
            "physical receipt files still pass the existing reader");
    const auto &encode = capture.points[static_cast<size_t>(IoCodecKind::ReceiptEncode)];
    const auto &window = capture.windows[static_cast<size_t>(IoCodecWindow::JsonDump)];
    const auto output = bytes.size() + NameStorageAlignmentSlack(bytes.size());
    Require(capture.calls == 2 && !encode.scratch_bytes && encode.output_bytes == output &&
                encode.stack_buffer_bytes == persisted_light_detail::kReceiptEncodingBufferBytes &&
                capture.point_live[static_cast<size_t>(IoCodecKind::ReceiptEncode)] == output &&
                window.calls == 1 && (!plain || window.additional == output),
            "receipt encoder publishes its exact output and fixed buffer widths");
    if (plain)
    {
      bool refused = false;
      {
        observation::Scope allocations(0);
        try { (void)storage.WriteColumnLight(folder, column, receipts); }
        catch (const std::bad_alloc &) { refused = true; }
        Require(refused && observation::State.refusals == 1 && !observation::State.live,
                "refused receipt output allocation retires before writing");
      }
      std::vector<uint8_t> preserved;
      Require(storage.ReadBytesFromFile(storage.ColumnLightFilePath(folder, column), preserved) &&
                  std::equal(preserved.begin(), preserved.end(), bytes.begin(), bytes.end()) &&
                  storage.WriteColumnLight(folder, column, receipts),
              "output refusal keeps the committed receipt file and permits the same retry");
    }
    return nlohmann::json{{"file_bytes", bytes.size()}, {"peak", capture.operation_peak},
                          {"encode_scratch", encode.scratch_bytes}, {"encode_output", encode.output_bytes},
                          {"encode_window_peak", window.additional}, {"encode_stack_buffer", encode.stack_buffer_bytes},
                          {"byte_match", true}, {"sections", sections}};
  };
  const std::string escaped = std::string("v1 \"\\\n\t\x01\xC3\xA9\xF0\x9F\x8C\x8D") + '\0';
  const auto folder = (root / "column").string();
  auto escaped_result = measure(1, escaped, false, (root / "escaped").string());
  auto max_escaped = measure(1, std::string(persisted_light_detail::kMaxSignatureBytes, '\0'), false,
                             (root / "max-escaped").string());
  auto column_result = measure(24, "light-v6:fixture", true, folder);

  PersistedColumnLight oversized;
  size_t input_bytes = 0;
  constexpr int low = (std::numeric_limits<int>::min)();
  for (int y = 0; y < 5; ++y)
  {
    auto row = std::make_shared<PersistedSectionLight>();
    row->coord = {column.x, y, column.z};
    row->signature = "light-v6:fixture";
    row->inputs.reserve(PersistedSectionLight::MaxDependencies);
    for (int i = 0; i < PersistedSectionLight::MaxDependencies; ++i)
      row->inputs.push_back({{low, low, low + i}, UINT64_MAX});
    input_bytes += PersistedLightBytes(*row);
    oversized.push_back(std::move(row));
  }
  Require(input_bytes < kMaxColumnLightBytes &&
              oversized.size() * PersistedSectionLight::MaxDependencies * 50 > kMaxColumnLightBytes,
          "overflow fixture fits the source budget and exceeds the serialized budget");
  const auto path = storage.ColumnLightFilePath(folder, column);
  std::vector<uint8_t> previous, preserved;
  Require(storage.ReadBytesFromFile(path, previous), "measure the existing committed file");
  {
    std::error_code failure;
    observation::Scope allocations;
    Require(!storage.WriteColumnLight(folder, column, oversized, &failure) &&
                failure == std::errc::invalid_argument && !observation::State.attempts,
            "oversized canonical receipt text is refused before output allocation");
  }
  Require(storage.ReadBytesFromFile(path, preserved) && preserved == previous,
          "output length refusal keeps the committed file byte-identical");
  return {{"escaped", std::move(escaped_result)}, {"max_escaped", std::move(max_escaped)},
          {"column", std::move(column_result)}, {"output_limit", kMaxColumnLightBytes},
          {"output_upper_request", kMaxColumnLightBytes + NameStorageAlignmentSlack(kMaxColumnLightBytes)},
          {"oversized_input_bytes", input_bytes}, {"oversized_without_allocation", true},
          {"allocation_refused_and_retried", true}};
}

inline nlohmann::json CheckReceiptTemporaries()
{
  PersistedSectionLight receipt;
  receipt.coord = {0, 0, 0};
  receipt.profile = {0, 15, 7};
  receipt.signature = "io-receipt-transient-fixture";
  for (int x = -1; x <= 1; ++x)
    for (int z = -1; z <= 1; ++z)
      receipt.inputs.push_back({{x, 0, z}, 1});
  receipt.checksum = persisted_light_detail::Checksum(receipt);
  auto document = EncodeSectionLight(receipt);
  const auto check = [&](bool valid) {
    DecodeSectionLight(document); // warm C++ library support outside the observation
    CapturedRequest capture;
    const auto probe = capture.Probe();
    {
      observation::Scope allocations;
      {
        const auto decoded = DecodeSectionLight(document, &probe);
        Require(bool(decoded) == valid, "receipt verdict is preserved during observation");
      }
      capture.operation_peak = observation::State.peak;
      Require(!observation::State.live && !observation::State.blocks, "receipt temporaries retire");
    }
    Require(capture.calls == 2, "receipt validation and checksum checkpoints run");
    for (const auto kind : {IoCodecKind::ReceiptValidation, IoCodecKind::ReceiptChecksum})
    {
      const auto index = static_cast<size_t>(kind);
      const auto &sample = capture.points[index];
      Require(sample.kind == kind && sample.supported &&
                  sample.scratch_bytes + sample.input_bytes == capture.point_live[index],
              "receipt temporary snapshot matches ordinary allocation observation");
    }
    const auto &map = capture.windows[static_cast<size_t>(IoCodecWindow::ReceiptValidate)];
    const auto &hash = capture.windows[static_cast<size_t>(IoCodecWindow::ReceiptHash)];
    Require(map.calls == 1 && hash.calls == 1 && map.additional > 0 && !hash.additional,
            "ordinary ASCII checksum streams without heap scratch");
    return nlohmann::json{{"validation_peak", map.peak}, {"checksum_peak", hash.peak},
                         {"checksum_extra_heap", hash.additional}, {"peak", capture.operation_peak}, {"decoded", valid}};
  };
  const auto valid = check(true);
  document["checksum"] = receipt.checksum ^ UINT64_C(1);
  return {{"valid", valid}, {"mismatched_checksum", check(false)}};
}

inline nlohmann::json CheckFileStreams(const std::filesystem::path &root)
{
  std::filesystem::create_directories(root);
  const auto path = (root / "bytes.bin").string();
  const std::vector<uint8_t> input(8192, 0x46);
  UChunkStorageService storage;
  Require(storage.WriteBytesAtomically(path, input), "real file write warmup");
  { std::vector<uint8_t> output; Require(storage.ReadBytesFromFile(path, output), "real file read warmup"); }
  size_t write_peak = 0, read_peak = 0, retained = 0;
  {
    observation::Scope allocations;
    Require(storage.WriteBytesAtomically(path, input), "observed file write");
    write_peak = observation::State.peak;
    Require(!observation::State.live && !observation::State.blocks, "file write ordinary requests retire");
  }
  {
    observation::Scope allocations;
    {
      std::vector<uint8_t> output;
      Require(storage.ReadBytesFromFile(path, output) && output == input, "observed file read");
      retained = observation::State.live;
      read_peak = observation::State.peak;
      Require(read_peak >= retained && retained >= input.size(), "read buffer is included in actual peak");
    }
    Require(!observation::State.live && !observation::State.blocks, "file read ordinary requests retire");
  }
  return {{"write_peak", write_peak}, {"read_peak", read_peak}, {"read_retained", retained}};
}

inline nlohmann::json CheckDiskIndex(const std::filesystem::path &root)
{
  std::filesystem::create_directories(root / "chunks");
  const auto folder = root.string();
  // Disk indexing consumes file names. The real codec fixtures provide the
  // payloads separately. Several hundred columns force a large bucket table.
  UChunkStorageService paths;
  for (int n = 0; n < 320; ++n)
    std::ofstream(paths.ChunkFilePath(folder, {n, 0, 0}, ChunkDiskFormat::Binary)).put('0');
  paths.PrepareHighestChunkSliceIndex(folder); // warm filesystem infrastructure
  IoDiskIndexStorage cold, warmed, empty_column;
  CapturedRequest traversal;
  const auto probe = traversal.Probe();
  size_t warmup_additional_peak = 0;
  {
    observation::Scope allocations;
    {
      UChunkStorageService storage;
      cold = storage.InspectDiskIndexStorage();
      const auto baseline = observation::State.live;
      storage.PrepareHighestChunkSliceIndex(folder, &probe);
      warmed = storage.InspectDiskIndexStorage();
      warmup_additional_peak = observation::State.peak - baseline;
      Require(warmed.supported && warmed.initialized == 1 && warmed.heights == 320 &&
                  warmed.columns == 320 && warmed.slices == 320 && warmed.tables == 324 &&
                  observation::State.live - baseline == warmed.requested - cold.requested,
              "real disk index heap matches default ordinary allocations");
      Require(traversal.calls == 1 && traversal.storage.kind == IoCodecKind::DiskDirectoryEntry &&
                  traversal.storage.supported && traversal.storage.scratch_bytes &&
                  traversal.storage.stack_buffer_bytes &&
                  warmup_additional_peak >= warmed.requested - cold.requested &&
                  warmup_additional_peak >= traversal.storage.scratch_bytes,
              "observed directory traversal sits within the measured whole warmup peak");
      const auto revision = storage.GetKnownAirEvidenceRevision();
      const auto attempts = observation::State.attempts;
      Require(storage.InspectDiskIndexStorage().requested == warmed.requested &&
                  observation::State.attempts == attempts &&
                  storage.GetKnownAirEvidenceRevision() == revision,
              "disk index census neither allocates nor advances evidence");
      Require(storage.RemoveChunkSliceFromDisk(folder, {0, 0, 0}), "test slice removes");
      Require(storage.GetHighestChunkSliceOnDisk(folder, {0, 0, 0}) == kNoChunkSliceOnDisk,
              "dirty index refresh sees the removed slice");
      empty_column = storage.InspectDiskIndexStorage();
      Require(empty_column.heights == 319 && empty_column.columns == 319 &&
                  empty_column.slices == 319 && empty_column.dirty == 0 &&
                  empty_column.requested < warmed.requested &&
                  observation::State.live - baseline == empty_column.requested - cold.requested,
              "disk index refresh retires the removed column owners");
    }
    Require(!observation::State.live && !observation::State.blocks, "disk index destruction retires requests");
  }
  return {{"cold", cold.requested}, {"warmed", warmed.requested},
          {"after_removal", empty_column.requested}, {"bucket_slots", warmed.bucket_slots},
          {"columns", warmed.columns}, {"warmup_additional_peak", warmup_additional_peak},
          {"sampled_traversal", traversal.storage.scratch_bytes}};
}
} // namespace io_codec_fixture
} // namespace cutum
