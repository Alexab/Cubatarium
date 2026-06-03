#ifndef TESTMOB_H
#define TESTMOB_H

#include "Creature.h"
#include "MobController.h"

namespace cutum {

class TestMob : public Creature {
public:
 TestMob(CreatureId id, glm::vec3 bodyOrigin);

 void ApplyIntent(World& world, float dt) override;

private:
 MobController ai_;
};

} // namespace cutum

#endif
