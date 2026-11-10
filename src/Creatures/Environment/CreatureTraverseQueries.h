#ifndef CREATURETRAVERSEQUERIES_H
#define CREATURETRAVERSEQUERIES_H

#include <glm/glm.hpp>

namespace cutum
{

class UWorld;

constexpr float kCreatureStandCollisionSkin = 0.01f;

/// Fast terrestrial stand for A* / feelers / line-of-sight: column ground +
/// block AABB (stand skin). Skips fluid volume scan (too expensive for nav).
bool CanCreatureStandAtNav(const UWorld &world, const glm::vec3 &body_origin,
                           const glm::vec3 &size_blocks,
                           float max_climb_drop_blocks = 1.25f);

/// Post-motor / habitat gate: nav stand + cheap fluid cell sample (not full
/// ProbeEnvironmentAt AABB fluid scan).
bool CanCreatureStandAt(const UWorld &world, const glm::vec3 &body_origin,
                        const glm::vec3 &size_blocks,
                        float max_climb_drop_blocks = 1.25f);

/// Step from → to on XZ: stand at destination + |Δy| within jump/drop +
/// step-up headroom (same rules as nav CanStepTerrestrial).
bool CanCreatureStep(const UWorld &world, const glm::vec3 &from_body,
                     const glm::vec3 &to_body, const glm::vec3 &size_blocks,
                     float max_jump, float max_drop);

/// Sampled XZ line-of-stand (uses nav-fast stand).
bool CanCreatureMoveDirectlyXZ(const UWorld &world, const glm::vec3 &from_body,
                               const glm::vec3 &to_body,
                               const glm::vec3 &size_blocks,
                               float max_climb_drop_blocks,
                               float sample_step = 0.45f);

/// Block collision only (no entity AABB).
bool AreBlocksClearAt(const UWorld &world, const glm::vec3 &body_origin,
                      const glm::vec3 &size_blocks);

/// O(height) fluid sample along the body column at `feet_origin`: liquid
/// blocks and water held inside fluid-permeable decor (kelp, seagrass).
/// Shared by the post-motor habitat gate and the A* node filter.
bool CreatureColumnTouchesFluid(const UWorld &world,
                                const glm::vec3 &feet_origin,
                                float body_height);

} // namespace cutum

#endif
