#include "CreatureBounds.h"

#include <algorithm>

namespace cutum
{

glm::vec3 BoundsHalfExtents(const glm::vec3 &sizeBlocks)
{
  return sizeBlocks * 0.5f;
}

glm::vec3 BoundsCollisionCenter(const glm::vec3 &bodyOrigin,
                                const glm::vec3 &currentSizeBlocks)
{
  return bodyOrigin + glm::vec3(0.0f, currentSizeBlocks.y * 0.5f, 0.0f);
}

float BoundsFeetY(const glm::vec3 &bodyOrigin) { return bodyOrigin.y; }

glm::vec3 BoundsEyePosition(const glm::vec3 &bodyOrigin,
                            const glm::vec3 &eyeOffset)
{
  return bodyOrigin + eyeOffset;
}

CreatureBoundsState LerpBoundsStance(const CreatureBoundsState &state,
                                     float blend01)
{
  const float t = std::clamp(blend01, 0.0f, 1.0f);
  CreatureBoundsState out = state;
  out.stanceBlend01 = t;
  out.currentSizeBlocks =
      state.profile.restSizeBlocks +
      (state.profile.minSizeBlocks - state.profile.restSizeBlocks) * t;
  return out;
}

CreatureBoundsProfile ProfileFromPlayerCapsule(const PlayerCapsule &cap)
{
  CreatureBoundsProfile profile;
  profile.restSizeBlocks =
      glm::vec3(cap.halfWidth * 2.0f, cap.height, cap.halfWidth * 2.0f);
  profile.minSizeBlocks = profile.restSizeBlocks;
  profile.maxSizeBlocks = profile.restSizeBlocks;
  return profile;
}

CreatureBoundsState StateFromPlayerCapsule(const PlayerCapsule &cap,
                                           float stanceBlend)
{
  CreatureBoundsState state;
  state.profile = ProfileFromPlayerCapsule(PlayerCapsule::Standing());
  const PlayerCapsule stand = PlayerCapsule::Standing();
  const PlayerCapsule crouch = PlayerCapsule::Crouching();
  state.profile.restSizeBlocks =
      glm::vec3(stand.halfWidth * 2.0f, stand.height, stand.halfWidth * 2.0f);
  state.profile.minSizeBlocks = glm::vec3(
      crouch.halfWidth * 2.0f, crouch.height, crouch.halfWidth * 2.0f);
  state.profile.maxSizeBlocks = state.profile.restSizeBlocks;
  return LerpBoundsStance(state, stanceBlend);
}

CollisionVolume CollisionVolumeFromBody(const glm::vec3 &bodyOrigin,
                                        const glm::vec3 &currentSizeBlocks)
{
  CollisionVolume vol;
  vol.center = BoundsCollisionCenter(bodyOrigin, currentSizeBlocks);
  vol.halfExtents = BoundsHalfExtents(currentSizeBlocks);
  return vol;
}

CollisionVolume CollisionVolumeFromEye(const glm::vec3 &eyePos,
                                       const PlayerCapsule &cap)
{
  const glm::vec3 eyeOffset(0.0f, cap.eyeHeight, 0.0f);
  return CollisionVolumeFromBody(
      BodyOriginFromEye(eyePos, eyeOffset),
      glm::vec3(cap.halfWidth * 2.0f, cap.height, cap.halfWidth * 2.0f));
}

glm::vec3 BodyOriginFromEye(const glm::vec3 &eyePos, const glm::vec3 &eyeOffset)
{
  return eyePos - eyeOffset;
}

float FeetYFromEye(const glm::vec3 &eyePos, float eyeHeight)
{
  return eyePos.y - eyeHeight;
}

} // namespace cutum
