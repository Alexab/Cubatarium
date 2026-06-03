#ifndef PLAYER_H
#define PLAYER_H

#include "Creature.h"
#include <memory>

namespace cutum {

class User;

class Player : public Creature {
public:
 Player(CreatureId id, glm::vec3 bodyOrigin);

 bool IsPlayer() const override { return true; }

 void BindUser(const std::shared_ptr<User>& user) { user_ = user; }
 std::shared_ptr<User> GetUser() const { return user_.lock(); }

private:
 std::weak_ptr<User> user_;
};

} // namespace cutum

#endif
