#ifndef CREATUREGLTFBONEPALETTE_H
#define CREATUREGLTFBONEPALETTE_H

#include "Creatures/Visual/Gltf/CreatureGltfTypes.h"
#include <glm/glm.hpp>
#include <vector>

namespace cutum
{

/// Clip length: the last key time over all channels.
float GltfAnimationDuration(const GltfAnimationCpu &anim);

/// Bone palette at `timeSec` (wrapped by the clip duration when `loop`).
std::vector<glm::mat4>
ComputeGltfSkinMatrices(const CreatureGltfMeshAsset &asset,
                        const GltfAnimationCpu *animation, float timeSec,
                        bool loop);

/// Bone palette at clip-local times, with the pose of `fadeAnimation` mixed
/// in by `fadeWeight` (0 = only `animation`, 1 = only the faded clip) per
/// node translation/rotation/scale.
std::vector<glm::mat4> ComputeGltfSkinMatricesBlended(
    const CreatureGltfMeshAsset &asset, const GltfAnimationCpu *animation,
    float clipTimeSec, const GltfAnimationCpu *fadeAnimation,
    float fadeClipTimeSec, float fadeWeight);

/// Reusable per-visual work arrays for ComputeGltfSkinMatricesInto.
struct GltfSkinScratch
{
  std::vector<glm::mat4> locals;
  std::vector<glm::mat4> globals;
  std::vector<unsigned char> globalReady;
};

/// Same as ComputeGltfSkinMatricesBlended, writing into `palette` and using
/// `scratch`: no allocation per frame once the arrays have grown.
void ComputeGltfSkinMatricesInto(const CreatureGltfMeshAsset &asset,
                                 const GltfAnimationCpu *animation,
                                 float clipTimeSec,
                                 const GltfAnimationCpu *fadeAnimation,
                                 float fadeClipTimeSec, float fadeWeight,
                                 GltfSkinScratch &scratch,
                                 std::vector<glm::mat4> &palette);

} // namespace cutum

#endif
