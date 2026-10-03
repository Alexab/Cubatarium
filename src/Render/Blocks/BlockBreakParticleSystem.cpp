#include "Render/Blocks/BlockBreakParticleSystem.h"

#include <algorithm>
#include <cmath>

namespace cutum
{

namespace
{

constexpr float kGravity = 16.0f;
constexpr float kDrag = 1.6f;
constexpr float kFadeSeconds = 0.22f;
/// Neutral gray-brown rubble; per-block texture color is TD-BB-003.
constexpr glm::vec3 kDebrisBaseColor{0.42f, 0.36f, 0.30f};

} // namespace

void UBlockBreakParticleSystem::Reset()
{
  for (Particle &p : Pool)
  {
    p.Alive = false;
  }
  Instances.clear();
  ActiveCount = 0;
  NextSlot = 0;
}

void UBlockBreakParticleSystem::EnsurePool()
{
  if (Pool.empty())
  {
    Pool.resize(static_cast<size_t>(kMaxParticles));
    Instances.reserve(static_cast<size_t>(kMaxParticles));
  }
}

float UBlockBreakParticleSystem::Random01()
{
  RandState ^= RandState << 13;
  RandState ^= RandState >> 17;
  RandState ^= RandState << 5;
  return static_cast<float>(RandState & 0xFFFFFFu) /
         static_cast<float>(0x1000000u);
}

float UBlockBreakParticleSystem::RandomRange(float min_value, float max_value)
{
  return min_value + (max_value - min_value) * Random01();
}

glm::vec3 UBlockBreakParticleSystem::RandomDebrisColor()
{
  const float shade = RandomRange(0.82f, 1.18f);
  const glm::vec3 jitter(RandomRange(-0.03f, 0.03f), RandomRange(-0.03f, 0.03f),
                         RandomRange(-0.03f, 0.03f));
  return glm::clamp(kDebrisBaseColor * shade + jitter, glm::vec3(0.05f),
                    glm::vec3(0.95f));
}

UBlockBreakParticleSystem::Particle *
UBlockBreakParticleSystem::AcquireParticle()
{
  EnsurePool();
  const size_t pool_size = Pool.size();
  for (size_t probe = 0; probe < pool_size; ++probe)
  {
    const size_t idx = (NextSlot + probe) % pool_size;
    if (!Pool[idx].Alive)
    {
      NextSlot = (idx + 1) % pool_size;
      return &Pool[idx];
    }
  }
  // Pool saturated: recycle the oldest slot in round-robin order.
  Particle *victim = &Pool[NextSlot];
  NextSlot = (NextSlot + 1) % pool_size;
  return victim;
}

void UBlockBreakParticleSystem::SpawnHitDebris(const glm::vec3 &block_center,
                                               int count)
{
  for (int i = 0; i < count; ++i)
  {
    Particle *p = AcquireParticle();
    if (!p)
    {
      return;
    }
    const int axis = static_cast<int>(Random01() * 3.0f) % 3;
    const float sign = Random01() < 0.5f ? -1.0f : 1.0f;
    glm::vec3 normal(0.0f);
    normal[axis] = sign;

    glm::vec3 offset(RandomRange(-0.42f, 0.42f), RandomRange(-0.42f, 0.42f),
                     RandomRange(-0.42f, 0.42f));
    offset[axis] = sign * 0.52f;

    p->Pos = block_center + offset;
    p->Vel = normal * RandomRange(0.7f, 1.9f) +
             glm::vec3(RandomRange(-0.5f, 0.5f), RandomRange(0.9f, 2.4f),
                       RandomRange(-0.5f, 0.5f));
    p->Color = RandomDebrisColor();
    p->LifeSpan = RandomRange(0.35f, 0.65f);
    p->Life = p->LifeSpan;
    p->Size = RandomRange(0.030f, 0.055f);
    p->Alive = true;
  }
}

void UBlockBreakParticleSystem::SpawnBreakBurst(const glm::vec3 &block_center,
                                                int count)
{
  for (int i = 0; i < count; ++i)
  {
    Particle *p = AcquireParticle();
    if (!p)
    {
      return;
    }
    const glm::vec3 offset(RandomRange(-0.45f, 0.45f),
                           RandomRange(-0.45f, 0.45f),
                           RandomRange(-0.45f, 0.45f));
    glm::vec3 radial = offset;
    const float radial_len = glm::length(radial);
    if (radial_len > 1e-4f)
    {
      radial /= radial_len;
    }
    else
    {
      radial = glm::vec3(0.0f, 1.0f, 0.0f);
    }

    p->Pos = block_center + offset;
    p->Vel = radial * RandomRange(1.4f, 3.4f) +
             glm::vec3(0.0f, RandomRange(1.2f, 3.2f), 0.0f);
    p->Color = RandomDebrisColor();
    p->LifeSpan = RandomRange(0.55f, 1.05f);
    p->Life = p->LifeSpan;
    p->Size = RandomRange(0.040f, 0.085f);
    p->Alive = true;
  }
}

void UBlockBreakParticleSystem::Update(float dt_seconds)
{
  Instances.clear();
  ActiveCount = 0;
  if (Pool.empty())
  {
    return;
  }

  const float dt = std::clamp(dt_seconds, 0.0f, 0.1f);
  const float drag_scale = std::max(0.0f, 1.0f - kDrag * dt);

  for (Particle &p : Pool)
  {
    if (!p.Alive)
    {
      continue;
    }
    p.Life -= dt;
    if (p.Life <= 0.0f)
    {
      p.Alive = false;
      continue;
    }
    p.Vel.x *= drag_scale;
    p.Vel.z *= drag_scale;
    p.Vel.y -= kGravity * dt;
    p.Pos += p.Vel * dt;

    BlockBreakParticleGpuInstance inst;
    inst.WorldPos = p.Pos;
    inst.Size = p.Size;
    const float fade = std::min(1.0f, p.Life / kFadeSeconds);
    inst.Color = glm::vec4(p.Color, fade);
    Instances.push_back(inst);
    ++ActiveCount;
  }
}

} // namespace cutum
