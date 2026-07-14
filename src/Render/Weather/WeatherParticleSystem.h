#ifndef WEATHER_PARTICLE_SYSTEM_H
#define WEATHER_PARTICLE_SYSTEM_H

#include <glm/glm.hpp>
#include <vector>

namespace cutum
{

class UWorld;

struct WeatherParticleGpuInstance
{
  glm::vec3 WorldPos{0.0f};
  float Kind{1.0f};
  float Size{0.08f};
};

class UWeatherParticleSystem
{
public:
  static constexpr int kMaxParticles = 3500;

  void Reset();
  void Update(const UWorld &world, const glm::vec3 &camera_pos,
              float dt_seconds, int budget);
  const std::vector<WeatherParticleGpuInstance> &GetInstances() const
  {
    return Instances;
  }
  int GetActiveCount() const { return ActiveCount; }

private:
  struct Particle
  {
    glm::vec3 Pos{0.0f};
    glm::vec3 Vel{0.0f};
    float Life{0.0f};
    float Size{0.08f};
    int Kind{1};
    bool Alive{false};
  };

  void SpawnParticle(const glm::vec3 &camera_pos, int kind, float wind,
                     float intensity);
  float Random01();

  std::vector<Particle> Pool;
  std::vector<WeatherParticleGpuInstance> Instances;
  int ActiveCount{0};
  uint32_t RandState{0xC0FFEEu};
};

} // namespace cutum

#endif
