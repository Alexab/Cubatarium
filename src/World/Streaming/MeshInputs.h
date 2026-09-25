#pragma once

#include "World/Chunks/ChunkInputStamp.h"

#include <algorithm>
#include <array>
#include <cstdint>

namespace cutum
{

/// Q4 MeshInputs (Strategy A): geom/light/catalog only.
/// Visual boundary is ABSENT by Phase1–Q4a contract — if later policy adds it,
/// it MUST appear here AND in InputsStillValid symmetrically.
struct MeshInputs
{
  uint64_t content_revision{0};
  uint64_t light_revision{0};
  uint64_t material_catalog_revision{0};
  /// Exact mesher read set (center + 26 neighbors).
  std::array<ChunkInputStamp, kChunkMeshInputStampCount> halo_stamps{};
  bool stamps_valid{false};
};

/// Q4 LightInputs: read/write region stamps + propagation settings identity.
struct LightInputs
{
  std::array<ChunkInputStamp, 7> read_region_stamps{};
  std::array<ChunkInputStamp, 7> write_region_stamps{};
  uint64_t propagation_settings_revision{0};
  bool stamps_valid{false};
};

inline MeshInputs MeshInputsFromSnapshotStamps(
    const std::array<ChunkInputStamp, kChunkMeshInputStampCount> &stamps,
    bool valid,
    uint64_t catalog_revision)
{
  MeshInputs in;
  in.halo_stamps = stamps;
  in.stamps_valid = valid;
  in.material_catalog_revision = catalog_revision;
  if (valid)
  {
    in.content_revision = stamps[0].content;
    in.light_revision = stamps[0].light;
  }
  return in;
}

// Compatibility for callers that only model a center plus six face shell.
inline MeshInputs MeshInputsFromSnapshotStamps(
    const std::array<ChunkInputStamp, 7> &stamps, bool valid,
    uint64_t catalog_revision)
{
  MeshInputs in;
  std::copy(stamps.begin(), stamps.end(), in.halo_stamps.begin());
  in.stamps_valid = valid;
  in.material_catalog_revision = catalog_revision;
  if (valid)
  {
    in.content_revision = stamps[0].content;
    in.light_revision = stamps[0].light;
  }
  return in;
}

} // namespace cutum
