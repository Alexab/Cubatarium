#pragma once

#include <glm/glm.hpp>

namespace cutum
{

class UWorld;

float SampleSkyExposure01(const UWorld &world, const glm::vec3 &eye);
bool CanReceiveOutdoorPrecipitation(const UWorld &world, const glm::vec3 &eye);

} // namespace cutum
