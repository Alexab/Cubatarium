#ifndef CREATUREACTIVITYTYPES_H
#define CREATUREACTIVITYTYPES_H

#include <cstdint>
#include <glm/glm.hpp>
#include <string>

namespace cutum
{

using CreatureId = uint64_t;

struct ControlledCreatureInfo
{
  CreatureId Id{0};
  glm::vec3 eyePosition{0.0f};
  /// Feet / body origin for navigation stand-nodes (not eye).
  glm::vec3 bodyOrigin{0.0f};
};

struct CreatureActivityView
{
  CreatureId Id{0};
  glm::vec3 bodyOrigin{0.0f};
  std::string typeId;
  std::string behaviorId;
  bool possessed{false};
  bool isPlayerCharacter{false};
};

struct CreatureNeighborView
{
  CreatureId Id{0};
  glm::vec3 bodyOrigin{0.0f};
};

} // namespace cutum

#endif
