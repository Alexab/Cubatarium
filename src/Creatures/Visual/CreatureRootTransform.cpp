#include "Creatures/Visual/CreatureRootTransform.h"

#include <glm/gtc/matrix_transform.hpp>

namespace cutum
{

glm::mat4
BuildCreatureRootMatrix(const glm::vec3 &feetPosition, float yawDegrees,
                        float feetOffsetY,
                        const std::function<glm::mat4()> &conventionMatrix,
                        float modelScale, const glm::mat4 &animRoot)
{
  glm::mat4 bodyMat = glm::translate(glm::mat4(1.f), feetPosition);
  if (feetOffsetY != 0.f)
  {
    bodyMat = glm::translate(bodyMat, glm::vec3(0.f, feetOffsetY, 0.f));
  }
  bodyMat =
      glm::rotate(bodyMat, glm::radians(yawDegrees), glm::vec3(0.f, 1.f, 0.f));
  if (conventionMatrix)
  {
    bodyMat = bodyMat * conventionMatrix();
  }
  if (modelScale != 1.f)
  {
    bodyMat = bodyMat * glm::scale(glm::mat4(1.f), glm::vec3(modelScale));
  }
  return bodyMat * animRoot;
}

} // namespace cutum
