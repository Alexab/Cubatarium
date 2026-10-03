#ifndef CREATURECATALOGTYPES_H
#define CREATURECATALOGTYPES_H

#include <cstdint>
#include <glm/glm.hpp>
#include <string>
#include <unordered_map>
#include <vector>

namespace cutum
{

struct CreatureCatalogInfo
{
  std::vector<std::string> tags;
  bool spawnable{false};
  int sortOrder{0};
};

enum class CreatureRole
{
  Mob,
  ControlledDefault,
  Bot,
  Unknown
};

inline CreatureRole ParseCreatureRole(const std::string &role)
{
  if (role == "controlled_default")
  {
    return CreatureRole::ControlledDefault;
  }
  if (role == "bot")
  {
    return CreatureRole::Bot;
  }
  if (role == "mob")
  {
    return CreatureRole::Mob;
  }
  return CreatureRole::Unknown;
}

inline const char *CreatureRoleToString(CreatureRole role)
{
  switch (role)
  {
  case CreatureRole::ControlledDefault:
    return "controlled_default";
  case CreatureRole::Bot:
    return "bot";
  case CreatureRole::Mob:
    return "mob";
  default:
    return "unknown";
  }
}

struct CreatureBehaviorParams
{
  std::string Id{"none"};
  float moveSpeed{2.f};
  float wanderIntervalMin{2.f};
  float wanderIntervalMax{4.f};
  float fleeRadius{8.f};
  float fleeSpeedMultiplier{1.2f};
  float safeDistance{12.f};
  float aggroRadius{10.f};
  float attackRange{2.f};
  float attackCooldown{1.5f};
};

enum class CreatureVisualBackend : uint8_t
{
  RigidVoxels,
  BoneSkeleton,
  GltfSkeleton,
};

CreatureVisualBackend ParseCreatureVisualBackend(const std::string &s);
const char *ToString(CreatureVisualBackend backend);
std::string CreatureVisualBackendToString(CreatureVisualBackend backend);

enum class CreatureTextureLayout : uint8_t
{
  RigidCrop,
  PlayerSkinAtlas,
};

CreatureTextureLayout ParseCreatureTextureLayout(const std::string &s);
const char *ToString(CreatureTextureLayout layout);

struct CreatureVisualPartDef
{
  std::string Id;
  glm::vec3 offsetBlocks{0.f};
  glm::vec3 sizeBlocks{0.6f, 1.8f, 0.6f};
  std::string textureStem;
  glm::vec3 PivotBlocks{0.f};
  bool HasPivot{false};
  std::string LimbKind;
  std::string LimbAxis{"x"};
};

struct CreatureAnimationClipDef
{
  float startSec{0.f};
  float endSec{0.f};
  bool loop{true};
  float speed{1.f};
};

struct CreatureAnimationParams
{
  float walkCycleHz{2.0f};
  float legSwingDeg{25.0f};
  float armSwingDeg{15.0f};
  float flyBodyPitchDeg{10.0f};
  float bodyBobBlocks{0.025f};
  float tailSwingDeg{12.0f};
  float runSpeedMultiplier{1.3f};
  float crouchLegBendDeg{25.0f};
  float wingIdleSwingDeg{5.0f};
  float lookAtDeg{30.0f};
  std::unordered_map<std::string, CreatureAnimationClipDef> clips;
  std::unordered_map<std::string, std::string> stateMap;
};

struct CreatureBoneSkeletonSpec
{
  std::string geometryId;
  std::string geometryFile{"geometry.geo.json"};
  std::string textureStem{"diffuse"};
  glm::ivec2 textureSize{64, 32};
  std::string animationProfile;
};

struct CreatureGltfSpec
{
  std::string modelPath;
  std::vector<std::string> texturePaths;
  float modelScale{1.f};
  float modelYawOffsetDeg{0.f};
  /// Extra Y offset (blocks) after automatic feet alignment from bindMinY.
  float modelOffsetY{0.f};
};

struct CreatureRigSpec
{
  std::string templateId{"biped"};
  std::vector<std::string> partIds;
};

struct CreatureSpriteSpec
{
  bool billboard{false};
  glm::vec4 emissiveTint{1.f, 1.f, 1.f, 1.f};
};

struct CreatureVisualSpec
{
  std::string backend{"rigid_voxels"};
  std::string fallbackBackend;
  std::string textureLayout{"rigid_crop"};
  /// Extra Y rotation (degrees) for mob wander facing; 180 if model faces
  /// backward vs Movement.
  float modelYawOffsetDeg{0.f};
  glm::vec4 wireframeColor{1.f, 1.f, 1.f, 1.f};
  std::string defaultTextureKey;
  std::string iconMode{"bounds_wireframe"};
  CreatureRigSpec rig;
  CreatureSpriteSpec sprite;
  CreatureAnimationParams Animation;
  CreatureBoneSkeletonSpec boneSkeleton;
  CreatureGltfSpec gltf;
  /// Relative path under species dir (e.g. rigid_model.json).
  std::string rigidModelPath;
  std::vector<CreatureVisualPartDef> Parts;
};

struct ResolvedCreaturePart
{
  std::string partId;
  glm::vec3 offsetBlocks{0.f};
  glm::vec3 sizeBlocks{0.6f, 1.8f, 0.6f};
  std::string textureAssetKey;
  glm::vec3 PivotBlocks{0.f};
  bool HasPivot{false};
  std::string LimbKind;
  std::string LimbAxis{"x"};
};

struct ResolvedCreatureAppearance
{
  glm::vec4 wireframeColor{1.f, 1.f, 1.f, 1.f};
  std::string visualBackend{"rigid_voxels"};
  std::string textureLayout{"rigid_crop"};
  std::string defaultTextureKey{"body"};
  std::vector<ResolvedCreaturePart> Parts;
  bool useWireframeFallback{false};
};

} // namespace cutum

#endif
