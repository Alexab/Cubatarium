#pragma once
// BUDGET_MS: 0.0  // perf-root P4: measure via Tracy; kill-switch required for new heuristics

#include <cstdint>

namespace cutum
{

/// FZ2.7-B1: O(1) mesh-vs-light stale (replaces ChunkHasStaleDarkFaces hot path).
inline bool IsMeshLightStale(uint64_t meshed_light_revision,
                             uint64_t light_field_revision)
{
  return meshed_light_revision < light_field_revision;
}

/// GPU-resident stale: revision mismatch only.
/// A21-07 / P4: equal-rev dark face is observational census, not proof of
/// invalid light (legal caves are fully dark). Callers that need geom-stale
/// dark reject keep a separate path.
inline bool IsMeshLightStaleGpu(bool gpu_resident, bool gpu_has_dark_face,
                                uint64_t meshed_light_revision,
                                uint64_t light_field_revision)
{
  (void)gpu_has_dark_face;
  if (!gpu_resident)
  {
    return false;
  }
  return IsMeshLightStale(meshed_light_revision, light_field_revision);
}

} // namespace cutum
