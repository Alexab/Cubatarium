#pragma once

#include "Creatures/Visual/Gltf/CreatureGltfBonePalette.h"
#include "Creatures/Visual/Gltf/CreatureGltfAnimPlayer.h"
#include <algorithm>
#include <limits>
#include <optional>
#include <utility>

namespace cutum
{
// Geometry envelope, independent of collider bounds and current animation
// phase. Sample authored keys plus intermediate poses; callers cache per clip.
inline std::optional<std::pair<glm::vec3, glm::vec3>> ComputeGltfPreviewBounds(
    const CreatureGltfMeshAsset &asset, const GltfAnimationCpu *animation,
    bool fullCycle)
{
  std::vector<float> times{0.f};
  if (animation && fullCycle)
  {
    const float duration = GltfAnimationDuration(*animation);
    for (int i = 1; i <= 32; ++i) times.push_back(duration*i/32.f);
    for (const auto &channel : animation->channels)
      times.insert(times.end(), channel.keyTimes.begin(), channel.keyTimes.end());
    std::sort(times.begin(), times.end());
    times.erase(std::unique(times.begin(), times.end()), times.end());
  }
  glm::vec3 low(std::numeric_limits<float>::max()), high(-std::numeric_limits<float>::max());
  bool any = false;
  for (float time : times)
  {
    const auto palette = ComputeGltfSkinMatrices(asset, animation, time, false);
    const glm::mat4 rootAnimation = !asset.hasSkin && animation
        ? SampleGltfRootTransform(*animation, time, false) : glm::mat4(1.f);
    for (const auto &primitive : asset.primitives)
    {
      const auto &vertices = primitive.mesh.interleavedPosUv;
      for (size_t i = 0; i+4 < vertices.size(); i += 5)
      {
        const glm::vec4 position(vertices[i], vertices[i+1], vertices[i+2], 1.f);
        glm::vec4 posed = rootAnimation * position;
        const size_t offset = (i/5)*4;
        if (primitive.skinned && offset+3 < primitive.jointIndices.size() &&
            offset+3 < primitive.jointWeights.size())
        {
          posed = glm::vec4(0.f);
          for (size_t influence = 0; influence < 4; ++influence)
          {
            const size_t joint = primitive.jointIndices[offset+influence];
            const float weight = primitive.jointWeights[offset+influence];
            if (joint < palette.size() && weight > 0.f)
              posed += palette[joint]*position*weight;
          }
        }
        low = glm::min(low, glm::vec3(posed));
        high = glm::max(high, glm::vec3(posed));
        any = true;
      }
    }
  }
  if (!any) return std::nullopt;
  return std::make_pair(low, high);
}
} // namespace cutum
