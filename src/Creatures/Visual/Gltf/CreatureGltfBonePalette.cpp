#include "Creatures/Visual/Gltf/CreatureGltfBonePalette.h"

#include "Creatures/Visual/Gltf/CreatureGltfAnimPlayer.h"
#include <glm/gtc/matrix_transform.hpp>
#include <cmath>

namespace cutum
{

namespace
{

glm::mat4 NodeLocalMatrix(const GltfNodeCpu &node)
{
  glm::mat4 m = glm::translate(glm::mat4(1.f), node.translation);
  m = m * glm::mat4_cast(node.rotation);
  m = glm::scale(m, node.scale);
  return m;
}

glm::mat4 SampleNodeLocal(const GltfNodeCpu &rest, const GltfAnimationCpu *anim,
                          int nodeIndex, float timeSec, bool loop)
{
  glm::vec3 translation = rest.translation;
  glm::quat rotation = rest.rotation;
  glm::vec3 scale = rest.scale;
  if (!anim)
  {
    return NodeLocalMatrix(rest);
  }

  float duration = 0.f;
  for (const GltfAnimationChannelCpu &ch : anim->channels)
  {
    if (ch.nodeIndex == nodeIndex && !ch.keyTimes.empty())
    {
      duration = std::max(duration, ch.keyTimes.back());
    }
  }
  float t = timeSec;
  if (loop && duration > 0.f)
  {
    t = std::fmod(timeSec, duration);
    if (t < 0.f)
    {
      t += duration;
    }
  }

  for (const GltfAnimationChannelCpu &ch : anim->channels)
  {
    if (ch.nodeIndex != nodeIndex || ch.keyTimes.empty())
    {
      continue;
    }
    size_t i1 = 1;
    while (i1 < ch.keyTimes.size() && ch.keyTimes[i1] < t)
    {
      ++i1;
    }
    const size_t i0 = (i1 > 0) ? i1 - 1 : 0;
    i1 = std::min(i1, ch.keyTimes.size() - 1);
    const float t0 = ch.keyTimes[i0];
    const float t1 = ch.keyTimes[i1];
    const float alpha = (t1 > t0) ? (t - t0) / (t1 - t0) : 0.f;

    if (ch.path == "translation" && i0 < ch.keyVec3.size())
    {
      const glm::vec3 a = ch.keyVec3[i0];
      const glm::vec3 b = ch.keyVec3[std::min(i1, ch.keyVec3.size() - 1)];
      translation = glm::mix(a, b, alpha);
    }
    else if (ch.path == "scale" && i0 < ch.keyVec3.size())
    {
      const glm::vec3 a = ch.keyVec3[i0];
      const glm::vec3 b = ch.keyVec3[std::min(i1, ch.keyVec3.size() - 1)];
      scale = glm::mix(a, b, alpha);
    }
    else if (ch.path == "rotation" && i0 < ch.keyQuat.size())
    {
      const glm::quat a = ch.keyQuat[i0];
      const glm::quat b = ch.keyQuat[std::min(i1, ch.keyQuat.size() - 1)];
      rotation = glm::slerp(a, b, alpha);
    }
  }

  glm::mat4 m = glm::translate(glm::mat4(1.f), translation);
  m = m * glm::mat4_cast(rotation);
  m = glm::scale(m, scale);
  return m;
}

glm::mat4 GlobalNodeMatrix(const CreatureGltfMeshAsset &asset, int nodeIndex,
                           const std::vector<glm::mat4> &locals)
{
  glm::mat4 global = locals[static_cast<size_t>(nodeIndex)];
  const int parent = asset.nodes[static_cast<size_t>(nodeIndex)].parent;
  if (parent >= 0)
  {
    global = GlobalNodeMatrix(asset, parent, locals) * global;
  }
  return global;
}

} // namespace

std::vector<glm::mat4>
ComputeGltfSkinMatrices(const CreatureGltfMeshAsset &asset,
                        const GltfAnimationCpu *animation, float timeSec,
                        bool loop)
{
  std::vector<glm::mat4> locals(asset.nodes.size(), glm::mat4(1.f));
  for (size_t i = 0; i < asset.nodes.size(); ++i)
  {
    locals[i] = SampleNodeLocal(asset.nodes[i], animation,
                                static_cast<int>(i), timeSec, loop);
  }

  std::vector<glm::mat4> palette(asset.skin.jointNodes.size(), glm::mat4(1.f));
  for (size_t j = 0; j < asset.skin.jointNodes.size(); ++j)
  {
    const int nodeIndex = asset.skin.jointNodes[j];
    const glm::mat4 global = GlobalNodeMatrix(asset, nodeIndex, locals);
    if (j < asset.skin.inverseBindMatrices.size())
    {
      palette[j] = global * asset.skin.inverseBindMatrices[j];
    }
    else
    {
      palette[j] = global;
    }
  }
  return palette;
}

} // namespace cutum
