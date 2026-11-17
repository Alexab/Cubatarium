#pragma once

#include "World/Chunks/Chunk.h"
#include "World/Core/BlockWorld.h"
#include "World/Lighting/ChunkRelightSnapshot.h"
#include "World/Lighting/LightUtil.h"

namespace cutum
{

constexpr uint8_t kHorizontalLightBoundaryXNegative = 1u << 0;
constexpr uint8_t kHorizontalLightBoundaryXPositive = 1u << 1;
constexpr uint8_t kHorizontalLightBoundaryZNegative = 1u << 2;
constexpr uint8_t kHorizontalLightBoundaryZPositive = 1u << 3;
constexpr uint8_t kVerticalLightBoundaryYNegative = 1u << 4;
constexpr uint8_t kVerticalLightBoundaryYPositive = 1u << 5;

/// A partial column job does not own adjacent vertical sections. All six
/// changed boundaries must carry additions and removals to their consumers.
inline uint8_t
ChangedLightBoundaryMask(const std::array<uint8_t, CHUNK_VOLUME> &before,
                         const std::array<uint8_t, CHUNK_VOLUME> &computed,
                         bool include_skylight, bool include_block_light)
{
  const uint8_t light_mask = static_cast<uint8_t>(
      (include_skylight ? 0x0Fu : 0u) | (include_block_light ? 0xF0u : 0u));
  if (light_mask == 0)
  {
    return 0;
  }
  const auto changed = [&](int x, int y, int z)
  {
    const size_t index =
        static_cast<size_t>(UChunk::LocalIndex(glm::ivec3(x, y, z)));
    return ((before[index] ^ computed[index]) & light_mask) != 0;
  };

  uint8_t changed_faces = 0;
  const int edge = CHUNK_SIZE - 1;
  bool x_negative = false;
  bool x_positive = false;
  bool z_negative = false;
  bool z_positive = false;
  bool y_negative = false;
  bool y_positive = false;
  for (int y = 0; y < CHUNK_SIZE; ++y)
  {
    for (int span = 0; span < CHUNK_SIZE; ++span)
    {
      x_negative = x_negative || changed(0, y, span);
      x_positive = x_positive || changed(edge, y, span);
      z_negative = z_negative || changed(span, y, 0);
      z_positive = z_positive || changed(span, y, edge);
      y_negative = y_negative || changed(y, 0, span);
      y_positive = y_positive || changed(y, edge, span);
    }
  }
  if (x_negative)
  {
    changed_faces |= kHorizontalLightBoundaryXNegative;
  }
  if (x_positive)
  {
    changed_faces |= kHorizontalLightBoundaryXPositive;
  }
  if (z_negative)
  {
    changed_faces |= kHorizontalLightBoundaryZNegative;
  }
  if (z_positive)
  {
    changed_faces |= kHorizontalLightBoundaryZPositive;
  }
  if (y_negative)
    changed_faces |= kVerticalLightBoundaryYNegative;
  if (y_positive)
    changed_faces |= kVerticalLightBoundaryYPositive;
  return changed_faces;
}

inline uint8_t ChangedHorizontalLightBoundaryMask(
    const std::array<uint8_t, CHUNK_VOLUME> &before,
    const std::array<uint8_t, CHUNK_VOLUME> &computed, bool include_skylight,
    bool include_block_light)
{
  return ChangedLightBoundaryMask(before, computed, include_skylight,
                                  include_block_light) &
         0x0Fu;
}

// Install only computed domains after read-set/catalog validation. A vertical
// seed is not a completed light solution: it would erase horizontal skylight.
// Bulk publication bumps the revision once, and never for an unchanged result.
inline bool
InstallComputedLight(UChunk &chunk,
                     const std::array<uint8_t, CHUNK_VOLUME> &computed,
                     bool include_skylight, bool include_block_light)
{
  const uint8_t mask = static_cast<uint8_t>((include_skylight ? 0x0F : 0) |
                                            (include_block_light ? 0xF0 : 0));
  if (mask == 0)
  {
    return false;
  }
  auto merged = chunk.GetLightData();
  bool changed = false;
  for (size_t i = 0; i < merged.size(); ++i)
  {
    const uint8_t next =
        static_cast<uint8_t>((merged[i] & ~mask) | (computed[i] & mask));
    changed |= next != merged[i];
    merged[i] = next;
  }
  if (changed)
  {
    chunk.ReplaceLightData(merged);
  }
  return changed;
}

/// Install one calculated sampling resource before recording settlement.
/// The interior must agree with the padded output in every selected channel.
inline bool ComputedLightInteriorMatches(const RelightChunkLightData &computed,
                                         bool sky, bool block)
{
  if (!computed.padded_light)
    return false;
  const uint8_t mask = (sky ? 0x0f : 0) | (block ? 0xf0 : 0);
  for (int z = 0; z < CHUNK_SIZE; ++z)
    for (int y = 0; y < CHUNK_SIZE; ++y)
      for (int x = 0; x < CHUNK_SIZE; ++x)
        if (((computed.light_packed[UChunk::LocalIndex({x,y,z})] ^
              computed.padded_light->cells[SectionLightVolume::Index(x,y,z)]) & mask) != 0)
          return false;
  return true;
}

inline bool InstallComputedLight(UChunk &chunk,
                                 const RelightChunkLightData &computed,
                                 bool sky, bool block)
{
  if (!computed.padded_light)
    return InstallComputedLight(chunk, computed.light_packed, sky, block);
  return ComputedLightInteriorMatches(computed, sky, block) &&
         chunk.ReplaceComputedLightVolume(*computed.padded_light, sky, block);
}

/// Caller validates the result's full input context before this installation.
/// Unknown absence and legacy unpadded bytes cannot allocate an AIR field.
inline bool InstallComputedLight(UBlockWorld &world,
                                 const RelightChunkLightData &computed,
                                 bool sky, bool block)
{
  auto &manager = world.GetChunkManager();
  if (auto *chunk = manager.GetChunk(computed.coord))
    return InstallComputedLight(*chunk, computed, sky, block);
  return world.IsKnownAbsentAirChunk(computed.coord) &&
         ComputedLightInteriorMatches(computed, sky, block) &&
         manager.ReplaceComputedSectionLightVolume(
             computed.coord, *computed.padded_light, sky, block);
}

} // namespace cutum
