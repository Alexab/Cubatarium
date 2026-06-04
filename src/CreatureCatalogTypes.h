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

struct CreatureVisualPartDef {
 std::string id;
 glm::vec3 offsetBlocks{0.f};
 glm::vec3 sizeBlocks{0.6f, 1.8f, 0.6f};
 std::string textureStem;
};

struct CreatureAnimationParams {
 float walkCycleHz{2.0f};
 float legSwingDeg{25.0f};
 float armSwingDeg{15.0f};
 float flyBodyPitchDeg{10.0f};
};

struct CreatureRigSpec {
 std::string templateId{"biped"};
 std::vector<std::string> partIds;
};

struct CreatureVisualSpec {
 std::string backend{"rigid_voxels"};
 glm::vec4 wireframeColor{1.f, 1.f, 1.f, 1.f};
 std::string defaultTextureKey;
 std::string iconMode{"bounds_wireframe"};
 CreatureRigSpec rig;
 CreatureAnimationParams animation;
 std::vector<CreatureVisualPartDef> parts;
};

struct ResolvedCreaturePart {
 std::string partId;
 glm::vec3 offsetBlocks{0.f};
 glm::vec3 sizeBlocks{0.6f, 1.8f, 0.6f};
 std::string textureAssetKey;
};

struct ResolvedCreatureAppearance {
 glm::vec4 wireframeColor{1.f, 1.f, 1.f, 1.f};
 std::string visualBackend{"rigid_voxels"};
 std::vector<ResolvedCreaturePart> parts;
 bool useWireframeFallback{false};
};

} // namespace cutum

#endif
