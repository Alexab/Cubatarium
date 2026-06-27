#ifndef DISTANCEFOG_H
#define DISTANCEFOG_H

#include <glm/glm.hpp>

namespace cutum
{

struct DistanceFogParams
{
  float Start{0.0f};
  float End{0.0f};
  float Density{1.0f};
  glm::vec3 Color{0.5f, 0.7f, 1.0f};
};

/// Full geometry/cull horizon in blocks (matches loaded chunk radius).
float RenderHorizonBlocks(int render_distance_chunks);

/// Fog fade horizon — slightly inside render horizon to hide streaming edge.
float FogHorizonBlocks(int render_distance_chunks);

/// @deprecated Use FogHorizonBlocks for fog; RenderHorizonBlocks for cull.
float StreamingHorizonBlocks(int render_distance_chunks);

DistanceFogParams ComputeDistanceFog(int render_distance_chunks,
                                     glm::vec3 sky_color, float start_ratio,
                                     float effective_fog_start_ratio = -1.0f,
                                     float fog_density = 1.0f);

} // namespace cutum

#endif
