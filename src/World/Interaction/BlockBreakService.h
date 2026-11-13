#ifndef BLOCK_BREAK_SERVICE_H
#define BLOCK_BREAK_SERVICE_H

#include "Creatures/Influence/DigSessionState.h"

#include <cstddef>
#include <cstdint>
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
  void BindSource(uint64_t owner, size_t slot, uint64_t instance)
  {
    SourceOwner = owner;
    SourceSlot = slot;
    SourceInstance = instance;
  }
  void Cancel();
  void Tick(float dt, float durationSeconds);
  bool Complete(UWorld &world);

  float GetProgress() const;
  bool HasSession() const { return Session.has_value(); }
  std::optional<glm::ivec3> GetBlockPos() const;

private:
  std::optional<DigSessionState> Session;
  uint64_t SourceOwner{0}, SourceInstance{0};
  size_t SourceSlot{0};
};

} // namespace cutum

#endif
