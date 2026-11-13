#ifndef ARTIFACT_MANIFEST_H
#define ARTIFACT_MANIFEST_H

#include <cstdint>
#include <glm/glm.hpp>

namespace cutum
{

/// A21 P3 / §2.1: immutable identity of a published mesh artifact.
struct ArtifactManifest
{
  uint64_t world_epoch{0};
  glm::ivec3 chunk_xyz{0};
  uint32_t incarnation{0};

  uint64_t content_rev{0};
  uint64_t material_catalog_rev{0};
  uint64_t neighbor_halo_rev{0}; // aggregate of actually-read neighbor data

  uint64_t light_field_rev{0};
  bool light_valid{false}; // LightValidity, not "has non-zero vertices"

  uint8_t representation{0}; // backend / path id
  uint64_t artifact_generation{0};
  uint64_t material_stamp_pass{0}; // per-pass domain
  uint64_t source_geom_rev{0};
  uint64_t source_light_rev{0};

  uint64_t seam_peer_coverage_gen{0}; // 0 if permanent mesh only
  uint64_t content_checksum{0};       // integrity, not source identity
};

inline bool ArtifactManifestSourceMatches(const ArtifactManifest &got,
                                          const ArtifactManifest &expected)
{
  return got.world_epoch == expected.world_epoch &&
         got.chunk_xyz == expected.chunk_xyz &&
         got.incarnation == expected.incarnation &&
         got.content_rev == expected.content_rev &&
         got.material_catalog_rev == expected.material_catalog_rev &&
         got.neighbor_halo_rev == expected.neighbor_halo_rev &&
         got.light_field_rev == expected.light_field_rev &&
         got.light_valid == expected.light_valid &&
         got.representation == expected.representation &&
         got.source_geom_rev == expected.source_geom_rev &&
         got.source_light_rev == expected.source_light_rev &&
         got.material_stamp_pass == expected.material_stamp_pass;
}

/// Publication epoch split (A21 P3 / ADR publication epoch).
struct PublicationEpochs
{
  uint64_t artifact_generation{0};     // geometry/material/light payload
  uint64_t resident_table_revision{0}; // MDI ranges/indices
  uint64_t cull_key_generation{0};     // camera/frustum/candidates
  uint64_t transparent_order_key{0};   // camera sort / refs
};

} // namespace cutum

#endif
