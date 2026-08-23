#pragma once

#include <cstdint>

namespace cutum
{

/// FZ2.7-B1: O(1) mesh-vs-light stale (replaces ChunkHasStaleDarkFaces hot path).
inline bool IsMeshLightStale(uint64_t meshed_light_revision,
                             uint64_t light_field_revision)
{
  return meshed_light_revision < light_field_revision;
}

/// GPU-resident: revision mismatch OR conservative dark-face until remesh completes.
inline bool IsMeshLightStaleGpu(bool gpu_resident, bool gpu_has_dark_face,
                                uint64_t meshed_light_revision,
                                uint64_t light_field_revision)
{
  if (!gpu_resident)
  {
    return false;
  }
  if (IsMeshLightStale(meshed_light_revision, light_field_revision))
  {
    return true;
  }
  return gpu_has_dark_face;
}

} // namespace cutum
