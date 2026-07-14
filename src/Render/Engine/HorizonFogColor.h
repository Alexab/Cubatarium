#ifndef HORIZONFOGCOLOR_H
#define HORIZONFOGCOLOR_H

#include "World/Core/World.h"
#include <glm/glm.hpp>

namespace cutum
{

struct HorizonFogColorInput
{
  glm::vec3 base_sky{0.5f, 0.7f, 1.0f};
  float day{1.0f};
  float moon{0.0f};
  float weather_atten{1.0f};
  float cloudiness{0.0f};
  float precip{0.0f};
  const std::vector<UWorld::UCelestialBodyVisual> *celestial_bodies{nullptr};
};

struct AtmosphericSkyColors
{
  glm::vec3 sky_tint{0.5f, 0.7f, 1.0f};
  glm::vec3 fog_color{0.5f, 0.7f, 1.0f};
};

AtmosphericSkyColors ComputeAtmosphericSkyColors(const HorizonFogColorInput &in);

} // namespace cutum

#endif
