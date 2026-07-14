#include "Render/Engine/HorizonFogColor.h"

#include <algorithm>
#include <cmath>

namespace cutum
{

namespace
{

float Smoothstep(float edge0, float edge1, float x)
{
  const float t = std::clamp((x - edge0) / std::max(edge1 - edge0, 1e-5f), 0.0f,
                             1.0f);
  return t * t * (3.0f - 2.0f * t);
}

} // namespace

AtmosphericSkyColors ComputeAtmosphericSkyColors(const HorizonFogColorInput &in)
{
  const float day = std::clamp(in.day, 0.0f, 1.0f);
  const float moon = std::clamp(in.moon, 0.0f, 1.0f);
  const float sky_mul = std::clamp(
      0.3f + day * in.weather_atten * 0.7f + moon * 0.26f, 0.3f, 1.0f);

  glm::vec3 color = in.base_sky * sky_mul;
  color = glm::mix(color, color * glm::vec3(0.75f, 0.82f, 0.95f),
                   moon * (1.0f - day) * 0.45f);

  const float weather_darken = std::clamp(
      in.cloudiness * 0.1f + in.precip * 0.08f, 0.0f, 0.16f);
  color *= (1.0f - weather_darken);

  const float night = 1.0f - day;
  const glm::vec3 night_base(0.035f, 0.045f, 0.075f);
  color = glm::mix(color, night_base, night * 0.82f);

  if (in.celestial_bodies)
  {
    for (const UWorld::UCelestialBodyVisual &body : *in.celestial_bodies)
    {
      const float elev = body.DirectionWorld.y;
      const float above_horizon = Smoothstep(0.0f, 0.08f, elev);
      const glm::vec3 lit = body.Color * std::max(body.Intensity, 0.0f);

      if (body.Type == UWorld::CelestialBodyType::Sun)
      {
        const float twilight =
            above_horizon * (1.0f - Smoothstep(0.12f, 0.42f, elev));
        color = glm::mix(color, lit, twilight * day * 0.42f);
      }
      else
      {
        color = glm::mix(color, lit * 0.35f, above_horizon * moon * 0.38f);
      }
    }
  }

  AtmosphericSkyColors out;
  out.sky_tint = color;
  out.fog_color = color;
  return out;
}

} // namespace cutum
