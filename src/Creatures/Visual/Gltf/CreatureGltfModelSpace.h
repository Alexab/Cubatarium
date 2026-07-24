#ifndef CREATUREGLTFMODELSPACE_H
#define CREATUREGLTFMODELSPACE_H

#include <glm/gtc/matrix_transform.hpp>

namespace cutum
{

/// Luanti b3d glTF meshes face entity +Z in block space (no Bedrock −Z flip).
inline glm::mat4 GltfEntityConventionMatrix()
{
  return glm::mat4(1.f);
}

} // namespace cutum

#endif
