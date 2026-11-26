#pragma once

#include "Core/Memory/StbiAllocation.h"
#include "Test/InventoryIconServiceStorageFixture.h"
#include "Test/SharedContentPreviewStorageFixture.h"

namespace cutum::inventory_icon_codec_fixture
{
using gui_context_fixture::Require;
using inventory_icon_storage_fixture::ReadFile;
using shared_content_preview_fixture::Binding;
using Json = nlohmann::json;

struct Samples
{
  static constexpr size_t Count = static_cast<size_t>(InventoryIconIoPhase::Count);
  std::array<InventoryIconIoStorage, Count> rows{};
  std::array<size_t, Count> ordinaryLive{}, codecLive{};
  std::array<bool, Count> present{};
  std::shared_ptr<UStbiAllocationLedger> ledger;

  static void Observe(void *context, const InventoryIconIoStorage &row) noexcept
  {
    auto &self = *static_cast<Samples *>(context);
    const auto index = static_cast<size_t>(row.phase);
    self.rows[index] = row;
    self.present[index] = true;
    self.ordinaryLive[index] = observation::State.live;
    if (self.ledger)
      self.codecLive[index] = self.ledger->GetSnapshot().live_request_bytes;
  }

  const InventoryIconIoStorage &Check(InventoryIconIoPhase phase) const
  {
    const auto index = static_cast<size_t>(phase);
    Require(present[index] && rows[index].supported && rows[index].ordinaryBytes <= ordinaryLive[index] &&
                rows[index].codecBytes == codecLive[index],
            "icon source sample agrees with independent live observation");
    return rows[index];
  }
};

inline void CheckPixels(GLuint texture)
{
  UGlStateScope restore(kGlMaskIconFbo);
  const auto caller = icon_lifecycle_fixture::ReadBindings();
  std::array<unsigned char, icon_lifecycle_fixture::Bytes> rgba{};
  glBindBuffer(GL_PIXEL_PACK_BUFFER, 0);
  glPixelStorei(GL_PACK_ALIGNMENT, 1);
  glPixelStorei(GL_PACK_ROW_LENGTH, 0);
  glPixelStorei(GL_PACK_SKIP_ROWS, 0);
  glPixelStorei(GL_PACK_SKIP_PIXELS, 0);
  glBindTexture(GL_TEXTURE_2D, texture);
  glGetTexImage(GL_TEXTURE_2D, 0, GL_RGBA, GL_UNSIGNED_BYTE, rgba.data());
  glBindBuffer(GL_PIXEL_PACK_BUFFER, GLuint(caller[2]));
  glPixelStorei(GL_PACK_ALIGNMENT, caller[3]);
  glPixelStorei(GL_PACK_ROW_LENGTH, caller[4]);
  glPixelStorei(GL_PACK_SKIP_ROWS, caller[5]);
  glPixelStorei(GL_PACK_SKIP_PIXELS, caller[6]);
  for (size_t i = 0; i < rgba.size(); i += 4)
    Require(rgba[i] == 19 && rgba[i + 1] == 71 && rgba[i + 2] == 137 && rgba[i + 3] == 255,
            "decoded icon reaches GL with the original RGBA at every pixel");
}

inline Json Run(const std::filesystem::path &report)
{
  auto root = report;
  root += ".icon-codecs";
  Require(std::filesystem::create_directory(root), "make isolated icon codec directory");
  const auto manifestPath = root / "manifest.json";
  UInventoryIconService service;
  Require(service.Initialize(root), "initialize real codec fixture cache");
  const auto source = icon_lifecycle_fixture::MakeTexture();
  constexpr int size = icon_lifecycle_fixture::Size;
  const std::string kind = "block", id(45, 'b'), variant(35, 'v'), fingerprint(55, 'f');
  Samples writes, reads;
  writes.ledger = std::make_shared<UStbiAllocationLedger>();
  const InventoryIconIoProbe writeProbe{&writes, Samples::Observe};
  const InventoryIconIoProbe readProbe{&reads, Samples::Observe};
  const auto store = [&] (const InventoryIconIoProbe *probe = nullptr)
  {
    return service.StoreIconTexture(kind, id, variant, fingerprint, size, source.Get(), probe);
  };
  std::size_t writePeak = 0;
  {
    observation::Scope observed;
    {
      UStbiAllocationScope codec(writes.ledger);
      Require(store(&writeProbe), "encode and stage a real PNG");
    }
    Require(!writes.ledger->GetSnapshot().live_request_bytes, "all writer requests retire after file publication");
    service.FlushManifest(&writeProbe);
    writePeak = observation::State.peak;
    // Drop the observed retained manifest row before the next observation.
    service.InvalidateAll();
    Require(!observation::State.live && !observation::State.blocks, "write fixture retires observed CPU allocations");
  }
  const auto encoded = writes.Check(InventoryIconIoPhase::PngEncode);
  const auto writing = writes.Check(InventoryIconIoPhase::ManifestWrite);
  const auto writingFile = writes.Check(InventoryIconIoPhase::ManifestFile);
  const auto liveAt = [&](InventoryIconIoPhase phase) { return writes.ordinaryLive[static_cast<size_t>(phase)]; };
  Require(encoded.codecBytes > encoded.payloadBytes && encoded.ordinaryBytes >= icon_lifecycle_fixture::Bytes &&
              liveAt(InventoryIconIoPhase::ManifestFile) < liveAt(InventoryIconIoPhase::ManifestWrite) &&
              writing.payloadBytes == writingFile.payloadBytes && writing.ordinaryBytes <= writePeak,
          "writer output overlaps real input and the manifest DOM retires before opening output");
  Require(store(), "repopulate the same PNG and manifest for reload checks");
  service.FlushManifest();
  const auto manifest = ReadFile(manifestPath);
  const auto metadata = Json::parse(manifest);
  const auto &entry = metadata.at("entries").at(UInventoryIconService::BuildCacheKey(kind, id, variant));
  const auto pngPath = root / entry.at("file").get<std::string>();
  const auto png = ReadFile(pngPath);
  const auto encode = writes.ledger->GetSnapshot();
  Require(encode.allocation_attempts > 0 && encode.peak_admitted_request_bytes >= encoded.codecBytes &&
              png.size() == encoded.payloadBytes && manifest.size() == writing.payloadBytes,
          "persisted file extents match the live codec fields");

  for (std::size_t cutoff = 0; cutoff < encode.allocation_attempts; ++cutoff)
  {
    auto ledger = std::make_shared<UStbiAllocationLedger>(SIZE_MAX, cutoff);
    bool refused = false;
    {
      UStbiAllocationScope codec(ledger);
      refused = !store();
    }
    const auto failure = ledger->GetSnapshot();
    Require(refused && failure.refusals, "encoder allocation refusal reaches its caller");
    Require(!failure.live_request_bytes && !failure.reserved_request_bytes,
            "encoder allocation refusal retires scratch");
    Require(!service.InspectOwnedStorage().dirty, "encoder allocation refusal preserves manifest state");
    Require(ReadFile(pngPath) == png && ReadFile(manifestPath) == manifest,
            "encoder allocation refusal preserves published bytes");
  }
  Require(store(), "normal PNG encode recovers after allocation refusal");
  service.FlushManifest();

  std::size_t parsePeak = 0;
  {
    observation::Scope observed;
    {
      UInventoryIconService reloaded;
      Require(reloaded.Initialize(root, &readProbe), "parse actual icon manifest");
      parsePeak = observation::State.peak;
    }
    Require(!observation::State.live && !observation::State.blocks, "parser and staged manifest allocations retire");
  }
  const auto parsed = reads.Check(InventoryIconIoPhase::ManifestRead);
  Require(parsed.parserUpper > 0 && parsed.payloadBytes == manifest.size() &&
              parsePeak <= parsed.parserUpper + parsed.ordinaryBytes,
          "actual manifest parser model and retained candidate cover the observed CPU peak");

  const std::array<GLenum, 5> unpackKeys{GL_PIXEL_UNPACK_BUFFER_BINDING, GL_UNPACK_ALIGNMENT,
      GL_UNPACK_ROW_LENGTH, GL_UNPACK_SKIP_ROWS, GL_UNPACK_SKIP_PIXELS};
  const auto unpackState = [&]
  {
    std::array<GLint, 5> state{};
    for (size_t i = 0; i < state.size(); ++i)
      state[i] = Binding(unpackKeys[i]);
    return state;
  };
  const auto original = unpackState();
  GLuint unpack = 0;
  glGenBuffers(1, &unpack);
  glBindBuffer(GL_PIXEL_UNPACK_BUFFER, unpack);
  glBufferData(GL_PIXEL_UNPACK_BUFFER, 4, nullptr, GL_STATIC_DRAW);
  glPixelStorei(GL_UNPACK_ALIGNMENT, 8);
  glPixelStorei(GL_UNPACK_ROW_LENGTH, 32);
  glPixelStorei(GL_UNPACK_SKIP_ROWS, 2);
  glPixelStorei(GL_UNPACK_SKIP_PIXELS, 3);
  const auto caller = unpackState();
  const auto boundTexture = Binding(GL_TEXTURE_BINDING_2D);
  reads.ledger = std::make_shared<UStbiAllocationLedger>();
  GLuint loaded = 0;
  {
    observation::Scope observed;
    UStbiAllocationScope codec(reads.ledger);
    Require(service.TryLoadIconTexture(kind, id, variant, fingerprint, size, loaded, &readProbe) &&
                !observation::State.live && !observation::State.blocks, "actual decoder uploads and retires all ordinary scratch");
  }
  UGlTextureOwner reloadedTexture(loaded);
  const auto decoded = reads.Check(InventoryIconIoPhase::PngDecode);
  const auto decode = reads.ledger->GetSnapshot();
  Require(decoded.ordinaryBytes == reads.ordinaryLive[static_cast<size_t>(InventoryIconIoPhase::PngDecode)] &&
              decoded.payloadBytes == icon_lifecycle_fixture::Bytes && !decode.live_request_bytes &&
              decode.peak_admitted_request_bytes >= decoded.codecBytes && unpackState() == caller &&
              Binding(GL_TEXTURE_BINDING_2D) == boundTexture && glGetError() == GL_NO_ERROR,
          "PNG upload borrows decoded pixels directly and restores caller unpack state");
  CheckPixels(reloadedTexture.Get());
  GLuint wrongSize = 5;
  Require(!service.TryLoadIconTexture(kind, id, variant, fingerprint, size * 2, wrongSize) && !wrongSize &&
              unpackState() == caller, "mismatched dimensions miss before creating a texture");

  for (std::size_t cutoff = 0; cutoff < decode.allocation_attempts; ++cutoff)
  {
    const auto ledger = std::make_shared<UStbiAllocationLedger>(SIZE_MAX, cutoff);
    GLuint result = 0;
    {
      UStbiAllocationScope codec(ledger);
      (void)service.TryLoadIconTexture(kind, id, variant, fingerprint, size, result);
    }
    const UGlTextureOwner image(result);
    const auto counts = ledger->GetSnapshot();
    Require(counts.refusals && !counts.live_request_bytes && !counts.reserved_request_bytes &&
                unpackState() == caller && Binding(GL_TEXTURE_BINDING_2D) == boundTexture,
            "every decoder refusal retires its temporary requests and preserves caller state");
    if (image.Get())
      CheckPixels(image.Get());
  }
  glBindBuffer(GL_PIXEL_UNPACK_BUFFER, GLuint(original[0]));
  glDeleteBuffers(1, &unpack);
  for (size_t i = 1; i < unpackKeys.size(); ++i)
    glPixelStorei(unpackKeys[i], original[i]);

  return {{"status", "PASS"}, {"manifest_parser_upper", parsed.parserUpper}, {"manifest_parse_peak", parsePeak},
          {"manifest_write_bytes", writing.ordinaryBytes}, {"manifest_file_bytes", writingFile.ordinaryBytes},
          {"encoded_request_bytes", encoded.codecBytes}, {"encode_peak_bytes", encode.peak_admitted_request_bytes},
          {"encoded_bytes", encoded.payloadBytes}, {"decoded_request_bytes", decoded.codecBytes},
          {"decode_peak_bytes", decode.peak_admitted_request_bytes}, {"encode_refusals", encode.allocation_attempts},
          {"decode_refusals", decode.allocation_attempts}, {"direct_rgba_upload", true},
          {"preserved_published_files", true}, {"unpack_state_restored", true}};
}
} // namespace cutum::inventory_icon_codec_fixture
