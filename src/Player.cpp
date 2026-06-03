#include "Player.h"

namespace cutum {

Player::Player(CreatureId id, const std::string& speciesId, glm::vec3 bodyOrigin)
    : Creature(id, speciesId, bodyOrigin, glm::vec3(0.0f, 1.62f, 0.0f))
{
 SetPlayerCharacter(true);
}

} // namespace cutum
