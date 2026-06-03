#ifndef CREATUREBOUNDS_H
#define CREATUREBOUNDS_H

#include <glm/glm.hpp>
#include "CollisionVolume.h"
#include "PlayerCapsule.h"

namespace cutum {

struct CreatureBoundsProfile {
 glm::vec3 restSizeBlocks{0.6f, 1.8f, 0.6f};
 glm::vec3 maxSizeBlocks{0.6f, 1.8f, 0.6f};
 glm::vec3 minSizeBlocks{0.6f, 1.5f, 0.6f};
};

struct CreatureBoundsState {
 CreatureBoundsProfile profile;
 glm::vec3 currentSizeBlocks{0.6f, 1.8f, 0.6f};
 float stanceBlend01{0.0f};
};

glm::vec3 BoundsHalfExtents(const glm::vec3& sizeBlocks);
glm::vec3 BoundsCollisionCenter(const glm::vec3& bodyOrigin, const glm::vec3& currentSizeBlocks);
float BoundsFeetY(const glm::vec3& bodyOrigin);
glm::vec3 BoundsEyePosition(const glm::vec3& bodyOrigin, const glm::vec3& eyeOffset);
CreatureBoundsState LerpBoundsStance(const CreatureBoundsState& state, float blend01);

CreatureBoundsProfile ProfileFromPlayerCapsule(const PlayerCapsule& cap);
CreatureBoundsState StateFromPlayerCapsule(const PlayerCapsule& cap, float stanceBlend);
CollisionVolume CollisionVolumeFromBody(const glm::vec3& bodyOrigin, const glm::vec3& currentSizeBlocks);
CollisionVolume CollisionVolumeFromEye(const glm::vec3& eyePos, const PlayerCapsule& cap);
glm::vec3 BodyOriginFromEye(const glm::vec3& eyePos, const glm::vec3& eyeOffset);
/// Feet Y from eye and species standing eye height (`eyeOffset.y`).
float FeetYFromEye(const glm::vec3& eyePos, float eyeHeight);

} // namespace cutum

#endif
