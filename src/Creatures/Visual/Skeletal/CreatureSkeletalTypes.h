#ifndef CREATURE_SKELETAL_TYPES_H
#define CREATURE_SKELETAL_TYPES_H

#include <glm/glm.hpp>
#include <string>
#include <unordered_map>
#include <vector>

namespace cutum
{

struct SkeletalCubeDef
{
  glm::vec3 originBlocks{0.f};
  glm::vec3 sizeBlocks{0.f};
  glm::vec3 pivotBlocks{0.f};
  glm::ivec2 uvPixels{0};
  glm::vec3 rotationDeg{0.f};
  float inflateBlocks{0.f};
  bool mirror{false};
  bool hasPivot{false};
};

struct SkeletalBoneDef
{
  std::string name;
  std::string parent;
  glm::vec3 pivotBlocks{0.f};
  /// geometry `bind_pose_rotation` (quadruped torso pitch).
  glm::vec3 bindPoseRotationDeg{0.f};
  /// geometry bone `rotation` field (bee wings, etc.).
  glm::vec3 boneRotationDeg{0.f};
  bool mirror{false};
  std::vector<SkeletalCubeDef> cubes;
};

struct CreatureSkeletalGeometry
{
  std::string identifier;
  glm::ivec2 textureSize{64, 32};
  glm::vec3 visibleBoundsOffsetBlocks{0.f};
  float visibleBoundsWidthBlocks{1.f};
  float visibleBoundsHeightBlocks{1.f};
  std::vector<SkeletalBoneDef> bones;
  std::unordered_map<std::string, size_t> boneIndexByName;
};

struct SkeletalBonePose
{
  glm::vec3 rotationDeg{0.f};
  glm::vec3 offsetBlocks{0.f};
};

struct SkeletalCreaturePose
{
  std::unordered_map<std::string, SkeletalBonePose> bones;
};

struct SkeletalCubeMeshCpu
{
  std::vector<float> interleavedPosUv;
  std::vector<unsigned int> indices;
  glm::mat4 restLocalMatrix{1.f};
};

struct SkeletalBoneMeshCpu
{
  std::string boneName;
  std::vector<SkeletalCubeMeshCpu> cubes;
};

struct CreatureSkeletalMeshAsset
{
  CreatureSkeletalGeometry geometry;
  std::vector<SkeletalBoneMeshCpu> boneMeshes;
};

} // namespace cutum

#endif
