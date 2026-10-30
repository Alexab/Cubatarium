#ifndef CREATUREBOUNDS_H
#define CREATUREBOUNDS_H

#include "Creatures/Player/PlayerCapsule.h"
#include "World/Math/CollisionVolume.h"
#include <algorithm>
#include <cmath>
#include <glm/glm.hpp>

namespace cutum
{

/// Boxes are authored facing +Z (yaw 0, length along Z). An axis-aligned box
/// cannot turn with the body, so a long creature (whale 5x11, cow 1.1x1.8)
/// turned toward +-X keeps its X/Z swapped. Hysteresis around the diagonal
/// (swap past 55 degrees from Z, back below 35) stops diagonal walkers from
/// flipping every frame.
inline bool WantsSwappedFootprint(float yawDeg, bool currentlySwapped)
{
  float folded = std::fmod(std::abs(yawDeg), 180.0f); // 0..180
  folded = std::min(folded, 180.0f - folded);         // 0 = along Z, 90 = X
  return currentlySwapped ? folded > 35.0f : folded > 55.0f;
}

inline glm::vec3 SwapFootprintXZ(const glm::vec3 &sizeBlocks)
{
  return glm::vec3(sizeBlocks.z, sizeBlocks.y, sizeBlocks.x);
}

struct CreatureBoundsProfile
{
  glm::vec3 restSizeBlocks{0.6f, 1.8f, 0.6f};
  glm::vec3 maxSizeBlocks{0.6f, 1.8f, 0.6f};
  glm::vec3 minSizeBlocks{0.6f, 1.5f, 0.6f};
};

struct CreatureBoundsState
{
  CreatureBoundsProfile profile;
  /// Stance-blended size (crouch lowers it toward profile.minSizeBlocks) for
  /// the visuals only. Creature collision, support and anything that reports
  /// on them use the rest size (UCreature::GetCollisionSize).
  glm::vec3 currentSizeBlocks{0.6f, 1.8f, 0.6f};
  float stanceBlend01{0.0f};
};

glm::vec3 BoundsHalfExtents(const glm::vec3 &sizeBlocks);
glm::vec3 BoundsCollisionCenter(const glm::vec3 &bodyOrigin,
                                const glm::vec3 &currentSizeBlocks);
float BoundsFeetY(const glm::vec3 &bodyOrigin);
glm::vec3 BoundsEyePosition(const glm::vec3 &bodyOrigin,
                            const glm::vec3 &eyeOffset);
CreatureBoundsState LerpBoundsStance(const CreatureBoundsState &state,
                                     float blend01);

CreatureBoundsProfile ProfileFromPlayerCapsule(const PlayerCapsule &cap);
CreatureBoundsState StateFromPlayerCapsule(const PlayerCapsule &cap,
                                           float stanceBlend);
CollisionVolume CollisionVolumeFromBody(const glm::vec3 &bodyOrigin,
                                        const glm::vec3 &currentSizeBlocks);
CollisionVolume CollisionVolumeFromEye(const glm::vec3 &eyePos,
                                       const PlayerCapsule &cap);
glm::vec3 BodyOriginFromEye(const glm::vec3 &eyePos,
                            const glm::vec3 &eyeOffset);
/// Feet Y from eye and species standing eye height (`eyeOffset.y`).
float FeetYFromEye(const glm::vec3 &eyePos, float eyeHeight);

glm::vec3 SizeBlocksFromCapsule(const PlayerCapsule &cap);
CollisionVolume CollisionVolumeAtFeet(float feetY, float centerX, float centerZ,
                                    const glm::vec3 &sizeBlocks);

} // namespace cutum

#endif
