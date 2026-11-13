#pragma once
#include <algorithm>
#include <cmath>
#include <glm/glm.hpp>
#include <optional>
namespace cutum
{
struct PhysicalProfile
{
  float mass{1.f};
  glm::vec3 size{.25f};
  float hardness{1.f};
};
struct SweepContact
{
  float time{1.f};
  glm::vec3 normal{0.f};
};
inline std::optional<SweepContact> SweepBody(glm::vec3 start, glm::vec3 delta,
                                             glm::vec3 half, glm::vec3 center,
                                             glm::vec3 otherHalf,
                                             glm::vec3 otherDelta = {})
{
  float enter = 0.f, leave = 1.f;
  glm::vec3 normal{};
  const auto origin = start - center, extent = half + otherHalf,
             movement = delta - otherDelta;
  bool overlap = true;
  for (int i = 0; i < 3; ++i)
    overlap &= std::abs(origin[i]) < extent[i] - .0001f;
  if (overlap)
  {
    int axis = 0;
    for (int i = 1; i < 3; ++i)
      if (extent[i] - std::abs(origin[i]) <
          extent[axis] - std::abs(origin[axis]))
        axis = i;
    normal[axis] = origin[axis] < 0 ? -1.f : 1.f;
    return SweepContact{0.f, normal};
  }
  for (int i = 0; i < 3; ++i)
  {
    if (std::abs(movement[i]) < 1e-8f)
    {
      if (std::abs(origin[i]) >= extent[i])
        return {};
      continue;
    }
    float a = (-extent[i] - origin[i]) / movement[i],
          b = (extent[i] - origin[i]) / movement[i];
    if (a > b)
      std::swap(a, b);
    if (a >= enter)
    {
      enter = a;
      normal = {};
      normal[i] = movement[i] > 0 ? -1.f : 1.f;
    }
    leave = std::min(leave, b);
    if (enter > leave)
      return {};
  }
  if (enter < 0 || enter > 1 || glm::dot(normal, movement) >= 0)
    return {};
  return SweepContact{enter, normal};
}
inline int CarryHands(const PhysicalProfile &p, int strength,
                      float height = 1.8f)
{
  if (!std::isfinite(height) || height <= 0 || strength <= 0 ||
      !std::isfinite(p.size.x) || !std::isfinite(p.size.y) ||
      !std::isfinite(p.size.z) ||
      glm::any(glm::lessThanEqual(p.size, glm::vec3(0))))
    return 0;
  float longest = std::max({p.size.x, p.size.y, p.size.z}),
        shortest = std::min({p.size.x, p.size.y, p.size.z});
  float cross = p.size.x + p.size.y + p.size.z - longest - shortest;
  if (!std::isfinite(p.mass) || p.mass <= 0 || p.mass > 2.f * strength ||
      longest > height * 1.2f || cross > height * .8f)
    return 0;
  return p.mass <= strength * .5f && longest <= height * .67f &&
                 cross <= height * .28f
             ? 1
             : 2;
}
inline float ThrowSpeed(const PhysicalProfile &p, int strength, float charge,
                        float height = 1.8f)
{
  if (!std::isfinite(charge) || !CarryHands(p, strength, height))
    return 0.f;
  float size = std::max({p.size.x, p.size.y, p.size.z});
  return (12.f + strength) / std::sqrt(1.f + p.mass / 4.f) /
         (1.f + .25f * size * size) *
         (.35f + .65f * std::clamp(charge, 0.f, 1.f));
}
inline float ImpactDamage(float mass, float normalSpeed, float hardness = 1.f)
{
  if (!std::isfinite(mass) || mass <= 0 || !std::isfinite(normalSpeed) ||
      normalSpeed <= 3 || !std::isfinite(hardness) || hardness <= 0)
    return 0;
  return std::clamp(
      .015f * mass * std::max(0.f, normalSpeed * normalSpeed - 9.f) * hardness,
      0.f, 50.f);
}
} // namespace cutum
