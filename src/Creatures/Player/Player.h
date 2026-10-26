#ifndef PLAYER_H
#define PLAYER_H

#include "Creatures/Core/Creature.h"
#include <memory>

namespace cutum
{

class UUser;

class UPlayer : public UCreature
{
public:
  UPlayer(CreatureId Id, const std::string &speciesId, glm::vec3 bodyOrigin,
          glm::vec3 eyeOffset);

  bool IsPlayer() const override { return true; }

  void BindUser(const std::shared_ptr<UUser> &user) { User = user; }
  std::shared_ptr<UUser> GetUser() const { return User.lock(); }

private:
  std::weak_ptr<UUser> User;
};

} // namespace cutum

#endif
