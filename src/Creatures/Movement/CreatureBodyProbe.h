#ifndef CREATUREBODYPROBE_H
#define CREATUREBODYPROBE_H

#include "Creatures/Movement/CreatureFootprint.h"
#include "Creatures/Movement/CreatureHabitatPolicy.h"
#include "Creatures/Locomotion/LocomotionTypes.h"
#include <algorithm>
#include <cstdint>
#include <glm/glm.hpp>

namespace cutum
{

using CreatureId = uint64_t;

class UWorld;

struct BodyMoveResult
{
  glm::vec3 resolvedOrigin{0.0f};
  float movedXZ{0.0f};
  bool blocked{true};
  bool blockedGeometry{true};
  bool blockedHabitat{true};
  bool habitatOk{false};
  FootprintSample footprintAtTarget{};
  EnvironmentSample envAtTarget{};
};

constexpr float kMinMoveApplyXZ = 0.02f;

inline float CreatureBodyRadius(const glm::vec3 &sizeBlocks)
{
  return std::max(sizeBlocks.x, sizeBlocks.z) * 0.5f;
}

inline float MinWanderProbeXZ(const glm::vec3 &sizeBlocks)
{
  return std::max(0.04f, CreatureBodyRadius(sizeBlocks) * 0.2f);
}

inline float WanderProbeDistance(const glm::vec3 &sizeBlocks)
{
  return std::max(0.8f, CreatureBodyRadius(sizeBlocks) * 2.5f);
}

inline BodyMoveResult EvaluateResolvedMove(const glm::vec3 &origin,
                                           const glm::vec3 &resolved,
                                           CreatureHabitat habitat,
                                           HabitatContext targetContext,
                                           const EnvironmentSample &envAtTarget,
                                           const glm::vec3 &sizeBlocks)
{
  BodyMoveResult result;
  result.resolvedOrigin = resolved;
  result.envAtTarget = envAtTarget;
  const glm::vec2 deltaXZ(resolved.x - origin.x, resolved.z - origin.z);
  result.movedXZ = glm::length(deltaXZ);
  const float minStep = targetContext == HabitatContext::WanderTarget
                            ? MinWanderProbeXZ(sizeBlocks)
                            : kMinMoveApplyXZ;
  result.blockedGeometry = result.movedXZ < minStep;
  result.habitatOk = HabitatAllows(habitat, targetContext, envAtTarget);
  result.blockedHabitat = !result.habitatOk;
  result.blocked = result.blockedGeometry || result.blockedHabitat;
  return result;
}

BodyMoveResult ProbeMove(const UWorld &world, CreatureId id,
                         const glm::vec3 &origin, const glm::vec3 &delta,
                         CreatureHabitat habitat, const glm::vec3 &sizeBlocks,
                         HabitatContext targetContext);

} // namespace cutum

#endif
