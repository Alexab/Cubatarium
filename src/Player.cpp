#include "Player.h"

namespace cutum {

Player::Player(CreatureId id, glm::vec3 bodyOrigin)
    : Creature(id, "player", bodyOrigin, glm::vec3(0.0f, 1.62f, 0.0f))
{
 SetPlayerCharacter(true);
 CreatureBoundsProfile profile;
 profile.restSizeBlocks = glm::vec3(0.6f, 1.8f, 0.6f);
 profile.minSizeBlocks = glm::vec3(0.6f, 1.5f, 0.6f);
 profile.maxSizeBlocks = glm::vec3(0.6f, 1.8f, 0.6f);
 GetBoundsMutable().profile = profile;
 GetBoundsMutable().currentSizeBlocks = profile.restSizeBlocks;
}

} // namespace cutum
