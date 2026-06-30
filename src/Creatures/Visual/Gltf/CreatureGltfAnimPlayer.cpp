#include "Creatures/Visual/Gltf/CreatureGltfAnimPlayer.h"

#include <glm/gtc/quaternion.hpp>
#include <algorithm>
#include <cmath>

namespace cutum
{

glm::mat4 SampleGltfRootTransform(const GltfAnimationCpu &anim, float timeSec,
                                  bool loop)
{
  glm::vec3 translation{0.f};
  glm::quat rotation{1.f, 0.f, 0.f, 0.f};
  glm::vec3 scale{1.f};

  float duration = 0.f;
  for (const GltfAnimationChannelCpu &ch : anim.channels)
  {
    if (!ch.keyTimes.empty())
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

  for (const GltfAnimationChannelCpu &ch : anim.channels)
  {
    if (ch.keyTimes.empty())
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

} // namespace cutum
