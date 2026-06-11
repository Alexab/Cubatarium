#ifndef COLLISIONVOLUME_H
#define COLLISIONVOLUME_H

#include <glm/glm.hpp>

namespace cutum {

struct CollisionVolume {
 glm::vec3 center{0.0f};
 glm::vec3 halfExtents{0.5f};
};

} // namespace cutum

#endif
