#include "Player.h"

namespace cutum
{

UPlayer::UPlayer(CreatureId id, const std::string &speciesId,
                 glm::vec3 bodyOrigin)
    : UCreature(id, speciesId, bodyOrigin, glm::vec3(0.0f, 1.62f, 0.0f))
{
  SetPlayerCharacter(true);
}

} // namespace cutum
