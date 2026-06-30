#ifndef CREATURE_BONE_SKELETON_TYPES_H
#define CREATURE_BONE_SKELETON_TYPES_H

#include <glm/glm.hpp>
#include <string>
#include <unordered_map>
#include <vector>

namespace cutum
{

struct BoneSkeletonCubeDef
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

struct BoneSkeletonBoneDef
{
  std::string name;
  std::string parent;
  glm::vec3 pivotBlocks{0.f};
  /// geometry `bind_pose_rotation` (quadruped torso pitch).
  glm::vec3 bindPoseRotationDeg{0.f};
  /// geometry bone `rotation` field (bee wings, etc.).
  glm::vec3 boneRotationDeg{0.f};
  bool mirror{false};
  std::vector<BoneSkeletonCubeDef> cubes;
};

struct CreatureBoneSkeletonGeometry
{
  std::string identifier;
  glm::ivec2 textureSize{64, 32};
  glm::vec3 visibleBoundsOffsetBlocks{0.f};
  float visibleBoundsWidthBlocks{1.f};
  float visibleBoundsHeightBlocks{1.f};
  std::vector<BoneSkeletonBoneDef> bones;
  std::unordered_map<std::string, size_t> boneIndexByName;
};

struct BoneSkeletonBonePose
{
  glm::vec3 rotationDeg{0.f};
  glm::vec3 offsetBlocks{0.f};
};

struct BoneSkeletonPose
{
  std::unordered_map<std::string, BoneSkeletonBonePose> bones;
};

struct BoneSkeletonCubeMeshCpu
{
  std::vector<float> interleavedPosUv;
  std::vector<unsigned int> indices;
  glm::mat4 restLocalMatrix{1.f};
};

struct BoneSkeletonBoneMeshCpu
{
  std::string boneName;
  std::vector<BoneSkeletonCubeMeshCpu> cubes;
};

struct CreatureBoneSkeletonMeshAsset
{
  CreatureBoneSkeletonGeometry geometry;
  std::vector<BoneSkeletonBoneMeshCpu> boneMeshes;
};

} // namespace cutum

#endif
