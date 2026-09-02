#pragma once

#include <cstdint>

namespace cutum
{

/// Immutable capture contract (M2a / §A.4).
struct MeshCaptureToken
{
  uint64_t world_epoch{0};
  uint64_t source_revision{0};
  uint64_t capture_id{0};
};

} // namespace cutum
