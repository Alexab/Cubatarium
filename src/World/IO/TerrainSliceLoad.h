#pragma once

#include "World/Chunks/ChunkBuffer.h"
#include "World/Chunks/ChunkGenerationToken.h"
#include "World/Core/BlockWorld.h"

namespace cutum
{

/// Main-thread commit after the caller has matched the actual demand owner.
/// A failed/default decode must never be confused with successful empty data.
inline bool ApplyTerrainSliceLoad(UBlockWorld &world, glm::ivec3 coord,
                                  const UChunkBuffer *decoded,
                                  const LoadedAirSlice *absent,
                                  const ChunkGenerationToken &token,
                                  uint64_t current_sequence, bool cancelled)
{
  if (cancelled || !token.IsValidFor({coord.x, 0, coord.z}, current_sequence))
    return false;
  auto &manager = world.GetChunkManager();
  const auto install_air = [&](const LoadedAirSlice &air)
  {
    if (air.GetCoord() != coord)
      return false;
    // A duplicate or a resident edit is authoritative; an empty disk result
    // cannot replace its field incarnation or discard local voxel changes.
    return manager.HasChunk(coord) || manager.HasLoadedAirSlice(coord) ||
           manager.InstallLoadedAirSlice(air, token, current_sequence,
                                         cancelled);
  };
  if (decoded)
  {
    if (const auto air = decoded->GetDecodedAirSlice(coord))
      return install_air(*air);
    if (!decoded->HasGeometryPayload())
      return false;
    decoded->ApplyToChunk(world, coord);
    return true;
  }
  return absent && install_air(*absent);
}

} // namespace cutum
