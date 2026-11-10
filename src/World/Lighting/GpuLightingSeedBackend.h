#pragma once

#include "World/Lighting/ILightingSeedBackend.h"

namespace cutum
{

class UWorld;

class GpuLightingSeedBackend : public ILightingSeedBackend
{
public:
  GpuLightingSeedBackend(UWorld &world, int relight_min, int relight_max);
  LightingSeedResult TrySeedColumnAtCommit(glm::ivec3 ground,
                                           double budget_ms) override;

private:
  UWorld &World;
  int RelightMin{0};
  int RelightMax{0};
};

} // namespace cutum
