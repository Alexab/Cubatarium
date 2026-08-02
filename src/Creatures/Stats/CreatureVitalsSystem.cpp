#include "Creatures/Stats/CreatureVitalsSystem.h"
#include "Creatures/Core/Creature.h"
#include "Creatures/Environment/CreatureEnvironment.h"
#include "Creatures/Locomotion/LocomotionTypes.h"
#include "World/Core/World.h"
#include <algorithm>
#include <cmath>
#include <vector>

namespace cutum
{

namespace
{

constexpr float kSatietyDrainPerSec = 0.15f;
constexpr float kThirstDrainPerSec = 0.2f;
constexpr float kStarveDamagePerSec = 2.0f;
constexpr float kFatigueRecoverPerSec = 4.0f;
constexpr float kFatigueSprintPerSec = 8.0f;
constexpr float kBreathDrainPerSec = 6.0f;
constexpr float kBreathRecoverPerSec = 20.0f;

} // namespace

void CreatureVitalsSystem::Tick(UWorld &world, WorldGameMode mode, float dt)
{
  if (mode != WorldGameMode::Survival || dt <= 0.f)
  {
    return;
  }

  std::vector<CreatureId> toRemove;
  world.ForEachCreature(
      [&](UCreature &creature)
      {
        if (!creature.NeedsNeedsTick())
        {
          return;
        }
        CreatureVitals &v = creature.GetVitals();
        const CreatureAttributes &a = creature.GetAttributes();
        const float endMul =
            1.f / std::max(0.5f, 0.5f + static_cast<float>(a.endurance) / 20.f);

        v.satiety = std::max(0.f, v.satiety - kSatietyDrainPerSec * dt);
        v.thirst = std::max(0.f, v.thirst - kThirstDrainPerSec * dt);

        const bool sprinting =
            creature.GetLocomotionFacts().state == LocomotionState::Run;
        if (sprinting)
        {
          v.fatigue = std::min(v.maxFatigue,
                               v.fatigue + kFatigueSprintPerSec * endMul * dt);
        }
        else
        {
          v.fatigue = std::max(0.f, v.fatigue - kFatigueRecoverPerSec * dt);
        }

        const EnvironmentSample env = ProbeEnvironmentAt(
            world, creature.GetBodyOrigin(),
            creature.GetBounds().profile.restSizeBlocks);
        if (env.inWater)
        {
          v.breath = std::max(0.f, v.breath - kBreathDrainPerSec * dt);
          if (v.breath <= 0.f)
          {
            if (ApplyDamage(world, creature, kStarveDamagePerSec * dt, mode))
            {
              toRemove.push_back(creature.GetId());
            }
          }
        }
        else
        {
          v.breath = std::min(v.maxBreath, v.breath + kBreathRecoverPerSec * dt);
        }

        if (v.satiety <= 0.f || v.thirst <= 0.f)
        {
          if (ApplyDamage(world, creature, kStarveDamagePerSec * dt, mode))
          {
            toRemove.push_back(creature.GetId());
          }
        }
        v.ClampCurrents();
      });

  for (CreatureId id : toRemove)
  {
    world.RemoveCreature(id);
  }
}

bool CreatureVitalsSystem::ApplyDamage(UWorld &world, UCreature &target,
                                       float amount, WorldGameMode mode)
{
  if (mode == WorldGameMode::Creative || amount <= 0.f)
  {
    return false;
  }
  CreatureVitals &v = target.GetVitals();
  const float mitigated =
      std::max(0.f, amount - v.armor * 0.5f);
  // Luck: small chance to shrug off a hit.
  if (target.GetAttributes().luck >= 18 && mitigated > 0.f)
  {
    const int roll = static_cast<int>(std::floor(mitigated * 100.f)) % 100;
    if (roll < target.GetAttributes().luck)
    {
      return false;
    }
  }
  v.health = std::max(0.f, v.health - mitigated);
  if (v.health <= 0.f)
  {
    return HandleLethal(world, target, mode);
  }
  return false;
}

bool CreatureVitalsSystem::HandleLethal(UWorld &world, UCreature &target,
                                        WorldGameMode mode)
{
  if (mode == WorldGameMode::Creative)
  {
    target.GetVitals().health = target.GetVitals().maxHealth;
    return false;
  }
  CreatureVitals &v = target.GetVitals();
  v.fatalWounds = std::min(v.maxFatalWounds, v.fatalWounds + 1);
  if (v.fatalWounds >= v.maxFatalWounds)
  {
    if (target.IsPlayerCharacter())
    {
      // Soft fail: keep player, refill with empty needs as spectate-ish.
      v.FillFull();
      v.fatalWounds = v.maxFatalWounds;
      v.health = std::max(1.f, v.maxHealth * 0.25f);
      return false;
    }
    return true;
  }
  v.FillFull();
  v.health = v.maxHealth;
  const glm::vec3 spawn = world.GetSpawnPoint();
  target.SetBodyOrigin(glm::vec3(spawn.x, spawn.y - target.GetEyeOffset().y,
                                 spawn.z));
  return false;
}

} // namespace cutum
