#include "Creatures/Player/Player.h"

namespace cutum
{

UPlayer::UPlayer(CreatureId Id, const std::string &speciesId,
                 glm::vec3 bodyOrigin)
    : UCreature(Id, speciesId, bodyOrigin, glm::vec3(0.0f, 1.62f, 0.0f))
{
  SetPlayerCharacter(true);
}

} // namespace cutum
