#ifndef COLLISIONREADINESS_H
#define COLLISIONREADINESS_H

#include <glm/glm.hpp>

namespace cutum
{

class UBlockWorld;

bool IsCollisionRingReady(const UBlockWorld &world, glm::ivec3 feet_block_pos,
                          int radius_chunks);

} // namespace cutum

#endif // COLLISIONREADINESS_H
