#include "World/Core/BlockWorld.h"
#include "Blocks/BlockDefinitionStorage.h"
#include "World/Chunks/ChunkBuffer.h"
#include "World/Math/FluidCellState.h"
#include "World/Physics/FluidKindPresetUtil.h"
#include "World/Physics/FluidPermeabilityUtil.h"

namespace cutum
{

bool UBlockWorld::IsKnownAbsentAirChunk(glm::ivec3 coord) const
{
  return !Chunks.GetChunk(coord) &&
         (coord.y < VerticalProfile.MinChunkY(CHUNK_SIZE) ||
          coord.y > VerticalProfile.MaxChunkY(CHUNK_SIZE) ||
          HasKnownOpenSkyAbsentProof(coord));
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

bool UBlockWorld::InputStampStillMatches(const ChunkInputStamp &stamp) const
{
  const UChunk *chunk = Chunks.GetChunk(stamp.coord);
  if (!stamp.Matches(chunk))
    return false;
  if (stamp.missingEvidence == MissingChunkEvidence::Untracked)
    return true;
  return !chunk &&
         IsKnownAbsentAirChunk(stamp.coord) ==
             (stamp.missingEvidence == MissingChunkEvidence::KnownAir);
}

bool UBlockWorld::InputStampLightDomainsStillMatch(const ChunkInputStamp &stamp,
                                                   bool reads_sky,
                                                   bool reads_block) const
{
  auto geometry = stamp;
  geometry.readsLight = false;
  if (!InputStampStillMatches(geometry))
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

void UBlockWorld::Clear() { Chunks.Clear(); }

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
