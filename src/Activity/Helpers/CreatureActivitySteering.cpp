#include "Activity/Helpers/CreatureActivitySteering.h"
#include <glm/gtc/constants.hpp>
#include <algorithm>

namespace cutum
{

namespace
{

constexpr int kMaxDirectionAttempts = 12;
constexpr float kProbeDistance = 1.25f;
constexpr float kStuckMoveEpsilon = 0.002f;

bool ProbeLocomotionClear(IUWorldPerception &perception, CreatureHabitat habitat,
                          const glm::vec3 &probe_origin,
                          const glm::vec3 &bounds_size, CreatureId skip_id)
{
  if (!perception.CreatureVolumeClearAt(probe_origin, bounds_size, skip_id))
  {
    return false;
  }
  if (habitat == CreatureHabitat::Aerial)
  {
    return true;
  }
  return perception.HabitatAllowsMovementAt(habitat, probe_origin, bounds_size);
}

} // namespace

glm::vec3 RandomLocomotionDirection(CreatureHabitat habitat)
{
  const float angle = static_cast<float>(std::rand() % 628) / 100.0f;
  glm::vec3 dir(std::cos(angle), 0.0f, std::sin(angle));
  if (habitat == CreatureHabitat::Aquatic ||
      habitat == CreatureHabitat::Amphibious)
  {
    const float pitch =
        static_cast<float>((std::rand() % 201) - 100) / 100.0f * 0.45f;
    dir.y = pitch;
  }
  else if (habitat == CreatureHabitat::Lava)
  {
    const float pitch =
        static_cast<float>((std::rand() % 101) - 50) / 100.0f * 0.25f;
    dir.y = pitch;
  }
  else if (habitat == CreatureHabitat::Aerial)
  {
    const float pitch =
        static_cast<float>((std::rand() % 101) - 50) / 100.0f * 0.35f;
    dir.y = pitch;
  }
  if (glm::length(dir) > 1e-4f)
  {
    dir = glm::normalize(dir);
  }
  return dir;
}

bool PickLocomotionDirection(IUWorldPerception &perception,
                             const CreatureActivityView &view,
                             CreatureHabitat habitat,
                             const glm::vec3 &bounds_size,
                             glm::vec3 &out_direction)
{
  if (habitat == CreatureHabitat::Aerial)
  {
    for (int attempt = 0; attempt < kMaxDirectionAttempts; ++attempt)
    {
      const glm::vec3 dir = RandomLocomotionDirection(CreatureHabitat::Aerial);
      const glm::vec3 probe = view.bodyOrigin + dir * kProbeDistance;
      if (ProbeLocomotionClear(perception, habitat, probe, bounds_size,
                               view.Id))
      {
        out_direction = dir;
        return true;
      }
    }
    out_direction = RandomLocomotionDirection(CreatureHabitat::Aerial);
    return glm::length(out_direction) > 1e-4f;
  }
  for (int attempt = 0; attempt < kMaxDirectionAttempts; ++attempt)
  {
    const glm::vec3 dir = RandomLocomotionDirection(habitat);
    const glm::vec3 probe = view.bodyOrigin + dir * kProbeDistance;
    if (ProbeLocomotionClear(perception, habitat, probe, bounds_size, view.Id))
    {
      out_direction = dir;
      return true;
    }
  }
  return false;
}

glm::vec3 ComputeSeparationDirection(
    const glm::vec3 &self_origin, const glm::vec3 &bounds_size,
    const std::vector<CreatureNeighborView> &neighbors, float min_distance)
{
  const float min_dist_sq = min_distance * min_distance;
  glm::vec3 push_sum(0.0f);
  for (const CreatureNeighborView &neighbor : neighbors)
  {
    glm::vec3 delta = self_origin - neighbor.bodyOrigin;
    if (neighbor.Id != 0)
    {
      delta.y = 0.0f;
    }
    const float dist_sq = glm::dot(delta, delta);
    if (dist_sq < 1e-8f)
    {
      const float angle =
          static_cast<float>((std::rand() % 628) + neighbor.Id) / 100.0f;
      push_sum += glm::vec3(std::cos(angle), 0.0f, std::sin(angle));
      continue;
    }
    if (dist_sq < min_dist_sq)
    {
      const float dist = std::sqrt(dist_sq);
      const float weight = (min_distance - dist) / min_distance;
      push_sum += (delta / dist) * weight;
    }
  }
  if (glm::length(push_sum) < 1e-4f)
  {
    return glm::vec3(0.0f);
  }
  glm::vec3 xz(push_sum.x, 0.0f, push_sum.z);
  if (glm::length(xz) < 1e-4f)
  {
    return glm::vec3(0.0f);
  }
  return glm::normalize(xz);
}

glm::vec3 BlendLocomotionDirection(const glm::vec3 &base_dir,
                                   const glm::vec3 &separation_dir,
                                   float separation_weight)
{
  if (glm::length(separation_dir) < 1e-4f)
  {
    return base_dir;
  }
  if (glm::length(base_dir) < 1e-4f)
  {
    return separation_dir;
  }
  const float weight = glm::clamp(separation_weight, 0.0f, 1.0f);
  glm::vec3 blended = base_dir * (1.0f - weight) + separation_dir * weight;
  if (glm::length(blended) < 1e-4f)
  {
    return base_dir;
  }
  return glm::normalize(blended);
}

bool IsLocomotionStuck(const glm::vec3 &prev_origin,
                       const glm::vec3 &cur_origin, float dt, float min_speed)
{
  if (dt < 1e-6f)
  {
    return false;
  }
  const glm::vec3 delta = cur_origin - prev_origin;
  const float xz_speed =
      std::sqrt(delta.x * delta.x + delta.z * delta.z) / dt;
  return xz_speed < min_speed;
}

float SeparationQueryRadius(const glm::vec3 &bounds_size)
{
  const float footprint =
      std::max(bounds_size.x, std::max(bounds_size.y, bounds_size.z));
  return footprint * 1.5f + 0.35f;
}

} // namespace cutum
