#pragma once

#include "World/Lighting/ILightingSeedBackend.h"

namespace cutum
{

class UWorld;

/// Desktop commit adapter; its current seed implementation runs on CPU.
class UGpuLightingSeedBackend : public ILightingSeedBackend
{
public:
  UGpuLightingSeedBackend(UWorld &world, int relight_min, int relight_max);
  LightingSeedResult TrySeedColumnAtCommit(glm::ivec3 ground,
                                           double budget_ms) override;

private:
  UWorld &World;
  int RelightMin{0};
  int RelightMax{0};
};

} // namespace cutum
