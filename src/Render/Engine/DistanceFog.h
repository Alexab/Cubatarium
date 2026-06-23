#ifndef DISTANCEFOG_H
#define DISTANCEFOG_H

#include <glm/glm.hpp>

namespace cutum
{

struct DistanceFogParams
{
  float Start{0.0f};
  float End{0.0f};
  glm::vec3 Color{0.5f, 0.7f, 1.0f};
};

DistanceFogParams ComputeDistanceFog(int render_distance_chunks,
                                     glm::vec3 sky_color, float start_ratio);

} // namespace cutum

#endif
