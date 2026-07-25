#include "Activity/Helpers/CreatureActivitySteering.h"
#include <algorithm>
#include <cmath>

namespace cutum
{

namespace
{

constexpr int kMaxDirectionAttempts = 12;
constexpr float kProbeDistance = 0.65f;

glm::vec3 RotateYawXZ(const glm::vec3 &dir, float yaw_rad)
{
  const float c = std::cos(yaw_rad);
  const float s = std::sin(yaw_rad);
  return glm::vec3(dir.x * c - dir.z * s, dir.y, dir.x * s + dir.z * c);
}

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

float NavigationBodyHeightForBounds(float bounds_height_blocks)
{
  // Tall bipeds (zombie 1.85): trim so A* fits 2-block gaps / light foliage.
  // Short mobs keep full height — do not inflate clearance above their AABB.
  if (bounds_height_blocks <= 1.25f)
  {
    return std::max(0.5f, bounds_height_blocks);
  }
  return bounds_height_blocks - 0.25f;
}

bool ProbeLocomotionDirectionClear(IUWorldPerception &perception,
                                   CreatureHabitat habitat,
                                   const glm::vec3 &body_origin,
                                   const glm::vec3 &dir,
                                   const glm::vec3 &bounds_size,
                                   CreatureId skip_id, float probe_distance)
{
  if (glm::length(dir) < 1e-4f)
  {
    return false;
  }
  const glm::vec3 normalized = glm::normalize(dir);
  const float dist = std::max(0.35f, probe_distance);
  const glm::vec3 probe = body_origin + normalized * dist;
  return ProbeLocomotionClear(perception, habitat, probe, bounds_size, skip_id);
}

bool PickApproachDirection(IUWorldPerception &perception,
                           CreatureHabitat habitat,
                           const glm::vec3 &body_origin,
                           const glm::vec3 &preferred_dir,
                           const glm::vec3 &bounds_size, CreatureId skip_id,
                           glm::vec3 &out_direction)
{
  if (glm::length(preferred_dir) < 1e-4f)
  {
    return false;
  }
  const glm::vec3 preferred = glm::normalize(preferred_dir);
  constexpr float kOffsetsRad[] = {0.0f,  0.45f, -0.45f, 0.9f,
                                   -0.9f, 1.4f,  -1.4f};
  for (float yaw : kOffsetsRad)
  {
    const glm::vec3 dir = yaw == 0.0f ? preferred : RotateYawXZ(preferred, yaw);
    if (ProbeLocomotionDirectionClear(perception, habitat, body_origin, dir,
                                      bounds_size, skip_id, kProbeDistance))
    {
      out_direction = dir;
      return true;
    }
  }
  // Soft: always advance toward preferred; motor + habitat gate arbitrate.
  out_direction = preferred;
  return true;
}

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
  const float speed = glm::length(delta) / dt;
  return speed < min_speed;
}

float SeparationQueryRadius(const glm::vec3 &bounds_size)
{
  const float footprint =
      std::max(bounds_size.x, std::max(bounds_size.y, bounds_size.z));
  return footprint * 1.5f + 0.35f;
}

} // namespace cutum
