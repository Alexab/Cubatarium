#pragma once

#include "World/Chunks/ChunkInputStamp.h"
#include "World/Lighting/SectionLightVolume.h"
#include "World/Math/WorldVerticalProfile.h"
#include <array>
#include <cstddef>
#include <memory>
#include <utility>

namespace cutum
{
class UBlockWorld;
struct BlockDefinitionCatalog;

/// Geometry-only provenance of one published section. Invalid by default;
/// raw legacy mesh bytes cannot invent a compatible physical-light binding.
struct SectionGeometryRenderInputs
{
  glm::ivec3 coord{0};
  std::shared_ptr<const uint64_t> world_geometry_identity;
  WorldVerticalProfile profile;
  std::shared_ptr<const BlockDefinitionCatalog> catalog;
  std::array<ChunkInputStamp, 7> stamps{};
  bool valid{false};

  static SectionGeometryRenderInputs
  FromStamps(glm::ivec3 coord, std::shared_ptr<const uint64_t> world_identity,
             WorldVerticalProfile profile,
             std::shared_ptr<const BlockDefinitionCatalog> catalog,
             const std::array<ChunkInputStamp, 7> &stamps)
  {
    SectionGeometryRenderInputs result;
    result.coord = coord;
    result.world_geometry_identity = std::move(world_identity);
    result.profile = profile;
    result.catalog = std::move(catalog);
    result.stamps = stamps;
    if (!result.world_geometry_identity || !result.catalog ||
        !profile.IsValid() || stamps[0].coord != coord)
      return result;
    unsigned faces = 0;
    for (size_t n = 0; n < stamps.size(); ++n)
    {
      const auto &stamp = stamps[n];
      if (!stamp.readsContent ||
          (!stamp.incarnation &&
           stamp.missingEvidence != MissingChunkEvidence::KnownAir))
        return result;
      size_t output = 0;
      if (n)
      {
        const int64_t dx = int64_t(stamp.coord.x) - coord.x;
        const int64_t dy = int64_t(stamp.coord.y) - coord.y;
        const int64_t dz = int64_t(stamp.coord.z) - coord.z;
        int face = -1;
        if (dx == -1 && dy == 0 && dz == 0)
          face = 0;
        if (dx == 1 && dy == 0 && dz == 0)
          face = 1;
        if (dx == 0 && dy == -1 && dz == 0)
          face = 2;
        if (dx == 0 && dy == 1 && dz == 0)
          face = 3;
        if (dx == 0 && dy == 0 && dz == -1)
          face = 4;
        if (dx == 0 && dy == 0 && dz == 1)
          face = 5;
        if (face < 0 || (faces & (1u << face)))
          return result;
        faces |= 1u << face;
        output = static_cast<size_t>(face + 1);
      }
      result.stamps[output] = stamp;
      auto &geometry = result.stamps[output];
      geometry.readsLight = false;
      geometry.light = geometry.skyLight = geometry.blockLight = 0;
      geometry.lightIncarnation = 0;
    }
    result.valid = faces == 63;
    return result;
  }

  bool Matches(const SectionGeometryRenderInputs &other) const
  {
    if (!valid || !other.valid || coord != other.coord ||
        world_geometry_identity != other.world_geometry_identity ||
        catalog != other.catalog || profile.MinY != other.profile.MinY ||
        profile.MaxY != other.profile.MaxY ||
        profile.SeaLevelY != other.profile.SeaLevelY ||
        profile.BedrockTopY != other.profile.BedrockTopY ||
        profile.DatumY != other.profile.DatumY)
      return false;
    for (size_t n = 0; n < stamps.size(); ++n)
    {
      const auto &a = stamps[n];
      const auto &b = other.stamps[n];
      if (!a.readsContent || !b.readsContent || a.coord != b.coord ||
          a.incarnation != b.incarnation || a.content != b.content ||
          a.missingEvidence != b.missingEvidence)
        return false;
    }
    return true;
  }
};

/// Immutable certified padded field captured by its owning world. Retained
/// snapshots may accompany retained predecessor geometry; they never become
/// authority for current world data just because their bytes remain alive.
class SectionRenderLightSnapshot
{
public:
  SectionRenderLightSnapshot(const SectionRenderLightSnapshot &) = default;
  SectionRenderLightSnapshot(SectionRenderLightSnapshot &&) = default;
  SectionRenderLightSnapshot &
  operator=(const SectionRenderLightSnapshot &) = default;
  SectionRenderLightSnapshot &
  operator=(SectionRenderLightSnapshot &&) = default;
  const SectionGeometryRenderInputs &GeometryInputs() const { return Geometry; }
  const std::shared_ptr<const SectionLightVolume> &Volume() const
  {
    return Data;
  }
  uint64_t FieldIncarnation() const { return Incarnation; }
  uint64_t FieldRevision() const { return Revision; }
  bool MatchesGeometry(const SectionGeometryRenderInputs &geometry) const
  {
    return Geometry.Matches(geometry);
  }

private:
  friend class UBlockWorld;
  SectionRenderLightSnapshot() = default;
  SectionGeometryRenderInputs Geometry;
  std::shared_ptr<const SectionLightVolume> Data;
  uint64_t Incarnation{0}, Revision{0};
};
} // namespace cutum
