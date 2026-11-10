#pragma once

#include <glm/glm.hpp>

namespace cutum
{

struct LightingSeedResult
{
  bool applied{false};
  double elapsed_ms{0.0};
};

/// Backend capability: commit-time skylight seed (V3/E4 parity).
class ILightingSeedBackend
{
public:
  virtual ~ILightingSeedBackend() = default;
  virtual LightingSeedResult TrySeedColumnAtCommit(glm::ivec3 ground,
                                                   double budget_ms) = 0;
};

} // namespace cutum
