#ifndef BLOCK_BREAK_PARTICLE_SYSTEM_H
#define BLOCK_BREAK_PARTICLE_SYSTEM_H

#include <cstdint>
#include <glm/glm.hpp>
#include <vector>

namespace cutum
{

/// One debris quad, laid out for direct instanced upload.
struct BlockBreakParticleGpuInstance
{
  glm::vec3 WorldPos{0.0f};
  float Size{0.05f};
  glm::vec4 Color{0.42f, 0.36f, 0.30f, 1.0f};
};

/// Fixed-pool block break debris: ballistic only, no world collision.
class UBlockBreakParticleSystem
{
public:
  static constexpr int kMaxParticles = 256;
  static constexpr int kHitDebrisPerStep = 3;
  static constexpr int kBurstDebris = 28;

  void Reset();

  /// Chips thrown off a block face while digging.
  void SpawnHitDebris(const glm::vec3 &block_center, int count);

  /// Wider scatter for the moment the block breaks.
  void SpawnBreakBurst(const glm::vec3 &block_center, int count);

  void Update(float dt_seconds);

  const std::vector<BlockBreakParticleGpuInstance> &GetInstances() const
  {
    return Instances;
  }
  int GetActiveCount() const { return ActiveCount; }

private:
  struct Particle
  {
    glm::vec3 Pos{0.0f};
    glm::vec3 Vel{0.0f};
    glm::vec3 Color{0.42f, 0.36f, 0.30f};
    float Life{0.0f};
    float LifeSpan{1.0f};
    float Size{0.05f};
    bool Alive{false};
  };

  void EnsurePool();
  Particle *AcquireParticle();
  float Random01();
  float RandomRange(float min_value, float max_value);
  glm::vec3 RandomDebrisColor();

  std::vector<Particle> Pool;
  std::vector<BlockBreakParticleGpuInstance> Instances;
  int ActiveCount{0};
  size_t NextSlot{0};
  uint32_t RandState{0x9E3779B9u};
};

} // namespace cutum

#endif
