#pragma once

#include "Items/ItemDrawRequest.h"
#include "Render/Camera/DrawDistance.h"

namespace cutum
{
/// Use the actual posed local envelope and affine world model. The item
/// origin alone can reject a visible long tool crossing the horizon.
inline bool ItemIntersectsDrawDistance(const ItemDrawRequest &request,
                                       glm::vec3 camera, float distance)
{
  const auto bounds =
      TransformDrawWorldBounds(request.Low, request.High, request.Model);
  return !bounds || BoundsIntersectDrawDistance(*bounds, camera, distance);
}
} // namespace cutum
