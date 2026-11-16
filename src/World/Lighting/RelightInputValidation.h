#pragma once

#include "World/Core/BlockWorld.h"
#include "World/Lighting/ChunkRelightSnapshot.h"

namespace cutum
{

enum class RelightInputValidation : uint8_t
{
  Current,
  MissingContext,
  Profile,
  Configuration,
  Catalog,
  ReadSet,
  UnknownInput,
};

/// Transient computation may still contain unknown inputs. A completed
/// calculation certificate must additionally require every input to be known.
inline RelightInputValidation ValidateRelightInputs(
    const RelightComputeResult &result, const UBlockWorld &world,
    const std::shared_ptr<const BlockDefinitionCatalog> &catalog,
    bool require_known_inputs = false, const ChunkInputStamp **stale = nullptr)
{
  if (stale)
    *stale = nullptr;
  if (!result.input_context_captured || result.read_set.empty())
    return RelightInputValidation::MissingContext;
  if (require_known_inputs && !catalog)
    return RelightInputValidation::MissingContext;
  const auto &captured = result.input_profile;
  const auto &current = world.GetVerticalProfile();
  if (!captured.IsValid() || !current.IsValid() ||
      captured.MinY != current.MinY || captured.MaxY != current.MaxY ||
      captured.SeaLevelY != current.SeaLevelY ||
      captured.BedrockTopY != current.BedrockTopY ||
      captured.DatumY != current.DatumY)
    return RelightInputValidation::Profile;
  if (result.input_lighting_configuration !=
      world.GetLightingConfigurationIdentity())
    return RelightInputValidation::Configuration;
  if (result.input_catalog != catalog)
    return RelightInputValidation::Catalog;
  for (const auto &stamp : result.read_set)
  {
    if (!world.InputStampLightDomainsStillMatch(stamp, result.include_skylight,
                                                result.include_block_light))
    {
      if (stale)
        *stale = &stamp;
      return RelightInputValidation::ReadSet;
    }
    if (require_known_inputs && (stamp.readsContent || stamp.readsLight) &&
        stamp.incarnation == 0 &&
        stamp.missingEvidence != MissingChunkEvidence::KnownAir)
    {
      if (stale)
        *stale = &stamp;
      return RelightInputValidation::UnknownInput;
    }
  }
  return RelightInputValidation::Current;
}

} // namespace cutum
