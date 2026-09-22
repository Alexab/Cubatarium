#pragma once

#include "Render/Mesh/ArtifactManifest.h"

#include <cstdint>

namespace cutum
{

/// Single Replace/Remove/Retain decision for greedy GPU publish (sysreset).
enum class MeshPublishAction : uint8_t
{
  Retain = 0,
  Replace = 1,
  Remove = 2,
  PublishedEmpty = 3,
};

/// A21 P3: common publication validator (CPU/GPU callers share this).
enum class PublicationValidation : uint8_t
{
  Ok = 0,
  SourceMismatch = 1,
  LightInvalid = 2,
  TableEpochStale = 3,
  OrderEpochStale = 4,
};

/// Validate candidate against expected source provenance + live draw epochs.
/// Partially stubbed: production may call with zeroed optional epochs until
/// full manifest plumbing lands; SourceMatches + light_valid still gate.
inline PublicationValidation ValidatePublicationCandidate(
    const ArtifactManifest &got, const ArtifactManifest &expected,
    const PublicationEpochs &draw_epochs = {},
    const PublicationEpochs &live_epochs = {})
{
  if (expected.light_valid && !got.light_valid)
  {
    return PublicationValidation::LightInvalid;
  }
  if (!ArtifactManifestSourceMatches(got, expected))
  {
    return PublicationValidation::SourceMismatch;
  }
  if (live_epochs.resident_table_revision != 0 &&
      draw_epochs.resident_table_revision < live_epochs.resident_table_revision)
  {
    return PublicationValidation::TableEpochStale;
  }
  if (live_epochs.transparent_order_key != 0 &&
      draw_epochs.transparent_order_key < live_epochs.transparent_order_key)
  {
    return PublicationValidation::OrderEpochStale;
  }
  return PublicationValidation::Ok;
}

inline bool PublicationCandidateAccepted(PublicationValidation v)
{
  return v == PublicationValidation::Ok;
}

struct MeshPublishRevs
{
  uint64_t geom_rev{0};
  uint64_t light_rev{0};
  uint64_t material_stamp{0};
};

inline bool MeshPublishRevsMatch(const MeshPublishRevs &got,
                                const MeshPublishRevs &expected)
{
  return got.geom_rev == expected.geom_rev &&
         got.light_rev == expected.light_rev &&
         got.material_stamp == expected.material_stamp;
}

/// Accept only when (geom, light, material) match expected — else Retain prior.
inline bool ShouldAcceptMeshPublish(const MeshPublishRevs &got,
                                    const MeshPublishRevs &expected,
                                    bool gpu_ready)
{
  return gpu_ready && MeshPublishRevsMatch(got, expected);
}

/// FNV-1a material stamp over batch blockIds (+ optional atlas generation).
inline uint64_t MeshPublishMaterialStamp(const uint16_t *block_ids, size_t n,
                                         uint64_t atlas_gen = 0)
{
  uint64_t h = 14695981039346656037ull;
  h ^= atlas_gen;
  h *= 1099511628211ull;
  for (size_t i = 0; i < n; ++i)
  {
    h ^= static_cast<uint64_t>(block_ids[i]);
    h *= 1099511628211ull;
  }
  return h;
}

/// Wrong-tex gate: blockId flip on Replace is Accept only when material stamp
/// matches the CPU bake (stale upload Retain). Empty expected → first publish OK.
inline bool ShouldAcceptMaterialBlockIdFlip(const MeshPublishRevs &got,
                                            const MeshPublishRevs &expected,
                                            bool gpu_ready,
                                            bool block_id_changed)
{
  if (!block_id_changed)
  {
    return true;
  }
  if (!gpu_ready)
  {
    return false;
  }
  if (expected.material_stamp == 0)
  {
    return true;
  }
  return got.material_stamp == expected.material_stamp;
}

/// Empty intentional publish is PublishedEmpty (not a silent Retain forever).
inline MeshPublishAction DecideMeshPublishAction(bool accept,
                                                 bool new_drawable,
                                                 bool had_prior_drawable)
{
  if (!accept)
  {
    return MeshPublishAction::Retain;
  }
  if (!new_drawable)
  {
    return had_prior_drawable ? MeshPublishAction::PublishedEmpty
                              : MeshPublishAction::Remove;
  }
  return MeshPublishAction::Replace;
}

/// Live∩Free must be empty: cannot free a slot that is still the live draw.
inline bool MeshPublishLiveFreeDisjoint(bool is_live_draw, bool freeing_slot)
{
  return !(is_live_draw && freeing_slot);
}

} // namespace cutum
