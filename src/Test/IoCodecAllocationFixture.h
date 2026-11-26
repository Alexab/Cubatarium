#pragma once

#include "Test/OrdinaryNewObservation.h"
#include "Blocks/BlockRegistry.h"
#include "World/Chunks/Chunk.h"
#include "World/IO/ChunkStorageService.h"
#include <fstream>
#include <nlohmann/json.hpp>
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

struct CapturedRequest
{
  IoCodecStorage storage;
  size_t observed{}, calls{};
  static void Sample(void *context, const IoCodecStorage &storage) noexcept
  {
    auto &value = *static_cast<CapturedRequest *>(context);
    value.storage = storage;
    value.observed = observation::State.live;
    ++value.calls;
  }
};

inline nlohmann::json Describe(const CapturedRequest &capture)
{
  return {{"scratch", capture.storage.scratch_bytes}, {"output", capture.storage.output_bytes},
          {"ordinary_live", capture.observed}};
}

inline void Check(const CapturedRequest &capture)
{
  if (capture.calls != 1 || !capture.storage.supported ||
      capture.observed != capture.storage.scratch_bytes + capture.storage.output_bytes ||
      !capture.storage.scratch_bytes || !capture.storage.output_bytes)
    throw std::runtime_error("codec checkpoint differs from independently observed ordinary requests: " +
        std::to_string(static_cast<int>(capture.storage.kind)) + " " + Describe(capture).dump());
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
    const IoCodecProbe encode_probe{&encode, CapturedRequest::Sample};
    {
      observation::Scope allocations;
      { const auto bytes = codec->Serialize(coord, chunk, registry, &encode_probe); }
      Require(!observation::State.live && !observation::State.blocks, "encoded allocations retire");
    }
    Check(encode);

    const auto bytes = codec->Serialize(coord, chunk, registry);
    CapturedRequest decode;
    const IoCodecProbe decode_probe{&decode, CapturedRequest::Sample};
    {
      observation::Scope allocations;
      {
        const auto decoded = codec->Deserialize(bytes.bytes, coord, registry, &decode_probe);
        Require(decoded.HasDecodedTerrainFrame(coord) && decoded.GetBlock({0, 0, 0}) == 2,
                "ordinary allocation fixture decodes the original game data");
      }
      Require(!observation::State.live && !observation::State.blocks, "decoded allocations retire");
    }
    Check(decode);
    report.push_back({{"format", static_cast<int>(codec->GetFormat())},
                      {"encode", Describe(encode)}, {"decode", Describe(decode)}});
  }
  return report;
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
  {
    observation::Scope allocations;
    {
      UChunkStorageService storage;
      cold = storage.InspectDiskIndexStorage();
      const auto baseline = observation::State.live;
      storage.PrepareHighestChunkSliceIndex(folder);
      warmed = storage.InspectDiskIndexStorage();
      Require(warmed.supported && warmed.initialized == 1 && warmed.heights == 320 &&
                  warmed.columns == 320 && warmed.slices == 320 && warmed.tables == 324 &&
                  observation::State.live - baseline == warmed.requested - cold.requested,
              "real disk index heap matches default ordinary allocations");
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
          {"columns", warmed.columns}};
}
} // namespace io_codec_fixture
} // namespace cutum
