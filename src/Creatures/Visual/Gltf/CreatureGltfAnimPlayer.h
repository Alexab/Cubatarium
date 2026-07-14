#ifndef CREATUREGLTFANIMPLAYER_H
#define CREATUREGLTFANIMPLAYER_H

#include "Creatures/Visual/Gltf/CreatureGltfTypes.h"
#include <glm/glm.hpp>

namespace cutum
{

glm::mat4 SampleGltfRootTransform(const GltfAnimationCpu &anim, float timeSec,
                                  bool loop);

} // namespace cutum

#endif
