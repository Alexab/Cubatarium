#ifndef VOXELDDATRAVERSAL_H
#define VOXELDDATRAVERSAL_H

#include <glm/glm.hpp>
#include <functional>

namespace cutum
{

bool TraverseVoxelRay(const glm::vec3 &origin, const glm::vec3 &dir, float maxDist,
                      const std::function<bool(glm::ivec3)> &visitCell);

} // namespace cutum

#endif // VOXELDDATRAVERSAL_H
