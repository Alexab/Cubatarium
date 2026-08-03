#include "Creatures/Influence/StatusEffectCatalog.h"

namespace cutum
{

UStatusEffectCatalog &UStatusEffectCatalog::Get()
{
  static UStatusEffectCatalog catalog;
  catalog.EnsureBuiltins();
  return catalog;
}

void UStatusEffectCatalog::EnsureBuiltins()
{
  if (BuiltinsReady)
  {
    return;
  }
  {
    StatusEffectDef bleed;
    bleed.Id = "bleed";
    bleed.DurationSec = 4.f;
    bleed.TickIntervalSec = 1.f;
    bleed.HealthPerTick = -2.f;
    bleed.Stack = StatusStackPolicy::Refresh;
    Register(bleed);
  }
  {
    StatusEffectDef slow;
    slow.Id = "slow";
    slow.DurationSec = 3.f;
    slow.TickIntervalSec = 0.f;
    slow.MoveSpeedMul = 0.6f;
    slow.AgilityDelta = -2;
    slow.Stack = StatusStackPolicy::Refresh;
    Register(slow);
  }
  BuiltinsReady = true;
}

void UStatusEffectCatalog::Register(const StatusEffectDef &def)
{
  if (def.Id.empty())
  {
    return;
  }
  ById[def.Id] = def;
}

const StatusEffectDef *UStatusEffectCatalog::Find(const std::string &id) const
{
  const auto it = ById.find(id);
  return it == ById.end() ? nullptr : &it->second;
}

} // namespace cutum
