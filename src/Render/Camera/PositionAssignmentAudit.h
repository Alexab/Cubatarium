#pragma once

#include <cstdint>
#include <glm/glm.hpp>

namespace cutum
{

/// External position assignments are distinct from integrated motor movement.
/// Count every axis, including sub-block corrections; unchanged setter calls
/// are harmless. Native diagnostic reports take a baseline after world load.
class PositionAssignmentAudit
{
public:
  void NoteAssignment(const glm::vec3 &before, const glm::vec3 &after)
  {
    if (before != after)
    {
      ++Count_;
    }
  }

  uint64_t Count() const { return Count_; }

private:
  uint64_t Count_{0};
};

} // namespace cutum
