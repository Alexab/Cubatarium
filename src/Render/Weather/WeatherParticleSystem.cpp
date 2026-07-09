#include "Render/Weather/WeatherParticleSystem.h"

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
                                           int kind, float wind,
                                           float intensity)
{
  for (Particle &particle : Pool)
  {
    if (particle.Alive)
    {
      continue;
    }
    const float radius = 8.0f + Random01() * 12.0f;
    const float angle = Random01() * 6.2831853f;
    const float height = (Random01() - 0.2f) * 10.0f;
    particle.Pos = camera_pos + glm::vec3(std::cos(angle) * radius, height,
                                          std::sin(angle) * radius);
    const float fall =
        kind == 2 ? 0.6f + Random01() * 0.5f : 4.0f + Random01() * 6.0f;
    particle.Vel =
        glm::vec3(wind * (0.4f + Random01() * 0.8f), -fall, wind * 0.2f);
    particle.Life = 1.5f + Random01() * 2.5f;
    particle.Size =
        kind == 2 ? 0.05f + Random01() * 0.05f : 0.04f + Random01() * 0.06f;
    particle.Size *= 0.75f + intensity * 0.5f;
    particle.Kind = kind;
    particle.Alive = true;
    break;
  }
}

void UWeatherParticleSystem::Update(const UWorld &world,
                                    const glm::vec3 &camera_pos,
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

  const UWorld::EnvironmentState &env = world.GetEnvironmentState();
  const float intensity = std::clamp(env.PrecipitationIntensity, 0.0f, 1.0f);
  const bool raining = env.Weather == UWorld::WeatherType::Rain ||
                       env.TargetWeather == UWorld::WeatherType::Rain ||
                       env.Weather == UWorld::WeatherType::Storm ||
                       env.TargetWeather == UWorld::WeatherType::Storm;
  const bool snowing = env.Weather == UWorld::WeatherType::Snow ||
                       env.TargetWeather == UWorld::WeatherType::Snow;
  if (intensity <= 0.05f || (!raining && !snowing))
  {
    for (Particle &particle : Pool)
    {
      particle.Alive = false;
    }
    Instances.clear();
    ActiveCount = 0;
    return;
  }

  const int kind = snowing && !raining ? 2 : 1;
  const float wind = std::clamp(env.WindStrength, 0.0f, 1.0f);
  const int spawn_target =
      std::min(budget, static_cast<int>(budget * intensity * 0.85f) + 8);

  int alive_count = 0;
  for (Particle &particle : Pool)
  {
    if (!particle.Alive)
    {
      continue;
    }
    particle.Pos += particle.Vel * dt_seconds;
    particle.Life -= dt_seconds;
    const glm::vec3 delta = particle.Pos - camera_pos;
    if (particle.Life <= 0.0f || glm::length(delta) > 24.0f ||
        particle.Pos.y < camera_pos.y - 18.0f)
    {
      particle.Alive = false;
      continue;
    }
    ++alive_count;
  }

  const int spawn_needed = std::max(0, spawn_target - alive_count);
  for (int i = 0; i < spawn_needed; ++i)
  {
    SpawnParticle(camera_pos, kind, wind, intensity);
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
