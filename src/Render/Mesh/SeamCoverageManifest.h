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

/// A26 N3: peer face must be published before subscriber may clear FaceDebt /
/// treat seam as subscribed. published_peer_gen==0 means peer not ready.
inline bool PeerReadyBeforeSubscribe(uint64_t published_peer_gen,
                                     uint64_t required_peer_gen)
{
  // required==0 → no face debt documented (unused). Debt raisers must pass ≥1.
  if (required_peer_gen == 0)
  {
    return true;
  }
  return published_peer_gen != 0 && published_peer_gen >= required_peer_gen;
}

/// A26 N3: all six faces satisfied → seam debt closed (provisional may clear).
inline bool SeamCoverageFullySatisfied(const SeamCoverageManifest &debt,
                                       const uint64_t published_peer_gen[6])
{
  if (!published_peer_gen)
  {
    return false;
  }
  for (int f = 0; f < 6; ++f)
  {
    if (!SeamCoveragePeerSatisfied(debt, f, published_peer_gen[f]))
    {
      return false;
    }
  }
  return true;
}

/// A26 N3: commit seam result closes debt only when generations match.
inline bool ShouldCommitSeamCoverage(const SeamCoverageManifest &debt,
                                     uint64_t published_seam_gen)
{
  if (debt.seam_artifact_generation == 0)
  {
    return published_seam_gen != 0;
  }
  return published_seam_gen >= debt.seam_artifact_generation;
}

/// A30 V3: provisional seam extract — temporary coverage gen, not permanent mesh.
inline SeamCoverageManifest MakeProvisionalSeamCoverage(glm::ivec3 chunk_xyz,
                                                        uint64_t world_epoch,
                                                        uint64_t peer_pub_gen)
{
  SeamCoverageManifest m{};
  m.chunk_xyz = chunk_xyz;
  m.world_epoch = world_epoch;
  m.seam_artifact_generation = peer_pub_gen;
  m.provisional = true;
  if (peer_pub_gen != 0)
  {
    for (uint64_t &g : m.peer_coverage_gen)
    {
      g = peer_pub_gen;
    }
  }
  return m;
}

} // namespace cutum

#endif
