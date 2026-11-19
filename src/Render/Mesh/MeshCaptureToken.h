#pragma once

#include "World/Mesh/Contracts/MeshLightInputs.h"
#include <cstdint>

namespace cutum
{

/// Immutable capture contract (M2a / §A.4). Maps to WorkToken on the mesh path.
struct MeshCaptureToken
{
  uint64_t world_epoch{0};
  uint64_t source_revision{0};
  uint64_t capture_id{0};
  MeshLightInputs light_inputs{MeshLightInputs::Baked};
  uint64_t capture_policy_generation{1};
};

} // namespace cutum
