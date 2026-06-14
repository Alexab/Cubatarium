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
  UPlayer(CreatureId id, const std::string &speciesId, glm::vec3 bodyOrigin);

  bool IsPlayer() const override { return true; }

  void BindUser(const std::shared_ptr<UUser> &user) { user_ = user; }
  std::shared_ptr<UUser> GetUser() const { return user_.lock(); }

private:
  std::weak_ptr<UUser> user_;
};

} // namespace cutum

#endif
