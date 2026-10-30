#ifndef RIDERCARRY_H
#define RIDERCARRY_H

#include <cmath>
#include <glm/glm.hpp>

namespace cutum
{

/// Shortest signed difference between two yaw angles in degrees, (-180, 180].
inline float WrapYawDeltaDeg(float deltaDeg)
{
  while (deltaDeg > 180.0f)
  {
    deltaDeg -= 360.0f;
  }
  while (deltaDeg <= -180.0f)
  {
    deltaDeg += 360.0f;
  }
  return deltaDeg;
}

/// Rider position after the mount turned by `mountYawDeltaDeg` (creature yaw
/// convention: forward = (sin yaw, cos yaw) on X/Z). The rider keeps its
/// offset from the mount pivot, rotated with the mount; Y is untouched.
inline glm::vec3 RotateRiderAroundMount(const glm::vec3 &riderPos,
                                        const glm::vec3 &pivot,
                                        float mountYawDeltaDeg)
{
  const float rad = glm::radians(mountYawDeltaDeg);
  const float c = std::cos(rad);
  const float s = std::sin(rad);
  const float ox = riderPos.x - pivot.x;
  const float oz = riderPos.z - pivot.z;
  return glm::vec3(pivot.x + ox * c + oz * s, riderPos.y,
                   pivot.z + oz * c - ox * s);
}

/// Camera yaw change that keeps the rider looking at the same point of the
/// mount: camera yaw uses forward = (cos yaw, sin yaw), i.e. it grows the
/// opposite way to the creature yaw.
inline float CameraYawDeltaForMountYaw(float mountYawDeltaDeg)
{
  return -mountYawDeltaDeg;
}

struct RiderCarryResult
{
  glm::vec3 eye{0.0f};
  glm::vec3 body{0.0f};
};

/// Carry the rider with its mount through collision: the mount's travel,
/// then (when yawDeltaDeg != 0) the orbit around the mount pivot. `resolve`
/// is the swept move (eye, delta) -> eye the player itself uses, so an
/// overhang or wall scrapes the rider off instead of pushing it into blocks
/// (the old carry teleported eye and body by the raw offset). Eye and body
/// move by the same applied delta and stay in step.
template <typename ResolveFn>
RiderCarryResult CarryRiderThroughCollision(const glm::vec3 &eye,
                                            const glm::vec3 &body,
                                            const glm::vec3 &carry,
                                            const glm::vec3 &pivot,
                                            float yawDeltaDeg,
                                            ResolveFn &&resolve)
{
  RiderCarryResult out{eye, body};
  const auto move = [&](const glm::vec3 &delta)
  {
    if (glm::dot(delta, delta) <= 1e-10f)
    {
      return;
    }
    const glm::vec3 resolved = resolve(out.eye, delta);
    out.body += resolved - out.eye;
    out.eye = resolved;
  };
  move(carry);
  if (std::abs(yawDeltaDeg) > 1e-3f)
  {
    move(RotateRiderAroundMount(out.eye, pivot, yawDeltaDeg) - out.eye);
  }
  return out;
}

} // namespace cutum

#endif
