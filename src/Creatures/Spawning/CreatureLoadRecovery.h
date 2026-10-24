#ifndef CREATURELOADRECOVERY_H
#define CREATURELOADRECOVERY_H

#include <glm/glm.hpp>

#include <optional>

namespace cutum
{

/// Search nearby positions in deterministic rings. The callbacks keep this
/// policy independent of world storage so the search and unloaded-terrain
/// behavior can be verified without constructing a renderer or UWorld.
template <typename AdjustOrigin, typename IsVolumeLoaded, typename IsSafe>
std::optional<glm::vec3>
FindSafeCreatureOrigin(const glm::vec3 &origin, AdjustOrigin adjustOrigin,
                       IsVolumeLoaded isVolumeLoaded, IsSafe isSafe)
{
  const glm::vec2 directions[] = {
      {1.f, 0.f},  {-1.f, 0.f}, {0.f, 1.f},  {0.f, -1.f},
      {0.707f, 0.707f}, {-0.707f, 0.707f}, {0.707f, -0.707f},
      {-0.707f, -0.707f}};
  auto tryCandidate = [&](const glm::vec3 &candidate)
      -> std::optional<glm::vec3>
  {
    const glm::vec3 adjusted = adjustOrigin(candidate);
    if (!isVolumeLoaded(adjusted) || !isSafe(adjusted))
    {
      return std::nullopt;
    }
    return adjusted;
  };

  if (const auto initial = tryCandidate(origin))
  {
    return initial;
  }
  for (int ring = 1; ring <= 8; ++ring)
  {
    const float distance = static_cast<float>(ring) * 0.5f;
    for (const glm::vec2 &direction : directions)
    {
      for (int verticalStep = 0; verticalStep <= 6; ++verticalStep)
      {
        const float dy = (verticalStep == 0)
                             ? 0.f
                             : ((verticalStep + 1) / 2) * 0.5f *
                                   (verticalStep % 2 == 1 ? 1.f : -1.f);
        const glm::vec3 candidate =
            origin + glm::vec3(direction.x * distance, dy,
                               direction.y * distance);
        if (const auto safe = tryCandidate(candidate))
        {
          return safe;
        }
      }
    }
  }
  return std::nullopt;
}

} // namespace cutum

#endif
