#ifndef PLAYERCAPSULE_H
#define PLAYERCAPSULE_H

#include <glm/glm.hpp>
#include <algorithm>

namespace cutum {

struct PlayerCapsule {
 float height{1.8f};
 float eyeHeight{1.62f};
 float halfWidth{0.3f};

 glm::vec3 halfExtents() const
 {
  return glm::vec3(halfWidth, height * 0.5f, halfWidth);
 }

 glm::vec3 centerFromEye(const glm::vec3& eye) const
 {
  return eye + glm::vec3(0.0f, height * 0.5f - eyeHeight, 0.0f);
 }

 float feetY(const glm::vec3& eye) const { return eye.y - eyeHeight; }

 static PlayerCapsule Standing() { return {1.8f, 1.62f, 0.3f}; }
 static PlayerCapsule Crouching() { return {1.5f, 1.27f, 0.3f}; }

 static PlayerCapsule Lerp(float blend01)
 {
  const float t = std::clamp(blend01, 0.0f, 1.0f);
  const PlayerCapsule stand = Standing();
  const PlayerCapsule crouch = Crouching();
  return {stand.height + (crouch.height - stand.height) * t,
          stand.eyeHeight + (crouch.eyeHeight - stand.eyeHeight) * t, stand.halfWidth};
 }

 /// Collision capsule from creature bounds (body origin = feet, size in blocks).
 static PlayerCapsule FromCreatureBlocks(const glm::vec3& sizeBlocks, float eyeHeight)
 {
  const float halfW = std::max(0.15f, sizeBlocks.x * 0.5f);
  const float height = std::max(0.5f, sizeBlocks.y);
  return {height, eyeHeight, halfW};
 }
};

} // namespace cutum

#endif
