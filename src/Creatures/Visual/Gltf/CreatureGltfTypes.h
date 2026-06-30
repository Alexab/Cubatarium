#ifndef CREATUREGLTFTYPES_H
#define CREATUREGLTFTYPES_H

#include "Creatures/Visual/BoneSkeleton/CreatureBoneSkeletonTypes.h"
#include <glm/gtc/quaternion.hpp>
#include <cstdint>
#include <string>
#include <unordered_map>
#include <vector>

namespace cutum
{

struct GltfNodeCpu
{
  std::string name;
  int parent{-1};
  glm::vec3 translation{0.f};
  glm::quat rotation{1.f, 0.f, 0.f, 0.f};
  glm::vec3 scale{1.f};
};

struct GltfSkinCpu
{
  std::vector<int> jointNodes;
  std::vector<glm::mat4> inverseBindMatrices;
};

struct GltfPrimitiveCpu
{
  BoneSkeletonCubeMeshCpu mesh;
  std::vector<uint8_t> jointIndices;
  std::vector<float> jointWeights;
  std::string textureStem;
  bool skinned{false};
};

struct GltfAnimationChannelCpu
{
  int nodeIndex{0};
  std::string path; // translation | rotation | scale
  std::vector<float> keyTimes;
  std::vector<glm::vec3> keyVec3;
  std::vector<glm::quat> keyQuat;
};

struct GltfAnimationCpu
{
  std::string name;
  std::vector<GltfAnimationChannelCpu> channels;
};

struct CreatureGltfMeshAsset
{
  std::vector<GltfNodeCpu> nodes;
  GltfSkinCpu skin;
  std::vector<GltfPrimitiveCpu> primitives;
  std::vector<GltfAnimationCpu> animations;
  std::unordered_map<std::string, size_t> animationIndexByName;
  int rootNodeIndex{0};
  bool hasSkin{false};
  bool loaded{false};
  /// Lowest bind-pose vertex Y in model space (Luanti b3d meshes extend below origin).
  float bindMinY{0.f};
};

} // namespace cutum

#endif
