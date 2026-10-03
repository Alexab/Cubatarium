#ifndef DIG_SESSION_STATE_H
#define DIG_SESSION_STATE_H

#include <algorithm>
#include <glm/glm.hpp>
#include <string>

namespace cutum
{

/// Mutable progress and deferred tool wear for one block-dig interaction.
struct DigSessionState
{
  glm::ivec3 blockPos{0};
  float progress{0.f};
  float pendingWearDelta{0.f};
  std::string pendingToolId;

  void Start(glm::ivec3 pos)
  {
    blockPos = pos;
    progress = 0.f;
    pendingWearDelta = 0.f;
    pendingToolId.clear();
  }

  void Cancel()
  {
    progress = 0.f;
    pendingWearDelta = 0.f;
    pendingToolId.clear();
  }

  void Tick(float dt, float durationSeconds)
  {
    if (durationSeconds < 0.f)
    {
      return;
    }
    if (durationSeconds <= 0.f)
    {
      progress = 1.f;
      return;
    }
    progress =
        std::min(1.f, progress + std::max(0.f, dt) / durationSeconds);
  }

  bool Complete() const { return progress >= 1.f; }
};

} // namespace cutum

#endif
