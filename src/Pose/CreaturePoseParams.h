#ifndef CREATUREPOSEPARAMS_H
#define CREATUREPOSEPARAMS_H

#include <glm/glm.hpp>
#include <string>
#include <unordered_map>

namespace cutum
{

struct CreaturePartPose
{
  glm::vec3 offsetDelta{0.0f};
  glm::vec3 eulerDeg{0.0f};
};

struct CreaturePoseParams
{
  std::unordered_map<std::string, CreaturePartPose> Parts;
  float crouchUpperDrop{0.0f};

  void SetPart(const std::string &Id, CreaturePartPose pose)
  {
    Parts[Id] = pose;
  }
};

} // namespace cutum

#endif
