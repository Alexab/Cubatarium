#include "Render/Weather/WeatherParticleSystem.h"

#include "Render/Weather/WeatherExposure.h"
#include "World/Core/World.h"

#include <algorithm>
#include <cmath>

namespace cutum
{

void UWeatherParticleSystem::Reset()
{
  Pool.assign(kMaxParticles, Particle{});
  Instances.clear();
  ActiveCount = 0;
}

float UWeatherParticleSystem::Random01()
{
  RandState = RandState * 1664525u + 1013904223u;
  return static_cast<float>(RandState & 0x00FFFFFFu) /
         static_cast<float>(0x01000000u);
}

void UWeatherParticleSystem::SpawnParticle(const glm::vec3 &camera_pos,
                                           int kind,
                                           const glm::vec2 &wind_direction,
                                           float wind, float intensity)
{
  for (Particle &particle : Pool)
  {
    if (particle.Alive)
    {
      continue;
    }
    // Recycle particles in a camera-centred 32x24x32 block volume. The wider
    // footprint avoids a short, sparse ring that disappears during movement.
    const float offset_x = (Random01() - 0.5f) * 32.0f;
    const float offset_z = (Random01() - 0.5f) * 32.0f;
    const float height = -4.0f + Random01() * 24.0f;
    particle.Pos = camera_pos + glm::vec3(offset_x, height, offset_z);
    const float fall =
        kind == 2 ? 0.45f + Random01() * 0.35f : 7.0f + Random01() * 7.0f;
    const float gust = 0.5f + Random01() * 1.0f;
    const glm::vec2 horizontal =
        glm::normalize(wind_direction + glm::vec2(1.0e-4f)) * wind * gust;
    particle.Vel = glm::vec3(horizontal.x, -fall, horizontal.y);
    particle.Life =
        kind == 2 ? (2.2f + Random01() * 2.4f) : (1.0f + Random01() * 1.0f);
    particle.Size =
        kind == 2 ? 0.16f + Random01() * 0.14f : 0.14f + Random01() * 0.12f;
    particle.Size *= 0.75f + intensity * 0.5f;
    particle.Kind = kind;
    particle.Alive = true;
    break;
  }
}

void UWeatherParticleSystem::Update(const UWorld &world,
                                    const glm::vec3 &camera_pos,
                                    const WeatherSample &weather,
                                    float dt_seconds, int budget)
{
  if (budget <= 0 || dt_seconds <= 0.0f)
  {
    Instances.clear();
    ActiveCount = 0;
    return;
  }

  if (Pool.empty())
  {
    Pool.resize(kMaxParticles);
  }

  const float intensity =
      std::clamp(weather.PrecipitationIntensity, 0.0f, 1.0f);
  if (!CanReceiveOutdoorPrecipitation(world, camera_pos))
  {
    for (Particle &particle : Pool)
    {
      particle.Alive = false;
    }
    Instances.clear();
    ActiveCount = 0;
    return;
  }
  if (intensity <= 0.05f)
  {
    for (Particle &particle : Pool)
    {
      particle.Alive = false;
    }
    Instances.clear();
    ActiveCount = 0;
    return;
  }

  const float snow_fraction = std::clamp(weather.SnowFraction, 0.0f, 1.0f);
  const float rain_fraction = 1.0f - snow_fraction;
  const float wind = std::clamp(weather.WindStrength, 0.0f, 1.0f);
  const int spawn_target =
      std::min(budget, static_cast<int>(budget * intensity * 0.98f) + 28);
  const int rain_target =
      static_cast<int>(std::round(spawn_target * rain_fraction));
  const int snow_target = spawn_target - rain_target;

  int alive_rain = 0;
  int alive_snow = 0;
  for (Particle &particle : Pool)
  {
    if (!particle.Alive)
    {
      continue;
    }
    particle.Pos += particle.Vel * dt_seconds;
    particle.Life -= dt_seconds;
    const glm::vec3 delta = particle.Pos - camera_pos;
    if (particle.Life <= 0.0f || glm::length(delta) > 28.0f ||
        particle.Pos.y < camera_pos.y - 8.0f ||
        particle.Pos.y > camera_pos.y + 26.0f)
    {
      particle.Alive = false;
      continue;
    }
    if (particle.Kind == 2)
    {
      ++alive_snow;
    }
    else
    {
      ++alive_rain;
    }
  }

  const int rain_spawn_needed = std::max(0, rain_target - alive_rain);
  for (int i = 0; i < rain_spawn_needed; ++i)
  {
    SpawnParticle(camera_pos, 1, weather.WindDirection, wind, intensity);
  }
  const int snow_spawn_needed = std::max(0, snow_target - alive_snow);
  for (int i = 0; i < snow_spawn_needed; ++i)
  {
    SpawnParticle(camera_pos, 2, weather.WindDirection, wind, intensity);
  }

  Instances.clear();
  Instances.reserve(static_cast<size_t>(budget));
  ActiveCount = 0;
  for (const Particle &particle : Pool)
  {
    if (!particle.Alive)
    {
      continue;
    }
    WeatherParticleGpuInstance inst;
    inst.WorldPos = particle.Pos;
    inst.Kind = static_cast<float>(particle.Kind);
    inst.Size = particle.Size;
    Instances.push_back(inst);
    ++ActiveCount;
    if (ActiveCount >= budget)
    {
      break;
    }
  }
}

} // namespace cutum
