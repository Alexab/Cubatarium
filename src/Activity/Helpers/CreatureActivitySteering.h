#ifndef CREATUREACTIVITYSTEERING_H
#define CREATUREACTIVITYSTEERING_H

#include "Activity/CreatureActivityTypes.h"
#include "Activity/IUWorldPerception.h"
#include "Creatures/Locomotion/LocomotionTypes.h"
#include <glm/glm.hpp>
#include <vector>

namespace cutum
{

glm::vec3 RandomLocomotionDirection(CreatureHabitat habitat);

bool PickLocomotionDirection(IUWorldPerception &perception,
                             const CreatureActivityView &view,
                             CreatureHabitat habitat,
                             const glm::vec3 &bounds_size,
                             glm::vec3 &out_direction);

/// True when a short forward probe along `dir` is volume+habitat clear.
bool ProbeLocomotionDirectionClear(IUWorldPerception &perception,
                                   CreatureHabitat habitat,
                                   const glm::vec3 &body_origin,
                                   const glm::vec3 &dir,
                                   const glm::vec3 &bounds_size,
                                   CreatureId skip_id,
                                   float probe_distance = 0.65f);

/// Prefer `preferred` (chase/flee), then yaw offsets, before random slide.
bool PickApproachDirection(IUWorldPerception &perception,
                           CreatureHabitat habitat,
                           const glm::vec3 &body_origin,
                           const glm::vec3 &preferred_dir,
                           const glm::vec3 &bounds_size, CreatureId skip_id,
                           glm::vec3 &out_direction);

/// A* clearance height: slightly shorter than visual AABB so 2-block gaps and
/// light foliage do not exhaust search for tall bipeds (zombie 1.85).
float NavigationBodyHeightForBounds(float bounds_height_blocks);

glm::vec3 ComputeSeparationDirection(
    const glm::vec3 &self_origin, const glm::vec3 &bounds_size,
    const std::vector<CreatureNeighborView> &neighbors, float min_distance);

glm::vec3 BlendLocomotionDirection(const glm::vec3 &base_dir,
                                   const glm::vec3 &separation_dir,
                                   float separation_weight);

bool IsLocomotionStuck(const glm::vec3 &prev_origin,
                       const glm::vec3 &cur_origin, float dt,
                       float min_speed);

float SeparationQueryRadius(const glm::vec3 &bounds_size);

} // namespace cutum

#endif
