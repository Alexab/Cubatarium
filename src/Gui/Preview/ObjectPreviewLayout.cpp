#include "Gui/Preview/ObjectPreviewLayout.h"

#include "World/Math/BlockTypes.h"
#include "World/Objects/ObjectLibrary.h"

#include <algorithm>
#include <glm/gtc/matrix_transform.hpp>

namespace cutum
{

ObjectPreviewFit FitWorldObjectPreview(const WorldObjectDefinition &object,
                                       float targetSpan)
{
  ObjectPreviewFit fit;
  glm::vec3 minB(1e6f);
  glm::vec3 maxB(-1e6f);
  bool any = false;
  for (const ObjectVoxel &voxel : object.voxels)
  {
    if (voxel.Id == BLOCK_AIR)
    {
      continue;
    }
    any = true;
    const glm::vec3 p(voxel.offset);
    minB = glm::min(minB, p);
    maxB = glm::max(maxB, p);
  }
  if (!any)
  {
    return fit;
  }

  fit.center = (minB + maxB) * 0.5f;
  const glm::vec3 size = maxB - minB;
  const float extent = std::max({size.x, size.y, size.z}) + 1.0f;
  fit.uniformScale = extent > 0.01f ? targetSpan / extent : 1.0f;
  return fit;
}

glm::mat4 ObjectPreviewVoxelModel(const ObjectPreviewFit &fit,
                                  const glm::ivec3 &offset,
                                  float blockFill)
{
  const glm::vec3 local =
      fit.uniformScale * (glm::vec3(offset) - fit.center);
  const float blockScale = std::max(0.01f, blockFill * fit.uniformScale);
  return glm::translate(glm::mat4(1.0f), local) *
         glm::scale(glm::mat4(1.0f), glm::vec3(blockScale));
}

} // namespace cutum
