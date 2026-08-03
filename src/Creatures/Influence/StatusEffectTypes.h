#ifndef STATUS_EFFECT_TYPES_H
#define STATUS_EFFECT_TYPES_H

#include <string>

namespace cutum
{

enum class StatusStackPolicy
{
  Refresh = 0,
  Stack,
  IgnoreIfPresent
};

struct StatusEffectDef
{
  std::string Id;
  float DurationSec{3.f};
  float TickIntervalSec{1.f};
  float HealthPerTick{0.f};
  float MoveSpeedMul{1.f};
  int StrengthDelta{0};
  int AgilityDelta{0};
  StatusStackPolicy Stack{StatusStackPolicy::Refresh};
  int MaxStacks{1};
};

struct StatusEffectInstance
{
  std::string DefId;
  float RemainingSec{0.f};
  float TickAccumulator{0.f};
  int Stacks{1};
};

} // namespace cutum

#endif
