#pragma once

#include <array>
#include <cstdint>
#include <cstring>

#include <glm/glm.hpp>

namespace cutum
{

enum class CullPassId : uint8_t
{
  FlatVisible = 0,
  OpaqueGpuCompact = 1,
  TransparentGpuCompact = 2,
};

enum class CullDistanceMode : uint8_t
{
  Full3D = 0,
  Horizontal = 1,
};

/// Canonical CPU/GPU cull cache key. Reuse is allowed only when every field
/// matches exactly; conservative frustum containment is not implemented yet.
struct CullInputKey
{
  CullPassId passId{CullPassId::FlatVisible};
  uint64_t boundsRevision{0};
  uint64_t tableRevision{0};
  glm::vec3 cameraPos{0.0f};
  uint64_t viewProjHash{0};
  float cullDistance{0.0f};
  CullDistanceMode cullMode{CullDistanceMode::Full3D};
  bool resultValid{false};

  bool operator==(const CullInputKey &o) const
  {
    return passId == o.passId && boundsRevision == o.boundsRevision &&
           tableRevision == o.tableRevision && cameraPos == o.cameraPos &&
           viewProjHash == o.viewProjHash && cullDistance == o.cullDistance &&
           cullMode == o.cullMode && resultValid == o.resultValid;
  }

  bool operator!=(const CullInputKey &o) const { return !(*this == o); }
};

inline uint64_t HashFloatBits(const float *values, int count)
{
  uint64_t h = 14695981039346656037ull;
  for (int i = 0; i < count; ++i)
  {
    uint32_t bits = 0;
    std::memcpy(&bits, values + i, sizeof(bits));
    h ^= static_cast<uint64_t>(bits);
    h *= 1099511628211ull;
  }
  return h;
}

inline uint64_t HashViewProjection(const glm::mat4 &viewProj)
{
  return HashFloatBits(&viewProj[0][0], 16);
}

inline uint64_t HashFrustumPlanes(const std::array<glm::vec4, 6> &planes)
{
  uint64_t h = 14695981039346656037ull;
  for (const glm::vec4 &plane : planes)
  {
    h ^= HashFloatBits(&plane[0], 4);
    h *= 1099511628211ull;
  }
  return h;
}

inline CullInputKey MakeCullInputKey(CullPassId passId, uint64_t boundsRevision,
                                     uint64_t tableRevision,
                                     const glm::vec3 &cameraPos,
                                     uint64_t viewFingerprint, float cullDistance,
                                     bool horizontalCull, bool resultValid)
{
  CullInputKey key;
  key.passId = passId;
  key.boundsRevision = boundsRevision;
  key.tableRevision = tableRevision;
  key.cameraPos = cameraPos;
  key.viewProjHash = viewFingerprint;
  key.cullDistance = cullDistance;
  key.cullMode = horizontalCull ? CullDistanceMode::Horizontal
                                : CullDistanceMode::Full3D;
  key.resultValid = resultValid;
  return key;
}

inline CullInputKey MakeCullInputKey(CullPassId passId, uint64_t boundsRevision,
                                     uint64_t tableRevision,
                                     const glm::vec3 &cameraPos,
                                     const glm::mat4 &viewProj, float cullDistance,
                                     bool horizontalCull, bool resultValid)
{
  return MakeCullInputKey(passId, boundsRevision, tableRevision, cameraPos,
                          HashViewProjection(viewProj), cullDistance,
                          horizontalCull, resultValid);
}

/// Strict reuse gate. Conservative candidate-volume containment is future work.
inline bool CullInputKeyAllowsCacheReuse(const CullInputKey &cached,
                                         const CullInputKey &current)
{
  if (!cached.resultValid || !current.resultValid)
  {
    return false;
  }
  return cached == current;
}

/// Placeholder for future conservative frustum containment reuse.
inline bool FrustumContainedInConservativeCandidateVolume(
    const CullInputKey & /*cached*/, const CullInputKey & /*current*/)
{
  return false;
}

} // namespace cutum
