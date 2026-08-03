#include "Creatures/Influence/StatusEffectSystem.h"
#include "Creatures/Core/Creature.h"
#include "Creatures/Influence/StatusEffectCatalog.h"
#include "Creatures/Stats/CreatureVitalsSystem.h"
#include "World/Core/World.h"
#include <algorithm>
#include <vector>

namespace cutum
{

void StatusEffectSystem::ApplyStatus(UCreature &target, const std::string &def_id)
{
  const StatusEffectDef *def = UStatusEffectCatalog::Get().Find(def_id);
  if (!def)
  {
    return;
  }
  auto &list = target.GetStatusEffects();
  for (StatusEffectInstance &inst : list)
  {
    if (inst.DefId != def_id)
    {
      continue;
    }
    switch (def->Stack)
    {
    case StatusStackPolicy::IgnoreIfPresent:
      return;
    case StatusStackPolicy::Stack:
      inst.Stacks = std::min(def->MaxStacks, inst.Stacks + 1);
      inst.RemainingSec = def->DurationSec;
      return;
    case StatusStackPolicy::Refresh:
    default:
      inst.RemainingSec = def->DurationSec;
      inst.TickAccumulator = 0.f;
      return;
    }
  }
  StatusEffectInstance created;
  created.DefId = def_id;
  created.RemainingSec = def->DurationSec;
  created.Stacks = 1;
  list.push_back(created);
}

float StatusEffectSystem::GetMoveSpeedMultiplier(const UCreature &creature)
{
  float mul = 1.f;
  for (const StatusEffectInstance &inst : creature.GetStatusEffects())
  {
    const StatusEffectDef *def = UStatusEffectCatalog::Get().Find(inst.DefId);
    if (!def)
    {
      continue;
    }
    mul *= def->MoveSpeedMul;
  }
  return mul;
}

void StatusEffectSystem::Tick(UWorld &world, WorldGameMode mode, float dt)
{
  if (dt <= 0.f)
  {
    return;
  }
  std::vector<CreatureId> to_remove;
  world.ForEachCreature(
      [&](UCreature &creature)
      {
        auto &list = creature.GetStatusEffects();
        for (size_t i = 0; i < list.size();)
        {
          StatusEffectInstance &inst = list[i];
          const StatusEffectDef *def =
              UStatusEffectCatalog::Get().Find(inst.DefId);
          if (!def)
          {
            list.erase(list.begin() + static_cast<std::ptrdiff_t>(i));
            continue;
          }
          inst.RemainingSec -= dt;
          if (def->TickIntervalSec > 0.f && def->HealthPerTick != 0.f)
          {
            inst.TickAccumulator += dt;
            while (inst.TickAccumulator >= def->TickIntervalSec)
            {
              inst.TickAccumulator -= def->TickIntervalSec;
              const float amount =
                  -def->HealthPerTick * static_cast<float>(inst.Stacks);
              if (amount > 0.f)
              {
                if (CreatureVitalsSystem::ApplyDamage(world, creature, amount,
                                                      mode, "status"))
                {
                  to_remove.push_back(creature.GetId());
                }
              }
              else if (amount < 0.f)
              {
                CreatureVitals &v = creature.GetVitals();
                v.health = std::min(v.maxHealth, v.health - amount);
                v.ClampCurrents();
              }
            }
          }
          if (inst.RemainingSec <= 0.f)
          {
            list.erase(list.begin() + static_cast<std::ptrdiff_t>(i));
            continue;
          }
          ++i;
        }
      });
  for (CreatureId id : to_remove)
  {
    world.RemoveCreature(id);
  }
}

} // namespace cutum
