#ifndef CREATURECATALOGTYPES_H
#define CREATURECATALOGTYPES_H

#include <string>
#include <vector>
#include <glm/glm.hpp>

namespace cutum {

struct CreatureCatalogInfo {
 std::vector<std::string> tags;
 bool spawnable{false};
 int sortOrder{0};
};

enum class CreatureRole { Mob, ControlledDefault, Unknown };

inline CreatureRole ParseCreatureRole(const std::string& role)
{
 if (role == "controlled_default") {
  return CreatureRole::ControlledDefault;
 }
 if (role == "mob") {
  return CreatureRole::Mob;
 }
 return CreatureRole::Unknown;
}

inline const char* CreatureRoleToString(CreatureRole role)
{
 switch (role) {
 case CreatureRole::ControlledDefault:
  return "controlled_default";
 case CreatureRole::Mob:
  return "mob";
 default:
  return "unknown";
 }
}

struct CreatureBehaviorParams {
 std::string id{"none"};
 float moveSpeed{2.f};
 float wanderIntervalMin{2.f};
 float wanderIntervalMax{4.f};
};

struct CreatureVisualSpec {
 std::string backend{"rigid_voxels"};
 glm::vec4 wireframeColor{1.f, 1.f, 1.f, 1.f};
 std::string defaultTextureKey;
 std::string iconMode{"bounds_wireframe"};
};

struct ResolvedCreatureAppearance {
 glm::vec4 wireframeColor{1.f, 1.f, 1.f, 1.f};
 std::string visualBackend{"rigid_voxels"};
};

} // namespace cutum

#endif
