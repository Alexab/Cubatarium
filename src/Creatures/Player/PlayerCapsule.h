#ifndef PLAYERCAPSULE_H
#define PLAYERCAPSULE_H

#include <algorithm>
#include <glm/glm.hpp>

namespace cutum
{

struct PlayerCapsule
{
  float height{1.8f};
  float eyeHeight{1.62f};
  float halfWidth{0.3f};
  /// Half size along Z; negative = same as halfWidth (square footprint: the
  /// player). Creature boxes are rectangular (a badger is 0.7 x 1.2).
  float halfDepth{-1.0f};

  float halfX() const { return halfWidth; }
  float halfZ() const { return halfDepth >= 0.0f ? halfDepth : halfWidth; }

  glm::vec3 halfExtents() const
  {
    return glm::vec3(halfX(), height * 0.5f, halfZ());
  }

  glm::vec3 centerFromEye(const glm::vec3 &eye) const
  {
    return eye + glm::vec3(0.0f, height * 0.5f - eyeHeight, 0.0f);
  }

  float feetY(const glm::vec3 &eye) const { return eye.y - eyeHeight; }

  static PlayerCapsule Standing() { return {1.8f, 1.62f, 0.3f}; }
  static PlayerCapsule Crouching() { return {1.5f, 1.27f, 0.3f}; }

  static PlayerCapsule Lerp(float blend01)
  {
    const float t = std::clamp(blend01, 0.0f, 1.0f);
    const PlayerCapsule stand = Standing();
    const PlayerCapsule crouch = Crouching();
    return {stand.height + (crouch.height - stand.height) * t,
            stand.eyeHeight + (crouch.eyeHeight - stand.eyeHeight) * t,
            stand.halfWidth};
  }

  /// Collision capsule from creature bounds (body origin = feet, size in
  /// blocks).
  static PlayerCapsule FromCreatureBlocks(const glm::vec3 &sizeBlocks,
                                          float eyeHeight)
  {
    // Slightly tighter than the body box so tall bipeds slide past block
    // lips/corners instead of permanently scraping (MC-style feel): 4% per
    // side, at most 0.025 (a 0.6 biped keeps the old 0.276). Both axes: the
    // motor used to be a square from X only, so a long body (badger 1.2,
    // whale 11 along Z) moved a box far shorter than itself and was then
    // shoved out of the blocks its real box overlapped.
    const auto tighten = [](float size)
    {
      const float half = size * 0.5f;
      return std::max(0.15f, half - std::min(0.025f, half * 0.08f));
    };
    const float height = std::max(0.5f, sizeBlocks.y);
    return {height, eyeHeight, tighten(sizeBlocks.x), tighten(sizeBlocks.z)};
  }
};

} // namespace cutum

#endif
