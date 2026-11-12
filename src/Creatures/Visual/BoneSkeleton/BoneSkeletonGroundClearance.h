#pragma once

#include "Creatures/Definition/CreatureDefinition.h"
#include "Creatures/Locomotion/CreatureLocomotionFacts.h"
#include "Creatures/Visual/BoneSkeleton/CreatureBoneSkeletonTypes.h"
#include <algorithm>
#include <cmath>
#include <limits>

namespace cutum
{
// This is a collision-clearance layer, not foot locking/IK. A positive lift
// prevents the authored mesh from entering the feet plane; intentional hop
// and airborne clearance is never pulled down to that plane.
inline bool UsesBoneGroundClearance(const CreatureLocomotionFacts &facts,
    const CreatureDefinition &def, const std::string &profile)
{
  if (!facts.onGround || (def.habitat != CreatureHabitat::Terrestrial &&
      def.habitat != CreatureHabitat::Amphibious)) return false;
  if (profile == "rabbit" || profile == "aerial" || profile == "aquatic" ||
      profile == "dolphin" || profile == "squid" || profile == "serpentine") return false;
  return facts.state == LocomotionState::Idle || facts.state == LocomotionState::Walk ||
      facts.state == LocomotionState::Run || facts.state == LocomotionState::Crouch;
}

inline float ComputeBoneGroundClearance(const CreatureBoneSkeletonMeshAsset &mesh,
    const std::vector<glm::mat4> &boneMatrices)
{
  if (boneMatrices.size() != mesh.boneMeshes.size()) return 0.f;
  float floor = std::numeric_limits<float>::max();
  for (size_t bone = 0; bone < mesh.boneMeshes.size(); ++bone)
    for (const auto &cube : mesh.boneMeshes[bone].cubes)
    {
      const glm::mat4 matrix = boneMatrices[bone] * cube.restLocalMatrix;
      const auto &vertices = cube.interleavedPosUv;
      // Only the Y row is needed. These are the actual submitted vertices,
      // including thin-axis and rotated cubes, not collider/rest-size corners.
      for (size_t v = 0; v + 4 < vertices.size(); v += 5)
        floor = std::min(floor, matrix[0].y * vertices[v] +
            matrix[1].y * vertices[v+1] + matrix[2].y * vertices[v+2] + matrix[3].y);
    }
  return std::isfinite(floor) ? std::max(0.f, -floor) : 0.f;
}

class BoneGroundClearanceState
{
public:
  float Update(float required, bool grounded, float dt)
  {
    if (grounded) Lift = std::max(0.f, required);
    else if (dt > 0.f) Lift *= std::exp(-20.f * dt);
    return Lift;
  }
private:
  float Lift{0.f};
};
} // namespace cutum
