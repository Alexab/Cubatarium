#ifndef COLLISIONREADINESS_H
#define COLLISIONREADINESS_H

#include <glm/glm.hpp>

namespace cutum
{

class UBlockWorld;

/// Availability of point-query geometry at the supplied height, including
/// explicitly proven absent AIR. Swept-body callers must check every queried
/// section; an unrelated resident section at datum Y is not data evidence.
bool IsCollisionRingReady(const UBlockWorld &world, glm::ivec3 feet_block_pos,
                          int radius_chunks);

} // namespace cutum

#endif // COLLISIONREADINESS_H
