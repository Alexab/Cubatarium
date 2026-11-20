#pragma once

#include "World/Chunks/ChunkBuffer.h"
#include "World/Chunks/ChunkGenerationToken.h"
#include "World/Core/BlockWorld.h"
#include <memory>
#include <vector>

namespace cutum
{

class TerrainSliceLoadOwnership;
bool ApplyTerrainSliceLoad(UBlockWorld &world, glm::ivec3 coord,
                           const UChunkBuffer *decoded,
                           const LoadedAirSlice *absent,
                           const ChunkGenerationToken &token,
                           uint64_t current_sequence, bool cancelled,
                           TerrainSliceLoadOwnership *ownership);

/// Main-thread receipts for AIR data actually installed by one disk demand.
/// Duplicate results, pre-existing data and materialized edits are not owned.
/// Columns containing geometry retain their AIR through the normal column
/// save/eviction path; cancellation must not make that save incomplete.
/// No manager pointer, geometry/light snapshot, or filesystem authority
/// retained.
class TerrainSliceLoadOwnership
{
public:
  size_t GetAirInstallCount() const { return Air.size(); }
  std::vector<glm::ivec3> RetireAir(UChunkManager &manager)
  {
    std::vector<glm::ivec3> retired;
    if (ManagerIdentity.lock() != manager.GetInputRevisionIdentity())
      return retired;
    for (const auto &receipt : Air)
    {
      const auto field = manager.GetSectionLightField(receipt.coord);
      if (!manager.HasChunk(receipt.coord) &&
          !manager.HasResidentGeometryColumn(receipt.coord) &&
          manager.HasLoadedAirSlice(receipt.coord) && field &&
          field->GetIncarnation() == receipt.incarnation)
      {
        manager.RemoveChunk(receipt.coord);
        retired.push_back(receipt.coord);
      }
    }
    Air.clear();
    ManagerIdentity.reset();
    return retired;
  }

private:
  friend bool ApplyTerrainSliceLoad(UBlockWorld &, glm::ivec3,
                                    const UChunkBuffer *,
                                    const LoadedAirSlice *,
                                    const ChunkGenerationToken &, uint64_t,
                                    bool, TerrainSliceLoadOwnership *);
  void InstalledAir(const UChunkManager &manager, glm::ivec3 coord)
  {
    const auto identity = manager.GetInputRevisionIdentity();
    if (!Air.empty() && ManagerIdentity.lock() != identity)
      return;
    ManagerIdentity = identity;
    if (const auto field = manager.GetSectionLightField(coord))
      Air.push_back({coord, field->GetIncarnation()});
  }
  struct Receipt
  {
    glm::ivec3 coord;
    uint64_t incarnation;
  };
  std::weak_ptr<const uint64_t> ManagerIdentity;
  std::vector<Receipt> Air;
};

/// Main-thread commit after the caller has matched the actual demand owner.
/// A failed/default decode must never be confused with successful empty data.
inline bool
ApplyTerrainSliceLoad(UBlockWorld &world, glm::ivec3 coord,
                      const UChunkBuffer *decoded, const LoadedAirSlice *absent,
                      const ChunkGenerationToken &token,
                      uint64_t current_sequence, bool cancelled,
                      TerrainSliceLoadOwnership *ownership = nullptr)
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
    if (manager.HasChunk(coord) || manager.HasLoadedAirSlice(coord))
      return true;
    if (!manager.InstallLoadedAirSlice(air, token, current_sequence, cancelled))
      return false;
    if (ownership)
      ownership->InstalledAir(manager, coord);
    return true;
  };
  if (decoded)
  {
    if (!decoded->HasDecodedTerrainFrame(coord))
      return false;
    if (const auto air = decoded->GetDecodedAirSlice(coord))
      return install_air(*air);
    if (!decoded->HasGeometryPayload())
      return false;
    // A retry reads an immutable disk base. It cannot replay that base over
    // a newer in-memory edit or a different resident producer. Existing
    // decoded data already has a complete base; unvalidated data stays unknown.
    if (manager.HasLoadedTerrainSlice(coord))
      return manager.HasDecodedTerrainSlice(coord);
    decoded->ApplyToChunk(world, coord);
    return true;
  }
  return absent && install_air(*absent);
}

} // namespace cutum
