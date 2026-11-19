#pragma once

#include "World/Core/BlockWorld.h"
#include "World/Lighting/LightCacheCertificate.h"
#include "World/Lighting/PersistedSectionLightTypes.h"
#include "World/Lighting/RelightInputValidation.h"
#include <limits>
#include <map>
#include <tuple>

namespace cutum
{
namespace persisted_light_detail
{
inline uint64_t HashBytes(std::string_view bytes)
{
  uint64_t hash = 14695981039346656037ull;
  for (const unsigned char byte : bytes)
  {
    hash ^= byte;
    hash *= 1099511628211ull;
  }
  return hash;
}

inline std::optional<uint64_t> GeometryContent(const UBlockWorld &world,
                                               glm::ivec3 coord)
{
  const auto *chunk = world.GetChunkManager().GetChunk(coord);
  if (!chunk && !world.IsKnownAbsentAirChunk(coord))
    return std::nullopt;
  return SectionGeometryContentHash(
      chunk && !chunk->IsAirOnly() ? &chunk->GetData() : nullptr);
}

inline auto Key(glm::ivec3 coord)
{
  return std::tuple(coord.x, coord.y, coord.z);
}

inline nlohmann::json Payload(const PersistedSectionLight &receipt)
{
  nlohmann::json inputs = nlohmann::json::array();
  for (const auto &input : receipt.inputs)
    inputs.push_back(
        {input.coord.x, input.coord.y, input.coord.z, input.content});
  std::string packed;
  packed.reserve(SectionLightVolume::CellCount * 2);
  constexpr char hex[] = "0123456789abcdef";
  for (const uint8_t cell : receipt.volume.cells)
  {
    packed.push_back(hex[cell >> 4]);
    packed.push_back(hex[cell & 15]);
  }
  const auto &p = receipt.profile;
  return {{"format_version", 1},
          {"coord", {receipt.coord.x, receipt.coord.y, receipt.coord.z}},
          {"profile", {p.MinY, p.MaxY, p.SeaLevelY, p.BedrockTopY, p.DatumY}},
          {"configuration", receipt.configuration},
          {"light_signature", receipt.signature},
          {"inputs", std::move(inputs)},
          {"padded_light", std::move(packed)}};
}

inline uint64_t Checksum(const PersistedSectionLight &receipt)
{
  // Corruption checksum, not authentication of hostile save files.
  return HashBytes(Payload(receipt).dump());
}

inline bool CoversSamplingDomain(const PersistedSectionLight &receipt)
{
  if (!receipt.profile.IsValid() || receipt.inputs.empty() ||
      receipt.inputs.size() > PersistedSectionLight::MaxDependencies ||
      !receipt.profile.SliceBounds(receipt.coord.y, CHUNK_SIZE))
    return false;
  std::map<std::tuple<int, int, int>, uint64_t> inputs;
  for (const auto &input : receipt.inputs)
    if (!inputs.emplace(Key(input.coord), input.content).second)
      return false;
  // Both domains are restored together: the radius-2 output's complete
  // geometry halo and all direct-sky context to the configured ceiling are
  // mandatory even if a damaged receipt removed an otherwise valid row.
  const int64_t bottom = std::max<int64_t>(
      receipt.profile.MinChunkY(CHUNK_SIZE), int64_t(receipt.coord.y) - 1);
  const int64_t top = receipt.profile.MaxChunkY(CHUNK_SIZE);
  if ((top - bottom + 1) * 9 > PersistedSectionLight::MaxDependencies)
    return false;
  for (int dx = -1; dx <= 1; ++dx)
    for (int dz = -1; dz <= 1; ++dz)
    {
      const int64_t x = int64_t(receipt.coord.x) + dx;
      const int64_t z = int64_t(receipt.coord.z) + dz;
      if (x < std::numeric_limits<int>::min() ||
          x > std::numeric_limits<int>::max() ||
          z < std::numeric_limits<int>::min() ||
          z > std::numeric_limits<int>::max())
        return false;
      for (int64_t y = bottom; y <= top; ++y)
        if (!inputs.count(std::tuple(int(x), int(y), int(z))))
          return false;
    }
  return true;
}

inline bool Integer(const nlohmann::json &value, int &out)
{
  if (!value.is_number_integer())
    return false;
  if (value.is_number_unsigned())
  {
    if (value.get<uint64_t>() > uint64_t(std::numeric_limits<int>::max()))
      return false;
  }
  else if (value.get<int64_t>() < std::numeric_limits<int>::min() ||
           value.get<int64_t>() > std::numeric_limits<int>::max())
    return false;
  out = value.get<int>();
  return true;
}

inline bool Unsigned(const nlohmann::json &value, uint64_t &out)
{
  if (!value.is_number_integer() ||
      (!value.is_number_unsigned() && value.get<int64_t>() < 0))
    return false;
  out = value.get<uint64_t>();
  return true;
}
} // namespace persisted_light_detail

inline nlohmann::json EncodeSectionLight(const PersistedSectionLight &receipt)
{
  auto payload = persisted_light_detail::Payload(receipt);
  payload["checksum"] = receipt.checksum;
  return payload;
}

inline std::optional<PersistedSectionLight>
DecodeSectionLight(const nlohmann::json &data)
{
  using namespace persisted_light_detail;
  PersistedSectionLight receipt;
  try
  {
    int version = 0;
    if (!Integer(data.at("format_version"), version) || version != 1 ||
        !data.at("coord").is_array() || data.at("coord").size() != 3 ||
        !data.at("profile").is_array() || data.at("profile").size() != 5 ||
        !data.at("inputs").is_array() ||
        data.at("inputs").size() > PersistedSectionLight::MaxDependencies ||
        !Unsigned(data.at("configuration"), receipt.configuration) ||
        !Unsigned(data.at("checksum"), receipt.checksum))
      return std::nullopt;
    for (int axis = 0; axis < 3; ++axis)
      if (!Integer(data.at("coord")[axis], receipt.coord[axis]))
        return std::nullopt;
    int *profile[] = {&receipt.profile.MinY, &receipt.profile.MaxY,
                      &receipt.profile.SeaLevelY, &receipt.profile.BedrockTopY,
                      &receipt.profile.DatumY};
    for (int n = 0; n < 5; ++n)
      if (!Integer(data.at("profile")[n], *profile[n]))
        return std::nullopt;
    receipt.signature = data.at("light_signature").get<std::string>();
    if (receipt.signature.empty() || receipt.signature.size() > 256)
      return std::nullopt;
    for (const auto &row : data.at("inputs"))
    {
      PersistedSectionLight::Input input;
      if (!row.is_array() || row.size() != 4 ||
          !Unsigned(row[3], input.content))
        return std::nullopt;
      for (int axis = 0; axis < 3; ++axis)
        if (!Integer(row[axis], input.coord[axis]))
          return std::nullopt;
      if (!receipt.inputs.empty() &&
          Key(input.coord) <= Key(receipt.inputs.back().coord))
        return std::nullopt;
      receipt.inputs.push_back(input);
    }
    const auto &hex = data.at("padded_light").get_ref<const std::string &>();
    if (hex.size() != SectionLightVolume::CellCount * 2)
      return std::nullopt;
    const auto nibble = [](char c)
    {
      return c >= '0' && c <= '9'   ? c - '0'
             : c >= 'a' && c <= 'f' ? c - 'a' + 10
                                    : -1;
    };
    for (size_t n = 0; n < receipt.volume.cells.size(); ++n)
    {
      const int hi = nibble(hex[n * 2]), lo = nibble(hex[n * 2 + 1]);
      if (hi < 0 || lo < 0)
        return std::nullopt;
      receipt.volume.cells[n] = static_cast<uint8_t>((hi << 4) | lo);
    }
    if (!CoversSamplingDomain(receipt) || Checksum(receipt) != receipt.checksum)
      return std::nullopt;
  }
  catch (const nlohmann::json::exception &)
  {
    return std::nullopt;
  }
  return receipt;
}

inline std::optional<PersistedSectionLight> CaptureSectionLight(
    const UBlockWorld &world, glm::ivec3 coord,
    const std::shared_ptr<const BlockDefinitionCatalog> &catalog)
{
  using namespace persisted_light_detail;
  if (!catalog || !world.HasRecordedLightSamplingOutput(coord))
    return std::nullopt;
  const auto field = world.GetChunkManager().GetSectionLightField(coord);
  const auto volume = field->GetPaddedData();
  PersistedSectionLight receipt;
  receipt.coord = coord;
  receipt.profile = world.GetVerticalProfile();
  receipt.configuration = world.GetLightingConfigurationIdentity();
  receipt.signature = MakeLightCacheSignature(receipt.profile, *catalog,
                                              int(receipt.configuration));
  std::map<std::tuple<int, int, int>, PersistedSectionLight::Input> inputs;
  for (const auto domain : {LightDomain::Sky, LightDomain::Block})
  {
    const auto context = world.GetRecordedLightInputContext(coord, domain);
    const auto &p = receipt.profile;
    if (!context || !context->content_certificate ||
        !context->retained_inputs_geometry_only || !context->input_catalog ||
        !LightCacheCatalogsEquivalent(*context->input_catalog, *catalog) ||
        context->input_lighting_configuration != receipt.configuration ||
        context->input_profile.MinY != p.MinY ||
        context->input_profile.MaxY != p.MaxY ||
        context->input_profile.SeaLevelY != p.SeaLevelY ||
        context->input_profile.BedrockTopY != p.BedrockTopY ||
        context->input_profile.DatumY != p.DatumY)
      return std::nullopt;
    for (const auto &input : context->content_certificate->Inputs())
    {
      const auto found = inputs.find(Key(input.coord));
      if (found != inputs.end())
      {
        if (found->second.content != input.content)
          return std::nullopt; // channels came from incompatible geometry
        continue;
      }
      if (inputs.size() >= PersistedSectionLight::MaxDependencies)
        return std::nullopt;
      inputs.emplace(Key(input.coord), input);
    }
  }
  for (const auto &entry : inputs)
    receipt.inputs.push_back(entry.second);
  if (!CoversSamplingDomain(receipt))
    return std::nullopt;
  // Export the computed artifact's original inputs, not a statement about
  // present geometry. Reload must independently validate every saved content.
  if (world.GetChunkManager().GetSectionLightField(coord) != field ||
      field->GetPaddedData() != volume ||
      !world.HasRecordedLightSamplingOutput(coord))
    return std::nullopt;
  receipt.volume = *volume;
  receipt.checksum = Checksum(receipt);
  return receipt;
}

enum class SectionLightRestoreResult
{
  Restored,
  InputsUnavailable,
  Rejected
};

inline SectionLightRestoreResult RestoreSectionLight(
    UBlockWorld &world, const PersistedSectionLight &receipt,
    const std::shared_ptr<const BlockDefinitionCatalog> &catalog,
    SectionLightRestoreDiagnostic *diagnostic)
{
  using namespace persisted_light_detail;
  if (diagnostic)
    *diagnostic = {};
  const auto fail = [&](SectionLightRestoreFailure reason,
                        SectionLightRestoreResult result =
                            SectionLightRestoreResult::Rejected)
  {
    if (diagnostic)
      diagnostic->reason = reason;
    return result;
  };
  const auto &p = world.GetVerticalProfile();
  const auto &saved = receipt.profile;
  if (!catalog)
    return fail(SectionLightRestoreFailure::Catalog);
  if (p.MinY != saved.MinY || p.MaxY != saved.MaxY ||
      p.SeaLevelY != saved.SeaLevelY || p.BedrockTopY != saved.BedrockTopY ||
      p.DatumY != saved.DatumY)
    return fail(SectionLightRestoreFailure::Profile);
  if (receipt.configuration != world.GetLightingConfigurationIdentity())
    return fail(SectionLightRestoreFailure::Configuration);
  if (receipt.signature !=
      MakeLightCacheSignature(p, *catalog, int(receipt.configuration)))
    return fail(SectionLightRestoreFailure::Materials);
  if (!CoversSamplingDomain(receipt))
    return fail(SectionLightRestoreFailure::Footprint);
  if (Checksum(receipt) != receipt.checksum)
    return fail(SectionLightRestoreFailure::Checksum);
  RelightComputeResult context;
  context.input_catalog = catalog;
  context.input_profile = p;
  context.input_lighting_configuration = receipt.configuration;
  context.input_context_captured = context.inputs_complete = true;
  // First reject unavailable inputs without hashing large resident sections.
  for (const auto &input : receipt.inputs)
  {
    auto stamp = world.CaptureInputStamp(input.coord, false, true);
    if (!stamp.incarnation &&
        stamp.missingEvidence != MissingChunkEvidence::KnownAir)
    {
      if (diagnostic)
      {
        diagnostic->has_input = true;
        diagnostic->input_coord = input.coord;
      }
      return fail(SectionLightRestoreFailure::UnknownInput,
                  SectionLightRestoreResult::InputsUnavailable);
    }
    context.read_set.push_back(stamp);
  }
  for (const auto &input : receipt.inputs)
  {
    const auto current = GeometryContent(world, input.coord);
    if (current != input.content)
    {
      if (diagnostic)
      {
        diagnostic->has_input = true;
        diagnostic->input_coord = input.coord;
        diagnostic->expected = input.content;
        diagnostic->current_known = current.has_value();
        diagnostic->current = current.value_or(0);
      }
      return fail(SectionLightRestoreFailure::ContentMismatch);
    }
  }
  if (ValidateRelightInputs(context, world, catalog, true) !=
      RelightInputValidation::Current)
    return fail(SectionLightRestoreFailure::ReadSet);
  RelightChunkLightData data;
  data.coord = receipt.coord;
  data.padded_light =
      std::make_shared<const SectionLightVolume>(receipt.volume);
  for (int z = 0; z < CHUNK_SIZE; ++z)
    for (int y = 0; y < CHUNK_SIZE; ++y)
      for (int x = 0; x < CHUNK_SIZE; ++x)
        data.light_packed[UChunk::LocalIndex({x, y, z})] =
            receipt.volume.cells[SectionLightVolume::Index(x, y, z)];
  context.chunks.push_back(std::move(data));
  // The disk checksum, material/profile contract, mandatory footprint and
  // every current geometry content were validated above. Bind this imported
  // artifact to fresh runtime stamps so a later eviction can export it again.
  auto certificate =
      std::shared_ptr<RelightContentCertificate>(new RelightContentCertificate);
  certificate->Stamps = context.read_set;
  certificate->Contents = receipt.inputs;
  certificate->Profile = p;
  certificate->Configuration = receipt.configuration;
  certificate->Catalog = catalog;
  certificate->Sky = certificate->Block = true;
  certificate->Outputs.emplace(
      receipt.coord,
      std::array<uint64_t, 2>{SectionLightDomainHash(receipt.volume, 0),
                              SectionLightDomainHash(receipt.volume, 1)});
  context.content_certificate = std::move(certificate);
  world.GetChunkManager().ReplaceComputedSectionLightVolume(
      receipt.coord, receipt.volume, true, true);
  return world.RecordLightCalculation(context, catalog) == 1
             ? SectionLightRestoreResult::Restored
             : fail(SectionLightRestoreFailure::Install);
}
} // namespace cutum
