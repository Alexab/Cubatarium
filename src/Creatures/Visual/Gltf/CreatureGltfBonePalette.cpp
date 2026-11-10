#include "Creatures/Visual/Gltf/CreatureGltfBonePalette.h"

#include "Creatures/Visual/Gltf/CreatureGltfAnimPlayer.h"
#include "Creatures/Visual/Gltf/CreatureGltfClipClock.h"
#include <algorithm>
#include <glm/gtc/quaternion.hpp>
#include <glm/gtc/matrix_transform.hpp>
#include <cmath>

namespace cutum
{

namespace
{

struct NodeTrs
{
  glm::vec3 translation;
  glm::quat rotation;
  glm::vec3 scale;
};

glm::mat4 TrsMatrix(const NodeTrs &trs)
{
  glm::mat4 m = glm::translate(glm::mat4(1.f), trs.translation);
  m = m * glm::mat4_cast(trs.rotation);
  m = glm::scale(m, trs.scale);
  return m;
}

/// `t` is clip-local (already wrapped by the clip duration). Nodes used to
/// wrap by their own channel length, so bones with shorter channels drifted
/// out of sync with the rest of the clip every loop.
NodeTrs SampleNodeTrs(const GltfNodeCpu &rest, const GltfAnimationCpu *anim,
                      int nodeIndex, float t)
{
  NodeTrs trs{rest.translation, rest.rotation, rest.scale};
  if (!anim)
  {
    return trs;
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
    const float alpha =
        (t1 > t0) ? std::clamp((t - t0) / (t1 - t0), 0.f, 1.f) : 0.f;

    if (ch.path == "translation" && i0 < ch.keyVec3.size())
    {
      const glm::vec3 a = ch.keyVec3[i0];
      const glm::vec3 b = ch.keyVec3[std::min(i1, ch.keyVec3.size() - 1)];
      trs.translation = glm::mix(a, b, alpha);
    }
    else if (ch.path == "scale" && i0 < ch.keyVec3.size())
    {
      const glm::vec3 a = ch.keyVec3[i0];
      const glm::vec3 b = ch.keyVec3[std::min(i1, ch.keyVec3.size() - 1)];
      trs.scale = glm::mix(a, b, alpha);
    }
    else if (ch.path == "rotation" && i0 < ch.keyQuat.size())
    {
      const glm::quat a = ch.keyQuat[i0];
      const glm::quat b = ch.keyQuat[std::min(i1, ch.keyQuat.size() - 1)];
      trs.rotation = glm::slerp(a, b, alpha);
    }
  }
  return trs;
}

} // namespace

float GltfAnimationDuration(const GltfAnimationCpu &anim)
{
  float duration = 0.f;
  for (const GltfAnimationChannelCpu &ch : anim.channels)
  {
    if (!ch.keyTimes.empty())
    {
      duration = std::max(duration, ch.keyTimes.back());
    }
  }
  return duration;
}

std::vector<glm::mat4>
ComputeGltfSkinMatrices(const CreatureGltfMeshAsset &asset,
                        const GltfAnimationCpu *animation, float timeSec,
                        bool loop)
{
  const float t =
      animation ? WrapGltfClipTime(timeSec, GltfAnimationDuration(*animation),
                                   loop)
                : 0.f;
  return ComputeGltfSkinMatricesBlended(asset, animation, t, nullptr, 0.f, 0.f);
}

std::vector<glm::mat4> ComputeGltfSkinMatricesBlended(
    const CreatureGltfMeshAsset &asset, const GltfAnimationCpu *animation,
    float clipTimeSec, const GltfAnimationCpu *fadeAnimation,
    float fadeClipTimeSec, float fadeWeight)
{
  GltfSkinScratch scratch;
  std::vector<glm::mat4> palette;
  ComputeGltfSkinMatricesInto(asset, animation, clipTimeSec, fadeAnimation,
                              fadeClipTimeSec, fadeWeight, scratch, palette);
  return palette;
}

void ComputeGltfSkinMatricesInto(const CreatureGltfMeshAsset &asset,
                                 const GltfAnimationCpu *animation,
                                 float clipTimeSec,
                                 const GltfAnimationCpu *fadeAnimation,
                                 float fadeClipTimeSec, float fadeWeight,
                                 GltfSkinScratch &scratch,
                                 std::vector<glm::mat4> &palette)
{
  const bool fading = fadeWeight > 1e-3f;
  const size_t nodeCount = asset.nodes.size();
  scratch.locals.resize(nodeCount);
  for (size_t i = 0; i < nodeCount; ++i)
  {
    const int node = static_cast<int>(i);
    NodeTrs trs = SampleNodeTrs(asset.nodes[i], animation, node, clipTimeSec);
    if (fading)
    {
      const NodeTrs from =
          SampleNodeTrs(asset.nodes[i], fadeAnimation, node, fadeClipTimeSec);
      trs.translation = glm::mix(trs.translation, from.translation, fadeWeight);
      trs.scale = glm::mix(trs.scale, from.scale, fadeWeight);
      trs.rotation = glm::slerp(trs.rotation, from.rotation, fadeWeight);
    }
    scratch.locals[i] = TrsMatrix(trs);
  }

  // Globals memoized per node: each joint used to walk (and multiply) its
  // whole parent chain again.
  scratch.globals.resize(nodeCount);
  scratch.globalReady.assign(nodeCount, 0);
  const auto globalOf = [&](int nodeIndex, const auto &self) -> const glm::mat4 &
  {
    const size_t i = static_cast<size_t>(nodeIndex);
    if (!scratch.globalReady[i])
    {
      const int parent = asset.nodes[i].parent;
      scratch.globals[i] = parent >= 0
                               ? self(parent, self) * scratch.locals[i]
                               : scratch.locals[i];
      scratch.globalReady[i] = 1;
    }
    return scratch.globals[i];
  };

  palette.resize(asset.skin.jointNodes.size());
  for (size_t j = 0; j < asset.skin.jointNodes.size(); ++j)
  {
    const glm::mat4 &global = globalOf(asset.skin.jointNodes[j], globalOf);
    palette[j] = j < asset.skin.inverseBindMatrices.size()
                     ? global * asset.skin.inverseBindMatrices[j]
                     : global;
  }
}

} // namespace cutum
