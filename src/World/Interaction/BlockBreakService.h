#ifndef BLOCK_BREAK_SERVICE_H
#define BLOCK_BREAK_SERVICE_H

#include "Creatures/Influence/DigSessionState.h"

#include <glm/glm.hpp>
#include <optional>
#include <string>

namespace cutum
{

class UWorld;

/// Owns dig/break session state; UWorld delegates break API here.
class UBlockBreakService
{
public:
  void Start(glm::ivec3 blockPos, float pendingWearDelta = 0.f,
             std::string pendingToolId = {});
  void Cancel();
  void Tick(float dt, float durationSeconds);
  bool Complete(UWorld &world);

  float GetProgress() const;
  bool HasSession() const { return Session.has_value(); }
  std::optional<glm::ivec3> GetBlockPos() const;

private:
  std::optional<DigSessionState> Session;
};

} // namespace cutum

#endif
