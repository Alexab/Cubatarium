#include "Creatures/Player/Player.h"

namespace cutum
{

UPlayer::UPlayer(CreatureId Id, const std::string &speciesId,
                 glm::vec3 bodyOrigin, glm::vec3 eyeOffset)
    : UCreature(Id, speciesId, bodyOrigin, eyeOffset)
{
  SetPlayerCharacter(true);
}

} // namespace cutum
