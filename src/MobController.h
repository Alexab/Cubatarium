#ifndef MOBCONTROLLER_H
#define MOBCONTROLLER_H

#include <glm/glm.hpp>

namespace cutum {

class World;
class Creature;

// TEMP: replace with CreatureAgent per docs/CREATURE_AGENTS.md
class MobController {
public:
 void Tick(World& world, Creature& self, float dt);

private:
 float wanderTimer_{0.0f};
 glm::vec3 wanderDir_{1.0f, 0.0f, 0.0f};
};

} // namespace cutum

#endif
