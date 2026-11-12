#ifndef CREATURECATALOGTYPES_H
#define CREATURECATALOGTYPES_H

#include <cstdint>
#include <algorithm>
#include <glm/glm.hpp>
#include <string>
#include <vector>
#include <unordered_map>
#include <vector>

namespace cutum
{

/// When the species may naturally spawn. Hostiles default to Night when the
/// catalog leaves this at Any (Minecraft-style surface rule); underground
/// spawn points bypass the window.
enum class CreatureActiveTime : uint8_t
{
  Any,
  Day,
  Night
};

inline CreatureActiveTime ParseCreatureActiveTime(const std::string &value)
{
  if (value == "day")
  {
    return CreatureActiveTime::Day;
  }
  if (value == "night")
  {
    return CreatureActiveTime::Night;
  }
  return CreatureActiveTime::Any;
}

inline const char *CreatureActiveTimeToString(CreatureActiveTime value)
{
  switch (value)
  {
  case CreatureActiveTime::Day:
    return "day";
  case CreatureActiveTime::Night:
    return "night";
  case CreatureActiveTime::Any:
  default:
    return "any";
  }
}

/// Effective natural-spawn window for a species: hostiles without explicit
/// data spawn at night (or underground, see AllowsNaturalSpawnTime).
inline CreatureActiveTime EffectiveNaturalActiveTime(
    const std::vector<std::string> &tags, CreatureActiveTime activeTime);

struct CreatureCatalogInfo
{
  std::vector<std::string> tags;
  bool spawnable{false};
  bool naturalSpawnable{false};
  float spawnWeight{1.f};
  int spawnGroupMin{1};
  int spawnGroupMax{1};
  int sortOrder{0};
  /// Biome ids (WorldGen registry names) the species naturally spawns in;
  /// empty = any biome.
  std::vector<std::string> spawnBiomes;
  CreatureActiveTime activeTime{CreatureActiveTime::Any};
};

/// Biome gate for natural spawns: an empty list is "anywhere".
inline bool SpeciesAllowedInBiome(const CreatureCatalogInfo &catalog,
                                  const std::string &biomeId)
{
  return catalog.spawnBiomes.empty() ||
         std::find(catalog.spawnBiomes.begin(), catalog.spawnBiomes.end(),
                   biomeId) != catalog.spawnBiomes.end();
}

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

inline bool HasCreatureTag(const std::vector<std::string> &tags,
                             const char *tag)
{
  return std::find(tags.begin(), tags.end(), tag) != tags.end();
}

inline CreatureActiveTime EffectiveNaturalActiveTime(
    const std::vector<std::string> &tags, CreatureActiveTime activeTime)
{
  if (activeTime == CreatureActiveTime::Any &&
      HasCreatureTag(tags, "hostile"))
  {
    return CreatureActiveTime::Night;
  }
  return activeTime;
}

/// Natural-spawn window check. `underground` (the point sits below the
/// surface) lets night species spawn in caves at any hour.
inline bool AllowsNaturalSpawnTime(const std::vector<std::string> &tags,
                                   CreatureActiveTime activeTime, bool isNight,
                                   bool underground)
{
  const CreatureActiveTime window =
      EffectiveNaturalActiveTime(tags, activeTime);
  if (window == CreatureActiveTime::Any)
  {
    return true;
  }
  if (underground && window == CreatureActiveTime::Night)
  {
    return true;
  }
  return window == CreatureActiveTime::Night ? isNight : !isNight;
}

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
  bool loop{true};
  float speed{1.f};
  // Opt-in glTF grounded gait: world blocks travelled over the entire file
  // loop, including every stride in a multi-rate upper-body macroloop.
  // Zero retains legacy speed/reference/minimum-rate playback.
  float groundDistancePerCycle{0.f};
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
};

struct CreatureSpriteSpec
{
  bool billboard{false};
  glm::vec4 emissiveTint{1.f, 1.f, 1.f, 1.f};
};

struct CreatureVisualSpec
{
  /// Runtime locator of the definition's folder; never serialized into assets.
  std::string sourceDirectory;
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
  // Original material key also defines the UV layout and remains a fallback
  // when an optional mapped skin file is absent.
  std::string baseTextureAssetKey;
  glm::vec3 PivotBlocks{0.f};
  bool HasPivot{false};
  std::string LimbKind;
  std::string LimbAxis{"x"};
};

struct ResolvedCreatureAppearance
{
  // Validated against the species by ResolveCreatureAppearance. Backends
  // must not bypass ownership or the player's effective selection.
  std::string effectiveSkinId;
  std::unordered_map<std::string, std::string> skinTextureOverrides;
  glm::vec4 wireframeColor{1.f, 1.f, 1.f, 1.f};
  std::string visualBackend{"rigid_voxels"};
  std::string textureLayout{"rigid_crop"};
  std::string defaultTextureKey{"body"};
  std::vector<ResolvedCreaturePart> Parts;
  bool useWireframeFallback{false};
};

} // namespace cutum

#endif
