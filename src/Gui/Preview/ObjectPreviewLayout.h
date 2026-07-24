#ifndef OBJECT_PREVIEW_LAYOUT_H
#define OBJECT_PREVIEW_LAYOUT_H

#include <glm/glm.hpp>

namespace cutum
{

struct WorldObjectDefinition;

struct ObjectPreviewFit
{
  glm::vec3 center{0.0f};
  float uniformScale{1.0f};
};

ObjectPreviewFit FitWorldObjectPreview(const WorldObjectDefinition &object,
                                       float targetSpan = 1.8f);

glm::mat4 ObjectPreviewVoxelModel(const ObjectPreviewFit &fit,
                                  const glm::ivec3 &offset,
                                  float blockFill = 0.98f);

} // namespace cutum

#endif
