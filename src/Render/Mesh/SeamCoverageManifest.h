#ifndef SEAM_COVERAGE_MANIFEST_H
#define SEAM_COVERAGE_MANIFEST_H

#include <cstdint>
#include <glm/glm.hpp>

namespace cutum
{

/// A21 P4: versioned seam / temporary coverage identity, independent of the
/// permanent mesh ArtifactManifest. Peer coverage generations are per-face.
struct SeamCoverageManifest
{
  uint64_t world_epoch{0};
  glm::ivec3 chunk_xyz{0};
  uint32_t incarnation{0};
  /// Required peer coverage generation for faces 0..5 (0 = unused).
  uint64_t peer_coverage_gen[6]{};
  uint64_t seam_artifact_generation{0};
  bool provisional{true}; // provisional ≠ Ready (ADR seam coverage)
};

inline bool SeamCoveragePeerSatisfied(const SeamCoverageManifest &debt,
                                      int face, uint64_t published_peer_gen)
{
  if (face < 0 || face > 5)
  {
    return false;
  }
  const uint64_t need = debt.peer_coverage_gen[face];
  if (need == 0)
  {
    return true;
  }
  return published_peer_gen != 0 && published_peer_gen >= need;
}

} // namespace cutum

#endif
