#include "World/Core/BlockWorld.h"
#include "Blocks/BlockDefinitionStorage.h"
#include "World/Chunks/ChunkBuffer.h"
#include "World/Lighting/RelightInputValidation.h"
#include "World/Math/FluidCellState.h"
#include "World/Physics/FluidKindPresetUtil.h"
#include "World/Physics/FluidPermeabilityUtil.h"
#include <unordered_set>

namespace cutum
{

int UBlockWorld::RecordLightCalculation(
    const RelightComputeResult &result,
    const std::shared_ptr<const BlockDefinitionCatalog> &current_catalog)
{
  if (ValidateRelightInputs(result, *this, current_catalog) !=
      RelightInputValidation::Current)
    return 0;
  // Retain provenance once per batch, never its output voxel arrays or retry
  // payload. All section/domain records share this immutable input context.
  auto context = std::make_shared<RelightComputeResult>();
  context->read_set = result.read_set;
  context->input_catalog = result.input_catalog;
  context->input_profile = result.input_profile;
  context->input_lighting_configuration = result.input_lighting_configuration;
  context->input_context_captured = result.input_context_captured;
  context->inputs_complete =
      ValidateRelightInputs(result, *this, current_catalog, true) ==
      RelightInputValidation::Current;
  context->unknown_input_chunks = result.unknown_input_chunks;
  context->include_skylight = result.include_skylight;
  context->include_block_light = result.include_block_light;
  int recorded = 0;
  for (const auto &data : result.chunks)
  {
    auto *chunk = Chunks.GetChunk(data.coord);
    if (!chunk)
      continue;
    bool owns_geometry = false;
    for (const auto &stamp : result.read_set)
      if (stamp.coord == data.coord && stamp.readsContent &&
          stamp.incarnation == chunk->GetIncarnation())
      {
        owns_geometry = true;
        break;
      }
    if (!owns_geometry)
      continue;
    bool any = false;
    for (int domain = 0; domain < 2; ++domain)
    {
      const bool sky = domain == 0;
      if (!(sky ? result.include_skylight : result.include_block_light))
        continue;
      const uint8_t mask = sky ? 0x0f : 0xf0;
      bool installed = true;
      for (int i = 0; i < CHUNK_VOLUME; ++i)
        if ((chunk->GetLightData()[i] & mask) != (data.light_packed[i] & mask))
        {
          installed = false;
          break;
        }
      if (!installed)
        continue; // publication was omitted or overwritten
      auto &proof = chunk->LightProofs[domain];
      proof.input = context;
      proof.owner = LightCalculationOwner;
      proof.incarnation = chunk->GetIncarnation();
      proof.field_revision =
          sky ? chunk->GetSkyLightRevision() : chunk->GetBlockLightRevision();
      any = true;
    }
    recorded += any;
  }
  return recorded;
}

LightCalculationState UBlockWorld::GetLightCalculationState(
    glm::ivec3 coord, LightDomain domain,
    const std::shared_ptr<const BlockDefinitionCatalog> &current_catalog) const
{
  const auto *chunk = Chunks.GetChunk(coord);
  if (!chunk)
    return LightCalculationState::Missing;
  const auto &proof = chunk->LightProofs[domain == LightDomain::Sky ? 0 : 1];
  if (!proof.input || proof.owner != LightCalculationOwner ||
      proof.incarnation != chunk->GetIncarnation() ||
      proof.field_revision != (domain == LightDomain::Sky
                                   ? chunk->GetSkyLightRevision()
                                   : chunk->GetBlockLightRevision()) ||
      ValidateRelightInputs(*proof.input, *this, current_catalog) !=
          RelightInputValidation::Current)
    return LightCalculationState::Missing;
  // Every input was known at record time and the full read set still matches.
  // Rechecking the same known stamps a second time adds no evidence.
  return proof.input->inputs_complete ? LightCalculationState::Complete
                                      : LightCalculationState::Transient;
}

bool UBlockWorld::LightCalculationsStillCurrent(
    const std::vector<glm::ivec3> &sections, bool sky, bool block,
    const std::shared_ptr<const BlockDefinitionCatalog> &current_catalog) const
{
  std::unordered_set<const RelightComputeResult *> contexts;
  bool any = false;
  for (const auto coord : sections)
  {
    const auto *chunk = Chunks.GetChunk(coord);
    if (!chunk)
      continue;
    any = true;
    for (int domain = 0; domain < 2; ++domain)
    {
      if (!(domain == 0 ? sky : block))
        continue;
      const auto &proof = chunk->LightProofs[domain];
      if (!proof.input || proof.owner != LightCalculationOwner ||
          proof.incarnation != chunk->GetIncarnation() ||
          proof.field_revision != (domain == 0
                                       ? chunk->GetSkyLightRevision()
                                       : chunk->GetBlockLightRevision()))
        return false;
      contexts.insert(proof.input.get());
    }
  }
  if (!any || (!sky && !block))
    return false;
  for (const auto *context : contexts)
    if (ValidateRelightInputs(*context, *this, current_catalog) !=
        RelightInputValidation::Current)
      return false;
  return true;
}

bool UBlockWorld::HasCompleteLightCalculation(
    glm::ivec3 coord,
    const std::shared_ptr<const BlockDefinitionCatalog> &current_catalog) const
{
  if (GetLightCalculationState(coord, LightDomain::Sky, current_catalog) !=
      LightCalculationState::Complete)
    return false;
  const auto *chunk = Chunks.GetChunk(coord);
  const auto &sky = chunk->LightProofs[0];
  const auto &block = chunk->LightProofs[1];
  if (block.input == sky.input && block.owner == LightCalculationOwner &&
      block.incarnation == chunk->GetIncarnation() &&
      block.field_revision == chunk->GetBlockLightRevision())
    return true;
  return GetLightCalculationState(coord, LightDomain::Block, current_catalog) ==
         LightCalculationState::Complete;
}

bool UBlockWorld::HasKnownOpenSkyAbsentProof(glm::ivec3 coord) const
{
  if (KnownAirColumnFloorQuery)
  {
    const auto floor = KnownAirColumnFloorQuery({coord.x, 0, coord.z});
    return floor && coord.y >= *floor;
  }
  return KnownOpenSkyAbsentQuery && KnownOpenSkyAbsentQuery(coord);
}

bool UBlockWorld::IsKnownAbsentAirChunk(glm::ivec3 coord,
                                        InputStampValidationBatch *batch) const
{
  if (Chunks.GetChunk(coord))
    return false;
  if (coord.y < VerticalProfile.MinChunkY(CHUNK_SIZE) ||
      coord.y > VerticalProfile.MaxChunkY(CHUNK_SIZE))
    return true;
  if (!batch || !KnownAirColumnFloorQuery)
    return HasKnownOpenSkyAbsentProof(coord);
  if (batch->owner != this)
  {
    batch->air_floors.clear();
    batch->owner = this;
  }
  const glm::ivec3 column(coord.x, 0, coord.z);
  auto it = batch->air_floors.find(column);
  if (it == batch->air_floors.end())
    it = batch->air_floors.emplace(column, KnownAirColumnFloorQuery(column))
             .first;
  return it->second && coord.y >= *it->second;
}

ChunkInputStamp UBlockWorld::CaptureInputStamp(glm::ivec3 coord,
                                               bool reads_light,
                                               bool reads_content) const
{
  const UChunk *chunk = Chunks.GetChunk(coord);
  auto stamp =
      ChunkInputStamp::Capture(coord, chunk, reads_light, reads_content);
  if (!chunk && (reads_light || reads_content))
    stamp.missingEvidence = IsKnownAbsentAirChunk(coord)
                                ? MissingChunkEvidence::KnownAir
                                : MissingChunkEvidence::Unknown;
  return stamp;
}

bool UBlockWorld::InputStampStillMatches(const ChunkInputStamp &stamp,
                                         InputStampValidationBatch *batch) const
{
  const UChunk *chunk = Chunks.GetChunk(stamp.coord);
  if (!stamp.Matches(chunk))
    return false;
  if (stamp.missingEvidence == MissingChunkEvidence::Untracked)
    return true;
  return !chunk &&
         IsKnownAbsentAirChunk(stamp.coord, batch) ==
             (stamp.missingEvidence == MissingChunkEvidence::KnownAir);
}

bool UBlockWorld::InputStampLightDomainsStillMatch(
    const ChunkInputStamp &stamp, bool reads_sky, bool reads_block,
    InputStampValidationBatch *batch) const
{
  auto geometry = stamp;
  geometry.readsLight = false;
  if (!InputStampStillMatches(geometry, batch))
    return false;
  if (!stamp.readsLight || (!reads_sky && !reads_block))
    return true;
  const UChunk *chunk = Chunks.GetChunk(stamp.coord);
  return stamp.incarnation == (chunk ? chunk->GetIncarnation() : 0) &&
         (!reads_sky ||
          stamp.skyLight == (chunk ? chunk->GetSkyLightRevision() : 0)) &&
         (!reads_block ||
          stamp.blockLight == (chunk ? chunk->GetBlockLightRevision() : 0));
}

BlockId UBlockWorld::GetBlock(glm::ivec3 pos) const
{
  return Chunks.GetBlock(pos);
}

BlockQueryResult UBlockWorld::QueryBlock(glm::ivec3 pos) const
{
  return Chunks.QueryBlock(pos);
}

FluidCellState UBlockWorld::GetFluidState(glm::ivec3 pos) const
{
  return Chunks.GetFluidState(pos);
}

void UBlockWorld::SetBlock(glm::ivec3 pos, BlockId Id)
{
  if (Id == BLOCK_AIR)
  {
    Chunks.SetBlock(pos, BLOCK_AIR);
    Chunks.ClearFluidState(pos);
    if (CaptureBuffer)
    {
      CaptureBuffer->SetBlock(pos, BLOCK_AIR);
    }
    return;
  }
  Chunks.SetBlock(pos, Id);
  if (FluidDefinitions != nullptr)
  {
    if (const BlockDefinition *def = FluidDefinitions->GetByIdFast(Id))
    {
      if (def->Physics.IsLiquid)
      {
        const FluidKind kind = FluidKindFromDefinition(def);
        Chunks.SetFluidState(pos, FluidCellState::Source().WithKind(kind));
      }
      else if (!IsFluidPermeableFromDefinition(Id, def,
                                               /*is_liquid=*/false))
      {
        // W6.2 (water audit E-3): a solid placed into water annihilates the
        // water — the stale fluid byte under the solid must not persist to
        // disk and resurrect when the block is later replaced by decor.
        Chunks.ClearFluidState(pos);
      }
    }
  }
  if (CaptureBuffer)
  {
    CaptureBuffer->SetBlock(pos, Id);
    CaptureBuffer->SetFluidPacked(pos,
                                  PackFluidCellState(Chunks.GetFluidState(pos)));
  }
}

void UBlockWorld::SetBlockInChunk(UChunk &chunk, glm::ivec3 pos, BlockId Id)
{
  const glm::ivec3 local = UChunkManager::WorldToLocal(pos);
  if (Id == BLOCK_AIR)
  {
    chunk.SetBlockLocal(local, BLOCK_AIR);
    chunk.ClearFluidLocal(local);
    if (CaptureBuffer)
    {
      CaptureBuffer->SetBlock(pos, BLOCK_AIR);
    }
    return;
  }
  chunk.SetBlockLocal(local, Id);
  if (FluidDefinitions != nullptr)
  {
    if (const BlockDefinition *def = FluidDefinitions->GetByIdFast(Id))
    {
      if (def->Physics.IsLiquid)
      {
        const FluidKind kind = FluidKindFromDefinition(def);
        chunk.SetFluidLocal(local, FluidCellState::Source().WithKind(kind));
      }
      else if (!IsFluidPermeableFromDefinition(Id, def,
                                               /*is_liquid=*/false))
      {
        // W6.2: clear the stale fluid byte under solids (see SetBlock).
        chunk.ClearFluidLocal(local);
      }
    }
  }
  if (CaptureBuffer)
  {
    CaptureBuffer->SetBlock(pos, Id);
    CaptureBuffer->SetFluidPacked(
        pos, PackFluidCellState(chunk.GetFluidLocal(local)));
  }
}

void UBlockWorld::SetFluidState(glm::ivec3 pos, FluidCellState state)
{
  Chunks.SetFluidState(pos, state);
  if (CaptureBuffer)
  {
    CaptureBuffer->SetFluidPacked(pos, PackFluidCellState(state));
  }
}

void UBlockWorld::SetFluidStateInChunk(UChunk &chunk, glm::ivec3 pos,
                                       FluidCellState state)
{
  const glm::ivec3 local = UChunkManager::WorldToLocal(pos);
  chunk.SetFluidLocal(local, state);
  if (CaptureBuffer)
  {
    CaptureBuffer->SetFluidPacked(pos, PackFluidCellState(state));
  }
}

void UBlockWorld::ClearFluidState(glm::ivec3 pos)
{
  Chunks.ClearFluidState(pos);
  if (CaptureBuffer)
  {
    CaptureBuffer->SetFluidPacked(pos, 0);
  }
}

bool UBlockWorld::IsAir(glm::ivec3 pos) const
{
  return GetBlock(pos) == BLOCK_AIR;
}

void UBlockWorld::Clear()
{
  Chunks.Clear();
  LightCalculationOwner = std::make_shared<const int>(0);
}

size_t UBlockWorld::CountNonAir() const
{
  size_t count = 0;
  ForEachBlock([&count](glm::ivec3, BlockId) { ++count; });
  return count;
}

bool UBlockWorld::HasAnyNonAir() const
{
  bool found = false;
  ForEachBlock([&found](glm::ivec3, BlockId id)
               {
                 if (id != BLOCK_AIR)
                 {
                   found = true;
                 }
               });
  return found;
}

void UBlockWorld::ForEachBlock(
    const std::function<void(glm::ivec3, BlockId)> &fn) const
{
  Chunks.ForEachBlock(fn);
}

} // namespace cutum
