#ifndef CREATUREROOTTRANSFORM_H
#define CREATUREROOTTRANSFORM_H

#include <functional>
#include <glm/glm.hpp>

namespace cutum
{

glm::mat4 BuildCreatureRootMatrix(
    const glm::vec3 &feetPosition, float yawDegrees, float feetOffsetY,
    const std::function<glm::mat4()> &conventionMatrix, float modelScale = 1.f,
    const glm::mat4 &animRoot = glm::mat4(1.f));

} // namespace cutum

#endif
