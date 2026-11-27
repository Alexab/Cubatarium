#pragma once

#include "Test/OrdinaryNewObservation.h"
#include "Blocks/BlockDefinitionStorage.h"
#include "Blocks/BlockRegistry.h"
#include "Core/Memory/NameStorageSource.h"
#include "World/Chunks/Chunk.h"
#include "World/IO/ChunkStorageService.h"
#include "World/Lighting/PersistedSectionLight.h"
#include <fstream>
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
      ((capture.storage.kind != IoCodecKind::BinaryRead && capture.storage.kind != IoCodecKind::JsonRead) &&
       !capture.storage.scratch_bytes) ||
      !capture.storage.output_bytes)
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
      Require(parse.calls == 1 && parse.additional > 0 && dump.calls == 1 && dump.additional > 0,
              "real JSON parse and dump publish their allocation windows");
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
    Require(map.calls == 1 && hash.calls == 1 && map.additional > 0 && hash.additional > 0,
            "receipt allocation windows close");
    return nlohmann::json{{"validation_peak", map.peak}, {"checksum_peak", hash.peak},
                          {"peak", capture.operation_peak}, {"decoded", valid}};
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
